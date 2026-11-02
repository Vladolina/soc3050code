/*
 * breakout.c - BREAKOUT.  Paddle, ball, 48 bricks, three lives.   PURE C.
 *
 * The ball's position and velocity are FIXED POINT: 1/256ths of a pixel, so a
 * ball can move 1.19 pixels per tick with integers only (no FPU on the M0+;
 * lesson 10 measures what float would have cost).  The paddle and the bricks
 * are whole pixels.
 *
 * All 48 bricks are four 16-bit words: bit c of rows[r] is the brick in row r,
 * column c.  "Is it there?" is a mask, "break it" is an AND, "all gone?" is
 * four compares.  The AVR edition packed 24 bricks into 3 bytes the same way.
 *
 * Every collision is aabb() from engine.h.  Which way to bounce off a brick
 * is decided by where the ball WAS: if it already overlapped the brick's
 * columns a tick ago it came in through the top or bottom (flip vy),
 * otherwise through a side (flip vx).
 */
#include "engine.h"

#define ROWS     4
#define COLS     12
#define BRW      9                      /* brick: 9 x 3, on a 10 x 5 pitch  */
#define BRH      3
#define BX0      4
#define BY0      (FIELD_Y + 4)
#define PY       60                     /* paddle top row                   */
#define PH       2
#define BALL     2                      /* the ball is 2 x 2                */
#define LEFT_X   1                      /* walls at x = 0 and x = 127       */
#define RIGHT_X  (OLED_W - 1)
#define SERVE_AUTO (3 * TICK_HZ)        /* serve by itself after 3 s        */

static uint16_t rows[ROWS];
static int px, pw;                      /* paddle left edge and width       */
static int32_t bx, by, vx, vy;          /* ball, in 1/256 px (FP = 8)        */
static int32_t pxf;                     /* paddle, in 1/256 px               */
static int speed, level, lives, stuck, stuck_ticks, wave;
static int32_t score;

static int shown_px = -1, shown_bx = -1, shown_by = -1;
static uint8_t broken[8];               /* bricks to erase: r * COLS + c     */
static int n_broken, need_full;

static void fill_wall(void)
{
    for (int r = 0; r < ROWS; r++) { rows[r] = (1u << COLS) - 1u; }
}

static void serve(void)
{
    stuck = 1;  stuck_ticks = 0;
    vx = vy = 0;
}

static void breakout_start(int lvl)
{
    level = lvl;  lives = 3;  score = 0;  wave = 0;
    pw = 22 - 2 * lvl;                  /* 20 px at level 1 .. 12 at level 5 */
    px = (OLED_W - pw) / 2;
    pxf = (int32_t)px << FP;
    speed = 128 + 20 * (lvl - 1);       /* 0.5 .. 0.8 px per tick (halved    */
                                        /* 2026-11-03: too fast to follow)   */
    fill_wall();
    serve();
    n_broken = 0;  need_full = 1;
}

static void bounce_sound(void) { sfx(SFX_BOUNCE); }

static int breakout_step(const pad_t *in)
{
    /* The paddle: full stick = 4 px per tick = 200 px/s.  Kept in fixed point
     * too: in whole pixels, x * 4 / 100 is 0 for any stick under 25%, and a
     * gentle push would not move it at all. */
    pxf += (int32_t)in->x * (4 << FP) / 100;
    if (pxf < (LEFT_X << FP)) { pxf = LEFT_X << FP; }
    if (pxf > ((RIGHT_X - pw) << FP)) { pxf = (int32_t)(RIGHT_X - pw) << FP; }
    px = (int)(pxf >> FP);

    if (stuck) {                         /* riding the paddle, waiting       */
        bx = (int32_t)(px + pw / 2 - 1) << FP;
        by = (int32_t)(PY - BALL) << FP;
        if ((in->pressed & PAD_A) || ++stuck_ticks >= SERVE_AUTO) {
            stuck = 0;
            vx = (rng_next() & 1) ? speed / 2 : -speed / 2;
            vy = -speed;
            sfx(SFX_SELECT);
        }
        return 0;
    }

    int oldx = (int)(bx >> FP);          /* where it was: decides the bounce */
    bx += vx;  by += vy;

    /* walls and ceiling */
    if (bx < (LEFT_X << FP))                 { bx = LEFT_X << FP;  vx = -vx; bounce_sound(); }
    if (bx > ((RIGHT_X - BALL) << FP))       { bx = (RIGHT_X - BALL) << FP; vx = -vx; bounce_sound(); }
    if (by < (FIELD_Y << FP))                { by = FIELD_Y << FP; vy = -vy; bounce_sound(); }

    int x = (int)(bx >> FP), y = (int)(by >> FP);

    /* the paddle: where it hits sets the angle - the centre sends it straight
     * up, the ends send it off at up to 45 degrees.  This is the one control
     * the player has over the ball, and what makes Breakout a game. */
    if (vy > 0 && aabb(x, y, BALL, BALL, px, PY, pw, PH)) {
        int off = (x + BALL / 2) - (px + pw / 2);            /* -pw/2 .. pw/2 */
        vx = (int32_t)off * speed * 2 / pw;
        if (vx >  speed) { vx =  speed; }
        if (vx < -speed) { vx = -speed; }
        vy = -speed;
        by = (int32_t)(PY - BALL) << FP;
        bounce_sound();
    }

    /* the bricks: at most one per tick, which at < 3 px/tick never tunnels */
    for (int r = 0; r < ROWS; r++) {
        int ry = BY0 + r * (BRH + 2);
        if (!aabb(x, y, BALL, BALL, BX0, ry, COLS * (BRW + 1), BRH)) { continue; }
        for (int c = 0; c < COLS; c++) {
            int rx = BX0 + c * (BRW + 1);
            if (!(rows[r] & (1u << c)) || !aabb(x, y, BALL, BALL, rx, ry, BRW, BRH)) { continue; }
            rows[r] &= (uint16_t)~(1u << c);
            if (n_broken < 8) { broken[n_broken++] = (uint8_t)(r * COLS + c); }
            else              { need_full = 1; }
            if (oldx + BALL > rx && oldx < rx + BRW) { vy = -vy; } else { vx = -vx; }
            score += (ROWS - r) * level;                    /* top rows pay more */
            sfx(SFX_BRICK);
            goto bricks_done;
        }
    }
bricks_done:

    if ((rows[0] | rows[1] | rows[2] | rows[3]) == 0) {      /* wave cleared  */
        wave++;
        speed += 12;
        if (speed > 3 * 256 - 64) { speed = 3 * 256 - 64; }   /* < 3 px: no tunnelling */
        fill_wall();
        serve();
        need_full = 1;
        sfx(SFX_LEVEL);
    }

    if (y >= OLED_H) {                                       /* missed it     */
        lives--;
        if (lives == 0) { return 1; }
        sfx(SFX_LOSE);
        serve();
    }
    return 0;
}

/* Every standing brick that overlaps the rectangle (all of them, for the
 * whole screen).  Erasing the ball can clip a brick it was grazing; this puts
 * those pixels back. */
static void draw_bricks(int x, int y, int w, int h)
{
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if ((rows[r] & (1u << c)) &&
                aabb(x, y, w, h, BX0 + c * (BRW + 1), BY0 + r * (BRH + 2), BRW, BRH)) {
                oled_fill_rect(BX0 + c * (BRW + 1), BY0 + r * (BRH + 2), BRW, BRH, OLED_ON);
            }
        }
    }
}

/* Erase what moved, then draw everything that moves.  Drawing a pixel that is
 * already lit costs CPU but no bus: oled_pixel() only dirties a page when a
 * byte actually changes. */
static void breakout_draw(int full)
{
    int x = (int)(bx >> FP), y = (int)(by >> FP);
    if (full || need_full) {
        oled_clear();
        oled_vline(0, FIELD_Y, OLED_H - FIELD_Y, OLED_ON);
        oled_vline(OLED_W - 1, FIELD_Y, OLED_H - FIELD_Y, OLED_ON);
        draw_bricks(0, 0, OLED_W, OLED_H);
        need_full = 0;
    } else {
        for (int i = 0; i < n_broken; i++) {
            int r = broken[i] / COLS, c = broken[i] % COLS;
            oled_fill_rect(BX0 + c * (BRW + 1), BY0 + r * (BRH + 2), BRW, BRH, OLED_OFF);
        }
        if (shown_bx != x || shown_by != y) {
            oled_fill_rect(shown_bx, shown_by, BALL, BALL, OLED_OFF);
            draw_bricks(shown_bx, shown_by, BALL, BALL);
        }
        if (shown_px != px) { oled_fill_rect(shown_px, PY, pw, PH, OLED_OFF); }
    }
    n_broken = 0;
    oled_fill_rect(px, PY, pw, PH, OLED_ON);
    if (y < OLED_H) { oled_fill_rect(x, y, BALL, BALL, OLED_ON); }
    shown_px = px;  shown_bx = x;  shown_by = y;
    hud("BREAKOUT", score, lives, full);
}

static int32_t breakout_score(void) { return score; }

const game_t game_breakout = { "BREAKOUT", breakout_start, breakout_step, breakout_draw, breakout_score };

/* ---- for the host test ---------------------------------------------------- */
void breakout_peek(peek_t *p)
{
    int n = 0;
    for (int r = 0; r < ROWS; r++) { for (int c = 0; c < COLS; c++) { n += (rows[r] >> c) & 1; } }
    p->x = px;  p->y = PY;  p->w = pw;  p->h = PH;
    p->tx = (int)(bx >> FP);  p->ty = (int)(by >> FP);  p->vx = (int)vx;  p->vy = (int)vy;
    p->count = n;                            /* bricks standing now */
    p->lives = lives;
}
