/*
 * Main.c - SOC3050 lesson 16: Robot Navigation
 *          ST Nucleo-C031C6, STM32C031C6, Cortex-M0+ at 48 MHz
 *
 * A robot that has never seen the room finds its way across it: it maps the
 * walls with five rangefinders as it drives, plans with A* on what it has
 * mapped, replans when the map proves the plan wrong, and lets a reactive
 * safety layer overrule the planner when a wall is closer than the map says.
 *
 * There is no robot.  The robot AND the room are simulated on this chip, in
 * two separate halves that share nothing but sim.h's two structs:
 *
 *   task "world" (20 ms)   world.c: true map, true pose, motors, sensors
 *   task "robot" (40 ms)   nav.c:   control_step(&sensors, &actuators)
 *   task "ui"    (20 ms)   pad: A start/reset, B next level, SEL show truth,
 *                          knob speed, joystick moves the goal; $NAV frames;
 *                          every 5th time, view.c -> oled.c: the OLED frame,
 *                          and its copy for the station (mirror.h)
 *   task "shell"           the serial line: commands, $GOAL/$MAP frames from
 *                          host/planner.py, $KEY/$MIRROR from the station
 *
 * The station, python tools/station/station.py, draws the robot's map large
 * and sends the pad's controls as keys (kbd.h): arrows = stick, a = A,
 * b = B, s = SEL, 1-5 = knob.  "quiet" / "loud" stop and restart $NAV.
 *
 * This file is the HARNESS: it alone sees both halves, so it can draw the
 * true pose beside the estimated one and report what the robot cannot know.
 *
 * Build:     build.bat
 * Simulate:  simulate.bat, paste diagram.json, upload Main.elf, press A.
 * Host:      host\run.bat                         (the whole thing, on a PC)
 *            python host\planner.py --level 3     (the same A*, in Python)
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
#include "pad.h"
#include "kbd.h"
#include "mirror.h"
#include "beep.h"
#include "sim.h"
#include "levels.h"
#include "world.h"
#include "nav.h"
#include "astar.h"
#include "view.h"

#define BAUD        115200u
#define LD4_PIN     5u            /* PA5: on when the robot has arrived      */
#define PENALTY_MS  5000u         /* leaderboard: each collision costs 5 s   */

#define STACK(name, words) static uint32_t name[words] __attribute__((aligned(8)))
/* Sized from -fstack-usage plus the 16-word context frame, then checked
 * with the "stats" command's high-water marks: see README.md. */
STACK(stk_world,  96);  STACK(stk_robot, 224);  STACK(stk_ui, 272);  STACK(stk_shell, 304);

/* ---- the two halves meet only here: one sample, one command ---------------
 * Each is a small struct written by one task and read by another, so each
 * copy is a two-line critical section (lesson 03: no torn reads). */
static sensors_t   shared_sens;
static actuators_t shared_cmd;

static os_mutex_t world_lock;      /* world.c's state: world, view, shell, ui  */
static os_mutex_t nav_lock;        /* nav.c's state:   robot, view, shell, ui  */
static os_mutex_t print_lock;      /* whole lines on the serial port           */
static os_mutex_t kbd_lock;        /* the station's keys: shell feeds, ui reads */
/* Lock order, everywhere: world_lock, then nav_lock.  Never the other way
 * round, or two tasks can each hold one and wait for the other for ever. */

static volatile uint8_t  show_truth;
static volatile uint32_t run_start;        /* world time when RUN began      */
static volatile uint32_t ctrl_cycles_max, world_cycles_max, flush_ms_max;
static volatile int      level = 0;
static volatile uint8_t  custom_loaded;
static volatile uint8_t  nav_quiet;        /* "quiet": no periodic $NAV      */
static kbd_t             kbd;              /* under kbd_lock                 */

/* ============================================================================
 *  The planner's stopwatch: CPU cycles, from SysTick
 * ============================================================================
 * The kernel owns SysTick: it counts down from LOAD (47999) to 0 once per
 * millisecond, and os_ticks() counts the milliseconds.  Together they give a
 * 48 MHz cycle count.  Read the millisecond count on both sides of VAL and
 * retry if it changed, so a tick between the two reads cannot tear it. */
uint32_t nav_clock(void)
{
    uint32_t ms, val;
    do {
        ms  = os_ticks();
        val = SysTick->VAL;
    } while (ms != os_ticks());
    return ms * (SysTick->LOAD + 1u) + (SysTick->LOAD - val);
}

/* All output goes vsnprintf -> _write (uart.c's ring buffer).  Not printf:
 * vprintf links a second copy of newlib's formatter beside vsnprintf's; dropping
 * it and atoi/strtoul (below) saved 917 B of flash, measured. */
int _write(int fd, const char *buf, int len);

static void out(const char *fmt, va_list ap)
{
    char line[112];
    int n = vsnprintf(line, sizeof line, fmt, ap);
    if (n > (int)sizeof line - 1) { n = (int)sizeof line - 1; }
    if (n > 0) { _write(1, line, n); }
}

static void boot(const char *fmt, ...)        /* before the kernel: no lock */
{
    va_list ap;
    va_start(ap, fmt);
    out(fmt, ap);
    va_end(ap);
}

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    os_mutex_lock(&print_lock);
    out(fmt, ap);
    os_mutex_unlock(&print_lock);
    va_end(ap);
}

/* atoi() and strtoul() pull in strtol and the ctype table.  These do not. */
static int num(const char *t)
{
    int v = 0, neg = 0;
    if (!t) { return 0; }
    if (*t == '-') { neg = 1; t++; }
    while (*t >= '0' && *t <= '9') { v = v * 10 + (*t++ - '0'); }
    return neg ? -v : v;
}

/* Split `s` in place at each `sep`; up to `max` fields.  Not strtok():
 * newlib's strtok() keeps its state in a malloc'd block and asserts, and
 * that one call linked malloc, assert and fprintf - over 3 KB. */
static int split(char *s, char sep, char **f, int max)
{
    int n = 0;
    while (*s && n < max) {
        while (*s == sep) { s++; }
        if (!*s) { break; }
        f[n++] = s;
        while (*s && *s != sep) { s++; }
        if (*s) { *s++ = '\0'; }
    }
    return n;
}

static uint32_t hex(const char *t)
{
    uint32_t v = 0;
    for (; t && *t; t++) {
        char c = *t;
        uint32_t d = (c >= '0' && c <= '9') ? (uint32_t)(c - '0')
                   : (c >= 'A' && c <= 'F') ? (uint32_t)(c - 'A' + 10)
                   : (c >= 'a' && c <= 'f') ? (uint32_t)(c - 'a' + 10) : 16u;
        if (d > 15u) { break; }
        v = (v << 4) | d;
    }
    return v;
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

/* mm and radians to integers for printf: newlib-nano prints no floats */
static long mm(float v)    { return (long)(v + (v >= 0.0f ? 0.5f : -0.5f)); }
static long mdeg(float th) { return (long)(th * 57295.78f); }

/* ============================================================================
 *  Levels: load one into the world, and tell the robot only where it starts
 *  and where to go - never the map
 * ============================================================================ */
static void load_level_unlocked(int lv)
{
    world_load(lv);
    const world_t *w = world();
    nav_reset(w->start_x, w->start_y);
    nav_set_goal(w->goal_x, w->goal_y);
    level = w->level;
}

static void load_level(int lv)
{
    os_mutex_lock(&world_lock);
    os_mutex_lock(&nav_lock);
    load_level_unlocked(lv);
    os_mutex_unlock(&nav_lock);
    os_mutex_unlock(&world_lock);
    __disable_irq();
    shared_cmd.wheel_l_mm_s = shared_cmd.wheel_r_mm_s = 0;
    __enable_irq();
    run_start = 0;
    pin_low(GPIOA, LD4_PIN);
}

static const char *level_name(int lv) { return lv < N_LEVELS ? level_table[lv].name : "Custom"; }

/* ============================================================================
 *  task "world": reality, 50 times a second
 * ============================================================================ */
static void task_world(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, WORLD_MS);
        actuators_t cmd;
        __disable_irq(); cmd = shared_cmd; __enable_irq();

        uint32_t c0 = nav_clock();
        os_mutex_lock(&world_lock);
        world_step(&cmd);
        if (world()->t_ms % CONTROL_MS == 0u) {           /* sensors at 25 Hz */
            sensors_t s;
            world_sense(&s);
            __disable_irq(); shared_sens = s; __enable_irq();
        }
        os_mutex_unlock(&world_lock);
        uint32_t c = nav_clock() - c0;
        if (c > world_cycles_max) { world_cycles_max = c; }
    }
}

/* ============================================================================
 *  task "robot": the robot's whole software, 25 times a second
 * ============================================================================ */
static void task_robot(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    int      last_mode = NAV_IDLE;
    unsigned last_coll = 0;
    for (;;) {
        os_delay_until(&last, CONTROL_MS);
        sensors_t s;
        __disable_irq(); s = shared_sens; __enable_irq();

        actuators_t cmd;
        os_mutex_lock(&nav_lock);
        uint32_t c0 = nav_clock();
        control_step(&s, &cmd);                 /* <- the only call that matters */
        uint32_t c = nav_clock() - c0;
        int mode = nav_mode();
        os_mutex_unlock(&nav_lock);
        if (c > ctrl_cycles_max) { ctrl_cycles_max = c; }

        __disable_irq(); shared_cmd = cmd; __enable_irq();

        /* events, for the speaker, the LED and the serial line */
        uint32_t now = os_ticks();
        const world_t *w = world();
        if (w->collisions != last_coll) {
            last_coll = w->collisions;
            if (last_coll) { beep(220, 80, now); }
        }
        if (mode != last_mode) {
            if (mode == NAV_RUN) { run_start = s.t_ms; }
            if (mode == NAV_ARRIVED) {
                const nav_stats_t *st = nav_stats();
                uint32_t score = st->run_ms + PENALTY_MS * w->collisions;
                beep(1047, 200, now);
                pin_high(GPIOA, LD4_PIN);
                say("ARRIVED  level %d %s: %lu.%lu s, %u collision(s), %u replans -> score %lu.%lu\n",
                    level + 1, level_name(level), (unsigned long)(st->run_ms / 1000u),
                    (unsigned long)(st->run_ms / 100u % 10u), (unsigned)w->collisions,
                    (unsigned)st->replans, (unsigned long)(score / 1000u), (unsigned long)(score / 100u % 10u));
            }
            if (mode == NAV_NOPATH) {
                beep(147, 500, now);
                say("NOPATH   level %d: the goal is walled off in the robot's map\n", level + 1);
            }
            last_mode = mode;
        }
    }
}

/* ============================================================================
 *  task "ui": the pad, the buzzer, and $NAV telemetry
 * ============================================================================ */
static void send_nav(void)
{
    os_mutex_lock(&world_lock);
    os_mutex_lock(&nav_lock);
    const world_t *w = world();
    const nav_stats_t *st = nav_stats();
    float ex, ey, eth;
    nav_pose(&ex, &ey, &eth);
    long tx = mm(w->x), ty = mm(w->y), tth = mdeg(w->th);
    int mode = nav_mode();
    unsigned coll = w->collisions, rp = st->replans, exp = st->last_expanded;
    unsigned long t = w->t_ms, pus = st->last_plan_clk / 48u;
    os_mutex_unlock(&nav_lock);
    os_mutex_unlock(&world_lock);
    /* $NAV,t,level,mode,est x,y,th, true x,y,th, collisions,replans,expanded,plan us */
    send_frame("NAV,%lu,%d,%d,%ld,%ld,%ld,%ld,%ld,%ld,%u,%u,%u,%lu", t, level + 1, mode,
               mm(ex), mm(ey), mdeg(eth), tx, ty, tth, coll, rp, exp, pus);
}

/* One OLED frame: draw in RAM under the locks (~1-2 ms), then send only
 * the dirty pages over I2C with no lock held (the bus is this task's alone). */
static volatile uint8_t oled_ok;

/* The station's copy of the screen (mirror.h): after each flush, at most two
 * $FB frames, and only while the UART's ring has room for a whole one - so
 * the mirror falls behind rather than ever making this task wait.  The room
 * is checked with print_lock held, so no other task can fill the ring between
 * the check and the write, and no line can land inside a frame.  `frame` is
 * static: 96 B of RAM once, not 96 B more of this task's stack. */
static void mirror_send(void)
{
    static char frame[MIRROR_FRAME_MAX];
    os_mutex_lock(&print_lock);
    for (int i = 0; i < 2 && uart_tx_room() >= MIRROR_FRAME_MAX; i++) {
        size_t n = mirror_next(frame, sizeof frame, os_ticks());
        if (n == 0) { break; }                  /* not asked for, or nothing new */
        _write(1, frame, (int)n);
    }
    os_mutex_unlock(&print_lock);
}

static void draw_frame(void)
{
    uint32_t run_ms = 0;
    os_mutex_lock(&world_lock);
    os_mutex_lock(&nav_lock);
    int mode = nav_mode();
    if (mode == NAV_RUN && run_start)  { run_ms = world()->t_ms - run_start; }
    if (mode == NAV_ARRIVED)           { run_ms = nav_stats()->run_ms; }
    view_draw(show_truth, run_ms);
    os_mutex_unlock(&nav_lock);
    os_mutex_unlock(&world_lock);
    if (oled_ok) {
        uint32_t t0 = os_ticks();
        oled_flush();
        uint32_t d = os_ticks() - t0;
        if (d > flush_ms_max) { flush_ms_max = d; }
    }
    mirror_send();
}

static void task_ui(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), next_move = 0, next_nav = 0, frame = 0;
    pad_t p;
    for (;;) {
        os_delay_until(&last, 20);
        uint32_t now = os_ticks();
        pad_read(&p);
        os_mutex_lock(&kbd_lock);                        /* the station's keys, */
        kbd_apply(&kbd, &p, now);                        /* into the same pad   */
        os_mutex_unlock(&kbd_lock);
        beep_poll(now);

        if (p.adc_ok) {                                  /* knob: 100..400 mm/s */
            nav_params()->speed_mm_s = (int16_t)(100 + p.knob * 3 / 10);
        }
        if (p.pressed & PAD_SEL) { show_truth = !show_truth; }
        if (p.pressed & PAD_B) {                          /* next level */
            int n = custom_loaded ? N_LEVELS + 1 : N_LEVELS;
            load_level((level + 1) % n);
            say("level %d: %s\n", level + 1, level_name(level));
        }
        if (p.pressed & PAD_A) {
            if (nav_mode() == NAV_IDLE) {
                os_mutex_lock(&nav_lock);
                nav_start();
                os_mutex_unlock(&nav_lock);
                say("GO       level %d %s\n", level + 1, level_name(level));
            } else {
                load_level(level);                        /* again, from the start */
            }
        }
        /* joystick: move the goal one cell at a time, before the start */
        if (nav_mode() == NAV_IDLE && (p.x > 50 || p.x < -50 || p.y > 50 || p.y < -50)
            && (int32_t)(now - next_move) >= 0) {
            int gx, gy;
            os_mutex_lock(&nav_lock);
            nav_get_goal(&gx, &gy);
            gx += p.x > 50 ? 1 : (p.x < -50 ? -1 : 0);
            gy += p.y > 50 ? 1 : (p.y < -50 ? -1 : 0);
            if (gx >= 1 && gx <= GRID_W - 2 && gy >= 1 && gy <= GRID_H - 2) { nav_set_goal(gx, gy); }
            os_mutex_unlock(&nav_lock);
            next_move = now + 150u;
        }
        if ((int32_t)(now - next_nav) >= 0) {             /* $NAV at 4 Hz */
            next_nav = now + 250u;
            if (!nav_quiet) { send_nav(); }               /* "quiet" stops it */
        }
        if (++frame % 5u == 0u) { draw_frame(); }         /* the OLED at 10 Hz */
    }
}

/* ============================================================================
 *  The shell: words for people, $frames for host/planner.py
 * ============================================================================ */
static void cmd_stats(void)
{
    os_mutex_lock(&nav_lock);
    nav_stats_t st = *nav_stats();
    os_mutex_unlock(&nav_lock);
    say("A*      : %u plans (%u replans, %u recoveries), last %u nodes, peak %u nodes / %u open\n",
        st.plans, st.replans, st.recoveries, st.last_expanded, st.peak_expanded, st.peak_open);
    say("A* time : last %lu cycles = %lu us, peak %lu cycles = %lu us\n",
        (unsigned long)st.last_plan_clk, (unsigned long)(st.last_plan_clk / 48u),
        (unsigned long)st.peak_plan_clk, (unsigned long)(st.peak_plan_clk / 48u));
    say("budget  : control_step peak %lu us of %u ms, world_step peak %lu us of %u ms, OLED flush peak %lu ms\n",
        (unsigned long)(ctrl_cycles_max / 48u), CONTROL_MS, (unsigned long)(world_cycles_max / 48u), WORLD_MS,
        (unsigned long)flush_ms_max);
    say("safety  : %u braking steps, %u bumper backups, %u vetoes; gyro bias measured %ld urad/s\n",
        st.brakes, st.backups, st.vetoes, (long)st.gyro_bias_urad_s);
    say("RAM     : navigation %lu B (A* %lu B); OLED %lu bytes sent in %lu flushes\n",
        (unsigned long)nav_ram_bytes(), (unsigned long)astar_ram_bytes(),
        (unsigned long)oled_bytes_sent(), (unsigned long)oled_flushes());
    for (uint32_t i = 0; i < os_task_count(); i++) {
        os_task_t *t = os_task(i);
        say("  task %-6s stack %3lu of %3lu words, cpu %lu ms\n", t->name,
            (unsigned long)os_stack_used(t), (unsigned long)t->stack_words, (unsigned long)t->ticks);
    }
}

/* "dump": re-plan from here, then send the plan and the map it was made on,
 * as frames, for planner.py --file to check against its own A*.
 *
 * The path is sent while nav_lock is still held, straight out of nav.c's
 * own buffer - a copy would cost 256 B of RAM this lesson does not have.
 * The price: the robot task waits while ~250 characters go into the UART's
 * ring (a few ms), so it may miss one 40 ms step.  For a debug dump, fine. */
static void cmd_map(void)
{
    uint16_t len, from, veto[4];
    uint32_t occ[GRID_H], fre[GRID_H];
    int gx, gy;
    os_mutex_lock(&nav_lock);
    int ok = nav_plan_now();
    const uint16_t *p = nav_path(&len, &from);
    send_frame("RPATH,%u,%u,%u", ok ? nav_stats()->path_cost : 0xFFFFu,
               nav_stats()->last_expanded, ok ? len : 0u);
    for (uint16_t k = 0; ok && k < len; k += 16u) {   /* 16 cells a frame, 3 hex digits each */
        char txt[16 * 3 + 1];
        int n = 0;
        for (uint16_t j = k; j < len && j < k + 16u; j++) {
            n += snprintf(txt + n, sizeof txt - (size_t)n, "%02X%X", CELL_X(p[j]), CELL_Y(p[j]));
        }
        send_frame("RSTEP,%u,%s", k, txt);
    }
    for (int y = 0; y < GRID_H; y++) {
        occ[y] = fre[y] = 0;
        for (int x = 0; x < GRID_W; x++) {
            int c = nav_cell(x, y);
            if (c == MAP_OCC)  { occ[y] |= 1u << (31 - x); }
            if (c == MAP_FREE) { fre[y] |= 1u << (31 - x); }
        }
    }
    nav_get_goal(&gx, &gy);
    uint16_t start = nav_plan_start();
    int nv = nav_vetoes(veto, 4);
    unsigned inf = nav_params()->inflate;
    os_mutex_unlock(&nav_lock);

    for (int y = 0; y < GRID_H; y++) {
        send_frame("RMAP,%d,%08lX,%08lX", y, (unsigned long)occ[y], (unsigned long)fre[y]);
    }
    for (int i = 0; i < nv; i++) { send_frame("RVETO,%d,%d", CELL_X(veto[i]), CELL_Y(veto[i])); }
    send_frame("RGOAL,%d,%d,%d,%d,%u", CELL_X(start), CELL_Y(start), gx, gy, inf);   /* last: "complete" */
}

/* ONE dispatcher for both kinds of listener.  A person types words,
 *     level 3      goal 5 9      react 0      stats
 * and host/planner.py sends the same words as checksummed frames,
 *     $LEVEL,3*..  $GOAL,5,9*..  $REACT,0*..
 * Both end up here as fields: f[0] the word (any case), f[1], f[2] numbers.
 * Every change is answered with an $ACK frame, so a program can check it
 * and a person can read it. */
static int is(const char *a, const char *b)       /* case-blind compare */
{
    for (; *a && *b; a++, b++) {
        char c = *a >= 'a' && *a <= 'z' ? (char)(*a - 32) : *a;
        if (c != *b) { return 0; }
    }
    return *a == *b;
}

static void dispatch(char **f, int n)
{
    nav_params_t *P = nav_params();
    int a = num(f[1]), b = num(f[2]);
    if (is(f[0], "GO")) {
        os_mutex_lock(&nav_lock); nav_start(); os_mutex_unlock(&nav_lock);
    } else if (is(f[0], "RESET")) {
        load_level(level);
    } else if (is(f[0], "LEVEL") && n == 2) {
        if (a - 1 == LEVEL_CUSTOM) { custom_loaded = 1; }
        load_level(a >= 1 && a - 1 <= LEVEL_CUSTOM ? a - 1 : 0);
        a = level + 1;
    } else if (is(f[0], "GOAL") && n == 3) {
        os_mutex_lock(&nav_lock); nav_set_goal(a, b); os_mutex_unlock(&nav_lock);
        if (level == LEVEL_CUSTOM) { world_custom_goal(a, b); }
    } else if (is(f[0], "START") && n == 3) {
        world_custom_start(a, b);
    } else if (is(f[0], "MAP") && n == 3) {          /* one row of the custom level */
        world_custom_row(a, hex(f[2]));
        custom_loaded = 1;
    } else if (is(f[0], "SPEED") && n == 2)   { P->speed_mm_s = (int16_t)a;          /* the knob overrides it */
    } else if (is(f[0], "INFLATE") && n == 2) { P->inflate = (uint8_t)(a > 2 ? 2 : a);
    } else if (is(f[0], "REACT") && n == 2)   { P->reactive = (uint8_t)(a != 0);
    } else if (is(f[0], "CAL") && n == 2)     { P->gyro_cal = (uint8_t)(a != 0);
    } else if (is(f[0], "WHEELS") && n == 2)  { P->heading = (uint8_t)(a ? HEAD_WHEELS : HEAD_GYRO);
    } else if (is(f[0], "QUIET"))   { nav_quiet = 1;               /* $NAV off: a person can type */
    } else if (is(f[0], "LOUD"))    { nav_quiet = 0;
    } else if (is(f[0], "STATS")) { cmd_stats(); return;
    } else if (is(f[0], "DUMP"))  { cmd_map();   return;
    } else {
        say("go  reset  level N  goal X Y  speed MM_S  inflate 0-2  react 0|1  cal 0|1\n");
        say("wheels 0|1  stats  dump  quiet  loud    - or as frames: $GOAL,x,y  $MAP,y,hex  $START,x,y\n");
        return;
    }
    send_frame("ACK,%s,%d,%d", f[0], a, b);
}

/* The station's keys, the same list the banner prints. */
static const char KEYS[] =
    "  keys     : arrows = stick (moves the goal), a = A, b/p = B, s = SEL, 1-5 = knob, ? = this\n";

/* The station's two frames, taken before the shell's own parser sees them:
 * $KEY,<key>,<1|0>  a key down or up       -> kbd.c, applied by task "ui"
 * $MIRROR,<0|1>     "send me the screen"   -> mirror.c, sent by task "ui"
 * Both arrive several times a second, so neither is ever answered - no $ACK,
 * no $NAK for a bad checksum (the station repeats them anyway), no echo.
 * Returns 1 if the line was one of them. */
static int station_frame(const char *line)
{
    uint32_t now = os_ticks();
    if (kbd_is_key(line)) {
        os_mutex_lock(&kbd_lock);
        int r = kbd_frame(&kbd, line, now);
        os_mutex_unlock(&kbd_lock);
        if (r == KBD_HELP) { say("%s", KEYS); }      /* the one key that answers */
        /* KBD_SLOWER / KBD_FASTER (- +): nothing here - the robot's speed is
         * the knob's, and the keys 1-5 already set the knob. */
        return 1;
    }
    if (strncmp(line, "$MIRROR,", 8) == 0) {
        mirror_frame(line, now);                     /* ignores a bad checksum */
        return 1;
    }
    return 0;
}

static void task_shell(void *arg)
{
    (void)arg;
    char line[80];
    uint32_t n = 0;
    for (;;) {
        char c = (char)uart_getc();
        if (c != '\r' && c != '\n') { if (n < sizeof line - 1u) { line[n++] = c; } continue; }
        if (n == 0) { continue; }
        line[n] = '\0';
        n = 0;
        if (station_frame(line)) { continue; }        /* $KEY, $MIRROR: silent */
        char *f[3] = { 0 }, *text = line, sep = ' ';
        if (line[0] == '$') {                         /* a frame: check it first */
            const char *body;
            size_t len;
            int rc = proto_check(line, &body, &len);
            if (rc != PROTO_OK) { send_frame("NAK,%d", rc); continue; }
            text = (char *)body;
            text[len] = '\0';                         /* cut at the '*'          */
            sep = ',';
        }
        int k = split(text, sep, f, 3);
        if (k > 0) { dispatch(f, k); }
    }
}

/* ============================================================================ */
int main(void)
{
    uart_init(SystemCoreClock, BAUD);
    boot("\n=== SOC3050 lesson 16 - Robot Navigation ===\n");

    RCC->IOPENR |= RCC_IOPENR_GPIOAEN;
    pin_mode(GPIOA, LD4_PIN, MODE_OUTPUT);
    pin_low(GPIOA, LD4_PIN);

    int pr = pad_init();
    boot("  pad      : %s\n", pr == 0 ? "joystick, knob, A/B/SEL" : "buttons only - ADC did not start");
    beep_init();
    i2c_init();
    int o = oled_init();
    oled_ok = (uint8_t)(o == 0);
    boot("  OLED     : %s\n", o == 0 ? "SSD1306 at 0x3C" : o == I2C_NACK ? "no ACK at 0x3C" : "bus timeout");
    boot("  arena    : %d x %d cells of %d mm, %d levels; robot r=%d mm, %d rangefinders\n",
           GRID_W, GRID_H, CELL_MM, N_LEVELS, ROBOT_R_MM, N_RANGE);
    boot("  RAM      : navigation %lu B, of which A* %lu B\n",
           (unsigned long)nav_ram_bytes(), (unsigned long)astar_ram_bytes());
    boot("  controls : A start/restart  B next level  SEL show true walls  knob speed  stick moves goal\n");
    boot("  station  : python tools/station/station.py - the map large, these controls as keys\n");
    boot("%s", KEYS);
    boot("  shell    : type help; quiet / loud stop and restart $NAV\n\n");

    kbd_init(&kbd);
    load_level_unlocked(0);           /* no locks yet: the kernel has not started */

    os_task_create("world", task_world, 0, stk_world,  96, 4);
    os_task_create("robot", task_robot, 0, stk_robot, 224, 3);
    os_task_create("ui",    task_ui,    0, stk_ui,    272, 1);
    os_task_create("shell", task_shell, 0, stk_shell, 304, 2);
    os_start();
}
