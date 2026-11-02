/*
 * Main.c - SOC3050 lesson 17: Robot Balancing - PID vs LQR
 *          ST Nucleo-C031C6, STM32C031C6, Cortex-M0+ at 48 MHz, no FPU
 *
 * A two-wheeled self-balancing robot that lives INSIDE the chip.
 *
 *   world    (world.c)    1 kHz   the robot's physics: a wheeled inverted
 *                                 pendulum, its DC motors, and the sensors a
 *                                 real one would have - gyro, accelerometer,
 *                                 wheel encoder - noise, bias and all
 *   control  (control.c)  200 Hz  sees ONLY those sensors and answers with a
 *                                 motor voltage: PID on tilt, cascaded PID, or
 *                                 LQR with a gain computed by host/lqr.py
 *   ui                    50 Hz   the app board: joystick, buttons, knob; $BAL
 *                                 telemetry at 10 Hz
 *   display  (view.c)     10 Hz   the robot from the side on the OLED, and a
 *                                 tilt strip chart
 *   shell                         stats  mode  noise  alpha  vbat  push  k
 *                                 quiet  loud, and the station's $KEY/$MIRROR
 *
 * The app board:
 *   joystick up/down   drive forward / back (a speed command, up to 0.4 m/s)
 *   joystick SEL       next controller: PID tilt -> cascade -> LQR
 *   button A           stand the robot up again
 *   button B           push it (forward, 0.20 N s by default - "push" changes it)
 *   knob               payload on top of the robot, 0 .. 0.6 kg
 *
 * Nothing physical is simulated outside this chip: the "robot" is world.c,
 * and control.c could be moved onto a real robot unchanged (Slide 3).
 *
 * Build:     build.bat
 * Simulate:  simulate.bat, paste diagram.json, upload Main.elf
 * Host:      host\run.bat - lqr.py, then the SAME world.c and control.c on the PC
 * Chart:     python ..\08_UART_And_Python_Host\host.py --file capture.txt --chart BAL,3,-30000,30000
 * Station:   uv run tools/station/station.py - the robot large, the OLED, keys
 *            held for as long as they are held (kbd.h, mirror.h)
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stm32c031xx.h"
#include "os.h"
#include "uart.h"
#include "proto.h"
#include "i2c.h"
#include "oled.h"
#include "pad.h"
#include "beep.h"
#include "kbd.h"
#include "mirror.h"
#include "params.h"
#include "world.h"
#include "control.h"
#include "view.h"

#define BAUD         115200u
#define V_PER_STICK  0.004f        /* joystick -100..100 -> -0.4..0.4 m/s */

#define STACK(name, words) static uint32_t name[words] __attribute__((aligned(8)))
/* world and control never call printf: arm-none-eabi-gcc -fstack-usage puts
 * their deepest frames near 150 bytes, so 160 words (640 B) leaves room for
 * the soft-float library and an interrupt's 32-byte stacked frame.  The three
 * that format text get lesson 09's 384. */
STACK(stk_world, 160);  STACK(stk_ctrl, 160);
STACK(stk_ui, 384);     STACK(stk_disp, 384);   STACK(stk_shell, 384);

/* ============================================================================
 *  What the tasks share - and who writes it
 * ============================================================================
 * world_t belongs to the world task ALONE.  Everyone else asks it to do
 * things through the req_* variables, which it reads at the top of its step.
 * One writer per variable means no locks: the world task is the most urgent,
 * so nothing can interrupt it half way through a step.
 *
 * Several-field values (the sensor snapshot, the truth for the display) are
 * copied inside a two-line critical section, as in lesson 09: without it the
 * controller could read a new gyro with an old encoder - lesson 03's tearing. */
static world_t world;                       /* world task only               */

typedef struct {                            /* the world, as an eye sees it  */
    float th, x, xd, u, payload;
    uint8_t fallen;
    uint32_t saturated;
} truth_t;

static sensors_t snap;                      /* world -> control              */
static truth_t   truth;                     /* world -> ui, display          */
static volatile float   cmd_volts;          /* control -> world              */
static volatile float   v_cmd;              /* ui -> world (into sensors_t)  */
static volatile float   req_payload;        /* ui -> world                   */
static volatile float   req_push;           /* ui/shell -> world, N s; 0 = none */
static volatile uint8_t req_standup;        /* ui -> world                   */
static volatile uint8_t req_reset;          /* world -> control: forget the past */
static volatile float   req_noise = 1.0f;   /* shell -> world: noise scale   */
static volatile float   req_vbat  = VBAT;   /* shell -> world: supply, V     */
static volatile float   push_size = 0.20f;  /* N s, button B                 */

/* profile: microseconds, from TIM14 counting at 1 MHz */
static volatile uint16_t us_world, us_world_max, us_ctrl, us_ctrl_max, us_disp, us_disp_max;
static volatile uint32_t ctrl_steps;

static os_mutex_t print_lock;

/* The station's keys.  The shell task feeds kbd_frame(), the ui task calls
 * kbd_apply(): two tasks, one kbd_t, so one mutex around both (kbd.h). */
static kbd_t      kbd;
static os_mutex_t kbd_lock;
static volatile uint8_t bal_on = 1;         /* shell -> ui: quiet / loud     */

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    os_mutex_lock(&print_lock);
    vprintf(fmt, ap);
    os_mutex_unlock(&print_lock);
    va_end(ap);
}

static void send_frame(const char *fmt, ...)
{
    char body[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    if (n < 0) { return; }
    if ((size_t)n >= sizeof body) { n = (int)sizeof body - 1; }
    say("$%s*%02X\n", body, (unsigned)proto_checksum(body, (size_t)n));
}

/* ============================================================================
 *  TIM14 as a stopwatch: 1 MHz, free-running, 16 bits
 * ============================================================================
 * The M0+ has no cycle counter (the M3's DWT->CYCCNT does not exist here), so
 * a spare timer does the job.  16 bits at 1 us wraps every 65.5 ms - far
 * longer than anything timed here - and unsigned subtraction handles the wrap. */
static void stopwatch_init(void)
{
    RCC->APBENR2 |= RCC_APBENR2_TIM14EN;
    TIM14->PSC = (uint16_t)(SystemCoreClock / 1000000u - 1u);   /* 48 MHz / 48 = 1 MHz */
    TIM14->ARR = 0xFFFFu;
    TIM14->EGR = TIM_EGR_UG;                                     /* load PSC now        */
    TIM14->CR1 = TIM_CR1_CEN;
}
static inline uint16_t now_us(void) { return (uint16_t)TIM14->CNT; }

/* ============================================================================
 *  The world task - 1 kHz, the most urgent
 * ============================================================================ */
static void task_world(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), seed = 1;
    for (;;) {
        os_delay_until(&last, 1);
        uint16_t t0 = now_us();

        /* requests from the other tasks */
        if (req_standup) {
            req_standup = 0;
            seed = seed * 1664525u + 1013904223u;            /* a new noise sequence */
            world_init(&world, seed, 0.03f);                 /* upright-ish: 1.7 deg */
            req_reset = 1;                   /* the CONTROL task resets its own state */
        }
        world.noise = req_noise;
        world.vbat  = req_vbat;
        if (req_payload != world.payload) { world_set_payload(&world, req_payload); }
        if (req_push != 0.0f) { world_push(&world, req_push); req_push = 0.0f; }

        world_step(&world, cmd_volts);
        sensors_t s;
        world_sense(&world, v_cmd, &s);

        truth_t tr = { world.th, world.x, world.xd, world.u, world.payload,
                       world.fallen, world.saturated };
        __disable_irq();
        snap = s;
        truth = tr;
        __enable_irq();

        uint16_t dt = (uint16_t)(now_us() - t0);
        us_world = dt;
        if (dt > us_world_max) { us_world_max = dt; }
    }
}

/* ============================================================================
 *  The control task - 200 Hz.  Sensors in, volts out.  Nothing else.
 * ============================================================================ */
static void task_control(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, (uint32_t)(CTRL_DT * 1000.0f + 0.5f));
        if (req_reset) { req_reset = 0; control_init(); }
        sensors_t s;
        __disable_irq(); s = snap; __enable_irq();

        uint16_t t0 = now_us();
        actuators_t a;
        control_step(&s, &a);
        uint16_t dt = (uint16_t)(now_us() - t0);

        cmd_volts = a.volts;            /* one 32-bit store: no tearing possible */
        us_ctrl = dt;
        if (dt > us_ctrl_max) { us_ctrl_max = dt; }
        ctrl_steps++;
    }
}

/* ============================================================================
 *  The UI task - 50 Hz: the app board's controls, the buzzer, telemetry
 * ============================================================================ */
static void task_ui(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), n = 0;
    uint8_t was_fallen = 0;
    pad_t p;
    for (;;) {
        os_delay_until(&last, 20);
        uint32_t now = os_ticks();
        pad_read(&p);
        os_mutex_lock(&kbd_lock);
        kbd_apply(&kbd, &p, now);       /* the station's keys, into the same pad */
        os_mutex_unlock(&kbd_lock);
        beep_poll(now);

        v_cmd = (float)p.y * V_PER_STICK;
        req_payload = (float)p.knob * (PAY_MAX / 1000.0f);

        if (p.pressed & PAD_SEL) {
            control_set_mode((control_mode() + 1) % CTRL_MODES);
            beep(1200, 40, now);
            say("controller: %s\n", control_name(control_mode()));
        }
        if (p.pressed & PAD_A) { req_standup = 1; beep(880, 80, now); }
        if (p.pressed & PAD_B) { req_push = push_size; beep(300, 60, now); }

        truth_t tr;
        __disable_irq(); tr = truth; __enable_irq();
        if (tr.fallen && !was_fallen) {
            beep(150, 400, now);
            say("FALLEN (%s) - press A to stand it up\n", control_name(control_mode()));
        }
        was_fallen = tr.fallen;

        /* $BAL,<ms>,<mode>,<tilt mdeg>,<estimate mdeg>,<x mm>,<speed mm/s>,<mV>,<fallen>,<payload g>
         * every 100 ms.  Integers: newlib-nano's printf has no %f. */
        if (++n % 5u == 0u && bal_on) {
            const estimate_t *e = control_estimate();
            send_frame("BAL,%lu,%d,%ld,%ld,%ld,%ld,%ld,%u,%ld", (unsigned long)now, control_mode(),
                       (long)(tr.th * 57295.78f), (long)(e->th * 57295.78f),
                       (long)(tr.x * 1000.0f), (long)(tr.xd * 1000.0f),
                       (long)(tr.u * 1000.0f), (unsigned)tr.fallen, (long)(tr.payload * 1000.0f));
        }
    }
}

/* ============================================================================
 *  The display task - 10 Hz, the least urgent: it can take its time on I2C
 * ============================================================================ */
static view_t view;

/* The OLED mirror, for the station: after each flush, at most 2 $FB frames,
 * and only while the UART has room for a whole one - so the mirror does not
 * make this task wait for the link (unless another task fills the ring in the
 * moment between the check and the send).  The frame buffer is static: task
 * stacks are small.  say() keeps a frame from being cut in two by another
 * task's line. */
static void mirror_send(void)
{
    static char frame[MIRROR_FRAME_MAX];
    for (int i = 0; i < 2 && uart_tx_room() >= MIRROR_FRAME_MAX; i++) {
        size_t n = mirror_next(frame, sizeof frame, os_ticks());
        if (n == 0) { break; }
        say("%s", frame);
    }
}

static void task_display(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, 100);
        truth_t tr;
        __disable_irq(); tr = truth; __enable_irq();
        const estimate_t *e = control_estimate();

        uint16_t t0 = now_us();
        view.mode = control_name(control_mode());
        view.th = tr.th;  view.x = tr.x;  view.xd = tr.xd;  view.u = tr.u;
        view.payload = tr.payload;  view.fallen = tr.fallen;  view.x_ref = e->x_ref;
        view_history(&view, tr.th);
        view_draw(&view);
        oled_flush();                 /* only the pages that changed go on the bus */
        mirror_send();                /* ...and the station's copy of the screen  */
        uint16_t dt = (uint16_t)(now_us() - t0);
        us_disp = dt;
        if (dt > us_disp_max) { us_disp_max = dt; }
    }
}

/* ============================================================================
 *  The shell
 * ============================================================================ */
static void cmd_stats(void)
{
    truth_t tr;
    __disable_irq(); tr = truth; __enable_irq();
    /* CPU share of the two control-loop tasks: time per call x calls per second */
    uint32_t load = ((uint32_t)us_world * 1000u + (uint32_t)us_ctrl * 200u) / 10000u;
    say("world   %u us/step (max %u) x 1000/s\n", us_world, us_world_max);
    say("control %u us/step (max %u) x 200/s  -> ~%lu%% of the CPU for both\n",
        us_ctrl, us_ctrl_max, (unsigned long)load);
    say("display %u us/frame (max %u), %lu bytes to the OLED so far\n",
        us_disp, us_disp_max, (unsigned long)oled_bytes_sent());
    say("control steps %lu, supply clipped %lu of the world's steps\n",
        (unsigned long)ctrl_steps, (unsigned long)tr.saturated);
    for (uint32_t i = 0; i < os_task_count(); i++) {
        os_task_t *t = os_task(i);
        say("  %-8s ran %6lu ms   stack %3lu of %3lu words\n", t->name, (unsigned long)t->ticks,
            (unsigned long)os_stack_used(t), (unsigned long)t->stack_words);
    }
}

/* The keys the station sends.  The caller holds print_lock (or the scheduler
 * has not started yet). */
static void print_keys(void)
{
    printf("  keys     : arrows = stick, a = stand up, b/p = push, s = next controller,\n"
           "             1-5 = payload, - / + = smaller / bigger push, ? = this list\n");
}

/* A line starting with '$' is a station frame, never a command: $KEY goes to
 * the keyboard, $MIRROR to the mirror.  Neither prints a word - a reply to
 * every key would flood the link - and any other '$' line is dropped as
 * quietly.  Only what a key DOES may print: ? lists the keys, - and + say the
 * new push. */
static void station_line(const char *line)
{
    if (kbd_is_key(line)) {
        uint32_t now = os_ticks();
        os_mutex_lock(&kbd_lock);
        int cmd = kbd_frame(&kbd, line, now);
        os_mutex_unlock(&kbd_lock);
        if (cmd == KBD_HELP) {
            os_mutex_lock(&print_lock);
            print_keys();
            os_mutex_unlock(&print_lock);
        } else if (cmd == KBD_SLOWER || cmd == KBD_FASTER) {
            /* nothing here has a pace to slow: - and + size button B's push */
            long mns = (long)(push_size * 1000.0f + 0.5f);
            mns += cmd == KBD_FASTER ? 50 : -50;
            if (mns < 50)  { mns = 50; }
            if (mns > 1000) { mns = 1000; }
            push_size = (float)mns / 1000.0f;
            say("button B pushes %ld mN s\n", mns);
        }
    } else {
        (void)mirror_frame(line, os_ticks());
    }
}

static void task_shell(void *arg)
{
    (void)arg;
    char line[32];
    uint32_t n = 0;
    for (;;) {
        char c = (char)uart_getc();
        if (c != '\r' && c != '\n') { if (n < sizeof line - 1u) { line[n++] = c; } continue; }
        if (n == 0) { continue; }
        line[n] = '\0';
        n = 0;
        if (line[0] == '$') { station_line(line); continue; }   /* before the parser */
        char *arg1 = strchr(line, ' ');
        long v = arg1 ? strtol(arg1 + 1, 0, 10) : -1;
        if (arg1) { *arg1 = '\0'; }

        if (strcmp(line, "stats") == 0) {
            cmd_stats();
        } else if (strcmp(line, "mode") == 0 && v >= 0 && v < CTRL_MODES) {
            control_set_mode((int)v);
            say("controller: %s\n", control_name((int)v));
        } else if (strcmp(line, "noise") == 0 && v >= 0) {
            req_noise = (float)v / 100.0f;
            say("sensor noise %ld%% of nominal\n", v);
        } else if (strcmp(line, "alpha") == 0 && v >= 0 && v <= 100) {
            control_set_alpha((float)v / 100.0f);
            say("complementary filter alpha = 0.%02ld%s\n", v, v == 100 ? " (gyro only)" : v == 0 ? " (accelerometer only)" : "");
        } else if (strcmp(line, "vbat") == 0 && v > 0) {
            req_vbat = (float)v / 10.0f;              /* tenths of a volt */
            say("supply %ld.%ld V\n", v / 10, v % 10);
        } else if (strcmp(line, "push") == 0 && v > 0) {
            push_size = (float)v / 1000.0f;           /* milli-newton-seconds */
            say("button B pushes %ld mN s\n", v);
        } else if (strcmp(line, "k") == 0) {
            say("LQR K x1000 = [%ld %ld %ld %ld]  (u = -K [x, x_dot, theta, theta_dot])\n",
                (long)(LQR_K[0] * 1000.0f), (long)(LQR_K[1] * 1000.0f),
                (long)(LQR_K[2] * 1000.0f), (long)(LQR_K[3] * 1000.0f));
        } else if (strcmp(line, "quiet") == 0) {
            bal_on = 0;
            say("quiet: $BAL off - type loud to resume\n");
        } else if (strcmp(line, "loud") == 0) {
            bal_on = 1;
            say("loud: $BAL on\n");
        } else {
            say("commands: stats  mode 0|1|2  noise <%%>  alpha <%%>  vbat <tenths V>  push <mN s>  k  quiet  loud\n");
        }
    }
}

int main(void)
{
    uart_init(SystemCoreClock, BAUD);
    printf("\n=== SOC3050 lesson 17 - Robot Balancing: PID vs LQR ===\n");
    printf("  the robot is simulated INSIDE this chip: world.c at 1 kHz\n");

    stopwatch_init();
    int a = pad_init();
    printf("  pad      : %s\n", a == 0 ? "joystick, knob, buttons A/B/SEL" : "ADC failed - buttons only");
    beep_init();
    i2c_init();
    int o = oled_init();
    printf("  OLED     : %s\n", o == 0 ? "SSD1306 at 0x3C" : "no answer at 0x3C - running without a picture");

    world.noise = 1.0f;                 /* before world_init: it keeps this */
    world.vbat  = VBAT;
    world_init(&world, 1u, 0.03f);
    control_init();
    control_set_mode(CTRL_LQR);
    printf("  world    : %d g body, %d mm wheels, %d.%d V supply, IMU %d.%d deg off\n",
           (int)(BOT_MB * 1000.0f), (int)(BOT_R * 2000.0f),
           (int)VBAT, (int)(VBAT * 10.0f) % 10, (int)(IMU_TILT * 57.3f), (int)(IMU_TILT * 573.0f) % 10);
    printf("  control  : LQR (SEL cycles PID tilt -> cascade -> LQR)\n");
    printf("  buttons  : A stand up, B push, knob payload, stick drive\n");
    printf("  shell    : stats  mode  noise  alpha  vbat  push  k  quiet  loud\n");
    printf("  station  : uv run tools/station/station.py - the robot large, keys up and down\n");
    print_keys();
    printf("\n");
    kbd_init(&kbd);

    os_task_create("world",   task_world,   0, stk_world, 160, 5);
    os_task_create("control", task_control, 0, stk_ctrl,  160, 4);
    os_task_create("ui",      task_ui,      0, stk_ui,    384, 3);
    os_task_create("shell",   task_shell,   0, stk_shell, 384, 2);
    os_task_create("display", task_display, 0, stk_disp, 384, 1);
    os_start();
}
