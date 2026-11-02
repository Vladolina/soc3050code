/*
 * kbd.h - the app board's pad, from the serial terminal   LIBS=kbd keys pad adc
 *
 * Written for lesson 12; shared by every lesson on the app board.  Every
 * control has a key, so a lesson can be run with the OLED zoomed to fill the
 * window and the rest of the board out of sight:
 *
 *   arrows    the stick                     a        button A
 *   1 .. 5    the knob, in fifths           b or p   button B
 *   s         stick press (SEL)             ?        KBD_HELP: the lesson prints its list
 *   -  +      KBD_SLOWER / KBD_FASTER: the lesson decides what they mean
 *             (lesson 12: the game's pace)
 *
 * and two words, typed and ended with Enter:
 *
 *   quiet     KBD_QUIET: stop periodic output   loud   KBD_LOUD: start it again
 *
 * kbd.c never prints and never touches the UART: the lesson hands it bytes,
 * from wherever its bytes come from, and acts on what kbd_feed() returns.
 *
 * A key does not reach the application as a key.  kbd_apply() writes it into
 * the same pad_t that pad_read() fills from the pins, so the code above -
 * lesson 12's games, for one - cannot tell the keyboard from the board.
 *
 * The one real difference is the stick.  A terminal sends an arrow when it is
 * PRESSED, never when it is released - so "held" has to be inferred: an arrow
 * holds the stick for KBD_HOLD_MS, and the terminal's auto-repeat keeps
 * renewing it while the key stays down.  Before auto-repeat starts (about half
 * a second, set by the PC) the stick lets go once, so a BREAKOUT paddle
 * stutters at first.  A short tap is a short push: it steers SNAKE, flaps in
 * FLAP and moves a menu one line.
 *
 * Pure C, no hardware: lesson 12's host/test_games.c runs it on a PC.
 */
#ifndef KBD_H
#define KBD_H

#include <stdint.h>
#include "keys.h"
#include "pad.h"

#define KBD_HOLD_MS  150u        /* an arrow holds the stick this long       */
#define KBD_WORD_MAX 8u          /* longest typed word, quiet and loud fit   */

enum { KBD_NONE, KBD_QUIET, KBD_LOUD, KBD_HELP, KBD_SLOWER, KBD_FASTER, KBD_UNKNOWN };

typedef struct {
    keydec_t dec;                /* lesson 08's escape-sequence decoder      */
    int8_t   dx, dy;             /* the stick the arrows ask for, -1/0/+1    */
    uint32_t x_until, y_until;   /* ...until these times, ms                 */
    uint8_t  pressed;            /* PAD_* bits typed since the last apply    */
    int16_t  knob;               /* 0..1000 from a digit; -1 = use the knob  */
    int16_t  knob_ref;           /* the real knob when the digit was typed   */
    char     word[KBD_WORD_MAX + 1];
    uint8_t  wlen;
} kbd_t;

void kbd_init(kbd_t *k);

/* One received byte.  Returns KBD_QUIET, KBD_LOUD, KBD_HELP, KBD_SLOWER,
 * KBD_FASTER or KBD_UNKNOWN when a command is complete, KBD_NONE otherwise.
 * The caller acts on it and prints. */
int  kbd_feed(kbd_t *k, uint8_t c, uint32_t now_ms);

/* After pad_read(): fold the keyboard into the pad, once per update. */
void kbd_apply(kbd_t *k, pad_t *p, uint32_t now_ms);

/* The typed word KBD_UNKNOWN refers to. */
const char *kbd_word(const kbd_t *k);

#endif
