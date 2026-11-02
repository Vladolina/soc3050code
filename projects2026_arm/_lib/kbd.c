/*
 * kbd.c - shared library, LIBS=kbd keys: the app board's pad from the
 *         serial terminal.  See kbd.h.
 *
 * Two layers.  lesson 08's keys.c turns bytes into keys - ESC [ A is Up -
 * and this file turns keys into a pad: arrows into a stick that is held for a
 * while, letters into button presses, digits into a knob position.
 */
#include <string.h>
#include "kbd.h"

void kbd_init(kbd_t *k)
{
    memset(k, 0, sizeof *k);
    k->knob = -1;
}

static void arrow(kbd_t *k, keycode_t key, uint32_t now)
{
    switch (key) {                          /* the opposite arrow takes over at once */
    case KEY_UP:    k->dy =  1; k->y_until = now + KBD_HOLD_MS; break;
    case KEY_DOWN:  k->dy = -1; k->y_until = now + KBD_HOLD_MS; break;
    case KEY_RIGHT: k->dx =  1; k->x_until = now + KBD_HOLD_MS; break;
    case KEY_LEFT:  k->dx = -1; k->x_until = now + KBD_HOLD_MS; break;
    default: break;
    }
}

/* Letters that are not keys collect into a word, for Enter to look up.  None
 * of the key letters (a b p s) occurs in "quiet" or "loud", so typing either
 * word presses nothing on the way. */
static int enter(kbd_t *k)
{
    if (k->wlen == 0) { return KBD_NONE; }          /* a bare Enter, or \n of \r\n */
    k->word[k->wlen] = 0;
    k->wlen = 0;
    if (strcmp(k->word, "quiet") == 0) { return KBD_QUIET; }
    if (strcmp(k->word, "loud") == 0)  { return KBD_LOUD; }
    return KBD_UNKNOWN;
}

int kbd_feed(kbd_t *k, uint8_t c, uint32_t now)
{
    if (key_timeout(&k->dec, now) != KEY_NONE) { k->wlen = 0; }   /* Esc: forget the word */

    keycode_t key = key_feed(&k->dec, c, now);
    if (key == KEY_NONE) { return KBD_NONE; }        /* inside an escape sequence */
    if (key != KEY_CHAR) {                           /* an arrow, Esc or other    */
        arrow(k, key, now);
        k->wlen = 0;
        return KBD_NONE;
    }

    if (c >= 'A' && c <= 'Z') { c = (uint8_t)(c - 'A' + 'a'); }
    switch (c) {
    case '\r': case '\n':  return enter(k);
    case 0x08: case 0x7F:  if (k->wlen) { k->wlen--; } return KBD_NONE;   /* Backspace */
    case '?':              k->wlen = 0; return KBD_HELP;
    case '-':              k->wlen = 0; return KBD_SLOWER;
    case '+': case '=':    k->wlen = 0; return KBD_FASTER;     /* = is + unshifted */
    case 'a':              k->pressed |= PAD_A;   k->wlen = 0; return KBD_NONE;
    case 'b': case 'p':    k->pressed |= PAD_B;   k->wlen = 0; return KBD_NONE;
    case 's':              k->pressed |= PAD_SEL; k->wlen = 0; return KBD_NONE;
    default: break;
    }
    if (c >= '1' && c <= '5') {                      /* level n = the middle of its band */
        k->knob = (int16_t)((c - '1') * 200 + 100);
        k->knob_ref = -1;
        k->wlen = 0;
        return KBD_NONE;
    }
    if (c >= 'a' && c <= 'z' && k->wlen < KBD_WORD_MAX) { k->word[k->wlen++] = (char)c; }
    return KBD_NONE;
}

void kbd_apply(kbd_t *k, pad_t *p, uint32_t now)
{
    /* The stick: an arrow overrides the real stick on its own axis only. */
    if (k->dx && (int32_t)(k->x_until - now) > 0) { p->x = (int16_t)(k->dx * 100); }
    else                                          { k->dx = 0; }
    if (k->dy && (int32_t)(k->y_until - now) > 0) { p->y = (int16_t)(k->dy * 100); }
    else                                          { k->dy = 0; }

    /* Buttons: a typed key is a press that lasts one update. */
    p->pressed |= k->pressed;
    p->down    |= k->pressed;
    k->pressed  = 0;

    /* The knob: a digit sets it until someone turns the real one. */
    if (k->knob >= 0) {
        if (k->knob_ref < 0) { k->knob_ref = p->knob; }
        int moved = p->knob - k->knob_ref;
        if (moved > 100 || moved < -100) { k->knob = -1; }
        else                             { p->knob = k->knob; }
    }
}

const char *kbd_word(const kbd_t *k) { return k->word; }
