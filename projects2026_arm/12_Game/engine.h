/*
 * engine.h - the arcade's game engine: the contract every game keeps
 *
 * PURE C.  Nothing in this file, engine.c, arcade.c or the three games
 * includes stm32c031xx.h, so all of it compiles on a PC - host/test_games.c
 * runs the real games there, frame by frame, with scripted joysticks.
 *
 * The model, in four sentences.
 *   1. Time advances in fixed TICKS of 20 ms; step() moves the world one tick
 *      and never looks at a clock, so a game runs identically on the board,
 *      on a PC, fast or slow.
 *   2. draw() changes the framebuffer (oled.h) and touches no wire.
 *   3. Main.c decides how many ticks to run and when to draw and flush -
 *      "update at a fixed rate, render when time allows".
 *   4. Anything a game wants from the platform - a sound - goes through one
 *      hook, sfx(), which Main.c implements with the buzzer and the host test
 *      implements by counting.
 */
#ifndef ENGINE_H
#define ENGINE_H

#include <stdint.h>
#include "pad.h"          /* pad_t: joystick, knob, button edges - pure C */
#include "oled.h"

#define TICK_MS   20      /* one step() = 20 ms of game time: 50 updates/s  */
#define TICK_HZ   (1000 / TICK_MS)
#define HUD_H     9       /* rows 0..7 text, row 8 a line: the HUD           */
#define FIELD_Y   HUD_H   /* the first row a game may play in                */
#define FP        8       /* fixed point: positions in 1/256ths of a pixel   */

/* ---- what every game provides ------------------------------------------- */
typedef struct {
    const char *name;                 /* menu text and the $SCORE field       */
    void    (*start)(int level);      /* level 1..5, from the knob            */
    int     (*step)(const pad_t *in); /* one tick; returns 1 when it is over  */
    void    (*draw)(int full);        /* full = 1: repaint everything         */
    int32_t (*score)(void);
} game_t;

extern const game_t game_snake, game_breakout, game_flap, game_doom, game_prince;

/* ---- sound: the one hook to the platform -------------------------------- */
enum { SFX_MOVE, SFX_SELECT, SFX_START, SFX_EAT, SFX_BOUNCE, SFX_BRICK,
       SFX_FLAP, SFX_POINT, SFX_LOSE, SFX_LEVEL, SFX_OVER, SFX_HIGH,
       SFX_SHOT, SFX_HIT, SFX_HURT, SFX_DOOR, SFX_SWORD, SFX_THUD,   /* DOOM, PRINCE */
       SFX_COUNT };
void sfx(int id);                     /* Main.c on the board; the test on a PC */

/* ---- a tiny deterministic random number generator ----------------------- */
void     rng_seed(uint32_t s);
uint32_t rng_next(void);              /* xorshift32: 3 shifts, 3 XORs          */
uint32_t rng_below(uint32_t n);       /* 0 .. n-1                              */

/* ---- collision: axis-aligned bounding boxes ----------------------------- */
/* Two rectangles overlap unless one is entirely left of, right of, above or
 * below the other.  Four comparisons; the whole of collision in this arcade. */
static inline int aabb(int ax, int ay, int aw, int ah,
                       int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* ---- shared drawing ------------------------------------------------------ */
void hud(const char *name, int32_t score, int lives, int full);  /* lives < 0: none */
int  text_center(int y, const char *s, int colour);
void box_message(const char *l1, const char *l2, const char *l3, const char *l4);

/* ---- read-only peeks, for the host test's autopilots -------------------- */
typedef struct {
    int x, y, w, h;       /* the thing the player steers, in pixels          */
    int tx, ty;           /* where it should go: food, ball, gap centre       */
    int vx, vy;           /* speed, 1/256 px per tick (ball, bird)            */
    int gap;              /* FLAP: the gap's height                           */
    int count;            /* snake length, bricks left, pipes passed          */
    int lives;
} peek_t;
void snake_peek(peek_t *p);
int  snake_cell_free(int cx, int cy);     /* inside the walls and not snake */
void breakout_peek(peek_t *p);
void flap_peek(peek_t *p);

/* DOOM: x, y = the player in Q8 cells, vx, vy = the way it faces (Q8 unit
 * vector), w = waves cleared, h = monsters alive, count = kills, lives =
 * health, gap = ammunition.  The rest are for an autopilot that has to find
 * its way: the map's cells, the things in it, and a line-of-sight test. */
void doom_peek(peek_t *p);
int  doom_cell(int cx, int cy);                   /* the map char there        */
int  doom_thing(int i, int *x, int *y);           /* -1 no more, 0 gone, 1+type: 1 zombie 2 imp 3 ammo 4 health */
int  doom_los(int32_t x0, int32_t y0, int32_t x1, int32_t y1);

/* PRINCE: x, y = the prince's centre and feet in world pixels, vx, vy = his
 * speed (Q4), w = the awake guard's distance in px (999 if none), h = the
 * guard's health, count = the prince's room (0..5), lives = his health, tx =
 * which way he faces (+1 right).  gap = his state:
 *     0 standing  1 running  2 jumping  3 falling  4 hanging  5 climbing
 *     6 en garde  7 striking  8 parrying  9 dead
 * and ty = the guard's state: 0 asleep  1 fighting  2 striking  3 blocking
 * 4 dead.  prince_tile() is the world's char at a tile (col, row); a tile
 * is '.' air, '#' block, anything else has a floor slab.  prince_fall_cost()
 * is the rule for landing: hearts lost for a drop of `rows` rows, 99 = dead. */
void prince_peek(peek_t *p);
int  prince_tile(int tx, int ty);
int  prince_fall_cost(int rows);

#endif
