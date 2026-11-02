/*
 * kbd.h - the app board's pad, from a keyboard   LIBS=kbd keys proto pad adc
 *
 * Written for lesson 12; shared by every lesson on the app board.  Every
 * control has a key, so a lesson can be run with the OLED zoomed to fill the
 * window - or with no OLED in sight at all, from the station on the PC:
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
 * The keys arrive two ways, and land in the same place:
 *
 *   kbd_feed()   raw bytes from a TERMINAL.  An arrow is ESC [ A (lesson 08).
 *   kbd_frame()  one line from the STATION, tools/station/station.py:
 *                  $KEY,<key>,<1|0>*CS     1 = pressed, 0 = released
 *                <key> is UP DOWN LEFT RIGHT, or the character itself: a, p, 1, -
 *                kbd_feed() also recognises such a line arriving byte by byte.
 *
 * kbd.c never prints and never touches the UART: the lesson hands it bytes or
 * lines, from wherever they come from, and acts on what is returned.
 *
 * A key does not reach the application as a key.  kbd_apply() writes it into
 * the same pad_t that pad_read() fills from the pins, so the code above -
 * lesson 12's games, for one - cannot tell the keyboard from the board.
 *
 * HELD KEYS.  A terminal sends a key when it is PRESSED, never when it is
 * released, so "held" has to be inferred: a terminal arrow holds the stick
 * for KBD_HOLD_MS, and the PC's auto-repeat keeps renewing it.  Before
 * auto-repeat starts (about half a second) the stick lets go once - a BREAKOUT
 * paddle stutters at first.  The station knows when a key goes UP, so its
 * keys are held exactly as long as the finger is: down until the $KEY,..,0.
 * In case that frame is lost - the window closed with a key down - the station
 * repeats a held key's down frame every 200 ms, and a station key that is not
 * renewed for KBD_LIVE_MS lets go by itself.
 *
 * Shared between tasks (lessons 13-19: the shell feeds, the control task
 * applies)?  Guard both calls with one mutex; kbd.c has no lock of its own.
 *
 * Pure C, no hardware: lesson 12's host/test_games.c runs it on a PC.
 */
#ifndef KBD_H
#define KBD_H

#include <stdint.h>
#include "keys.h"
#include "pad.h"

#define KBD_HOLD_MS  150u        /* a terminal arrow holds the stick this long */
#define KBD_LIVE_MS  600u        /* a station key not renewed this long: up    */
#define KBD_WORD_MAX 8u          /* longest typed word, quiet and loud fit     */
#define KBD_LINE_MAX 24u         /* longest $KEY line kbd_feed() collects      */

enum { KBD_NONE, KBD_QUIET, KBD_LOUD, KBD_HELP, KBD_SLOWER, KBD_FASTER, KBD_UNKNOWN,
       KBD_FRAME };    /* kbd_feed(): a whole $ line that is not $KEY - see kbd_line() */

typedef struct {
    keydec_t dec;                /* lesson 08's escape-sequence decoder        */
    int8_t   dx, dy;             /* the stick the arrows ask for, -1/0/+1      */
    uint32_t x_until, y_until;   /* ...until these times, ms                   */
    uint8_t  pressed;            /* PAD_* bits pressed since the last apply    */
    uint8_t  held;               /* PAD_* bits a station key holds down        */
    uint32_t held_until;         /* ...until this time, unless renewed         */
    int16_t  knob;               /* 0..1000 from a digit; -1 = use the knob    */
    int16_t  knob_ref;           /* the real knob when the digit was typed     */
    char     word[KBD_WORD_MAX + 1];
    uint8_t  wlen;
    char     line[KBD_LINE_MAX + 1];   /* a $ line arriving through kbd_feed() */
    uint8_t  llen;               /* 0 = not inside a $ line                    */
} kbd_t;

void kbd_init(kbd_t *k);

/* One received byte from a terminal.  Returns KBD_QUIET, KBD_LOUD, KBD_HELP,
 * KBD_SLOWER, KBD_FASTER or KBD_UNKNOWN when a command is complete, KBD_NONE
 * otherwise.  The caller acts on it and prints. */
int  kbd_feed(kbd_t *k, uint8_t c, uint32_t now_ms);

/* One whole line, "$KEY,UP,1*CS" (no newline).  Returns as kbd_feed() does;
 * KBD_NONE also for a line that is not a $KEY or fails its checksum - the
 * caller can then treat it as one of its own frames. */
int  kbd_frame(kbd_t *k, const char *line, uint32_t now_ms);

/* 1 if this line is a $KEY frame (checksum not checked): lets a shell route
 * it to kbd_frame() before its own parser sees it. */
int  kbd_is_key(const char *line);

/* After pad_read(): fold the keyboard into the pad, once per update. */
void kbd_apply(kbd_t *k, pad_t *p, uint32_t now_ms);

/* The typed word KBD_UNKNOWN refers to. */
const char *kbd_word(const kbd_t *k);

/* The $ line KBD_FRAME refers to, e.g. the station's $MIRROR,1*CS. */
const char *kbd_line(const kbd_t *k);

#endif
