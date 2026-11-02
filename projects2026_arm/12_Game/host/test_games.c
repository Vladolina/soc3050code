/*
 * test_games.c - the arcade, run on a PC.   SOC3050 lesson 12
 *
 * Compiles the lesson's REAL game files (../snake.c, ../breakout.c,
 * ../flappy.c, ../engine.c, ../arcade.c) and the REAL display driver
 * (../../_lib/oled.c) with plain gcc.  Only two things are replaced:
 *
 *   i2c_write_read()  a stub that counts bytes and models the bus time at
 *                     400 kHz, instead of driving PB8/PB9
 *   sfx(), on_score() the platform hooks: counted and checked, not played
 *
 * Each game is played by an AUTOPILOT - a few lines that read the game's
 * state through its peek function and push the stick - and also with no
 * input at all, which must end the game at a predictable tick.  Every run is
 * deterministic: same seed, same inputs, same pixels.
 *
 * Prints a results table, writes ASCII and SVG screenshots to host/out/, and
 * exits non-zero if any check fails.
 *
 *   host/run.sh   or   host\run.bat
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "arcade.h"
#include "i2c.h"
#include "proto.h"
#include "kbd.h"
#include "mirror.h"

/* ============================================================================
 *  The stubs: an I2C bus that only counts, and the two platform hooks
 * ============================================================================ */
#define BIT_US 2.5                      /* 400 kHz nominal                     */

static long   bus_bytes;                /* every byte on the wire, incl. addr  */
static long   bus_transfers;
static double bus_us;

int i2c_write_read(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{
    (void)addr; (void)w; (void)r;
    /* START + (address + wn data bytes) x 9 bits (8 + ACK) + STOP */
    size_t bytes = 1 + wn + (rn ? 1 + rn : 0);
    bus_bytes += (long)bytes;
    bus_transfers++;
    bus_us += (double)(bytes * 9 + 2) * BIT_US;
    return I2C_OK;
}
void i2c_init(void) {}
int  i2c_probe(uint8_t addr) { (void)addr; return I2C_OK; }

static long sfx_count[SFX_COUNT];
void sfx(int id) { if (id >= 0 && id < SFX_COUNT) { sfx_count[id]++; } }

static char last_score_line[64];
static int  scores_reported;
void on_score(const char *game, int32_t score)
{
    score_frame(last_score_line, sizeof last_score_line, game, score);
    scores_reported++;
}

/* ============================================================================
 *  Checks
 * ============================================================================ */
static int failures;
#define CHECK(cond, ...) do { \
    if (cond) { printf("  ok    "); } else { printf("  FAIL  "); failures++; } \
    printf(__VA_ARGS__); printf("\n"); } while (0)

/* ============================================================================
 *  Screenshots: ASCII (two pixel rows per character) and SVG
 * ============================================================================ */
static void shot(const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "out/%s.txt", name);
    FILE *f = fopen(path, "w");
    if (f) {
        /* ' ' none, '\'' top pixel, '.' bottom pixel, ':' both - 128 x 32 */
        fprintf(f, "+--------------------------------------------------------------------------------------------------------------------------------+\n");
        for (int y = 0; y < OLED_H; y += 2) {
            fputc('|', f);
            for (int x = 0; x < OLED_W; x++) {
                int t = oled_get(x, y), b = oled_get(x, y + 1);
                fputc(t && b ? ':' : t ? '\'' : b ? '.' : ' ', f);
            }
            fputs("|\n", f);
        }
        fprintf(f, "+--------------------------------------------------------------------------------------------------------------------------------+\n");
        fclose(f);
    }
    snprintf(path, sizeof path, "out/%s.svg", name);
    f = fopen(path, "w");
    if (f) {
        /* one <path>, a subpath per horizontal run of lit pixels, filled
         * with currentColor so it follows the deck's light and dark themes */
        fprintf(f, "<svg viewBox=\"-2 -2 132 68\" role=\"img\" aria-label=\"%s, rendered on the host from the real game code\">\n", name);
        fprintf(f, "  <rect x=\"-1.5\" y=\"-1.5\" width=\"131\" height=\"67\" rx=\"2\" fill=\"none\" stroke=\"currentColor\" stroke-opacity=\"0.4\"/>\n  <path fill=\"currentColor\" d=\"");
        int n = 0;
        for (int y = 0; y < OLED_H; y++) {
            for (int x = 0; x < OLED_W; x++) {
                if (!oled_get(x, y)) { continue; }
                int x0 = x;
                while (x + 1 < OLED_W && oled_get(x + 1, y)) { x++; }
                fprintf(f, "%sM%d %dh%dv1h-%dz", (n++ % 12) ? "" : "\n    ", x0, y, x - x0 + 1, x - x0 + 1);
            }
        }
        fprintf(f, "\"/>\n</svg>\n");
        fclose(f);
    }
}

static uint32_t fb_hash(void)
{
    uint32_t h = 2166136261u;                         /* FNV-1a over 1024 bytes */
    for (int p = 0; p < OLED_PAGES; p++) {
        const uint8_t *b = oled_page(p);
        for (int x = 0; x < OLED_W; x++) { h = (h ^ b[x]) * 16777619u; }
    }
    return h;
}

/* ============================================================================
 *  Autopilots: stick and buttons for tick t, from what the game shows
 * ============================================================================ */
typedef pad_t (*pilot_fn)(int t);

static pad_t no_input(int t) { (void)t; pad_t p; memset(&p, 0, sizeof p); p.knob = 500; return p; }

/* Bytes into kbd.c as a terminal would send them; the last command wins. */
static int kbd_type(kbd_t *k, const char *s, uint32_t now)
{
    int cmd = KBD_NONE;
    for (; *s; s++) { int c = kbd_feed(k, (uint8_t)*s, now); if (c != KBD_NONE) { cmd = c; } }
    return cmd;
}

/* One $KEY frame from the station, checksum and all. */
static int kbd_key(kbd_t *k, const char *key, int down, uint32_t now)
{
    char body[16], line[24];
    int n = snprintf(body, sizeof body, "KEY,%s,%d", key, down);
    snprintf(line, sizeof line, "$%s*%02X", body, (unsigned)proto_checksum(body, (size_t)n));
    return kbd_frame(k, line, now);
}

/* SNAKE: of the moves that do not die at once, prefer those that leave room
 * to live (a flood fill counts the reachable cells), then the shortest way to
 * the apple.  Not optimal - good enough to eat for a long time. */
#define SGW 31
#define SGH 13
static int reachable(int sx, int sy)
{
    static uint8_t seen[SGW * SGH];
    static int q[SGW * SGH];
    memset(seen, 0, sizeof seen);
    int head = 0, tail = 0, n = 0;
    if (!snake_cell_free(sx, sy)) { return 0; }
    q[tail++] = sy * SGW + sx;  seen[sy * SGW + sx] = 1;
    while (head < tail) {
        int c = q[head++], cx = c % SGW, cy = c / SGW;
        n++;
        static const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, -1, 0, 1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (snake_cell_free(nx, ny) && !seen[ny * SGW + nx]) { seen[ny * SGW + nx] = 1; q[tail++] = ny * SGW + nx; }
        }
    }
    return n;
}
static pad_t snake_pilot(int t)
{
    pad_t p = no_input(t);
    peek_t s;  snake_peek(&s);
    static const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, -1, 0, 1};   /* R U L D */
    int best = -1;  long best_v = -1000000;
    for (int d = 0; d < 4; d++) {
        if (d == ((s.vy + 2) & 3)) { continue; }                     /* no reverse */
        int nx = s.x + dx[d], ny = s.y + dy[d];
        if (!snake_cell_free(nx, ny)) { continue; }
        int room = reachable(nx, ny);
        int dist = abs(nx - s.tx) + abs(ny - s.ty);
        long v = (room >= s.count + 4 ? 100000 : room * 100) - dist;
        if (v > best_v) { best_v = v; best = d; }
    }
    if (best < 0) { best = s.vy; }
    p.x = (int16_t)(dx[best] * 100);
    p.y = (int16_t)(-dy[best] * 100);                                /* up is + */
    return p;
}

/* BREAKOUT: predict where the ball will come down - fly it forward through
 * the side walls in integer steps - and get the paddle there early, aiming a
 * little off-centre (the aim drifts over time) so it goes somewhere new. */
static pad_t breakout_pilot(int t)
{
    pad_t p = no_input(t);
    peek_t b;  breakout_peek(&b);
    int land = b.tx;
    if (b.vy > 0) {
        long x = (long)b.tx << 8, y = (long)b.ty << 8, vx = b.vx;
        while (y < ((long)(b.y - 2) << 8)) {
            x += vx;  y += b.vy;
            if (x < (1L << 8))   { x = 1L << 8;   vx = -vx; }
            if (x > (125L << 8)) { x = 125L << 8; vx = -vx; }
        }
        land = (int)(x >> 8);
    }
    int aim = ((t / 300) % 5 - 2) * (b.w / 6);
    int target = land + 1 - b.w / 2 - aim;
    int e = target - b.x;
    p.x = (int16_t)(e > 25 ? 100 : e < -25 ? -100 : e * 4);
    if (t % 50 == 0) { p.pressed = PAD_A; }
    return p;
}

/* FLAP: flap when the bird's feet near the bottom of the next gap and it is
 * not already rising.  (Flapping at the gap's MIDDLE overshoots: a flap
 * climbs ~11 px, and the gap is 22 at level 3.) */
static pad_t flap_pilot(int t)
{
    pad_t p = no_input(t);
    peek_t f;  flap_peek(&f);
    if (f.y + f.h >= f.ty + f.gap - 3 && f.vy >= 0) { p.pressed = PAD_A; }
    return p;
}

/* ============================================================================
 *  One run: start, then step + draw + flush every tick, measuring the bus
 * ============================================================================ */
typedef struct {
    int     ticks, over;
    int32_t score;
    long    frames, bytes, max_frame_bytes;
    double  bus_ms_total, bus_ms_max;
    long    frames_over_budget;              /* bus alone > 20 ms             */
    uint32_t hash;
} run_t;

static void reset_panel(void)
{
    oled_clear();  oled_invalidate();  oled_flush();
    bus_bytes = 0;  bus_transfers = 0;  bus_us = 0;
}

static run_t run(const game_t *g, int level, uint32_t seed, pilot_fn pilot, int max_ticks,
                 int full_repaint, const char *shot_name, int shot_tick)
{
    run_t r;  memset(&r, 0, sizeof r);
    reset_panel();
    rng_seed(seed);
    g->start(level);
    g->draw(1);
    oled_flush();
    for (int t = 1; t <= max_ticks; t++) {
        pad_t in = pilot(t);
        int over = g->step(&in);
        g->draw(0);
        if (full_repaint) { oled_invalidate(); }
        long b0 = bus_bytes;  double u0 = bus_us;
        oled_flush();
        long fb = bus_bytes - b0;  double fu = (bus_us - u0) / 1000.0;
        r.frames++;  r.bytes += fb;  r.bus_ms_total += fu;
        if (fb > r.max_frame_bytes) { r.max_frame_bytes = fb; }
        if (fu > r.bus_ms_max) { r.bus_ms_max = fu; }
        if (fu > TICK_MS) { r.frames_over_budget++; }
        if (shot_name && t == shot_tick) { shot(shot_name); }
        r.ticks = t;
        if (over) { r.over = 1; break; }
    }
    r.score = g->score();
    r.hash = fb_hash();
    return r;
}

/* Main.c's loop, run against a SIMULATED clock: updates are due every 20 ms,
 * a frame costs its modelled bus time (CPU time taken as zero - the board's
 * $PERF line measures the real thing).  Predicts $PERF's fps and ups. */
static void loop_row(const char *name, const game_t *g, uint32_t seed, pilot_fn pilot, int full)
{
    reset_panel();
    rng_seed(seed);
    g->start(3);
    g->draw(1);
    oled_flush();
    double now = 0, next = 0, end = 60000;                 /* one minute    */
    long frames = 0, updates = 0, catchups = 0;
    int t = 0, over = 0;
    while (now < end && !over) {
        if (now < next) { now = next; continue; }          /* __WFI()       */
        int ran = 0;
        while (now >= next && ran < 4) {
            pad_t in = pilot(++t);
            over |= g->step(&in);
            next += TICK_MS;  ran++;  updates++;
        }
        if (ran > 1) { catchups++; }
        g->draw(0);
        if (full) { oled_invalidate(); }
        double u0 = bus_us;
        oled_flush();
        now += (bus_us - u0) / 1000.0;
        frames++;
    }
    printf("  %-9s %-6s  %5.1f fps  %5.1f updates/s  %5.1f %% of frames ran 2+ updates  (%.0f s simulated)\n",
           name, full ? "FULL" : "DIRTY", frames * 1000.0 / now, updates * 1000.0 / now,
           100.0 * catchups / frames, now / 1000.0);
}

static void bus_row(const char *name, const run_t *d, const run_t *f)
{
    printf("  %-9s %7.0f B %6.2f ms %6.2f ms | %7.0f B %6.2f ms | %5.1f %%   %4.0f %%\n", name,
           (double)d->bytes / d->frames, d->bus_ms_total / d->frames, d->bus_ms_max,
           (double)f->bytes / f->frames, f->bus_ms_total / f->frames,
           100.0 * (double)d->bytes / (double)f->bytes,
           100.0 * (double)d->frames_over_budget / d->frames);
}

/* --playtest: the autopilots as playtesters, at every level.  No checks -
 * a table for tuning the difficulty curve (Lab Part 6). */
static int playtest(void)
{
    printf("PLAYTEST: each autopilot at levels 1-5, up to 5 minutes, 3 seeds each (mean)\n");
    printf("  level | SNAKE apples  survived | BREAKOUT bricks  survived | FLAP pipes  survived\n");
    for (int lv = 1; lv <= 5; lv++) {
        double sa = 0, st = 0, ba = 0, bt = 0, fa = 0, ft = 0;
        for (uint32_t seed = 1; seed <= 3; seed++) {
            long e0 = sfx_count[SFX_EAT], b0 = sfx_count[SFX_BRICK];
            run_t r = run(&game_snake, lv, seed * 101u, snake_pilot, 15000, 0, 0, 0);
            sa += (double)(sfx_count[SFX_EAT] - e0);  st += r.ticks / 50.0;
            r = run(&game_breakout, lv, seed * 101u, breakout_pilot, 15000, 0, 0, 0);
            ba += (double)(sfx_count[SFX_BRICK] - b0);  bt += r.ticks / 50.0;
            r = run(&game_flap, lv, seed * 101u, flap_pilot, 15000, 0, 0, 0);
            fa += (double)r.score;  ft += r.ticks / 50.0;
        }
        printf("    %d   |   %6.1f    %6.1f s  |    %6.1f       %6.1f s  |  %6.1f    %6.1f s\n",
               lv, sa / 3, st / 3, ba / 3, bt / 3, fa / 3, ft / 3);
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--playtest") == 0) { return playtest(); }
    printf("=== lesson 12 host test: the real game code, scripted inputs ===\n\n");
    oled_init();                               /* the init sequence, into the stub */
    printf("oled_init(): %ld transfers, %ld bytes on the bus\n\n", bus_transfers, bus_bytes);

    /* ---- SNAKE ---------------------------------------------------------- */
    printf("SNAKE\n");
    long eat0 = sfx_count[SFX_EAT];
    run_t s = run(&game_snake, 3, 12345u, snake_pilot, 20000, 0, "snake", 2000);
    peek_t sp;  snake_peek(&sp);
    long eats = sfx_count[SFX_EAT] - eat0;
    printf("        autopilot, level 3: %d ticks (%.1f s), %ld apples, length %d, score %ld, %s\n",
           s.ticks, s.ticks / 50.0, eats, sp.count, (long)s.score, s.over ? "died" : "still alive");
    CHECK(eats >= 20, "snake eats: %ld apples (need >= 20)", eats);
    CHECK(sp.count == 3 + 2 * (int)eats - sp.lives, "snake grows 2 per apple: length %d = 3 + 2 x %ld - %d pending", sp.count, eats, sp.lives);
    CHECK(s.score == 3 * eats, "score = level x apples: %ld = 3 x %ld", (long)s.score, eats);
    run_t s0 = run(&game_snake, 3, 12345u, no_input, 2000, 0, 0, 0);
    /* head starts at cell 10 of 31 heading right: 20 moves to the last
     * column, the 21st hits the wall; level 3 moves every 6 ticks. */
    CHECK(s0.over && s0.ticks == 21 * 6, "no input: hits the wall on tick %d (predicted 21 x 6 = 126)", s0.ticks);
    run_t sd = run(&game_snake, 3, 12345u, snake_pilot, 20000, 1, 0, 0);
    run_t sa = run(&game_snake, 3, 12345u, snake_pilot, 20000, 0, 0, 0);
    CHECK(sa.hash == s.hash && sa.score == s.score && sa.ticks == s.ticks,
          "deterministic: same seed, same inputs -> same %d ticks, score, framebuffer hash %08lX",
          sa.ticks, (unsigned long)sa.hash);
    run_t sb = run(&game_snake, 3, 777u, snake_pilot, 300, 0, 0, 0);
    run_t sc = run(&game_snake, 3, 12345u, snake_pilot, 300, 0, 0, 0);
    CHECK(sb.hash != sc.hash, "a different seed puts the apples elsewhere: %08lX vs %08lX",
          (unsigned long)sb.hash, (unsigned long)sc.hash);

    /* ---- BREAKOUT ------------------------------------------------------- */
    printf("\nBREAKOUT\n");
    long brick0 = sfx_count[SFX_BRICK];
    run_t b = run(&game_breakout, 3, 4242u, breakout_pilot, 15000, 0, "breakout", 900);
    peek_t bp;  breakout_peek(&bp);
    long bricks = sfx_count[SFX_BRICK] - brick0;
    printf("        autopilot, level 3: %d ticks (%.1f s), %ld bricks, %ld waves cleared, lives %d, score %ld, %s\n",
           b.ticks, b.ticks / 50.0, bricks, (long)(sfx_count[SFX_LEVEL]), bp.lives, (long)b.score, b.over ? "game over" : "still playing");
    CHECK(bricks >= 48, "ball breaks bricks: %ld (need >= 48, one full wave)", bricks);
    CHECK(b.score >= bricks * 3, "score rises with every brick: %ld for %ld bricks (>= 3 each at level 3)", (long)b.score, bricks);
    long lose0 = sfx_count[SFX_LOSE];
    run_t b0 = run(&game_breakout, 3, 4242u, no_input, 5000, 0, 0, 0);
    CHECK(b0.over && sfx_count[SFX_LOSE] - lose0 == 2, "no input: auto-serve after 3 s, 3 balls lost, game over on tick %d", b0.ticks);
    run_t bd = run(&game_breakout, 3, 4242u, breakout_pilot, 15000, 1, 0, 0);

    /* ---- FLAP ----------------------------------------------------------- */
    printf("\nFLAP\n");
    run_t f = run(&game_flap, 3, 99u, flap_pilot, 6000, 0, "flap", 700);
    printf("        autopilot, level 3: %d ticks (%.1f s), %ld pipes, %ld flaps, %s\n",
           f.ticks, f.ticks / 50.0, (long)f.score, sfx_count[SFX_FLAP], f.over ? "crashed" : "still flying");
    CHECK(f.score >= 25, "bird passes pipes: %ld (need >= 25)", (long)f.score);
    run_t f0 = run(&game_flap, 3, 99u, no_input, 2000, 0, 0, 0);
    /* Hovers until tick 150, which is also the first tick of falling from
     * y = 32.  After n ticks it has fallen 28 x n(n+1)/2 / 256 px, and it is
     * down when y + 6 > 64, i.e. 27 px: n(n+1) >= 494 -> n = 22, tick 171. */
    CHECK(f0.over && f0.ticks == 171, "no input: hovers 3 s then falls to the ground on tick %d (predicted 149 + 22 = 171)", f0.ticks);
    run_t fd = run(&game_flap, 3, 99u, flap_pilot, 6000, 1, 0, 0);

    /* ---- the bus budget --------------------------------------------------- */
    printf("\nTHE BUS BUDGET, per frame (one frame per 20 ms tick), modelled at 400 kHz\n");
    printf("            ------- dirty pages --------------- | --- full repaint ---- | dirty/full  frames\n");
    printf("  game         bytes   mean      worst          |   bytes    mean       |  of bytes  > 20 ms\n");
    bus_row("SNAKE", &sa, &sd);
    bus_row("BREAKOUT", &b, &bd);
    bus_row("FLAP", &f, &fd);
    CHECK((double)sa.bytes / sa.frames < 0.25 * (double)sd.bytes / sd.frames,
          "dirty pages cut SNAKE's bus traffic by more than 4x");
    CHECK(sd.bus_ms_max > 24.0 && sd.bus_ms_max < 26.0, "a full repaint costs %.2f ms of bus: over the 20 ms tick", sd.bus_ms_max);

    printf("\nTHE LOOP, simulated: frames vs updates per second (Main.c's $PERF fps and ups)\n");
    loop_row("SNAKE", &game_snake, 12345u, snake_pilot, 0);
    loop_row("SNAKE", &game_snake, 12345u, snake_pilot, 1);
    loop_row("BREAKOUT", &game_breakout, 4242u, breakout_pilot, 0);
    loop_row("BREAKOUT", &game_breakout, 4242u, breakout_pilot, 1);
    loop_row("FLAP", &game_flap, 99u, flap_pilot, 0);
    loop_row("FLAP", &game_flap, 99u, flap_pilot, 1);

    /* ---- the arcade: menu, play, game over, $SCORE, high score ------------ */
    printf("\nARCADE\n");
    reset_panel();
    arcade_init();
    pad_t in = no_input(0);
    for (int t = 0; t < 10; t++) { arcade_step(&in); arcade_draw(); }
    in.knob = 650;                                    /* level 4 */
    arcade_step(&in); arcade_draw();
    in.y = 100; arcade_step(&in); in.y = 0; arcade_step(&in);      /* up: wraps to FLAP */
    in.y = -100; arcade_step(&in); in.y = 0; arcade_step(&in);     /* down: back to SNAKE */
    arcade_draw();
    shot("menu");
    CHECK(arcade_game() == 0 && arcade_state() == ARC_MENU, "menu: stick up then down returns to SNAKE");
    in.pressed = PAD_A; arcade_step(&in); in.pressed = 0;
    CHECK(arcade_state() == ARC_PLAY, "A starts the game");
    int t = 0;
    while (arcade_state() == ARC_PLAY && t < 30000) {
        pad_t a = snake_pilot(t++);
        arcade_step(&a);  arcade_draw();
    }
    for (int k = 0; k < 20; k++) { arcade_step(&in); arcade_draw(); }
    shot("gameover");
    const char *body;  size_t blen;
    int rc = proto_check(last_score_line, &body, &blen);
    printf("        game over after %d ticks: %s\n", t, last_score_line);
    CHECK(arcade_state() == ARC_OVER && scores_reported == 1, "game over reported once");
    CHECK(rc == PROTO_OK && strncmp(body, "SCORE,SNAKE,", 12) == 0, "the $SCORE frame passes lesson 08's checksum");
    int32_t first = arcade_high(0);
    CHECK(first > 0 && sfx_count[SFX_HIGH] >= 1, "first score %ld is the new high score", (long)first);
    for (int k = 0; k < 50; k++) { arcade_step(&in); }               /* past the hold */
    in.pressed = PAD_A; arcade_step(&in); in.pressed = 0;            /* again */
    for (int k = 0; k < 2000 && arcade_state() == ARC_PLAY; k++) { arcade_step(&in); }
    CHECK(arcade_high(0) == first && scores_reported == 2, "a worse second game keeps the high score at %ld", (long)arcade_high(0));
    char bad[64];
    strcpy(bad, last_score_line);
    bad[13] = (char)(bad[13] == '9' ? '8' : '9');                    /* "edit" the score */
    CHECK(proto_check(bad, &body, &blen) == PROTO_MISMATCH, "an edited score fails the checksum: %s", bad);

    /* ---- the keyboard: kbd.c turns terminal bytes into the same pad --------- */
    printf("\nKEYBOARD (kbd.c, the terminal's keys as a pad)\n");
    kbd_t kb;
    kbd_init(&kb);
    pad_t kp;
    uint32_t now = 1000;
    kbd_type(&kb, "\x1b[A", now);                         /* Up, as a terminal sends it */
    kp = no_input(0); kbd_apply(&kb, &kp, now + 20);
    int up_now = kp.y;
    kp = no_input(0); kbd_apply(&kb, &kp, now + KBD_HOLD_MS + 1);
    CHECK(up_now == 100 && kp.y == 0, "ESC [ A pushes the stick up (y %d), and lets go after %u ms (y %d)",
          up_now, (unsigned)KBD_HOLD_MS, kp.y);
    int held = 1;                                         /* auto-repeat: every 33 ms */
    for (uint32_t ms = 0; ms < 600; ms += 20) {
        if (ms % 33u < 20u) { kbd_type(&kb, "\x1b[C", now + ms); }
        kp = no_input(0); kbd_apply(&kb, &kp, now + ms);
        if (kp.x != 100) { held = 0; }
    }
    CHECK(held, "a held Right, auto-repeating at 30 Hz, holds the stick right for 600 ms");
    kbd_type(&kb, "\x1b[D", now + 700); kbd_type(&kb, "\x1b[C", now + 701);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 702);
    CHECK(kp.x == 100, "Left then Right: the last arrow wins at once (x %d)", kp.x);
    kbd_type(&kb, "a", now + 800);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 800);
    int first_a = kp.pressed;
    kp = no_input(0); kbd_apply(&kb, &kp, now + 820);
    CHECK(first_a == PAD_A && kp.pressed == 0, "a is ONE press of A, not a held button");
    kbd_type(&kb, "P", now + 900);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 900);
    CHECK(kp.pressed == PAD_B, "p (either case) is B - pause");
    int got_q = kbd_type(&kb, "quiet\r\n", now + 1000);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 1000);
    CHECK(got_q == KBD_QUIET && kp.pressed == 0, "typing quiet + Enter is KBD_QUIET and presses no button");
    CHECK(kbd_type(&kb, "loud\r", now + 1100) == KBD_LOUD, "loud + Enter is KBD_LOUD");
    CHECK(kbd_type(&kb, "-", now + 1150) == KBD_SLOWER && kbd_type(&kb, "+", now + 1160) == KBD_FASTER
          && kbd_type(&kb, "=", now + 1170) == KBD_FASTER, "- is slower; + and = (unshifted +) are faster");
    CHECK(kbd_type(&kb, "lod\x7fud\r", now + 1200) == KBD_LOUD, "Backspace edits the word: lod<BS>ud = loud");
    int unk = kbd_type(&kb, "xyz\r", now + 1300);
    CHECK(unk == KBD_UNKNOWN && strcmp(kbd_word(&kb), "xyz") == 0, "an unknown word is reported by name: '%s'", kbd_word(&kb));
    kbd_type(&kb, "4", now + 1400);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 1400);
    int k4 = kp.knob;
    kp = no_input(0); kp.knob = 900; kbd_apply(&kb, &kp, now + 1420);
    CHECK(k4 == 700 && kp.knob == 900, "4 sets the knob to 700 (level 4) until the real knob turns (now %d)", kp.knob);

    /* The station's $KEY frames: real key-down and key-up. */
    kbd_init(&kb);
    now = 3000;
    kbd_key(&kb, "LEFT", 1, now);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 500);
    int left_held = kp.x;
    kbd_key(&kb, "LEFT", 0, now + 510);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 511);
    CHECK(left_held == -100 && kp.x == 0, "$KEY,LEFT,1 holds the stick left for 500 ms (x %d); $KEY,LEFT,0 lets go at once (x %d)",
          left_held, kp.x);
    kbd_key(&kb, "UP", 1, now + 600);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 600 + KBD_LIVE_MS + 1);
    CHECK(kp.y == 0, "a station key never renewed lets go after %u ms (lost key-up)", (unsigned)KBD_LIVE_MS);
    kbd_key(&kb, "a", 1, now + 2000);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 2000);
    int a_press = kp.pressed, a_down1 = kp.down;
    kbd_key(&kb, "a", 1, now + 2200);                     /* the station's 200 ms repeat */
    kp = no_input(0); kbd_apply(&kb, &kp, now + 2220);
    int a_press2 = kp.pressed, a_down2 = kp.down;
    kbd_key(&kb, "a", 0, now + 2300);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 2320);
    CHECK(a_press == PAD_A && a_down1 == PAD_A && a_press2 == 0 && a_down2 == PAD_A && kp.down == 0,
          "$KEY,a: one press, held down through a repeat, up on $KEY,a,0");
    char bad_key[] = "$KEY,UP,1*00";
    CHECK(kbd_frame(&kb, bad_key, now + 2400) == KBD_NONE && kb.dy == 0, "a $KEY with a wrong checksum is ignored");
    CHECK(kbd_key(&kb, "-", 1, now + 2500) == KBD_SLOWER && kbd_key(&kb, "-", 0, now + 2510) == KBD_NONE,
          "$KEY,-,1 is KBD_SLOWER; its key-up does nothing");
    char line[24];
    snprintf(line, sizeof line, "$KEY,RIGHT,1*%02X\r", (unsigned)proto_checksum("KEY,RIGHT,1", 11));
    kbd_type(&kb, line, now + 2600);
    kp = no_input(0); kbd_apply(&kb, &kp, now + 2700);
    CHECK(kp.x == 100 && kb.wlen == 0, "a $KEY line arriving byte by byte through kbd_feed() works too (x %d)", kp.x);

    /* ---- the OLED mirror: the screen to the station, in 32-byte segments --- */
    printf("\nMIRROR (mirror.c, the OLED for the station)\n");
    reset_panel();
    oled_clear();
    char fr[MIRROR_FRAME_MAX];
    now = 20000;
    CHECK(mirror_next(fr, sizeof fr, now) == 0, "silent until the station asks: no $FB to a plain terminal");
    char req[24];
    snprintf(req, sizeof req, "$MIRROR,1*%02X", (unsigned)proto_checksum("MIRROR,1", 8));
    int took = mirror_frame(req, now);
    int frames_all = 0, longest = 0, all_ok = 1;
    size_t n;
    while ((n = mirror_next(fr, sizeof fr, now)) != 0 && frames_all < 100) {
        frames_all++;
        if ((int)n > longest) { longest = (int)n; }
        fr[n - 1] = 0;                                    /* drop the newline */
        const char *b; size_t bl;
        if (proto_check(fr, &b, &bl) != PROTO_OK || strncmp(b, "FB,", 3) != 0) { all_ok = 0; }
    }
    CHECK(took && frames_all == 32 && all_ok, "$MIRROR,1 -> the whole screen once: %d frames (8 pages x 4), all pass the checksum", frames_all);
    CHECK(longest <= (int)MIRROR_FRAME_MAX - 1, "longest $FB frame %d B, under MIRROR_FRAME_MAX %u", longest, (unsigned)MIRROR_FRAME_MAX);
    CHECK(mirror_next(fr, sizeof fr, now + 1) == 0, "then nothing, until something changes");
    oled_pixel(70, 20, OLED_ON);                          /* page 2, columns 64..95 */
    n = mirror_next(fr, sizeof fr, now + 2);
    fr[n ? n - 1 : 0] = 0;
    CHECK(n && strncmp(fr, "$FB,2,64,", 9) == 0 && mirror_next(fr, sizeof fr, now + 3) == 0,
          "one pixel at (70, 20) sends one segment: %.9s...", n ? fr : "(none)");
    n = mirror_next(fr, sizeof fr, now + MIRROR_REFRESH_MS + 10);
    CHECK(n != 0, "with nothing changed, one segment is re-sent every %u ms anyway", (unsigned)MIRROR_REFRESH_MS);
    CHECK(mirror_next(fr, sizeof fr, now + MIRROR_LIVE_MS + 10) == 0, "no $MIRROR,1 for %u ms: it stops", (unsigned)MIRROR_LIVE_MS);
    printf("        bandwidth: a whole screen is 32 x %d B = %d B = %.0f ms at 115200 baud; one segment %.1f ms\n",
           longest, 32 * longest, 32.0 * longest * 10.0 / 115.2, longest * 10.0 / 115.2);

    reset_panel();                                        /* the arcade, played by keys */
    arcade_init();
    kbd_init(&kb);
    now = 5000;
    for (int i = 0; i < 10; i++) { kp = no_input(0); kbd_apply(&kb, &kp, now); arcade_step(&kp); now += TICK_MS; }
    kbd_type(&kb, "\x1b[A", now);                         /* one tap: wraps to FLAP */
    for (int i = 0; i < 20; i++) { kp = no_input(0); kbd_apply(&kb, &kp, now); arcade_step(&kp); now += TICK_MS; }
    kbd_type(&kb, "a", now);
    kp = no_input(0); kbd_apply(&kb, &kp, now); arcade_step(&kp);
    CHECK(arcade_game() == 2 && arcade_state() == ARC_PLAY, "the arcade by keys: one Up tap selects FLAP, a starts it");

    printf("\nscreenshots written to host/out/: snake breakout flap menu gameover (.txt and .svg)\n");
    printf("%s: %d check(s) failed\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
