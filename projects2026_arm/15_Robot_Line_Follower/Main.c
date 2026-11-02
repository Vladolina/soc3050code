/*
 * Main.c - SOC3050 lesson 15: Robot Line Follower
 *          ST Nucleo-C031C6, STM32C031C6, Cortex-M0+ at 48 MHz
 *
 * A line-following robot races laps on the OLED - and the robot, its motors,
 * its tyres and its sensors are all simulated INSIDE this firmware.
 *
 *   world   prio 5  200 Hz  world.c: physics, sensor synthesis, the judge
 *   control prio 4  100 Hz  control.c: sensors in, wheel speeds out -
 *                           or the joystick, in manual mode
 *   ui      prio 3   50 Hz  buttons, knob, beeps, $LINE / $LAP telemetry
 *   shell   prio 2          kp ki kd df slow thr  noise ambient fail  gains stats
 *                           quiet loud, and the station's $KEY / $MIRROR lines
 *   display prio 1   10 Hz  view.c on the OLED: the track, the robot, the bar -
 *                           and the same screen as $FB frames, for the station
 *
 * The control task is the part that would ship on a real robot.  It talks to
 * the world through two small structs (control.h) and nothing else, which is
 * why host/sitl.c can race the same control.c on a PC.
 *
 * Controls (the app board, see _lib/pad.h):
 *   menu:   joystick up/down picks a track, A starts it
 *   race:   A  start / stop (back to the line)     B  next track
 *           SEL  manual <-> auto                   knob  speed, 0..2000 mm/s
 *           joystick drives in manual mode
 *
 * The same controls from the station on the PC (kbd.h), which also draws the
 * OLED large and the race from above: uv run tools/station/station.py
 *   arrows = stick, a = A, b or p = B, s = SEL, 1-5 = knob, - + = knob -/+ 50
 *
 * Build:     build.bat
 * Simulate:  simulate.bat, paste diagram.json, upload Main.elf.
 * Host:      host\run.bat   (the races, measured, with no chip at all)
 */

#include <stdarg.h>
#include <stdio.h>
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
#include "fmath.h"
#include "control.h"
#include "world.h"
#include "track.h"
#include "view.h"

#define BAUD   115200u

#define STACK(name, words) static uint32_t name[words] __attribute__((aligned(8)))
/* Sizes from gcc -fstack-usage plus newlib's printf frames (README, "Stacks"):
 * the deepest chain in each task, the 64 bytes an interrupt and a context
 * switch push on top, and about a third spare.  `stats` prints the real
 * high-water marks - check them before shrinking anything. */
#define W_WORLD 160
#define W_CTL   160
#define W_UI    288     /* was 256: the longer $LINE frame added ~110 B */
#define W_SHELL 224
#define W_DISP  256
STACK(stk_world, W_WORLD);  STACK(stk_ctl, W_CTL);  STACK(stk_ui, W_UI);
STACK(stk_shell, W_SHELL);  STACK(stk_disp, W_DISP);

/* ---- what the tasks share ----------------------------------------------
 * Each struct has ONE writer.  Readers copy it whole inside a critical
 * section (lesson 03's tearing: a half-updated sensor bar is a bar that
 * never existed).  The world itself (world.c's `world`) is written only by
 * the world task and copied the same way. */
static sensors_t   sh_sens;              /* world   -> control, display        */
static actuators_t sh_act;               /* control -> world, telemetry        */
static pad_t       sh_pad;               /* ui      -> control (manual), world */

static volatile int8_t   req_track = -1; /* ui -> world: (re)build this track  */
static volatile uint8_t  req_start;      /* ui -> world: lights out            */
static volatile uint32_t run_id;         /* world: bumped on every reset/start */
static volatile uint32_t track_gen;      /* world: bumped when track.pt changes */
static volatile uint8_t  manual, in_menu = 1, menu_sel;

static volatile uint8_t  telemetry = 1;  /* $LINE on; the shell's quiet / loud */

static os_mutex_t print_lock;            /* whole lines on the serial port     */
static os_mutex_t track_lock;            /* track.pt: world rebuilds, display draws */

/* The station's keys.  The shell feeds them in (kbd_frame), the ui task folds
 * them into the pad (kbd_apply) - two tasks, one kbd_t, and kbd.c has no lock
 * of its own, so this one guards both calls. */
static kbd_t      kbd;
static os_mutex_t kbd_lock;

/* One copy of the key list: printf'd by main() (no task runs yet, so no
 * mutex), said by the shell when the station sends '?'. */
static const char keys_help[] =
    "  keys     : arrows = stick, a = A, b/p = B, s = SEL, 1-5 = knob, ? = this\n"
    "             - / + = knob -/+ 50 (100 mm/s),  quiet / loud = stop / restart $LINE\n";

/* ---- measured on the chip, printed by `stats` --------------------------- */
static volatile uint32_t w_cyc_max, w_cyc_sum, w_n, c_cyc_max, c_cyc_sum, c_n;
static volatile uint32_t d_ms_max, d_frames, oled_rc;

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
    char body[128];                      /* $LINE is the longest: ~110 at worst */
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    if (n < 0) { return; }
    if ((size_t)n >= sizeof body) { n = (int)sizeof body - 1; }
    say("$%s*%02X\n", body, (unsigned)proto_checksum(body, (size_t)n));
}

/* CPU cycles since os_start, from the kernel's 1 ms tick plus SysTick's
 * down-counter.  Re-read if a tick landed between the two reads.  Wraps
 * after 89 s, which a difference of two readings does not mind. */
static uint32_t cycles_now(void)
{
    uint32_t ms, val;
    do { ms = os_ticks(); val = SysTick->VAL; } while (ms != os_ticks());
    return ms * (SysTick->LOAD + 1u) + (SysTick->LOAD - val);
}

/* ============================================================================
 *  The world: 200 Hz, top priority.  It is the stand-in for physics, and
 *  physics does not wait for anybody.
 * ============================================================================ */
static void task_world(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, 1000u / WORLD_HZ);

        if (req_track >= 0) {                   /* new track, or back to the line */
            os_mutex_lock(&track_lock);
            world_reset(req_track);
            os_mutex_unlock(&track_lock);
            req_track = -1;
            track_gen++;
            run_id++;
        }
        if (req_start) {
            req_start = 0;
            if (world.state == W_READY) { world_start(); run_id++; }
        }

        actuators_t a;
        __disable_irq(); a = sh_act; __enable_irq();

        uint32_t c0 = cycles_now();
        world_step(&a);
        sensors_t s;
        world_sense(&s);
        uint32_t c = cycles_now() - c0;

        s.knob = sh_pad.knob;                   /* one 16-bit read: atomic */
        __disable_irq(); sh_sens = s; __enable_irq();

        w_cyc_sum += c;  w_n++;
        if (c > w_cyc_max) { w_cyc_max = c; }
    }
}

/* ============================================================================
 *  The controller: 100 Hz.  This task, control.c and nothing else is what
 *  would run on a real robot.
 * ============================================================================ */
static void task_control(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), seen = run_id;
    for (;;) {
        os_delay_until(&last, 1000u / CONTROL_HZ);
        if (seen != run_id) { seen = run_id; control_reset(); }

        sensors_t s;
        pad_t p;
        __disable_irq(); s = sh_sens; p = sh_pad; __enable_irq();

        actuators_t a;
        uint32_t c0 = cycles_now();
        if (manual) {
            /* the joystick: up = forward, right = turn right (left wheel faster) */
            int fwd = p.y * 12, turn = p.x * 5;
            a.left  = (int16_t)(fwd + turn);
            a.right = (int16_t)(fwd - turn);
        } else {
            control_step(&s, &a);
        }
        uint32_t c = cycles_now() - c0;

        __disable_irq(); sh_act = a; __enable_irq();
        if (!manual) {
            c_cyc_sum += c;  c_n++;
            if (c > c_cyc_max) { c_cyc_max = c; }
        }
    }
}

/* ============================================================================
 *  The UI: buttons, beeps, telemetry.  50 Hz.
 * ============================================================================ */
static void print_time(const char *what, uint32_t ms)
{
    say("%s %lu.%02lu s", what, (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u / 10u));
}

static void task_ui(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), tick = 0;
    uint16_t laps_seen = 0;
    uint8_t  state_seen = W_READY, stick_free = 1;
    world_t  w;

    for (;;) {
        os_delay_until(&last, 20);
        uint32_t now = os_ticks();
        pad_t p;
        pad_read(&p);
        os_mutex_lock(&kbd_lock);       /* the station's keys, into the same pad */
        kbd_apply(&kbd, &p, now);
        os_mutex_unlock(&kbd_lock);
        __disable_irq(); sh_pad = p; __enable_irq();
        beep_poll(now);

        if (in_menu) {
            /* joystick up/down moves the cursor once per push */
            if (stick_free && (p.y > 60 || p.y < -60)) {
                menu_sel = (uint8_t)((menu_sel + (p.y < 0 ? 1 : TRACK_COUNT - 1)) % TRACK_COUNT);
                beep(2000, 10, now);
                stick_free = 0;
            }
            if (p.y > -30 && p.y < 30) { stick_free = 1; }
            if (p.pressed & PAD_A) {
                req_track = (int8_t)menu_sel;
                in_menu = 0;
                beep(880, 60, now);
                say("track %d %s: A to start, B next track, SEL manual/auto\n",
                    menu_sel + 1, track_name(menu_sel));
            }
            continue;
        }

        __disable_irq(); w = world; __enable_irq();

        if (p.pressed & PAD_A) {
            if (w.state == W_READY) { req_start = 1; beep(880, 60, now); say("GO\n"); }
            else                    { req_track = (int8_t)track.id; say("stopped\n"); }
        }
        if (p.pressed & PAD_B) {
            int next = (track.id + 1) % TRACK_COUNT;
            req_track = (int8_t)next;
            say("track %d %s\n", next + 1, track_name(next));
        }
        if (p.pressed & PAD_SEL) {
            manual ^= 1u;
            say("%s\n", manual ? "MANUAL: the joystick drives" : "AUTO: control.c drives");
        }

        /* a lap: beep, and a $LAP frame for the class leaderboard - auto only,
         * because the leaderboard ranks controllers, not thumbs */
        if (w.laps != laps_seen) {
            int best = w.last_lap == w.best_lap;
            beep(best ? 1760 : 1319, best ? 150 : 80, now);
            print_time(manual ? "manual lap" : "lap", w.last_lap);
            say("%s\n", best ? "  BEST" : "");
            if (!manual) { send_frame("LAP,%d,%lu", track.id + 1, (unsigned long)w.last_lap); }
        }
        laps_seen = w.laps;
        if (w.state == W_DNF && state_seen != W_DNF) {
            beep(220, 400, now);
            say("DNF: %s - A to go back to the line\n",
                w.dnf == DNF_LOST ? "no sensor saw tape for 1 s" : "more than 150 mm off the line");
        }
        state_seen = w.state;

        /* $LINE,<ms>,<est x10>,<true x10>,<cmd L>,<cmd R>,<speed>,<s>,<slip>,
         *       <x>,<y>,<heading>,<track>,<line 0>..<line 6>              - 10 Hz
         * The first eight fields are the original frame; the rest were added
         * at the END for the station's map, so anything that counts fields
         * from the front (host.py --chart LINE,3) reads what it always did.
         * x, y in mm and heading in degrees (0 = east, + = anticlockwise) are
         * the world's TRUTH - the controller never sees them.  track is 1..4;
         * line 0..6 are the readings the controller does see, rightmost first. */
        if (telemetry && w.state == W_RUN && ++tick % 5u == 0u) {
            actuators_t a;
            sensors_t s;
            __disable_irq(); a = sh_act; s = sh_sens; __enable_irq();
            int v = (int)f_sqrt(w.vx * w.vx + w.vy * w.vy);
            send_frame("LINE,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u,%u,%u,%u,%u,%u,%u",
                       (unsigned long)w.t_ms,
                       (int)(ctl_state.pos * 10.0f), (int)(w.err * 10.0f),
                       a.left, a.right, v, (int)w.s, w.slipping,
                       (int)w.x, (int)w.y, (int)(w.th * (180.0f / F_PI)), track.id + 1,
                       s.line[0], s.line[1], s.line[2], s.line[3],
                       s.line[4], s.line[5], s.line[6]);
        }
    }
}

/* ============================================================================
 *  The display: 10 Hz, lowest priority - it only gets the time nothing else
 *  wants.  The I2C driver is polled, so a flush is CPU time.
 * ============================================================================ */

/* The OLED mirror: after each flush, up to two $FB frames - only while the
 * station keeps asking (mirror.h), and only when the UART ring has room for a
 * whole one, so the display task never waits on the serial port.  Through
 * say(), so a frame is never cut into by another task's line.  The buffer is
 * static: 96 bytes is a lot to add to a task stack.  It is sent from HERE
 * because this task is the only writer of the framebuffer - the picture
 * cannot change half-way through a frame. */
static void mirror_send(void)
{
    static char frame[MIRROR_FRAME_MAX];
    for (int i = 0; i < 2 && uart_tx_room() >= MIRROR_FRAME_MAX; i++) {
        if (mirror_next(frame, sizeof frame, os_ticks()) == 0) { break; }
        say("%s", frame);
    }
}

static void task_display(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), shown_gen = 0xFFFFFFFFu;
    int shown_sel = -1;
    world_t w;
    sensors_t s;
    for (;;) {
        os_delay_until(&last, 100);
        if (in_menu) {
            if (shown_sel != menu_sel) { view_menu(menu_sel); shown_sel = menu_sel; }
            oled_rc = (uint32_t)oled_flush();
            mirror_send();
            shown_gen = 0xFFFFFFFFu;
            continue;
        }
        shown_sel = -1;
        if (shown_gen != track_gen) {
            os_mutex_lock(&track_lock);
            shown_gen = track_gen;
            view_track();
            os_mutex_unlock(&track_lock);
        }
        __disable_irq(); w = world; s = sh_sens; __enable_irq();
        view_robot(w.x, w.y, w.th);
        view_hud(&w, &s, manual, sh_pad.knob);

        uint32_t t0 = os_ticks();
        oled_rc = (uint32_t)oled_flush();
        uint32_t dt = os_ticks() - t0;
        if (dt > d_ms_max) { d_ms_max = dt; }
        d_frames++;
        mirror_send();
    }
}

/* ============================================================================
 *  The shell: tune the controller and torment the sensors, live.
 * ============================================================================ */
static int parse_num(const char *p, float *out)    /* "-12.5" -> -12.5; no strtof */
{
    float v = 0.0f, scale = 1.0f;
    int neg = 0, digits = 0;
    while (*p == ' ') { p++; }
    if (*p == '-') { neg = 1; p++; }
    for (; *p >= '0' && *p <= '9'; p++, digits++) { v = v * 10.0f + (float)(*p - '0'); }
    if (*p == '.') {
        for (p++; *p >= '0' && *p <= '9'; p++, digits++) { scale *= 0.1f; v += (float)(*p - '0') * scale; }
    }
    *out = neg ? -v : v;
    return digits > 0;
}

static void say_fixed(const char *name, float v)     /* printf has no %f here */
{
    int32_t m = (int32_t)(v * 1000.0f + (v >= 0.0f ? 0.5f : -0.5f));
    uint32_t a = (uint32_t)(m < 0 ? -m : m);
    say(" %s %s%lu.%03lu", name, m < 0 ? "-" : "", (unsigned long)(a / 1000u), (unsigned long)(a % 1000u));
}

static void cmd_gains(void)
{
    say("gains:");
    say_fixed("kp", ctl_gains.kp);   say_fixed("ki", ctl_gains.ki);
    say_fixed("kd", ctl_gains.kd);   say_fixed("df", ctl_gains.dfilt);
    say_fixed("slow", ctl_gains.slow);
    say("  thr %d | noise %d ambient %d fail %d\n", (int)ctl_gains.thr,
        world.noise, world.ambient, world.failed);
}

static void cmd_stats(void)
{
    uint32_t wn = w_n ? w_n : 1u, cn = c_n ? c_n : 1u;
    uint32_t hz = SystemCoreClock / 1000000u;               /* cycles per us */
    say("world step  : avg %lu, max %lu cycles  (%lu / %lu us of every 5000)\n",
        (unsigned long)(w_cyc_sum / wn), (unsigned long)w_cyc_max,
        (unsigned long)(w_cyc_sum / wn / hz), (unsigned long)(w_cyc_max / hz));
    say("control step: avg %lu, max %lu cycles  (%lu / %lu us of every 10000)\n",
        (unsigned long)(c_cyc_sum / cn), (unsigned long)c_cyc_max,
        (unsigned long)(c_cyc_sum / cn / hz), (unsigned long)(c_cyc_max / hz));
    say("display     : %lu frames, slowest flush %lu ms, %lu bytes sent, oled rc %ld\n",
        (unsigned long)d_frames, (unsigned long)d_ms_max,
        (unsigned long)oled_bytes_sent(), (long)(int32_t)oled_rc);
    for (uint32_t i = 0; i < os_task_count(); i++) {
        os_task_t *t = os_task(i);
        say("  %-8s stack %3lu/%3lu words  cpu %lu ms\n", t->name,
            (unsigned long)os_stack_used(t), (unsigned long)t->stack_words, (unsigned long)t->ticks);
    }
}

/* A line from the station: a key ($KEY) or "keep mirroring" ($MIRROR, every
 * second).  Neither may ever print anything back unasked - a reply per key or
 * a usage line per second would bury the console - so a $ line that is
 * neither (garbled, or meant for another lesson) is dropped silently too.
 * People type words; only programs type '$'. */
static void station_line(const char *line)
{
    uint32_t now = os_ticks();
    if (kbd_is_key(line)) {
        os_mutex_lock(&kbd_lock);
        int r = kbd_frame(&kbd, line, now);
        int knob = 0;
        if (r == KBD_SLOWER || r == KBD_FASTER) {
            /* - and + trim the speed: the knob, 50 counts (100 mm/s) a press.
             * The same override a digit sets, so turning the real knob takes
             * over again (kbd_apply). */
            knob = sh_pad.knob + (r == KBD_FASTER ? 50 : -50);
            knob = knob < 0 ? 0 : knob > 1000 ? 1000 : knob;
            kbd.knob = (int16_t)knob;
            kbd.knob_ref = -1;
        }
        os_mutex_unlock(&kbd_lock);
        if (r == KBD_HELP) { say("%s", keys_help); }
        if (r == KBD_SLOWER || r == KBD_FASTER) { say("knob %d: %d mm/s on a straight\n", knob, knob * 2); }
        return;
    }
    /* $MIRROR is written here and read by the display task's mirror_next().
     * No lock: a 32-bit store is atomic on the M0+, and the worst a badly
     * timed one can do is send one segment twice. */
    (void)mirror_frame(line, now);
}

static void task_shell(void *arg)
{
    (void)arg;
    char line[32];                       /* the longest $KEY line is 15 chars */
    uint32_t n = 0;
    for (;;) {
        char c = (char)uart_getc();
        if (c != '\r' && c != '\n') { if (n < sizeof line - 1u) { line[n++] = c; } continue; }
        if (n == 0) { continue; }
        line[n] = '\0';
        n = 0;

        if (line[0] == '$') { station_line(line); continue; }
        if (strcmp(line, "quiet") == 0) { telemetry = 0; say("quiet: $LINE off ($LAP stays) - loud to resume\n"); continue; }
        if (strcmp(line, "loud") == 0)  { telemetry = 1; say("loud: $LINE on\n"); continue; }

        char *arg1 = strchr(line, ' ');
        float v = 0.0f;
        int has = 0;
        if (arg1) { *arg1++ = '\0'; has = parse_num(arg1, &v); }

        if      (has && strcmp(line, "kp") == 0)      { ctl_gains.kp = v; }
        else if (has && strcmp(line, "ki") == 0)      { ctl_gains.ki = v; }
        else if (has && strcmp(line, "kd") == 0)      { ctl_gains.kd = v; }
        else if (has && strcmp(line, "df") == 0)      { ctl_gains.dfilt = f_clamp(v, 0.01f, 1.0f); }
        else if (has && strcmp(line, "slow") == 0)    { ctl_gains.slow = f_clamp(v, 0.0f, 1.0f); }
        else if (has && strcmp(line, "thr") == 0)     { ctl_gains.thr = v; }
        else if (has && strcmp(line, "noise") == 0)   { world.noise = (int16_t)v; }
        else if (has && strcmp(line, "ambient") == 0) { world.ambient = (int16_t)v; }
        else if (has && strcmp(line, "fail") == 0)    { world.failed = (int8_t)v; }
        else if (strcmp(line, "stats") == 0)          { cmd_stats(); continue; }
        else if (strcmp(line, "gains") != 0) {
            say("commands: kp ki kd df slow thr N | noise ambient fail N | gains | stats | quiet loud\n");
            continue;
        }
        cmd_gains();
    }
}

int main(void)
{
    uart_init(SystemCoreClock, BAUD);
    printf("\n=== SOC3050 lesson 15 - Robot Line Follower ===\n");

    int pr = pad_init();
    printf("  pad      : %s\n", pr == 0 ? "joystick, knob, A, B, SEL" : "buttons only - ADC did not start");
    beep_init();
    i2c_init();
    int o = oled_init();
    printf("  OLED     : %s\n", o == I2C_OK ? "SSD1306 at 0x3C" : o == I2C_NACK ? "no ACK at 0x3C - not connected?" : "bus timeout");

    world.noise  = 15;                  /* the defaults host/sitl.c measured with */
    world.failed = -1;
    world_reset(0);
    printf("  world    : %u Hz physics, %u Hz control, %d sensors at %d mm\n",
           WORLD_HZ, CONTROL_HZ, LINE_SENSORS, SENSOR_PITCH_MM);
    printf("  shell    : kp ki kd df slow thr N | noise ambient fail N | gains | stats | quiet loud\n");
    printf("  station  : uv run tools/station/station.py - the OLED large, the race from above\n");
    printf("%s\n", keys_help);
    kbd_init(&kbd);

    os_task_create("world",   task_world,   0, stk_world, W_WORLD, 5);
    os_task_create("control", task_control, 0, stk_ctl,   W_CTL,   4);
    os_task_create("ui",      task_ui,      0, stk_ui,    W_UI,    3);
    os_task_create("shell",   task_shell,   0, stk_shell, W_SHELL, 2);
    os_task_create("display", task_display, 0, stk_disp,  W_DISP,  1);
    os_start();
}
