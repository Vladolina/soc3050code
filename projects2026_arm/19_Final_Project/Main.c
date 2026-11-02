/*
 * Main.c - SOC3050 lesson 19: Final Project TEMPLATE
 *          ST Nucleo-C031C6, STM32C031C6, Cortex-M0+ at 48 MHz, app board
 *
 * Every app-board subsystem, wired together the way a product would be.
 * Your project goes in app.c (see app.h); this file should need only its
 * table of rates and priorities touched.
 *
 *   task      rate     prio  does                                   beats?
 *   input     100 Hz    5    buttons + stick + knob; owns the buzzer  yes
 *   app        50 Hz    4    app_step(): your logic                   yes
 *   telem      10 Hz    3    $APP frames; $SYS once a second          yes
 *   display    20 Hz    2    app_draw(), then oled_flush() over I2C   yes
 *   health      4 Hz    1    refresh the watchdog if ALL beat; LD4    -
 *   shell     on input  1    stats, health, scan, hang, spin, reset   -
 *   idle      always    0    (the kernel's) - whatever is left over
 *
 * Shared data and who protects it:
 *   controls   input -> app        2-line critical section, sticky edges
 *   sound      app -> input        a one-slot mailbox; input owns TIM3
 *   app state  app, display, telem app_lock (a mutex: draw takes ~ms)
 *   I2C1       display, shell      bus_lock
 *   the UART   everyone            print_lock, whole lines (say(), send_raw())
 *   keys       shell -> input      kbd_lock: the station's $KEY frames (kbd.h)
 *   mirror     shell -> display    mirror_lock: $MIRROR,1 vs building $FB
 *   quiet      shell -> telem      one byte, one writer: the shell
 *   heartbeats each task -> health one counter per task, one writer each
 *
 * THE STATION.  uv run tools/station/station.py, on the PC, is this board's
 * screen, keyboard and dashboard in one window.  Three things connect it:
 *   - its keys arrive as $KEY lines; the shell hands them to kbd_frame(),
 *     and input folds them into the pad with kbd_apply() - so app.c cannot
 *     tell the keyboard from the board;
 *   - display copies the OLED to it as $FB frames (mirror.h), but only while
 *     the station keeps asking with $MIRROR,1;
 *   - its view of THIS lesson, tools/station/views/l19_final.py, draws $APP
 *     and $SYS.  Your project's view starts as a copy of that file.
 *
 * Build:     build.bat          (LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror)
 * Simulate:  simulate.bat, paste diagram.json, upload Main.elf.
 * Host:      host\run.bat       (tests app.c and health.c on the PC)
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "stm32c031xx.h"
#include "gpio.h"
#include "os.h"
#include "uart.h"
#include "proto.h"
#include "i2c.h"
#include "oled.h"
#include "adc.h"
#include "pad.h"
#include "beep.h"
#include "kbd.h"
#include "mirror.h"
#include "wdog.h"
#include "health.h"
#include "prof.h"
#include "app.h"
#include "sys.h"

#define BAUD            115200u
#define LED_PIN         5u          /* PA5 = LD4: blinks while the watchdog is fed */

/* ---- THE TABLE.  Rates in ms, priorities (higher = more urgent). -------- */
#define INPUT_MS        10u
#define TEL_MS          100u
#define DISPLAY_MS      50u
#define HEALTH_MS       250u        /* 4 refreshes per 1000 ms IWDG timeout   */
#define FORCE_AFTER_MS  1500u       /* hardware should have reset by now      */

enum { PRIO_SHELL = 1, PRIO_HEALTH = 1, PRIO_DISPLAY = 2, PRIO_TEL = 3,
       PRIO_APP = 4, PRIO_INPUT = 5 };

#define STACK(name, words) static uint32_t name[words] __attribute__((aligned(8)))
/* Stack sizes, in 32-bit words, from -fstack-usage plus newlib's printf
 * frames read from the disassembly (README, "Memory budget").  A task that
 * prints needs ~650 bytes for printf alone.  Check with `stats`. */
#define W_INPUT   160u
#define W_APP     256u      /* your app_step() runs here: room to grow      */
#define W_TEL     288u
#define W_DISP    224u
#define W_HEALTH  256u
#define W_SHELL   320u
STACK(stk_input, W_INPUT);  STACK(stk_app, W_APP);        STACK(stk_tel, W_TEL);
STACK(stk_disp, W_DISP);    STACK(stk_health, W_HEALTH);  STACK(stk_shell, W_SHELL);

/* ---- shared state (declared in sys.h) ------------------------------------ */
os_mutex_t print_lock, bus_lock, app_lock, kbd_lock, mirror_lock;
health_t   health;
prof_t     prof[T_WATCHED] = {
    [T_INPUT]   = { .name = "input",   .period_ms = INPUT_MS },
    [T_APP]     = { .name = "app",     .period_ms = APP_PERIOD_MS },
    [T_DISPLAY] = { .name = "display", .period_ms = DISPLAY_MS },
    [T_TEL]     = { .name = "telem",   .period_ms = TEL_MS },
};
volatile uint32_t fault_hang, fault_spin, i2c_errors;
uint8_t           oled_ok;
uint32_t          boot_flags;
kbd_t             kbd;          /* the station's keys: shell writes, input applies */
volatile uint8_t  tel_quiet;    /* 1 after `quiet`: no $APP / $SYS until `loud` */

/* controls: written by input, consumed by app */
static pad_t shared_pad;

/* sound: a one-slot mailbox, app -> input.  The newest request wins. */
static volatile uint16_t snd_hz, snd_ms;
static volatile uint8_t  snd_req;

/* ============================================================================
 *  Printing: whole lines, and checksummed frames (lessons 08 and 09)
 * ============================================================================ */
void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    os_mutex_lock(&print_lock);
    vprintf(fmt, ap);
    os_mutex_unlock(&print_lock);
    va_end(ap);
}

void send_body(const char *body, int len)
{
    if (len <= 0) { return; }
    say("$%s*%02X\n", body, (unsigned)proto_checksum(body, (size_t)len));
}

/* A line that is already built - the OLED mirror's $FB frames.  Same lock as
 * say(), so it never lands inside someone else's line, but straight into the
 * UART ring: no printf, so none of printf's ~650 bytes of stack.  The display
 * task calls it, and stays as deep as app_draw()'s oled_printf() made it. */
int _write(int fd, const char *buf, int len);           /* uart.c */

void send_raw(const char *buf, size_t len)
{
    os_mutex_lock(&print_lock);
    _write(1, buf, (int)len);
    os_mutex_unlock(&print_lock);
}

/* ============================================================================
 *  The platform half of app.h
 * ============================================================================ */
void app_sound(uint32_t hz, uint32_t ms)
{
    snd_hz  = (uint16_t)(hz > 20000u ? 20000u : hz);
    snd_ms  = (uint16_t)(ms > 2000u ? 2000u : ms);
    snd_req = 1;                     /* last: input sees hz and ms complete */
}

/* ============================================================================
 *  Fault injection - the shell's `hang` and `spin` set these bits.  Every
 *  watched task passes through here once per loop, holding no lock.
 * ============================================================================ */
static void fault_point(uint32_t t)
{
    if (fault_spin & (1u << t)) { for (;;) { } }           /* starves the lower */
    if (fault_hang & (1u << t)) { for (;;) { os_delay(1000); } }  /* stuck alone */
}

/* ============================================================================
 *  input - 100 Hz.  The only task that touches the buttons, the ADC and TIM3.
 * ============================================================================ */
static void task_input(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, INPUT_MS);
        prof_begin(&prof[T_INPUT], last, os_ticks());
        fault_point(T_INPUT);

        pad_t p;
        pad_read(&p);
        os_mutex_lock(&kbd_lock);                /* the station's keys, folded  */
        kbd_apply(&kbd, &p, os_ticks());         /* into the same pad (kbd.h)   */
        os_mutex_unlock(&kbd_lock);

        /* STICKY EDGES.  Input runs twice per app step.  If it simply
         * overwrote shared_pad, a press seen on the first run would be gone
         * by the second - the app would miss it.  So edges accumulate until
         * the app takes them; levels (stick, knob, down) are just replaced. */
        __disable_irq();
        p.pressed  |= shared_pad.pressed;
        p.released |= shared_pad.released;
        shared_pad  = p;
        __enable_irq();

        uint32_t now = os_ticks();
        if (snd_req) { snd_req = 0; beep(snd_hz, snd_ms, now); }
        beep_poll(now);

        prof_end(&prof[T_INPUT]);
        health_beat(&health, T_INPUT);
    }
}

/* ============================================================================
 *  app - APP_PERIOD_MS.  Your logic runs here, at a fixed step.
 * ============================================================================ */
static void task_app(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, APP_PERIOD_MS);
        prof_begin(&prof[T_APP], last, os_ticks());
        fault_point(T_APP);

        pad_t p;
        __disable_irq();                         /* take the controls ...     */
        p = shared_pad;
        shared_pad.pressed = shared_pad.released = 0;   /* ... and the edges */
        __enable_irq();

        os_mutex_lock(&app_lock);
        app_step(APP_PERIOD_MS, &p);             /* fixed dt: deterministic   */
        os_mutex_unlock(&app_lock);

        prof_end(&prof[T_APP]);
        health_beat(&health, T_APP);
    }
}

/* ============================================================================
 *  display - 20 Hz, low priority.  Draw into RAM under app_lock (fast), then
 *  send the dirty pages under bus_lock (slow: up to ~25 ms for a full frame).
 *  The app is never blocked by the I2C transfer - only by the drawing.
 * ============================================================================ */
static void task_display(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, DISPLAY_MS);
        prof_begin(&prof[T_DISPLAY], last, os_ticks());
        fault_point(T_DISPLAY);

        os_mutex_lock(&app_lock);
        oled_clear();
        app_draw();
        os_mutex_unlock(&app_lock);

        if (oled_ok) {
            os_mutex_lock(&bus_lock);
            int rc = oled_flush();
            os_mutex_unlock(&bus_lock);
            if (rc != 0) { i2c_errors++; }
        }

        /* The station's copy of the screen: at most 2 frames a pass, and only
         * while the UART ring has room for a whole one - so the mirror never
         * waits for the serial port, it just falls behind.  It sends nothing
         * unless the station asked ($MIRROR,1).  Runs even with no OLED: it
         * copies the framebuffer in RAM, not the panel. */
        static char frame[MIRROR_FRAME_MAX];     /* static: off the task stack  */
        for (int i = 0; i < 2 && uart_tx_room() >= MIRROR_FRAME_MAX; i++) {
            os_mutex_lock(&mirror_lock);         /* the shell's mirror_frame()  */
            size_t len = mirror_next(frame, sizeof frame, os_ticks());
            os_mutex_unlock(&mirror_lock);
            if (len == 0) { break; }
            send_raw(frame, len);
        }

        prof_end(&prof[T_DISPLAY]);
        health_beat(&health, T_DISPLAY);
    }
}

/* ============================================================================
 *  telemetry - 10 Hz: $APP,... from the app; once a second $SYS,... for the
 *  system itself:  $SYS,<ms>,<idle per mille>,<switches>,<i2c err>,<oled bytes>,<feeds>
 * ============================================================================ */
static os_task_t *idle_task(void)
{
    for (uint32_t i = 0; i < os_task_count(); i++) {
        os_task_t *t = os_task(i);
        if (t && strcmp(t->name, "idle") == 0) { return t; }
    }
    return 0;
}

static void task_tel(void *arg)
{
    (void)arg;
    char body[80];
    uint32_t last = os_ticks(), n = 0, idle_prev = 0, t_prev = last;
    for (;;) {
        os_delay_until(&last, TEL_MS);
        prof_begin(&prof[T_TEL], last, os_ticks());
        fault_point(T_TEL);

        os_mutex_lock(&app_lock);
        int len = app_telemetry(body, sizeof body);
        os_mutex_unlock(&app_lock);
        if (len >= (int)sizeof body) { len = (int)sizeof body - 1; }   /* truncated */
        if (!tel_quiet) { send_body(body, len); }           /* `quiet` stops it */

        if (++n % 10u == 0u) {
            os_task_t *idle = idle_task();
            uint32_t now = os_ticks(), it = idle ? idle->ticks : 0;
            uint32_t permille = (now != t_prev) ? (it - idle_prev) * 1000u / (now - t_prev) : 0;
            idle_prev = it;  t_prev = now;
            len = snprintf(body, sizeof body, "SYS,%lu,%lu,%lu,%lu,%lu,%lu",
                           (unsigned long)now, (unsigned long)permille,
                           (unsigned long)os_switches(), (unsigned long)i2c_errors,
                           (unsigned long)oled_bytes_sent(), (unsigned long)health.feeds);
            if (!tel_quiet) { send_body(body, len); }
        }

        prof_end(&prof[T_TEL]);
        health_beat(&health, T_TEL);
    }
}

/* ============================================================================
 *  health - 4 Hz, LOWEST priority on purpose.  If anything above it hogs the
 *  CPU, this task starves too, the refreshes stop, and the IWDG resets the
 *  chip.  A watchdog fed from a high-priority task would hide exactly that.
 * ============================================================================ */
static void task_health(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), reported = 0;
    for (;;) {
        os_delay_until(&last, HEALTH_MS);
        int verdict = health_check(&health, os_ticks());

        if (verdict == HEALTH_FEED) {
            wdog_feed();
            GPIOA->ODR ^= 1u << LED_PIN;            /* only this task writes PA5 */
            if (reported) { say("health: all tasks beating again\n"); reported = 0; }
            continue;
        }
        if (health.missing != reported) {
            char names[48] = "";                    /* one line, one say()       */
            for (uint32_t i = 0; i < T_WATCHED; i++) {
                if (health.missing & (1u << i)) {
                    strncat(names, " ", sizeof names - strlen(names) - 1u);
                    strncat(names, prof[i].name, sizeof names - strlen(names) - 1u);
                }
            }
            say("health: NOT feeding the watchdog - missing:%s (IWDG resets within %u ms on silicon)\n",
                names, WDOG_TIMEOUT_MS);
            reported = health.missing;
        }
        if (verdict == HEALTH_FORCE) {
            /* Still alive 1.5 s after the last refresh.  The IWDG should have
             * reset us at 1.0 s; it did not, so there is no IWDG here - which
             * is Wokwi's documented case.  Do its job in software. */
            say("health: no IWDG reset after %u ms - no hardware watchdog here?"
                " NVIC_SystemReset()\n", FORCE_AFTER_MS);
            os_delay(50);                           /* let the UART drain        */
            NVIC_SystemReset();
        }
    }
}

/* ============================================================================
 *  main - bring everything up, report it, start the scheduler
 * ============================================================================ */
int main(void)
{
    boot_flags = wdog_reset_flags();           /* first: before anything resets */
    uart_init(SystemCoreClock, BAUD);
    printf("\n=== SOC3050 lesson 19 - Final Project template, app \"%s\" ===\n", APP_NAME);
    printf("  reset    : %s (CSR2 0x%08lX)\n", wdog_reset_cause(boot_flags),
           (unsigned long)boot_flags);

    int a = pad_init();
    printf("  pad      : %s\n", a == 0 ? "buttons + ADC ready" : "buttons only - ADC failed");
    pin_mode(GPIOA, LED_PIN, MODE_OUTPUT);     /* pad_init() opened GPIOA's gate */

    i2c_init();
    int o = oled_init();
    oled_ok = (uint8_t)(o == 0);
    printf("  oled     : %s\n", o == 0 ? "SSD1306 at 0x3C" : o == I2C_NACK ? "no ACK at 0x3C"
                                     : "bus timeout");

    beep_init();
    prof_init();

    /* Seed the app's RNG from things that differ between boards and runs:
     * the internal reference's noise and the knob.  In a simulator both are
     * steady, so the game repeats - which is what a test wants anyway. */
    uint32_t seed = (uint32_t)adc_read(ADC_CH_VREFINT) * 2654435761u
                  ^ (uint32_t)adc_read(ADC_CH_PA4);
    app_init(seed);

    health_init(&health, T_WATCHED, FORCE_AFTER_MS, 0);
    int w = wdog_start();
    printf("  watchdog : IWDG %u ms%s, fed by health every %u ms if all %u tasks beat\n",
           WDOG_TIMEOUT_MS, w == 0 ? "" : " (update flags stuck)", HEALTH_MS, (unsigned)T_WATCHED);
    printf("  shell    : help  stats  health  scan  hang <task>  spin <task>  reset  quiet  loud\n");
    printf("  station  : uv run tools/station/station.py - the OLED large, $APP/$SYS charts, keys\n");
    printf("%s\n", KEYS_HELP);              /* say() needs the scheduler running */
    kbd_init(&kbd);
    beep(660, 60, 0);

    os_task_create("input",   task_input,   0, stk_input,  W_INPUT,  PRIO_INPUT);
    os_task_create("app",     task_app,     0, stk_app,    W_APP,    PRIO_APP);
    os_task_create("telem",   task_tel,     0, stk_tel,    W_TEL,    PRIO_TEL);
    os_task_create("display", task_display, 0, stk_disp,   W_DISP,   PRIO_DISPLAY);
    os_task_create("health",  task_health,  0, stk_health, W_HEALTH, PRIO_HEALTH);
    os_task_create("shell",   task_shell,   0, stk_shell,  W_SHELL,  PRIO_SHELL);
    os_start();
}
