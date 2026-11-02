/*
 * Main.c - SOC3050 lesson 18: Robot Competition - a sumo tournament
 *          ST Nucleo-C031C6, STM32C031C6, Cortex-M0+ at 48 MHz
 *
 * Two sumo robots on a 77 cm dohyo, simulated INSIDE the firmware:
 *
 *   world.c     the truth: motors, tyres, pushing contact, the ring edge,
 *               and what each robot's sensors would read
 *   referee.c   the rules: countdown, out, time limit, best of three - and
 *               the 20 ms call into each robot's brain
 *   bots.c      five opponents with personalities
 *   student.c   YOUR robot's brain - the file this lesson is about
 *   scene.c     the picture on the OLED
 *
 * Each brain sees only its own sensors through strategy.h's contract and
 * answers with two wheel commands.  It cannot see the world, the other
 * brain's memory, or anything else - which is what makes a tournament fair
 * and every match repeatable from its seed.
 *
 * Three RTOS tasks (lesson 07's kernel):
 *   sim    prio 3, every 20 ms: buttons, rules, physics at the knob's speed
 *   shell  prio 2: commands from the serial monitor - help, tour, seed, stats
 *   oled   prio 1, every 80 ms: draws the latest snapshot, sends dirty pages
 *
 * Controls:  B  next opponent (the last item is the TOURNAMENT)
 *            A  start / abort
 *            SEL  in the menu: driver = student.c or the JOYSTICK (fun mode)
 *                 in a match: sensor cones on/off
 *            knob  simulation speed x1, x2, x4, x8, MAX
 *
 * Serial (115200):  $MATCH frames after every match, and after a tournament
 *   $RESULT,<name>,<W>,<D>,<L>,<hash>,<max cycles>,<build>*CS
 * - the line a student submits.  host/league.c re-runs the same tournament
 * on a PC and must print the same <hash>.
 *
 * The station on the PC (python tools/station/station.py) draws the arena
 * large, keeps the standings, and sends real key-down/up for every control
 * (kbd.h: arrows = stick, a = A, b = B, s = SEL, 1-5 = speed).  While it is
 * connected - and only then - the board also sends the OLED (mirror.h) and,
 * during a match, both robots' poses every 80 ms:
 *   $ARENA,<round>,<phase>,<t_ms>,<won0>,<won1>,
 *          <x0>,<y0>,<th0>,<x1>,<y1>,<th1>,<name0>,<name1>*CS    mm, mrad
 * Both come from the display task's copy of the snapshot: they cannot touch
 * the simulation, and the $RESULT hash is the same with or without them.
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
#include "referee.h"
#include "scene.h"

#define BAUD           115200u
#define STEP_BUDGET    20000u     /* CPU cycles a strategy_step may take    */
#define DEFAULT_SEED   2026u

#define STACK(name, words) static uint32_t name[words] __attribute__((aligned(8)))
STACK(stk_sim, 512);  STACK(stk_shell, 384);  STACK(stk_oled, 384);

static os_mutex_t print_lock;
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
 *  A 32-bit CPU-cycle clock, from SysTick
 * ============================================================================
 * The Cortex-M0+ has no cycle counter (no DWT->CYCCNT; that is M3 and up).
 * But the kernel's SysTick counts DOWN from LOAD at 48 MHz and interrupts at
 * 0, and the kernel counts those interrupts in os_ticks().  So
 *
 *      cycles = ticks * (LOAD + 1) + (LOAD - VAL)
 *
 * The trap is the instant VAL has wrapped but the interrupt has not yet
 * counted it.  With interrupts off, PENDSTSET says exactly that. */
static uint32_t cycles(void)
{
    __disable_irq();
    uint32_t t = os_ticks();
    uint32_t v = SysTick->VAL;
    uint32_t pend = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;
    __enable_irq();
    uint32_t load = SysTick->LOAD;
    if (pend && v > load / 2u) { t++; }      /* wrapped, not yet counted */
    return t * (load + 1u) + (load - v);
}

/* A fingerprint of this exact firmware: FNV-1a of every byte that goes into
 * flash (code, constants, and .data's initial values).  Any change to any
 * file - or a different compiler - changes it. */
extern uint32_t _sidata, _sdata, _edata;
static uint32_t build_id(void)
{
    const uint8_t *p = (const uint8_t *)FLASH_BASE;
    const uint8_t *end = (const uint8_t *)&_sidata + ((uint8_t *)&_edata - (uint8_t *)&_sdata);
    uint32_t h = FNV_START;
    while (p < end) { h = (h ^ *p++) * 16777619u; }
    return h;
}

/* ============================================================================
 *  State shared between the tasks
 * ============================================================================ */
static snap_t   snap;                       /* sim writes, oled reads (copy)  */
static volatile uint8_t  req_tour;          /* shell asks sim                 */
static volatile uint32_t base_seed = DEFAULT_SEED;
static volatile uint32_t build;
static struct {                             /* for `stats`                    */
    uint32_t steps, step_cyc_sum, step_cyc_max;
    uint32_t ticks_running;                 /* ms of real time spent stepping */
    uint32_t strat_cyc_max[1 + N_BOTS];
} perf;

static const entry_t you = { "you", student_step, "joystick" };

/* The station's keys: the shell feeds them in, the sim task folds them into
 * its pad.  kbd.c has no lock of its own, so both calls take this one. */
static kbd_t      kbd;
static os_mutex_t kbd_lock;

/* ============================================================================
 *  The sim task: menu, match, tournament
 * ============================================================================ */
enum { S_MENU, S_MATCH, S_TOUR, S_RESULT };

static match_t m;
static struct {
    uint8_t  opp, k;                         /* next match: opponent, index   */
    uint8_t  W, D, L, done;
    uint32_t base, hash, cyc;
} tour;

static uint8_t speed_from_knob(int16_t knob)
{
    static const uint8_t table[5] = { 1, 2, 4, 8, 0 };       /* 0 = MAX */
    int i = knob * 5 / 1001;
    return table[i < 0 ? 0 : i > 4 ? 4 : i];
}

static int roster_index(const entry_t *e) { return (int)(e - roster); }

static void begin_tour_match(void)
{
    int side = tour_student_side(tour.k);
    const entry_t *a = side ? &roster[tour.opp] : STUDENT;
    const entry_t *b = side ? STUDENT : &roster[tour.opp];
    m.manual = 0;
    match_begin(&m, a, b, tour_seed(tour.base, tour.opp, tour.k));
}

static void start_tour(uint32_t base)
{
    memset(&tour, 0, sizeof tour);
    tour.opp = 1;
    tour.base = base;
    tour.hash = FNV_START;
    say("\nTournament: %s vs the five bots, %u matches each, seed %lu\n",
        STUDENT->name, TOUR_MATCHES, (unsigned long)base);
    begin_tour_match();
}

/* Called when a tournament match ends.  Returns 1 when the tournament is over. */
static int tour_match_over(void)
{
    int side = tour_student_side(tour.k);
    if (m.winner == side) { tour.W++; } else if (m.winner < 0) { tour.D++; } else { tour.L++; }
    tour.hash = fnv1a(tour.hash, m.hash);
    if (m.cyc_max[side] > tour.cyc) { tour.cyc = m.cyc_max[side]; }
    tour.done++;
    if (++tour.k >= TOUR_MATCHES) { tour.k = 0; tour.opp++; }
    if (tour.opp <= N_BOTS) { begin_tour_match(); return 0; }

    send_frame("RESULT,%s,%u,%u,%u,%08lX,%lu,%08lX", STUDENT->name, tour.W, tour.D, tour.L,
               (unsigned long)tour.hash, (unsigned long)tour.cyc, (unsigned long)build);
    say("points %u.  Worst strategy_step: %lu cycles (%lu us) - budget %u: %s\n",
        3u * tour.W + tour.D, (unsigned long)tour.cyc, (unsigned long)(tour.cyc / 48u),
        STEP_BUDGET, tour.cyc <= STEP_BUDGET ? "OK" : "OVER BUDGET - this entry would be rejected");
    say("Check it on a PC: host\\run.bat -- -s %lu   must print hash %08lX\n",
        (unsigned long)tour.base, (unsigned long)tour.hash);
    return 1;
}

static void report_match(void)
{
    send_frame("MATCH,%s,%s,%08lX,%d,%u,%u,%08lX", m.e[0]->name, m.e[1]->name,
               (unsigned long)m.seed, m.winner, m.won[0], m.won[1], (unsigned long)m.hash);
    for (int i = 0; i < 2; i++) {
        int r = roster_index(m.e[i]);
        if (r >= 0 && r <= N_BOTS && m.cyc_max[i] > perf.strat_cyc_max[r]) {
            perf.strat_cyc_max[r] = m.cyc_max[i];
        }
    }
}

static char msg[sizeof snap.msg];                    /* "GO!", "o wins" ...  */
static void set_msg(const char *s) { strncpy(msg, s, sizeof msg - 1); }

static void publish(uint8_t screen, uint8_t sel, uint8_t joystick, uint8_t speed, uint8_t cones)
{
    snap_t s;                     /* build it here, then copy it in one go */
    memset(&s, 0, sizeof s);
    s.screen = screen;
    memcpy(s.msg, msg, sizeof s.msg);
    for (int i = 0; i < 2; i++) {
        s.x[i] = m.w.b[i].x;  s.y[i] = m.w.b[i].y;  s.th[i] = m.w.b[i].th;
        memcpy(s.dist[i], m.view[i].dist_mm, sizeof s.dist[i]);
        s.name[i] = m.e[i] ? m.e[i]->name : "";
        s.won[i] = m.won[i];
    }
    s.round = m.round;  s.phase = m.phase;  s.t_ms = match_time_ms(&m);
    s.cones = cones;    s.speed = speed;
    s.league = (sel == N_BOTS);
    s.opp_name = s.league ? "" : roster[1 + sel].name;
    s.opp_style = s.league ? "" : roster[1 + sel].style;
    s.joystick = joystick;
    if (screen == SCR_MENU) { s.name[0] = STUDENT->name; }
    s.tour_on = (tour.opp != 0);
    s.tour_done = tour.done;  s.tour_n = N_BOTS * TOUR_MATCHES;
    s.W = tour.W;  s.D = tour.D;  s.L = tour.L;
    if (screen == SCR_RESULT) { s.name[0] = STUDENT->name; }
    __disable_irq(); snap = s; __enable_irq();
}

static void task_sim(void *arg)
{
    (void)arg;
    pad_t pad;
    uint8_t state = S_MENU, sel = 0, joystick = 0, cones = 1, speed = 1;
    uint32_t pause_until = 0, played = 0;
    uint32_t last = os_ticks();
    m.clock = cycles;

    for (;;) {
        os_delay_until(&last, 20);
        uint32_t now = os_ticks();
        pad_read(&pad);
        os_mutex_lock(&kbd_lock);
        kbd_apply(&kbd, &pad, now);      /* the station's keys, into the same pad */
        os_mutex_unlock(&kbd_lock);
        beep_poll(now);
        speed = speed_from_knob(pad.knob);

        if (req_tour && state != S_TOUR) {                 /* from the shell */
            req_tour = 0;
            start_tour(base_seed);
            state = S_TOUR;  set_msg("");
        }

        switch (state) {
        case S_MENU:
        case S_RESULT:
            if (pad.pressed & PAD_B)   { sel = (uint8_t)((sel + 1u) % (N_BOTS + 1u)); state = S_MENU; tour.opp = 0; }
            if (pad.pressed & PAD_SEL) { joystick ^= 1u; state = S_MENU; tour.opp = 0; }
            if (pad.pressed & PAD_A) {
                if (sel == N_BOTS) {
                    start_tour(base_seed);
                    state = S_TOUR;
                } else {
                    tour.opp = 0;
                    m.manual = joystick ? 1u : 0u;
                    match_begin(&m, joystick ? &you : STUDENT, &roster[1 + sel],
                                tour_seed(base_seed, 1 + sel, (int)(100u + played++)));
                    state = S_MATCH;
                }
                set_msg("");
                pause_until = now;
            }
            publish(state == S_RESULT ? SCR_RESULT : SCR_MENU, sel, joystick, speed, cones);
            continue;
        default:
            break;
        }

        /* ---- a match is on (S_MATCH or S_TOUR) ---- */
        if (pad.pressed & PAD_A)   { state = S_MENU; tour.opp = 0; say("aborted\n"); continue; }
        if (pad.pressed & PAD_SEL) { cones ^= 1u; }
        if (m.manual) {                                     /* arcade drive  */
            int l = pad.y + pad.x, r = pad.y - pad.x;
            m.joy.left  = (int8_t)(l > 100 ? 100 : l < -100 ? -100 : l);
            m.joy.right = (int8_t)(r > 100 ? 100 : r < -100 ? -100 : r);
        }

        if ((int32_t)(now - pause_until) >= 0) {
            if (m.phase == PHASE_ROUND_OVER) { match_next_round(&m); set_msg(""); }
            if (m.phase == PHASE_MATCH_OVER) {              /* pause is over */
                if (state == S_TOUR) {
                    if (tour_match_over()) { state = S_RESULT; publish(SCR_RESULT, sel, joystick, speed, cones); continue; }
                } else {
                    state = S_MENU;
                    continue;
                }
                set_msg("");
            }

            /* Run steps: speed x 4 per 20 ms tick (one step = 5 ms of match),
             * or as many as fit at MAX.  Never more than 14 ms of each 20:
             * if the CPU cannot keep up with x8, the match runs slower than
             * asked - `stats` says by how much - and the display still gets
             * the other 6 ms instead of freezing. */
            uint32_t budget = speed ? 4u * speed : 100000u;
            perf.ticks_running += 20u;
            for (uint32_t n = 0; n < budget; n++) {
                if (os_ticks() - now >= 14u) { break; }
                uint32_t c0 = cycles();
                uint8_t ev = match_step(&m);
                uint32_t c = cycles() - c0;
                perf.steps++;  perf.step_cyc_sum += c;
                if (c > perf.step_cyc_max) { perf.step_cyc_max = c; }

                if (m.phase == PHASE_COUNTDOWN && m.t % 200u == 0u && speed) { beep(440, 60, now); }
                if (ev & EV_GO) { set_msg("GO!"); if (speed) { beep(880, 200, now); } }
                if (ev & EV_ROUND_OVER) {
                    int w = m.round_winner;
                    set_msg(w < 0 ? "draw" : w == 0 ? "o wins" : "* wins");
                    if (speed) { beep(w < 0 ? 330u : 1320u, 250, now); }  /* silent at MAX */
                    if (ev & EV_MATCH_OVER) { report_match(); }
                    pause_until = now + (speed ? ((ev & EV_MATCH_OVER) ? 2500u : 1200u) : 0u);
                    break;
                }
            }
        }
        publish(SCR_MATCH, sel, joystick, speed, cones);
    }
}

/* ============================================================================
 *  The display task
 * ============================================================================ */
/* For the station only - mirror_live() is 1 while it keeps asking, so a plain
 * terminal never sees these.  Each is sent only when the TX ring has room for
 * it: say() would otherwise wait for space while holding print_lock, and the
 * sim task's $MATCH would wait behind this, the lowest-priority task.
 *
 * $ARENA: the poses in the snapshot, as integers (printf has no %f here).
 * About 70 bytes every 80 ms - under 1 KB/s of the link's ~11. */
#define ARENA_ROOM 100u
static void station_send(const snap_t *s, uint32_t now)
{
    if (!mirror_live(now)) { return; }
    if (s->screen == SCR_MATCH && uart_tx_room() >= ARENA_ROOM) {
        send_frame("ARENA,%u,%u,%ld,%u,%u,%d,%d,%d,%d,%d,%d,%s,%s",
                   s->round, s->phase, (long)s->t_ms, s->won[0], s->won[1],
                   (int)(s->x[0] * 1000.0f), (int)(s->y[0] * 1000.0f), (int)(s->th[0] * 1000.0f),
                   (int)(s->x[1] * 1000.0f), (int)(s->y[1] * 1000.0f), (int)(s->th[1] * 1000.0f),
                   s->name[0], s->name[1]);
    }
    /* The OLED mirror: at most 2 frames a pass, so the screen is never held up. */
    static char frame[MIRROR_FRAME_MAX];
    for (int i = 0; i < 2 && uart_tx_room() >= MIRROR_FRAME_MAX; i++) {
        size_t n = mirror_next(frame, sizeof frame, now);
        if (n == 0) { break; }
        say("%s", frame);
    }
}

static void task_oled(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks();
    for (;;) {
        os_delay_until(&last, 80);
        snap_t s;
        __disable_irq(); s = snap; __enable_irq();
        scene_draw(&s);
        oled_flush();
        station_send(&s, os_ticks());
    }
}

/* ============================================================================
 *  The shell
 * ============================================================================ */
static void cmd_stats(void)
{
    say("world+referee step: %lu steps, mean %lu cycles, worst %lu cycles (%lu us)\n",
        (unsigned long)perf.steps,
        (unsigned long)(perf.steps ? perf.step_cyc_sum / perf.steps : 0u),
        (unsigned long)perf.step_cyc_max, (unsigned long)(perf.step_cyc_max / 48u));
    if (perf.ticks_running) {          /* 5 ms of match per step, x10 for one decimal */
        uint32_t x10 = perf.steps * 50u / perf.ticks_running;
        say("achieved speed x%lu.%lu (match time / real time while running)\n",
            (unsigned long)(x10 / 10u), (unsigned long)(x10 % 10u));
    }
    for (int i = 0; i <= N_BOTS; i++) {
        say("  %-8s worst strategy_step %6lu cycles%s\n", roster[i].name,
            (unsigned long)perf.strat_cyc_max[i],
            perf.strat_cyc_max[i] > STEP_BUDGET ? "  OVER BUDGET" : "");
    }
    say("oled: %lu bytes in %lu flushes\n", (unsigned long)oled_bytes_sent(), (unsigned long)oled_flushes());
    for (uint32_t i = 0; i < os_task_count(); i++) {
        os_task_t *t = os_task(i);
        say("  task %-6s stack %3lu/%lu words, cpu %lu ms\n", t->name,
            (unsigned long)os_stack_used(t), (unsigned long)t->stack_words, (unsigned long)t->ticks);
    }
    say("build %08lX  seed %lu\n", (unsigned long)build, (unsigned long)base_seed);
}

/* A decimal number: the whole rest of the line.  sscanf() would do this in
 * one call - and cost 2 KB of flash, which this lesson does not have. */
static int number(const char *p, uint32_t *v)
{
    uint32_t x = 0;
    if (!*p) { return 0; }
    for (; *p; p++) {
        if (*p < '0' || *p > '9') { return 0; }
        x = x * 10u + (uint32_t)(*p - '0');
    }
    *v = x;
    return 1;
}

static void print_keys(void)
{
    say("keys: arrows = stick, a = A, b = B, s = SEL, 1-5 = speed x1 x2 x4 x8 MAX, ? = this\n");
}

/* A $ line is the station's, never a command: $KEY goes to kbd.c, $MIRROR to
 * mirror.c, and neither may answer - the station would see its own keys
 * echoed between telemetry frames.  Only ? (KBD_HELP) prints.  - and + are
 * ignored: 1-5 already pick every speed there is. */
static void station_line(const char *line)
{
    uint32_t now = os_ticks();
    if (kbd_is_key(line)) {
        os_mutex_lock(&kbd_lock);
        int cmd = kbd_frame(&kbd, line, now);
        os_mutex_unlock(&kbd_lock);
        if (cmd == KBD_HELP) { print_keys(); }
    } else {
        (void)mirror_frame(line, now);     /* anything else: silently dropped */
    }
}

static void task_shell(void *arg)
{
    (void)arg;
    char line[32];                         /* >= KBD_LINE_MAX: a $KEY line fits */
    size_t n = 0;
    for (;;) {
        char c = (char)uart_getc();
        if (c != '\r' && c != '\n') {
            if (n < sizeof line - 1) { line[n++] = c; }
            continue;
        }
        line[n] = 0;
        n = 0;
        uint32_t v;
        if (line[0] == '$')              { station_line(line); }
        else if (!strcmp(line, "stats")) { cmd_stats(); }
        else if (!strcmp(line, "tour"))  { req_tour = 1; }
        else if (!strncmp(line, "tour ", 5) && number(line + 5, &v)) { base_seed = v; req_tour = 1; }
        else if (!strncmp(line, "seed ", 5) && number(line + 5, &v)) {
            base_seed = v;
            say("seed %lu\n", (unsigned long)v);
        }
        else if (line[0]) { say("commands: tour [seed]  seed N  stats\n"); }
    }
}

int main(void)
{
    uart_init(SystemCoreClock, BAUD);
    build = build_id();
    say("\nSOC3050 lesson 18 - Robot Competition: sumo, simulated in the firmware\n");
    say("student strategy '%s', build %08lX, %u-byte memory, budget %u cycles/step\n",
        STUDENT->name, (unsigned long)build, (unsigned)sizeof(strategy_mem_t), STEP_BUDGET);

    i2c_init();
    int rc = oled_init();
    say("OLED: %s\n", rc == 0 ? "ok" : "no answer at 0x3C - running without a screen");
    if (pad_init() != 0) { say("pad: ADC did not start - joystick and knob read 0\n"); }
    beep_init();
    say("A start  B next opponent  SEL driver/cones  knob speed.  Serial: tour [seed], seed N, stats\n");
    say("station: python tools/station/station.py - arena, standings, OLED; keys a b s 1-5, arrows\n");
    kbd_init(&kbd);

    os_task_create("sim",   task_sim,   0, stk_sim,   512, 3);
    os_task_create("shell", task_shell, 0, stk_shell, 384, 2);
    os_task_create("oled",  task_oled,  0, stk_oled,  384, 1);
    os_start();
}
