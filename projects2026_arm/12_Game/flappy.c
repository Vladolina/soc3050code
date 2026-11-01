/*
 * flappy.c - FLAP.  One button, gravity, gaps.   PURE C.
 *
 * The physics is two lines per tick, in fixed point (1/256 px):
 *
 *     vy += GRAVITY;        velocity integrates acceleration
 *     y  += vy;             position integrates velocity
 *
 * That is Euler integration - the same two lines lesson 13's drone uses for
 * its altitude, with a motor in place of the button.  A flap does not push:
 * it SETS the upward speed, which is why the game feels crisp.
 *
 *   gravity 28/256 px per tick^2  = 273 px/s^2   (the screen is 64 px tall)
 *   flap    -400/256 px per tick  = -78 px/s     peaks ~11 px higher, 0.29 s later
 *
 * Three pipes scroll left and are recycled: when one leaves the screen it
 * moves to 3 x spacing to the right with a new random gap.  Pipes span the
 * whole height, so every page changes every tick - this game is the worst
 * case for dirty pages, and slide 12 says so.
 */
#include "engine.h"

#define BIRD_X     20
#define BIRD_W     8
#define BIRD_H     6
#define GRAVITY    28
#define FLAP       (-400)
#define VMAX       640                  /* terminal velocity, 2.5 px/tick    */
#define NPIPES     3
#define PIPE_W     10
#define SPACING    52                   /* pixels between pipes              */
#define WAIT_AUTO  (3 * TICK_HZ)        /* starts by itself after 3 s        */

/* The bird, in the panel's own format: one byte per column, bit 0 at the
 * top (oled.h).  Each frame is the ASCII art beside it, read down a column:
 *
 *     frame 0 (wings up)    frame 1 (wings down)
 *       ..###...              ..###...
 *       ##...#..              .#...#..
 *       ###.#.#.              .#..#.#.
 *       .#....##              ##....##
 *       .#....#.              ###...#.
 *       ..####..              ..####..
 */
static const uint8_t bird[2][BIRD_W] = {
    { 0x06, 0x1E, 0x25, 0x21, 0x25, 0x22, 0x1C, 0x08 },
    { 0x18, 0x1E, 0x31, 0x21, 0x25, 0x22, 0x1C, 0x08 },
};

typedef struct { int32_t x; int gap_y, passed; } pipe_t;

static pipe_t  pipes[NPIPES];
static int32_t by, vy;                  /* bird, 1/256 px                    */
static int     level, gap, speed, waiting, wait_ticks, flap_anim, stick_was_up;
static int32_t score;

static int shown_y = -1, shown_frame = -1, shown_px[NPIPES];
static int need_full, shown_prompt;

static int new_gap_y(void)
{
    int lo = FIELD_Y + 4, hi = OLED_H - 4 - gap;
    return lo + (int)rng_below((uint32_t)(hi - lo + 1));
}

static void flap_start(int lvl)
{
    level = lvl;  score = 0;
    gap   = 28 - 2 * lvl;               /* 26 px at level 1 .. 18 at level 5 */
    speed = 256 + 24 * (lvl - 1);       /* 1.0 .. 1.4 px per tick            */
    by = (int32_t)32 << FP;  vy = 0;
    waiting = 1;  wait_ticks = 0;  flap_anim = 0;  stick_was_up = 0;
    for (int i = 0; i < NPIPES; i++) {
        pipes[i].x = (int32_t)(OLED_W + 24 + i * SPACING) << FP;
        pipes[i].gap_y = new_gap_y();
        pipes[i].passed = 0;
        shown_px[i] = -100;
    }
    need_full = 1;
}

static int flap_step(const pad_t *in)
{
    /* A flap is an EDGE - button A going down, or the stick pushed up past
     * half way.  Holding does nothing; that is what makes it a skill.  (Not B:
     * the arcade takes B as pause before a game ever sees it.) */
    int up = in->y > 50;
    int flap = (in->pressed & PAD_A) || (up && !stick_was_up);
    stick_was_up = up;

    if (waiting) {                       /* hover until the first flap        */
        if (flap || ++wait_ticks >= WAIT_AUTO) { waiting = 0; }
        else { return 0; }
    }

    if (flap) { vy = FLAP; flap_anim = 8; sfx(SFX_FLAP); }
    if (flap_anim > 0) { flap_anim--; }

    vy += GRAVITY;
    if (vy > VMAX) { vy = VMAX; }
    by += vy;
    if (by < ((int32_t)FIELD_Y << FP)) { by = (int32_t)FIELD_Y << FP; vy = 0; }   /* ceiling */
    int y = (int)(by >> FP);
    if (y + BIRD_H > OLED_H) { return 1; }                                  /* ground  */

    int pace = speed + 8 * (int)(score / 5);           /* a little faster every 5 */
    for (int i = 0; i < NPIPES; i++) {
        pipe_t *p = &pipes[i];
        p->x -= pace;
        int x = (int)(p->x >> FP);
        if (x + PIPE_W < 0) {                          /* off the left: recycle */
            p->x += (int32_t)(NPIPES * SPACING) << FP;
            p->gap_y = new_gap_y();
            p->passed = 0;
            x = (int)(p->x >> FP);
        }
        if (!p->passed && x + PIPE_W < BIRD_X) {
            p->passed = 1;
            score++;
            sfx(SFX_POINT);
        }
        /* The bird's box is shrunk by a pixel all round: a hit the player can
         * see is fair, a hit on a corner pixel feels like a bug. */
        int bx1 = BIRD_X + 1, by1 = y + 1, bw = BIRD_W - 2, bh = BIRD_H - 2;
        if (aabb(bx1, by1, bw, bh, x, FIELD_Y, PIPE_W, p->gap_y - FIELD_Y) ||
            aabb(bx1, by1, bw, bh, x, p->gap_y + gap, PIPE_W, OLED_H - p->gap_y - gap)) {
            return 1;
        }
    }
    return 0;
}

static void pipe_shape(int x, int gap_y, int colour)
{
    /* body outlines, with a cap 2 px wider at each end of the gap */
    oled_rect(x + 1, FIELD_Y, PIPE_W - 2, gap_y - FIELD_Y - 3, colour);
    oled_fill_rect(x, gap_y - 3, PIPE_W, 3, colour);
    oled_fill_rect(x, gap_y + gap, PIPE_W, 3, colour);
    oled_rect(x + 1, gap_y + gap + 3, PIPE_W - 2, OLED_H - gap_y - gap - 3, colour);
}

static void flap_draw(int full)
{
    int y = (int)(by >> FP);
    int frame = flap_anim > 4 ? 0 : 1;
    if (full || need_full) {
        oled_clear();
        need_full = 0;
        shown_prompt = 0;
    } else {
        /* Erase everything that moved, THEN draw everything: an erase never
         * cuts into something drawn this frame. */
        if (shown_prompt && !waiting) { oled_fill_rect(40, 56, 42, 7, OLED_OFF); shown_prompt = 0; }
        for (int i = 0; i < NPIPES; i++) {
            int x = (int)(pipes[i].x >> FP);
            if (x != shown_px[i]) { oled_fill_rect(shown_px[i], FIELD_Y, PIPE_W, OLED_H - FIELD_Y, OLED_OFF); }
        }
        if (y != shown_y || frame != shown_frame) { oled_fill_rect(BIRD_X, shown_y, BIRD_W, BIRD_H, OLED_OFF); }
    }
    for (int i = 0; i < NPIPES; i++) {
        int x = (int)(pipes[i].x >> FP);
        if (x < OLED_W) { pipe_shape(x, pipes[i].gap_y, OLED_ON); }
        shown_px[i] = x;
    }
    oled_sprite(BIRD_X, y, BIRD_W, BIRD_H, bird[frame], OLED_ON);
    shown_y = y;  shown_frame = frame;
    if (waiting) { oled_text(40, 56, "A: FLAP", OLED_ON); shown_prompt = 1; }
    hud("FLAP", score, -1, full);
}

static int32_t flap_score(void) { return score; }

const game_t game_flap = { "FLAP", flap_start, flap_step, flap_draw, flap_score };

/* ---- for the host test ---------------------------------------------------- */
void flap_peek(peek_t *p)
{
    /* the next pipe the bird has not yet cleared */
    int best = -1, bx = 1 << 30;
    for (int i = 0; i < NPIPES; i++) {
        int x = (int)(pipes[i].x >> FP);
        if (x + PIPE_W >= BIRD_X && x < bx) { bx = x; best = i; }
    }
    p->x = BIRD_X;  p->y = (int)(by >> FP);  p->w = BIRD_W;  p->h = BIRD_H;
    p->tx = best >= 0 ? bx : -1;
    p->ty = best >= 0 ? pipes[best].gap_y : (OLED_H - gap) / 2;     /* gap top */
    p->gap = gap;
    p->vy = (int)vy;  p->count = (int)score;  p->lives = waiting;
}
