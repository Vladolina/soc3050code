/*
 * arcade.c - menu, pause, game over, high scores.   PURE C.
 *
 * Everything here is driven by arcade_step() at the game tick and drawn by
 * arcade_draw(); like the games, it never reads a clock.  Its tick counter
 * is also where randomness comes from: the RNG is seeded with the tick on
 * which the player pressed A.  A human cannot press a button on the same
 * 20 ms tick twice, so every game is different - and a test that presses A
 * on a scripted tick gets the same game every time.
 */
#include <stdio.h>
#include <string.h>
#include "engine.h"
#include "arcade.h"
#include "proto.h"

static const game_t *const games[] = { &game_snake, &game_breakout, &game_flap, &game_doom, &game_prince };
#define NGAMES ((int)(sizeof games / sizeof games[0]))

#define OVER_HOLD  (TICK_HZ * 3 / 4)    /* ignore buttons for 0.75 s after death */

static int      state, sel, level = 3, redraw, need_full, stick_zone, new_high, blink_on;
static uint32_t ticks, state_ticks;
static int32_t  high[NGAMES], last;

int     arcade_state(void)     { return state; }
int     arcade_game(void)      { return sel; }
int32_t arcade_high(int g)     { return (g >= 0 && g < NGAMES) ? high[g] : 0; }

static void enter(int s) { state = s;  state_ticks = 0;  redraw = 1; }

void arcade_init(void)
{
    sel = 0;  ticks = 0;  stick_zone = 0;
    enter(ARC_MENU);
}

/* The knob, 0..1000, as a level 1..5. */
static int level_from_knob(int knob)
{
    int l = 1 + knob / 200;
    return l > 5 ? 5 : l;
}

/* +1 when the stick is pushed up, -1 down - once per push, not per tick. */
static int stick_edge(int y)
{
    int zone = y > 50 ? 1 : y < -50 ? -1 : 0;
    int edge = (zone != 0 && stick_zone == 0) ? zone : 0;
    stick_zone = zone;
    return edge;
}

static void start_game(void)
{
    rng_seed(ticks * 2654435761u);     /* Knuth's multiplier spreads small counts */
    games[sel]->start(level);
    need_full = 1;
    sfx(SFX_START);
    enter(ARC_PLAY);
}

static void game_over(void)
{
    last = games[sel]->score();
    new_high = last > high[sel];
    if (new_high) { high[sel] = last; }
    sfx(new_high ? SFX_HIGH : SFX_OVER);
    on_score(games[sel]->name, last);
    enter(ARC_OVER);
}

void arcade_step(const pad_t *in)
{
    ticks++;  state_ticks++;
    int dy = stick_edge(in->y);

    switch (state) {
    case ARC_MENU: {
        int l = level_from_knob(in->knob);
        if (l != level) { level = l;  redraw = 1; }
        if (dy) { sel = (sel - dy + NGAMES) % NGAMES;  redraw = 1;  sfx(SFX_MOVE); }
        if (in->pressed & PAD_A) { start_game(); }
        break;
    }
    case ARC_PLAY:
        if (in->pressed & PAD_B) { enter(ARC_PAUSE); sfx(SFX_SELECT); break; }
        if (games[sel]->step(in)) { game_over(); }
        break;
    case ARC_PAUSE:
        if (in->pressed & PAD_B) { need_full = 1; enter(ARC_PLAY); sfx(SFX_SELECT); }
        break;
    case ARC_OVER:
        if (state_ticks < OVER_HOLD) { break; }
        if (in->pressed & PAD_A) { start_game(); }
        else if (in->pressed & PAD_B) { enter(ARC_MENU); sfx(SFX_SELECT); }
        break;
    }
}

static void draw_menu(void)
{
    oled_clear();
    text_center(0, "SOC3050 ARCADE", OLED_ON);
    oled_hline(0, 8, OLED_W, OLED_ON);
    for (int g = 0; g < NGAMES; g++) {                 /* 5 rows of 9: 11, 20, 29, 38, 47 */
        int y = 11 + g * 9;
        char hi[16];
        snprintf(hi, sizeof hi, "HI %ld", (long)high[g]);
        if (g == sel) { oled_fill_rect(2, y - 1, OLED_W - 4, 9, OLED_ON); }
        int c = (g == sel) ? OLED_XOR : OLED_ON;       /* inverted when chosen */
        oled_text(8, y, games[g]->name, c);
        oled_text(OLED_W - 8 - 6 * (int)strlen(hi), y, hi, c);
    }
    oled_text(2, 56, "SPEED", OLED_ON);
    for (int i = 0; i < 5; i++) {                     /* the knob, as 5 boxes */
        if (i < level) { oled_fill_rect(36 + i * 8, 56, 6, 7, OLED_ON); }
        else           { oled_rect(36 + i * 8, 56, 6, 7, OLED_ON); }
    }
    oled_text(OLED_W - 2 - 6 * 6 + 1, 56, "A:PLAY", OLED_ON);   /* stick: pick; A: play */
}

int score_frame(char *out, size_t n, const char *game, int32_t score)
{
    char body[40];
    int len = snprintf(body, sizeof body, "SCORE,%s,%ld", game, (long)score);
    if (len < 0) { return 0; }
    if ((size_t)len >= sizeof body) { len = (int)sizeof body - 1; }
    return snprintf(out, n, "$%s*%02X", body, (unsigned)proto_checksum(body, (size_t)len));
}

void arcade_draw(void)
{
    char buf[24];
    switch (state) {
    case ARC_MENU:
        if (redraw) { draw_menu(); redraw = 0; }
        break;
    case ARC_PLAY:
        games[sel]->draw(need_full);
        need_full = 0;
        break;
    case ARC_PAUSE:
        if (redraw) { box_message("PAUSED", 0, 0, "B: RESUME"); redraw = 0; }
        break;
    case ARC_OVER:
        if (redraw) {
            games[sel]->draw(0);                      /* the final position  */
            snprintf(buf, sizeof buf, "SCORE %ld", (long)last);
            box_message("GAME OVER", buf, 0, "A:AGAIN B:MENU");
            redraw = 0;  blink_on = 0;
        }
        /* NEW HIGH! blinks twice a second on the box's empty third row, by
         * XOR: drawing it twice puts the pixels back, so nothing is stored. */
        if (new_high && ((state_ticks / (TICK_HZ / 4)) & 1) != (uint32_t)blink_on) {
            blink_on ^= 1;
            text_center(38, "NEW HIGH!", OLED_XOR);
        }
        break;
    }
}
