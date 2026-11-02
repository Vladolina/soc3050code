/*
 * Main.c - SOC3050 lesson 14: Drone Missions
 *          ST Nucleo-C031C6, STM32C031C6, Cortex-M0+ at 48 MHz
 *
 * A drone that flies a mission, holds its position in a gale, and comes home
 * on its own when the fence or the battery says so - all inside the chip.
 * There is no drone in Wokwi, so the firmware carries one (world.c) and flies
 * it with a flight computer (flight.c) that sees it only through noisy,
 * late sensors.  The mission comes from Python, or from button A.
 *
 *   task    prio  rate     job
 *   world    5    100 Hz   step the simulated aircraft and the weather
 *   ctl      4     50 Hz   sensors -> control_step() -> actuators
 *   link     3    on byte  $-frames from the host: WP, MISSION, MODE, PARAM;
 *                           the station's $KEY and $MIRROR
 *   tel      2      5 Hz   $POS, $TRU, $EVT, $MIS to the host
 *   ui       1     50 Hz   joystick, buttons, knob; the OLED map at 10 Hz,
 *                           copied to the station as $FB
 *
 * Controls (the app board):
 *   A        on the ground: arm and fly the mission (the default one if none
 *            was uploaded); in HOLD: resume the mission
 *   B        return to launch
 *   SEL      (joystick press) HOLD here
 *   joystick in HOLD: move the hold point, up to 3 m/s
 *   knob     wind strength, 0..10 m/s
 *
 * The same controls from the station on the PC (kbd.h), which also shows the
 * OLED large (mirror.h): arrows = stick, a = A, b = B, s = SEL, 1-5 = knob,
 * - / + = cruise speed, ? = the list.  "quiet" / "loud" + Enter stop and
 * restart $POS and $TRU; events ($EVT, $MIS, $ACK) always go out.
 *
 * Build:     build.bat          Simulate:  simulate.bat
 * Host:      python host\mission.py host\default.mission     (frames to paste)
 *            python host\mission.py --score capture.txt      (score a flight)
 * Station:   python tools\station\station.py                 (from the repo root)
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
#include "world.h"
#include "flight.h"
#include "mavlite.h"
#include "ui.h"
#include "fmath.h"
#include "fm.h"

#define BAUD          115200u

#define STACK(name, words) static uint32_t name[words] __attribute__((aligned(8)))
STACK(stk_world, 160);  STACK(stk_ctl, 224);  STACK(stk_link, 320);
STACK(stk_tel, 288);    STACK(stk_ui, 288);

/* ---- shared state, and who guards it --------------------------------------
 *   world        world_lock   world task steps it; ctl senses it; tel reads
 *                             the truth; link and ui change the weather
 *   flight state fc_lock      ctl runs it; link, ui and tel command and read it
 *   act          a critical section: four words, copied in a few cycles
 *   kbd          kbd_lock     the station's keys: link feeds them, ui applies
 * Lock order, where one is held inside the other: fc_lock, THEN world_lock
 * (only the link task nests them).  One order, no deadlock - lesson 07.
 * kbd_lock is never held with another lock, so it needs no place in it. */
static world_t     world;
static actuators_t act;
static kbd_t       kbd;
static os_mutex_t  world_lock, fc_lock, print_lock, kbd_lock;
static volatile uint8_t knob_owns_wind = 1, tel_on = 1, oled_ok;

/* Every line out goes through here: format into a buffer on this task's own
 * stack, then hand the bytes to uart.c's _write() - the C library's one door
 * to the outside (lesson 04).  This lesson does NOT use printf(): printing to
 * a stdio STREAM drags in its buffering, flushing and malloc, about 1.7 KB of
 * flash this chip could not spare (slide 21).  vsnprintf only formats.     */
int _write(int fd, const char *buf, int len);           /* uart.c */

/* Bytes that are already formatted - an OLED mirror frame - skip vsnprintf
 * but take the same lock, so no line is ever cut in two by another task's. */
static void say_raw(const char *buf, int n)
{
    int tasks = os_current_task() != 0;                         /* before os_start: no lock */
    if (tasks) { os_mutex_lock(&print_lock); }
    (void)_write(1, buf, n);
    if (tasks) { os_mutex_unlock(&print_lock); }
}

static void say(const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n <= 0) { return; }
    if (n > (int)sizeof buf - 1) { n = (int)sizeof buf - 1; }   /* truncated */
    say_raw(buf, n);
}

/* ============================================================================
 *  Measuring: how many CPU cycles does one step cost?
 * ============================================================================
 * SysTick counts DOWN from LOAD (47999) once per millisecond, so a time stamp
 * is (milliseconds, counter).  The difference between two stamps is cycles,
 * across any number of ticks.  Re-read if a tick lands in between.          */
typedef struct { uint32_t ms, val; } stamp_t;

static stamp_t stamp(void)
{
    stamp_t s;
    do { s.ms = os_ticks();  s.val = SysTick->VAL; } while (s.ms != os_ticks());
    return s;
}

static uint32_t cycles(stamp_t a, stamp_t b)
{
    return (b.ms - a.ms) * (SysTick->LOAD + 1u) + a.val - b.val;
}

typedef struct { volatile uint32_t last, max, sum, n; } cost_t;
static cost_t cost_world, cost_ctl;

static void account(cost_t *c, uint32_t cy)
{
    c->last = cy;
    if (cy > c->max) { c->max = cy; }
    c->sum += cy;  c->n++;
}

/* ============================================================================
 *  world - the aircraft, 100 times a second
 * ============================================================================ */
static void task_world(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, 1000u / WORLD_HZ);
        actuators_t a;
        __disable_irq(); a = act; __enable_irq();
        os_mutex_lock(&world_lock);
        stamp_t t0 = stamp();
        world_step(&world, &a, WORLD_DT);
        stamp_t t1 = stamp();
        os_mutex_unlock(&world_lock);
        account(&cost_world, cycles(t0, t1));
    }
}

/* ============================================================================
 *  ctl - the flight computer, 50 times a second
 * ============================================================================ */
static void task_ctl(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, 1000u / CTRL_HZ);
        sensors_t s;
        os_mutex_lock(&world_lock);
        world_sense(&world, &s);                 /* the ONLY door from the world */
        os_mutex_unlock(&world_lock);

        actuators_t a;
        os_mutex_lock(&fc_lock);
        stamp_t t0 = stamp();
        control_step(&s, &a);
        stamp_t t1 = stamp();
        os_mutex_unlock(&fc_lock);
        account(&cost_ctl, cycles(t0, t1));

        __disable_irq(); act = a; __enable_irq();
    }
}

/* ============================================================================
 *  link - frames in, ACKs out; plain words go to a small shell
 * ============================================================================ */
static int sim_param(const char *name, int32_t v)       /* called with fc_lock held */
{
    int err = FC_OK;
    os_mutex_lock(&world_lock);
    if      (strcmp(name, "WIND")  == 0 && v >= 0 && v <= 150)  { world_set_wind(&world, v * 0.1f, world.wind_dir); knob_owns_wind = 0; }
    else if (strcmp(name, "WDIR")  == 0 && v >= -360 && v <= 360) { world.wind_dir = v * 0.0174533f; }
    else if (strcmp(name, "BAT")   == 0 && v >= 0 && v <= 100)  { world.battery = (float)v; }
    else if (strcmp(name, "DRAIN") == 0 && v >= 0 && v <= 1000) { world.drain = v * 0.01f; }
    else if (strcmp(name, "TAU")   == 0 && v >= 20 && v <= 1000) { world.tau_att = v * 0.001f; }   /* ms: lesson 13, slower */
    else if (strcmp(name, "WIND") == 0 || strcmp(name, "WDIR") == 0
          || strcmp(name, "BAT") == 0  || strcmp(name, "DRAIN") == 0 || strcmp(name, "TAU") == 0) { err = FC_E_RANGE; }
    else { err = FC_E_PARAM; }
    os_mutex_unlock(&world_lock);
    return err;
}

static uint32_t us(uint32_t cy) { return cy / 48u; }    /* 48 MHz */

static void cmd_stats(void)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < os_task_count(); i++) { total += os_task(i)->ticks; }
    say("step cost   world %lu us (max %lu, mean %lu)   ctl %lu us (max %lu, mean %lu)\n",
        (unsigned long)us(cost_world.last), (unsigned long)us(cost_world.max),
        (unsigned long)us(cost_world.n ? cost_world.sum / cost_world.n : 0),
        (unsigned long)us(cost_ctl.last), (unsigned long)us(cost_ctl.max),
        (unsigned long)us(cost_ctl.n ? cost_ctl.sum / cost_ctl.n : 0));
    for (uint32_t i = 0; i < os_task_count(); i++) {
        os_task_t *t = os_task(i);
        say("  %-6s CPU %3lu.%lu%%  stack %lu of %lu words\n", t->name,
            (unsigned long)(total ? t->ticks * 100u / total : 0),
            (unsigned long)(total ? t->ticks * 1000u / total % 10u : 0),
            (unsigned long)os_stack_used(t), (unsigned long)t->stack_words);
    }
    const ml_stats_t *m = ml_stats();
    const volatile uart_stats_t *u = uart_stats();
    say("frames good %lu bad %lu unknown %lu refused %lu; rx dropped %lu; oled bytes %lu\n",
        (unsigned long)m->good, (unsigned long)m->bad, (unsigned long)m->unknown,
        (unsigned long)m->refused, (unsigned long)u->rx_dropped, (unsigned long)oled_bytes_sent());
}

static void cmd_params(void)
{
    for (unsigned i = 0; fc_param_name(i); i++) {
        int32_t v = 0;
        os_mutex_lock(&fc_lock);
        (void)fc_param_get(fc_param_name(i), &v);
        os_mutex_unlock(&fc_lock);
        say("  %-9s %ld\n", fc_param_name(i), (long)v);
    }
    say("  sim: WIND (dm/s), WDIR (deg), BAT (%%), DRAIN (%%/s x100), TAU (ms) - set only\n");
}

static void cmd_mission(void)
{
    fc_t *f = fc_get();
    os_mutex_lock(&fc_lock);
    uint8_t n = f->n_wp, cur = f->cur, act_ = f->mission_active;
    vec3_t wp[FC_MAX_WP];
    memcpy(wp, f->wp, sizeof wp);
    os_mutex_unlock(&fc_lock);
    say("%u waypoints%s\n", (unsigned)n, act_ ? ", active" : "");
    for (uint8_t i = 0; i < n; i++) {
        say(" %c%u  x %ld  y %ld  alt %ld dm\n", (act_ && i == cur) ? '>' : ' ', (unsigned)i,
            (long)(wp[i].x * 10.0f), (long)(wp[i].y * 10.0f), (long)(wp[i].z * 10.0f));
    }
}

static void shell(const char *line)
{
    if      (strcmp(line, "stats") == 0)   { cmd_stats(); }
    else if (strcmp(line, "params") == 0)  { cmd_params(); }
    else if (strcmp(line, "mission") == 0) { cmd_mission(); }
    else if (strcmp(line, "tel") == 0)     { tel_on = !tel_on; say("telemetry %s\n", tel_on ? "on" : "off"); }
    else if (strcmp(line, "quiet") == 0)   { tel_on = 0; say("quiet: $POS $TRU off - type loud to resume\n"); }
    else if (strcmp(line, "loud") == 0)    { tel_on = 1; say("loud: $POS $TRU on\n"); }
    else { say("commands: stats  params  mission  tel  quiet  loud   - or a $-frame (mission.py prints them)\n"); }
}

static void print_keys(void)
{
    say("  keys     : arrows = stick (HOLD), a = A, b = B, s = SEL, 1-5 = knob (wind)\n");
    say("             - / + = cruise speed,  ? = this,  quiet / loud + Enter\n");  /* say(): 127 chars a call */
}

/* - and + from the station: the mission's cruise speed, 1 m/s a press -
 * the same $PARAM,CRUISE a host could send, clamped to its legal range. */
static void cruise_step(int dir)
{
    int32_t v = 0;
    os_mutex_lock(&fc_lock);
    (void)fc_param_get("CRUISE", &v);                   /* cm/s */
    v += dir * 100;
    if (v < 50)   { v = 50; }
    if (v > 1200) { v = 1200; }
    (void)fc_param_set("CRUISE", v);
    os_mutex_unlock(&fc_lock);
    say("cruise %ld.%02ld m/s\n", (long)(v / 100), (long)(v % 100));
}

/* A line from the station that is not a mission frame: a key ($KEY) or the
 * mirror's heartbeat ($MIRROR, every second).  Claimed by its NAME, before
 * mavlite's strict parser sees it - so even a garbled one is never NAK'd and
 * never counted as a bad frame.  Returns 1 if the line was one of these. */
static int station_line(const char *line)
{
    if (kbd_is_key(line)) {
        os_mutex_lock(&kbd_lock);
        int cmd = kbd_frame(&kbd, line, os_ticks());
        os_mutex_unlock(&kbd_lock);
        if      (cmd == KBD_HELP)   { print_keys(); }
        else if (cmd == KBD_SLOWER) { cruise_step(-1); }
        else if (cmd == KBD_FASTER) { cruise_step(+1); }
        return 1;
    }
    if (strncmp(line, "$MIRROR,", 8) == 0) {
        (void)mirror_frame(line, os_ticks());         /* a bad checksum: ignored */
        return 1;
    }
    return 0;
}

static void task_link(void *arg)
{
    (void)arg;
    static char line[96], reply[160];
    uint32_t n = 0;
    for (;;) {
        char c = (char)uart_getc();
        if (c != '\r' && c != '\n') { if (n < sizeof line - 1u) { line[n++] = c; } continue; }
        if (n == 0) { continue; }
        line[n] = '\0';
        n = 0;
        if (station_line(line)) { continue; }
        os_mutex_lock(&fc_lock);
        int r = ml_handle(line, reply, sizeof reply, sim_param);
        os_mutex_unlock(&fc_lock);
        if (r == ML_NOT_FRAME) { shell(line); }
        else                   { say("%s", reply); }
    }
}

/* ============================================================================
 *  tel - what the drone believes ($POS), what is true ($TRU), what happened
 * ============================================================================ */
static void task_tel(void *arg)
{
    (void)arg;
    static char b[112];
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, 200);
        fc_event_t ev[FC_EVENTS];
        unsigned nev = 0;

        os_mutex_lock(&fc_lock);
        int w = ml_pos(b, sizeof b, fc_get());
        while (nev < FC_EVENTS && fc_event_pop(&ev[nev])) { nev++; }
        os_mutex_unlock(&fc_lock);
        if (tel_on && w) { say("%s", b); }

        os_mutex_lock(&world_lock);
        float x = world.px, y = world.py, z = world.pz;
        float wx = world.wind_x, wy = world.wind_y;
        uint32_t t = world.t_ms;
        os_mutex_unlock(&world_lock);
        if (tel_on && ml_tru(b, sizeof b, t, x, y, z, m_sqrt(wx * wx + wy * wy))) { say("%s", b); }

        for (unsigned i = 0; i < nev; i++) {           /* events always go out */
            if (ml_evt(b, sizeof b, &ev[i])) { say("%s", b); }
            if (ev[i].kind == EV_MISSION) {             /* log the mission flown */
                for (uint8_t k = 0; k < ev[i].a; k++) {
                    os_mutex_lock(&fc_lock);
                    w = ml_mis(b, sizeof b, fc_get(), k);
                    os_mutex_unlock(&fc_lock);
                    if (w) { say("%s", b); }
                }
            }
        }
    }
}

/* ============================================================================
 *  ui - the sticks, the knob, the buttons, the screen, the beeper
 * ============================================================================ */
static void button(const char *what, int rc)
{
    say("button %s: %s\n", what, fc_error_name(rc));
}

/* The OLED mirror (mirror.h): up to 2 $FB frames a pass, and only while the
 * TX ring has room for a whole one - so the mirror never makes this task
 * wait, and never fills the ring a $POS or an $EVT needs.  Every pass, not
 * only the 10 Hz drawing ones: between two flushes the framebuffer does not
 * change, and a moving map changes several of the 32 segments a frame -
 * 2 a frame would fall behind.  The station asks for frames with $MIRROR; a
 * terminal never sees one.  The buffer is static: task stacks are small.  */
static void mirror_send(uint32_t now)
{
    static char frame[MIRROR_FRAME_MAX];
    for (int i = 0; i < 2 && uart_tx_room() >= MIRROR_FRAME_MAX; i++) {
        size_t n = mirror_next(frame, sizeof frame, now);
        if (n == 0) { break; }
        say_raw(frame, (int)n);
    }
}

static void task_ui(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), frame = 0;
    int16_t knob_last = -1000;
    uint8_t seen_mode = M_DISARMED, seen_cur = 0;
    ui_view_t v;

    for (;;) {
        os_delay_until(&last, 20);
        uint32_t now = os_ticks();
        beep_poll(now);
        pad_t p;
        pad_read(&p);
        os_mutex_lock(&kbd_lock);
        kbd_apply(&kbd, &p, now);          /* the station's keys, into the same pad */
        os_mutex_unlock(&kbd_lock);

        /* the knob is the weather - until a $PARAM,WIND takes over */
        if (p.adc_ok) {
            if (abs(p.knob - knob_last) > 20) { knob_last = p.knob;  knob_owns_wind = 1; }
            if (knob_owns_wind) {
                os_mutex_lock(&world_lock);
                world_set_wind(&world, p.knob * 0.01f, world.wind_dir);
                os_mutex_unlock(&world_lock);
            }
        }

        os_mutex_lock(&fc_lock);
        fc_t *f = fc_get();
        int rcA = -1, rcB = -1, rcS = -1;
        if (p.pressed & PAD_A) {
            if (f->mode == M_DISARMED) {
                if (f->n_wp == 0u) { fc_mission_default(); }   /* no host: the built-in one */
                rcA = fc_mission_start();
            } else {
                rcA = fc_set_mode(M_MISSION, R_BUTTON);        /* resume */
            }
        }
        if (p.pressed & PAD_B)   { rcB = fc_set_mode(M_RTL,  R_BUTTON); }
        if (p.pressed & PAD_SEL) { rcS = fc_set_mode(M_HOLD, R_BUTTON); }
        fc_nudge(p.x * 0.03f, p.y * 0.03f);                   /* 100 -> 3 m/s */

        /* sounds: a waypoint chirps, a failsafe growls, a mode change blips */
        if (f->mode != seen_mode) {
            if (f->reason == R_FENCE || f->reason == R_BATTERY || f->reason == R_BATTERY_CRIT) {
                beep(300, 500, now);
            } else {
                beep(f->mode == M_DISARMED ? 600 : 1200, 80, now);
            }
            seen_mode = f->mode;
        } else if (f->cur != seen_cur && f->mission_active) {
            beep(2000, 50, now);
        }
        seen_cur = f->cur;

        int draw = (++frame % 5u == 0u) && oled_ok;            /* 10 Hz */
        float wind = world.wind_speed;                         /* one aligned word */
        if (draw) { ui_from_fc(&v, f, wind); }
        os_mutex_unlock(&fc_lock);

        if (rcA >= 0) { button("A (fly)", rcA); }
        if (rcB >= 0) { button("B (RTL)", rcB); }
        if (rcS >= 0) { button("SEL (hold)", rcS); }
        if (draw) {
            ui_draw(&v);                 /* RAM: no lock needed, the copy is ours */
            (void)oled_flush();          /* ~25 ms of I2C at worst: lowest priority */
        }
        mirror_send(now);                /* the station's copy of the screen just flushed */
    }
}

int main(void)
{
    uart_init(SystemCoreClock, BAUD);
    say("\n=== SOC3050 lesson 14 - Drone Missions ===\n");

    int a = pad_init();
    say("  pad      : %s\n", a == 0 ? "joystick, knob, A/B/SEL" : "ADC FAILED - buttons only");
    i2c_init();
    int o = oled_init();
    oled_ok = (uint8_t)(o == 0);
    say("  OLED     : %s\n", o == 0 ? "0x3C answered" : "no answer at 0x3C - map off");
    beep_init();

    world_init(&world, 1u);
    fc_reset();
    say("  drone    : simulated in this chip - world %u Hz, control %u Hz, GPS 10 Hz\n",
        (unsigned)WORLD_HZ, (unsigned)CTRL_HZ);
    say("  press A to fly the built-in mission, or paste mission.py's frames\n");
    say("  shell    : stats  params  mission  tel  quiet  loud\n");
    say("  station  : uv run tools/station/station.py - OLED large; keys arrows a b s 1-5 - + ?\n\n");
    kbd_init(&kbd);

    os_task_create("world", task_world, 0, stk_world, 160, 5);
    os_task_create("ctl",   task_ctl,   0, stk_ctl,   224, 4);
    os_task_create("link",  task_link,  0, stk_link,  320, 3);
    os_task_create("tel",   task_tel,   0, stk_tel,   288, 2);
    os_task_create("ui",    task_ui,    0, stk_ui,    288, 1);
    os_start();
}
