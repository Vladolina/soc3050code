/*
 * test_games.c - the arcade, run on a PC.   SOC3050 lesson 12
 *
 * Compiles the lesson's REAL game files (../snake.c, ../breakout.c,
 * ../flappy.c, ../doom.c, ../prince.c, ../engine.c, ../arcade.c) and the REAL
 * display driver
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

/* DOOM: shoot what it can see, otherwise walk the shortest route (a BFS over
 * the map, doors passable) to the exit - or to ammunition or a medkit when it
 * is running low.  It stands its ground against zombies and backs away from
 * imps while shooting them.  It sees what a player sees: a monster counts only
 * if doom_los() says the ray from the player reaches it. */
#define DMW 24
#define DMH 16
static int dpass(int cx, int cy)
{
    int c = doom_cell(cx, cy);
    return c != '#' && c != '%' && c != 'E';
}
static void dbfs(int gx, int gy, int dist[DMH][DMW])         /* steps to (gx,gy), or -1 */
{
    int q[DMW * DMH], h = 0, t = 0;
    for (int y = 0; y < DMH; y++) { for (int x = 0; x < DMW; x++) { dist[y][x] = -1; } }
    dist[gy][gx] = 0;  q[t++] = gy * DMW + gx;
    while (h < t) {
        int c = q[h++], cx = c % DMW, cy = c / DMW;
        static const int dx[4] = {1, 0, -1, 0}, dy[4] = {0, -1, 0, 1};
        for (int d = 0; d < 4; d++) {
            int nx = cx + dx[d], ny = cy + dy[d];
            if (nx >= 0 && nx < DMW && ny >= 0 && ny < DMH && dist[ny][nx] < 0 && dpass(nx, ny)) {
                dist[ny][nx] = dist[cy][cx] + 1;  q[t++] = ny * DMW + nx;
            }
        }
    }
}
static int dturn(long lat, long depth)                       /* stick x to bring `lat` to zero */
{
    int ad = (int)(depth < 0 ? -depth : depth) + (int)(lat < 0 ? -lat : lat) + 1;
    int turn = (int)(lat * 300 / ad);
    return turn > 100 ? 100 : turn < -100 ? -100 : turn;
}
static pad_t doom_pilot(int t)
{
    pad_t p = no_input(t);
    peek_t d;  doom_peek(&d);
    int fx = d.vx, fy = d.vy, cx = d.x >> 8, cy = d.y >> 8;

    int best = -1, bx = 0, by = 0, bkind = 0, tx = 0, ty = 0, kind;       /* the nearest monster in sight */
    long bd2 = 1L << 40;
    for (int i = 0; (kind = doom_thing(i, &tx, &ty)) >= 0; i++) {
        if (kind != 1 && kind != 2) { continue; }
        long dx = tx - d.x, dy = ty - d.y, d2 = dx * dx + dy * dy;
        if (d2 < 8L * 256 * 8 * 256 && d2 < bd2 && doom_los(d.x, d.y, tx, ty)) { bd2 = d2; best = i; bx = tx; by = ty; bkind = kind; }
    }
    int gx = 22, gy = 2;                                     /* the cell beside the exit switch */
    int goal_x = 23 * 256 + 128, goal_y = 2 * 256 + 128;
    if (d.gap < 20 || d.lives < 55) {                        /* low on ammunition or health: shop */
        int want = d.gap < 20 ? 3 : 4, bestlen = 1 << 20, field[DMH][DMW];
        for (int i = 0; (kind = doom_thing(i, &tx, &ty)) >= 0; i++) {
            if (kind != want) { continue; }
            dbfs(tx >> 8, ty >> 8, field);
            int len = field[cy][cx];
            if (len >= 0 && len < bestlen) { bestlen = len; gx = tx >> 8; gy = ty >> 8; goal_x = tx; goal_y = ty; }
        }
    }
    if (best >= 0) {                                         /* fight */
        long dx = bx - d.x, dy = by - d.y;
        long depth = (dx * fx + dy * fy) >> 8, lat = (dx * -fy + dy * fx) >> 8;
        p.x = (int16_t)dturn(lat, depth);
        if (depth > 0 && (lat < 0 ? -lat : lat) < 50) { p.down |= PAD_A; }
        if (bkind == 2 && bd2 < 3L * 256 * 3 * 256) { p.y = -100; }        /* an imp near: back off */
        return p;
    }
    int field[DMH][DMW];                                     /* walk: to the neighbour closer to the goal */
    dbfs(gx, gy, field);
    int tcx = goal_x, tcy = goal_y;
    if (field[cy][cx] > 0) {
        static const int dxs[4] = {1, 0, -1, 0}, dys[4] = {0, -1, 0, 1};
        int bestd = field[cy][cx], bi = -1;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dxs[k], ny = cy + dys[k];
            if (nx >= 0 && nx < DMW && ny >= 0 && ny < DMH && field[ny][nx] >= 0 && field[ny][nx] < bestd) { bestd = field[ny][nx]; bi = k; }
        }
        if (bi >= 0) { tcx = (cx + dxs[bi]) * 256 + 128;  tcy = (cy + dys[bi]) * 256 + 128; }
    }
    long dx = tcx - d.x, dy = tcy - d.y;
    long depth = (dx * fx + dy * fy) >> 8, lat = (dx * -fy + dy * fx) >> 8;
    p.x = (int16_t)dturn(lat, depth);
    int al = (int)(lat < 0 ? -lat : lat);
    p.y = (int16_t)(depth > 0 && al * 3 < depth * 2 ? (al * 6 < depth ? 100 : 40) : 0);
    return p;
}
static pad_t doom_pacifist(int t) { pad_t p = doom_pilot(t); p.down &= (uint8_t)~PAD_A; return p; }

/* PRINCE: always heads right.  On the ground it looks one tile ahead:
 *   a wall with a ledge above it -> jump with A held, stick up to climb
 *   a hole, or spikes            -> running jump from the last few pixels
 *   anything else                -> run (a closed gate simply holds him until it opens)
 * Hanging: pull up.  Near a guard: close to sword range, parry the blade as it
 * falls, strike when the guard is open.  The three flags switch one skill off
 * each, to show that the game punishes the lack of it. */
enum { PS_STAND, PS_RUN, PS_JUMP, PS_FALL, PS_HANG, PS_CLIMB, PS_FIGHT, PS_STRIKE, PS_PARRY, PS_DEAD };
static int pr_skip_spikes, pr_no_grab, pr_no_parry;
static int pr_floorish(int c, int r) { int t = prince_tile(c, r); return t != '.' && t != '#'; }
static pad_t prince_right(int t) { pad_t p = no_input(t); p.x = 100; return p; }
static pad_t prince_pilot(int t)
{
    pad_t p = no_input(t);
    peek_t q;  prince_peek(&q);
    int c = q.x / 12, r = q.y / 18, e = q.x % 12;
    int a = pr_no_grab ? 0 : PAD_A;                          /* A held = grab ledges */
    if (q.gap == PS_HANG) { p.y = 100; p.down |= (uint8_t)a; return p; }
    if (q.gap == PS_JUMP || q.gap == PS_FALL) { p.down |= (uint8_t)a; p.x = (int16_t)(q.vx > 0 ? 100 : q.vx < 0 ? -100 : 0); return p; }
    if (q.gap == PS_CLIMB || q.gap == PS_DEAD) { return p; }
    if (q.w < 70 && q.gap >= PS_FIGHT) {                     /* the sword */
        if (q.ty == 2 && !pr_no_parry) { p.y = 100; return p; }      /* the blade is raised: parry */
        if (q.w > 19) { p.x = 100; return p; }                       /* close up */
        if (q.ty == 1 && q.gap == PS_FIGHT) { p.pressed = PAD_A; p.down |= PAD_A; }
        return p;
    }
    if (q.w < 70) { p.x = 0; return p; }                     /* the guard is waking: let the sword come out */
    int t1 = prince_tile(c + 1, r), edge = 12 - e;           /* edge: pixels to the next tile */
    if (t1 == '#' && pr_floorish(c + 1, r - 1) && !pr_floorish(c, r - 1)) {      /* a ledge above the wall */
        if (edge <= 4) { p.y = 100; p.down |= (uint8_t)a; } else { p.x = 100; }
        return p;
    }
    if ((t1 == '^' && !pr_skip_spikes) || t1 == '.') {       /* spikes or a hole ahead */
        if (edge <= 5) { p.x = 100; p.y = 100; p.down |= (uint8_t)a; } else { p.x = 100; }
        return p;
    }
    p.x = 100;
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
           100.0 * ((double)d->bytes / d->frames) / ((double)f->bytes / f->frames),   /* per frame: the runs differ in length */
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
    printf("\n  An autopilot with perfect aim and perfect parries cannot tell the levels apart, so each game\n"
           "  also gets a handicapped player: DOOM's pacifist (walks the route, never fires), PRINCE's\n"
           "  never-parry (fights, but never raises his guard).  How long one lasts, and how much one duel\n"
           "  costs the other, is the difficulty.\n");
    printf("  level | DOOM autopilot: kills waves | pacifist lasts | PRINCE autopilot: laps | never-parry: hearts lost per duel\n");
    for (int lv = 1; lv <= 5; lv++) {
        double dk = 0, dw = 0, dpt = 0, pl = 0, nh = 0, nl = 0;
        for (uint32_t seed = 1; seed <= 3; seed++) {
            run_t r = run(&game_doom, lv, seed * 101u, doom_pilot, 15000, 0, 0, 0);
            peek_t p;  doom_peek(&p);
            dk += p.count;  dw += p.w;
            r = run(&game_doom, lv, seed * 101u, doom_pacifist, 15000, 0, 0, 0);
            dpt += r.ticks / 50.0;
            long l0 = sfx_count[SFX_LEVEL];
            r = run(&game_prince, lv, seed * 101u, prince_pilot, 6000, 0, 0, 0);
            pl += (double)(sfx_count[SFX_LEVEL] - l0);
            long h0 = sfx_count[SFX_HURT];  l0 = sfx_count[SFX_LEVEL];
            pr_no_parry = 1;
            r = run(&game_prince, lv, seed * 101u, prince_pilot, 3000, 0, 0, 0);
            pr_no_parry = 0;
            nh += (double)(sfx_count[SFX_HURT] - h0);  nl += (double)(sfx_count[SFX_LEVEL] - l0) + 1.0;   /* +1: the duel in progress at the end */
        }
        printf("    %d   |           %5.1f  %5.1f |      %6.1f s  |            %5.1f       |                  %5.2f\n",
               lv, dk / 3, dw / 3, dpt / 3, pl / 3, nh / nl);
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
    run_t ddf = run(&game_doom, 3, 2024u, doom_pilot, 6000, 1, 0, 0);
    run_t pdf = run(&game_prince, 3, 99u, prince_pilot, 6000, 1, 0, 0);

    /* ---- DOOM ----------------------------------------------------------- */
    printf("\nDOOM\n");
    long shots0 = sfx_count[SFX_SHOT], hurts0 = sfx_count[SFX_HURT];
    run_t dm = run(&game_doom, 3, 2024u, doom_pilot, 15000, 0, "doom", 766);
    peek_t dp;  doom_peek(&dp);
    long dshots = sfx_count[SFX_SHOT] - shots0, dhurts = sfx_count[SFX_HURT] - hurts0;
    printf("        autopilot, level 3: %d ticks (%.1f s), %d waves cleared, %d kills, %ld shots, %ld times hurt, health %d, score %ld, %s\n",
           dm.ticks, dm.ticks / 50.0, dp.w, dp.count, dshots, dhurts, dp.lives, (long)dm.score, dm.over ? "killed" : "still alive");
    CHECK(dp.w >= 5, "reaches the exit switch again and again: %d waves in 5 minutes (need >= 5)", dp.w);
    CHECK(dp.count >= 30 && dshots > dp.count, "kills monsters: %d kills for %ld shots (need >= 30 kills)", dp.count, dshots);
    CHECK(!dm.over, "and survives: health %d", dp.lives);
    run_t d0 = run(&game_doom, 3, 2024u, no_input, 3000, 0, 0, 0);
    doom_peek(&dp);
    CHECK(!d0.over && dp.lives == 100 && dp.gap == 40 && dp.h == 8,
          "no input for 1 minute: all 8 monsters still asleep behind their doors, health %d, ammo %d", dp.lives, dp.gap);
    run_t dpac = run(&game_doom, 3, 2024u, doom_pacifist, 6000, 0, 0, 0);
    CHECK(dpac.over && dpac.ticks < 1500, "walking the route without firing is fatal: killed on tick %d (%.1f s)", dpac.ticks, dpac.ticks / 50.0);
    run_t dm2 = run(&game_doom, 3, 2024u, doom_pilot, 15000, 0, 0, 0);
    CHECK(dm2.hash == dm.hash && dm2.score == dm.score, "deterministic: same seed, same inputs -> score %ld, framebuffer hash %08lX",
          (long)dm2.score, (unsigned long)dm2.hash);
    run_t da = run(&game_doom, 3, 1u, doom_pilot, 2000, 0, 0, 0);
    run_t db = run(&game_doom, 3, 2u, doom_pilot, 2000, 0, 0, 0);
    CHECK(da.hash != db.hash, "a different seed deals different damage: %08lX vs %08lX", (unsigned long)da.hash, (unsigned long)db.hash);
    /* The ray walk is the one thing the pistol, the monsters and the renderer share.
     * (Q8 cells: x * 256.)  The last case is the bug this test was written for: the
     * line from (15.78, 11.48) to the zombie at (16.5, 13.5) clips the corner of a
     * pillar by less than a quarter cell, so a probing test said "visible" and every
     * bullet hit the pillar. */
    game_doom.start(3);
    CHECK(doom_los(896, 3456, 1664, 3456) == 1, "line of sight along a room: (3.5,13.5) -> (6.5,13.5) is clear");
    CHECK(doom_los(896, 3456, 2432, 3456) == 0, "a closed door blocks it: (3.5,13.5) -> (9.5,13.5)");
    CHECK(doom_los(4040, 2939, 4224, 3456) == 0, "a pillar's corner blocks it too: the exact walk catches a sliver thinner than a probe step");

    /* ---- PRINCE --------------------------------------------------------- */
    printf("\nPRINCE\n");
    long lap0 = sfx_count[SFX_LEVEL], hit0 = sfx_count[SFX_HIT], sw0 = sfx_count[SFX_SWORD], flap0 = sfx_count[SFX_FLAP];
    hurts0 = sfx_count[SFX_HURT];
    run_t pr = run(&game_prince, 3, 99u, prince_pilot, 6000, 0, "prince", 245);
    peek_t pp;  prince_peek(&pp);
    long laps = sfx_count[SFX_LEVEL] - lap0;
    printf("        autopilot, level 3: %d ticks (%.1f s), %ld laps of the level, %ld jumps, %ld sword hits, %ld parries/blocks, hurt %ld times, hearts %d, score %ld, %s\n",
           pr.ticks, pr.ticks / 50.0, laps, sfx_count[SFX_FLAP] - flap0, sfx_count[SFX_HIT] - hit0, sfx_count[SFX_SWORD] - sw0,
           sfx_count[SFX_HURT] - hurts0, pp.lives, (long)pr.score, pr.over ? "dead" : "still going");
    CHECK(laps >= 8 && !pr.over, "escapes: %ld times through the exit door in 2 minutes (need >= 8), still alive", laps);
    CHECK(sfx_count[SFX_HIT] - hit0 >= laps * 3, "the guard falls every lap: %ld sword hits, >= 3 a lap", sfx_count[SFX_HIT] - hit0);
    run_t p0 = run(&game_prince, 3, 99u, no_input, 3000, 0, 0, 0);
    prince_peek(&pp);
    CHECK(!p0.over && pp.x == 18 && pp.lives == 3, "no input for a minute: he stands at x = %d with %d hearts", pp.x, pp.lives);
    /* Running right with no jump: the floor ends at x = 48, and the first tick
     * that starts with his centre past it (tick 31) is the first of the fall.
     * Gravity adds 3/16 px a tick, capped at 3.5 px (terminal velocity), so the
     * 44 px to the bottom of the world take 25 ticks (dead on tick 56); then the
     * 61-tick collapse before the arcade's game-over box.  Traced, not derived. */
    run_t prun = run(&game_prince, 3, 99u, prince_right, 400, 0, 0, 0);
    prince_peek(&pp);
    CHECK(prun.over && prun.ticks > 100 && prun.ticks < 130 && pp.y > 5 * 18 + 15,
          "running right without jumping: into the first pit, game over on tick %d (31 to the edge + 25 falling + 61 dying = 117)", prun.ticks);
    pr_skip_spikes = 1;
    run_t pspk = run(&game_prince, 3, 99u, prince_pilot, 2000, 0, 0, 0);
    prince_peek(&pp);
    pr_skip_spikes = 0;
    CHECK(pspk.over && pp.x / 12 == 7 && pp.lives == 0, "jumping the pit but not the spikes: impaled on tile %d (the spikes are tile 7), game over on tick %d", pp.x / 12, pspk.ticks);
    pr_no_grab = 1;
    run_t pgr = run(&game_prince, 3, 99u, prince_pilot, 3000, 0, 0, 0);
    prince_peek(&pp);
    pr_no_grab = 0;
    CHECK(!pgr.over && pp.x < 170 && pp.count == 4, "jumping without A held never grabs the ledge: still below it at x = %d after a minute", pp.x);
    pr_no_parry = 1;
    hurts0 = sfx_count[SFX_HURT];
    run_t pnp = run(&game_prince, 3, 99u, prince_pilot, 3000, 0, 0, 0);
    pr_no_parry = 0;
    CHECK(sfx_count[SFX_HURT] - hurts0 >= 1, "fighting without ever parrying costs hearts: hurt %ld times (%s on tick %d)",
          sfx_count[SFX_HURT] - hurts0, pnp.over ? "dead" : "alive", pnp.ticks);
    CHECK(prince_fall_cost(1) == 0 && prince_fall_cost(2) == 1 && prince_fall_cost(3) >= 99,
          "a drop of 1 row is free, 2 rows cost a heart, 3 rows kill (rows, not pixels)");
    run_t pr2 = run(&game_prince, 3, 99u, prince_pilot, 6000, 0, 0, 0);
    CHECK(pr2.hash == pr.hash && pr2.score == pr.score, "deterministic: same seed, same inputs -> score %ld, framebuffer hash %08lX",
          (long)pr2.score, (unsigned long)pr2.hash);

    /* ---- the bus budget --------------------------------------------------- */
    printf("\nTHE BUS BUDGET, per frame (one frame per 20 ms tick), modelled at 400 kHz\n");
    printf("            ------- dirty pages --------------- | --- full repaint ---- | dirty/full  frames\n");
    printf("  game         bytes   mean      worst          |   bytes    mean       |  of bytes  > 20 ms\n");
    bus_row("SNAKE", &sa, &sd);
    bus_row("BREAKOUT", &b, &bd);
    bus_row("FLAP", &f, &fd);
    bus_row("DOOM", &dm, &ddf);
    bus_row("PRINCE", &pr, &pdf);
    CHECK((double)sa.bytes / sa.frames < 0.25 * (double)sd.bytes / sd.frames,
          "dirty pages cut SNAKE's bus traffic by more than 4x");
    CHECK(sd.bus_ms_max > 24.0 && sd.bus_ms_max < 26.0, "a full repaint costs %.2f ms of bus: over the 20 ms tick", sd.bus_ms_max);
    CHECK((double)dm.bytes / dm.frames > 0.6 * (double)ddf.bytes / ddf.frames,
          "a raycast view changes almost every page when you move: DOOM sends %.0f %% of a full repaint's bytes (no dirty-page saving to speak of)",
          100.0 * (double)dm.bytes / dm.frames / ((double)ddf.bytes / ddf.frames));
    /* A 15-pixel figure always straddles three 8-row pages (the tile rows are 18 px, not a
     * multiple of 8), so ~387 bytes a frame while it moves is the floor at this tile height. */
    CHECK((double)pr.bytes / pr.frames < 0.45 * (double)pdf.bytes / pdf.frames,
          "a tile world with two figures is what dirty pages are for: PRINCE sends %.0f %% of a full repaint's bytes",
          100.0 * (double)pr.bytes / pr.frames / ((double)pdf.bytes / pdf.frames));

    printf("\nTHE LOOP, simulated: frames vs updates per second (Main.c's $PERF fps and ups)\n");
    loop_row("SNAKE", &game_snake, 12345u, snake_pilot, 0);
    loop_row("SNAKE", &game_snake, 12345u, snake_pilot, 1);
    loop_row("BREAKOUT", &game_breakout, 4242u, breakout_pilot, 0);
    loop_row("BREAKOUT", &game_breakout, 4242u, breakout_pilot, 1);
    loop_row("FLAP", &game_flap, 99u, flap_pilot, 0);
    loop_row("FLAP", &game_flap, 99u, flap_pilot, 1);
    loop_row("DOOM", &game_doom, 2024u, doom_pilot, 0);
    loop_row("DOOM", &game_doom, 2024u, doom_pilot, 1);
    loop_row("PRINCE", &game_prince, 99u, prince_pilot, 0);
    loop_row("PRINCE", &game_prince, 99u, prince_pilot, 1);

    /* ---- the arcade: menu, play, game over, $SCORE, high score ------------ */
    printf("\nARCADE\n");
    reset_panel();
    arcade_init();
    pad_t in = no_input(0);
    for (int t = 0; t < 10; t++) { arcade_step(&in); arcade_draw(); }
    in.knob = 650;                                    /* level 4 */
    arcade_step(&in); arcade_draw();
    in.y = 100; arcade_step(&in); in.y = 0; arcade_step(&in);      /* up: wraps to PRINCE, the fifth */
    int wrapped = arcade_game();
    in.y = -100; arcade_step(&in); in.y = 0; arcade_step(&in);     /* down: back to SNAKE */
    arcade_draw();
    shot("menu");
    CHECK(wrapped == 4, "menu: stick up from SNAKE wraps round to the fifth game, PRINCE (game %d)", wrapped);
    CHECK(arcade_game() == 0 && arcade_state() == ARC_MENU, "menu: stick down returns to SNAKE");
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

    /* The two newer games, through the arcade: pick each with the stick, play
     * it until it ends, and the $SCORE frame names it and passes the checksum. */
    static const struct { int downs, game; const char *name; pilot_fn pilot; } extra[2] = {
        { 3, 3, "DOOM",   doom_pacifist },
        { 1, 4, "PRINCE", prince_right },
    };
    for (int e = 0; e < 2; e++) {
        char prefix[32];
        snprintf(prefix, sizeof prefix, "SCORE,%s,", extra[e].name);
        in = no_input(0);
        for (int k = 0; k < 50; k++) { arcade_step(&in); }
        in.pressed = PAD_B; arcade_step(&in); in.pressed = 0;                       /* back to the menu */
        CHECK(arcade_state() == ARC_MENU, "B from the game-over box returns to the menu");
        for (int k = 0; k < extra[e].downs; k++) { in.y = -100; arcade_step(&in); in.y = 0; arcade_step(&in); }
        arcade_draw();
        CHECK(arcade_game() == extra[e].game, "stick down picks %s on the menu (game %d)", extra[e].name, extra[e].game);
        int before = scores_reported;
        in.pressed = PAD_A; arcade_step(&in); in.pressed = 0;
        for (t = 0; arcade_state() == ARC_PLAY && t < 30000; t++) {
            pad_t a = extra[e].pilot(t);
            arcade_step(&a);  arcade_draw();
        }
        rc = proto_check(last_score_line, &body, &blen);
        printf("        game over after %d ticks: %s\n", t, last_score_line);
        CHECK(arcade_state() == ARC_OVER && scores_reported == before + 1 && rc == PROTO_OK
              && strncmp(body, prefix, strlen(prefix)) == 0,
              "%s is reported once, by name, and its frame passes the checksum", extra[e].name);
    }

    printf("\nscreenshots written to host/out/: snake breakout flap doom prince menu gameover (.txt and .svg)\n");
    printf("%s: %d check(s) failed\n", failures ? "FAILED" : "PASSED", failures);
    return failures ? 1 : 0;
}
