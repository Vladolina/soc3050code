/*
 * keys.c - shared copy of lesson 08's terminal key decoder.   LIBS=keys
 *
 * The sequences come in two shapes (ECMA-48, the standard behind every
 * terminal since the VT100):
 *
 *   CSI  ESC [  then any parameter bytes 0x20-0x3F  then ONE final byte 0x40-0x7E
 *        ESC [ A  (Up)      ESC [ 3 ~  (Delete)      ESC [ 1 ; 5 C  (Ctrl-Right)
 *   SS3  ESC O  then exactly one byte
 *        ESC O A  (Up, "application" mode)            ESC O P  (F1)
 *
 * The final byte is what ends a sequence - the same job the '*' does in a
 * $...*CS frame.  Without it, the decoder could not know when to stop.
 */

#include "keys.h"

static keycode_t name_final(uint8_t final)
{
    switch (final) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    default:  return KEY_OTHER;
    }
}

keycode_t key_feed(keydec_t *d, uint8_t c, uint32_t now_ms)
{
    if (d->done) { d->len = 0; d->done = 0; }        /* last key reported: forget it */

    if (d->len == 0) {                               /* not inside a sequence     */
        if (c != KEY_ESC_BYTE) { return KEY_CHAR; }
        d->seq[0] = c;
        d->len = 1;
        d->t0 = now_ms;
        return KEY_NONE;
    }

    d->seq[d->len++] = c;

    if (d->len == 2) {
        if (c == '[' || c == 'O') { return KEY_NONE; }  /* CSI or SS3: more coming  */
        d->done = 1;                                    /* ESC x - Alt-x, unnamed   */
        return KEY_OTHER;
    }

    if (d->seq[1] == 'O' || (c >= 0x40u && c <= 0x7Eu)) {   /* the final byte */
        d->done = 1;
        return d->len == 3 ? name_final(c) : KEY_OTHER;  /* ESC [ 1 ; 5 C has extras */
    }

    if (d->len == KEY_SEQ_MAX) {                     /* no final byte in sight */
        d->done = 1;
        return KEY_OTHER;
    }
    return KEY_NONE;
}

keycode_t key_timeout(keydec_t *d, uint32_t now_ms)
{
    if (d->done || d->len == 0)              { return KEY_NONE; }
    if (now_ms - d->t0 <= KEY_ESC_WAIT)      { return KEY_NONE; }   /* wraps safely */
    d->done = 1;
    return d->len == 1 ? KEY_ESC : KEY_OTHER;
}

void key_spell(const keydec_t *d, char *buf, size_t size)
{
    size_t n = 0;
    if (size == 0) { return; }
    for (uint8_t i = 0; i < d->len; i++) {
        const char *s;
        char one[2] = { (char)d->seq[i], 0 };
        if (d->seq[i] == KEY_ESC_BYTE)                       { s = "ESC"; }
        else if (d->seq[i] >= 0x20u && d->seq[i] < 0x7Fu)  { s = one;   }
        else                                                 { s = "?";   }
        if (i > 0 && n + 1 < size) { buf[n++] = ' '; }
        while (*s && n + 1 < size) { buf[n++] = *s++; }
    }
    buf[n] = '\0';
}

const char *key_name(keycode_t k)
{
    switch (k) {
    case KEY_UP:    return "Up";
    case KEY_DOWN:  return "Down";
    case KEY_RIGHT: return "Right";
    case KEY_LEFT:  return "Left";
    case KEY_ESC:   return "Esc";
    default:        return "?";
    }
}
