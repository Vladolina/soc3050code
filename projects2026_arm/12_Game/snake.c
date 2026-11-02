/*
 * snake.c - SNAKE.  Eat, grow, do not bite the wall or yourself.   PURE C.
 *
 * The playfield is a grid of 4x4-pixel cells, 31 across and 13 down, inside a
 * one-pixel wall.  The snake is a RING BUFFER of cell numbers (cy * 31 + cx):
 * moving is "push a new head, pop the tail", two O(1) operations however long
 * the snake gets.  An occupancy bitmap answers "is this cell snake?" in O(1)
 * too, so biting yourself costs one bit test, not a walk along the body.
 *
 *   RAM: 403 cells x 2 bytes of ring + 51 bytes of bitmap = 857 bytes.
 *
 * Drawing is incremental.  A move changes at most three cells - the new head,
 * the old tail, a new apple - so step() writes them to a short CHANGED list
 * and draw() repaints only those.  Clearing the screen and redrawing the
 * whole snake every frame would look identical and would dirty every page
 * the snake crosses: 7 pages of I2C instead of 1 or 2.  Slide 12 measures it.
 */
#include "engine.h"

#define CELL    4
#define GW      31                      /* cells across                     */
#define GH      13                      /* cells down                       */
#define NCELLS  (GW * GH)
#define GX0     2                       /* pixel of cell (0, 0)             */
#define GY0     (FIELD_Y + 1)

enum { RIGHT, UP, LEFT, DOWN };
static const int8_t DX[4] = { 1, 0, -1, 0 };
static const int8_t DY[4] = { 0, -1, 0, 1 };

static uint16_t ring[NCELLS];           /* body, tail .. head               */
static uint16_t tail, len;
static uint8_t  occ[(NCELLS + 7) / 8];  /* 1 bit per cell: is it snake?     */
static int      dir, want, grow, food, level, period, timer, eaten;
static int32_t  score;

static uint16_t changed[8];             /* cells to repaint at the next draw */
static int      n_changed, need_full;

static int  is_snake(int c)  { return (occ[c >> 3] >> (c & 7)) & 1; }
static void set_snake(int c, int on)
{
    if (on) { occ[c >> 3] |= (uint8_t)(1u << (c & 7)); }
    else    { occ[c >> 3] &= (uint8_t)~(1u << (c & 7)); }
}
static void mark(int c)
{
    if (n_changed < 8) { changed[n_changed++] = (uint16_t)c; }
    else               { need_full = 1; }     /* too many: repaint all */
}
static uint16_t head(void) { return ring[(tail + len - 1u) % NCELLS]; }

static void place_food(void)
{
    /* Random probes first: almost always lands at once.  Then, if the board
     * is nearly full, a linear scan from a random start - so it always ends. */
    for (int tries = 0; tries < 32; tries++) {
        int c = (int)rng_below(NCELLS);
        if (!is_snake(c)) { food = c; mark(c); return; }
    }
    int start = (int)rng_below(NCELLS);
    for (int i = 0; i < NCELLS; i++) {
        int c = (start + i) % NCELLS;
        if (!is_snake(c)) { food = c; mark(c); return; }
    }
    food = -1;                                       /* board full: you win */
}

/* The knob's level sets the starting pace; every fifth apple quickens it.
 * period = ticks per move: 16 at level 1 (3 cells/s) .. 8 at level 5 (6.25).
 * (Halved from 8 .. 4 on 2026-11-03: too fast to follow in the simulator.) */
static void set_pace(void)
{
    period = 2 * (9 - level) - eaten / 5;
    if (period < 4) { period = 4; }
}

static void snake_start(int lvl)
{
    for (unsigned i = 0; i < sizeof occ; i++) { occ[i] = 0; }
    level = lvl;  eaten = 0;  score = 0;  grow = 0;  timer = 0;
    tail = 0;  len = 3;
    for (int i = 0; i < 3; i++) {                    /* 3 cells, heading right */
        int c = (GH / 2) * GW + 8 + i;
        ring[i] = (uint16_t)c;
        set_snake(c, 1);
    }
    dir = want = RIGHT;
    n_changed = 0;  need_full = 1;
    set_pace();
    place_food();
}

static int snake_step(const pad_t *in)
{
    /* Steering: the stick's larger axis wins, past a third of its travel.
     * Reversing into your own neck is refused rather than fatal. */
    int ax = in->x < 0 ? -in->x : in->x, ay = in->y < 0 ? -in->y : in->y;
    int d = -1;
    if (ax > 35 && ax >= ay) { d = in->x > 0 ? RIGHT : LEFT; }
    else if (ay > 35)        { d = in->y > 0 ? UP : DOWN; }
    if (d >= 0 && d != ((dir + 2) & 3)) { want = d; }

    if (++timer < period) { return 0; }              /* not time to move yet */
    timer = 0;
    dir = want;

    int h = head();
    int nx = h % GW + DX[dir], ny = h / GW + DY[dir];
    if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) { return 1; }   /* the wall */
    int n = ny * GW + nx;

    /* Biting yourself - except the tail cell, which moves out of the way this
     * same tick unless the snake is growing.  Chasing your own tail is legal
     * in every Snake ever made. */
    if (is_snake(n) && !(n == ring[tail] && grow == 0)) { return 1; }

    if (grow > 0) { grow--; }                        /* grow: keep the tail  */
    else {
        int t = ring[tail];
        set_snake(t, 0);
        if (t != n) { mark(t); }
        tail = (uint16_t)((tail + 1u) % NCELLS);  len--;
    }

    ring[(tail + len) % NCELLS] = (uint16_t)n;  len++;
    set_snake(n, 1);  mark(n);

    if (n == food) {
        eaten++;
        score += level;                              /* harder pays more     */
        grow += 2;
        sfx(SFX_EAT);
        set_pace();
        place_food();
        if (food < 0) { return 1; }
    }
    return 0;
}

/* One cell, drawn from the truth (the bitmap), whatever it was before. */
static void draw_cell(int c)
{
    int x = GX0 + (c % GW) * CELL, y = GY0 + (c / GW) * CELL;
    oled_fill_rect(x, y, 3, 3, OLED_OFF);
    if (is_snake(c)) {
        oled_fill_rect(x, y, 3, 3, OLED_ON);
    } else if (c == food) {                          /* a small plus sign    */
        oled_pixel(x + 1, y, OLED_ON);
        oled_hline(x, y + 1, 3, OLED_ON);
        oled_pixel(x + 1, y + 2, OLED_ON);
    }
}

static void snake_draw(int full)
{
    if (full || need_full) {
        oled_clear();
        oled_rect(0, FIELD_Y, OLED_W, OLED_H - FIELD_Y, OLED_ON);    /* walls */
        for (unsigned i = 0; i < len; i++) { draw_cell(ring[(tail + i) % NCELLS]); }
        if (food >= 0) { draw_cell(food); }
        need_full = 0;
    } else {
        for (int i = 0; i < n_changed; i++) { draw_cell(changed[i]); }
    }
    n_changed = 0;
    hud("SNAKE", score, -1, full);
}

static int32_t snake_score(void) { return score; }

const game_t game_snake = { "SNAKE", snake_start, snake_step, snake_draw, snake_score };

/* ---- for the host test ---------------------------------------------------- */
void snake_peek(peek_t *p)
{
    int h = head();
    p->x = h % GW;  p->y = h / GW;  p->w = p->h = 1;
    p->tx = food >= 0 ? food % GW : -1;  p->ty = food >= 0 ? food / GW : -1;
    p->vy = dir;  p->count = len;  p->lives = grow;
}

int snake_cell_free(int cx, int cy)
{
    if (cx < 0 || cx >= GW || cy < 0 || cy >= GH) { return 0; }
    return !is_snake(cy * GW + cx);
}
