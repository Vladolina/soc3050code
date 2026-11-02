/*
 * kbd.c - shared library, LIBS=kbd keys proto: the app board's pad from a
 *         keyboard - a terminal's bytes or the station's $KEY lines.  See kbd.h.
 *
 * Two layers.  lesson 08's keys.c turns bytes into keys - ESC [ A is Up -
 * and this file turns keys into a pad: arrows into a stick that is held for a
 * while, letters into button presses, digits into a knob position.
 */
#include <string.h>
#include "proto.h"
#include "kbd.h"

void kbd_init(kbd_t *k)
{
    memset(k, 0, sizeof *k);
    k->knob = -1;
}

/* An arrow goes down: hold its axis until `until`.  The opposite arrow takes
 * over at once. */
static void arrow_down(kbd_t *k, keycode_t key, uint32_t until)
{
    switch (key) {
    case KEY_UP:    k->dy =  1; k->y_until = until; break;
    case KEY_DOWN:  k->dy = -1; k->y_until = until; break;
    case KEY_RIGHT: k->dx =  1; k->x_until = until; break;
    case KEY_LEFT:  k->dx = -1; k->x_until = until; break;
    default: break;
    }
}

/* An arrow comes up (station only): let go of its axis - unless the opposite
 * arrow has taken it over meanwhile. */
static void arrow_up(kbd_t *k, keycode_t key)
{
    if ((key == KEY_UP && k->dy > 0) || (key == KEY_DOWN && k->dy < 0))   { k->dy = 0; }
    if ((key == KEY_RIGHT && k->dx > 0) || (key == KEY_LEFT && k->dx < 0)) { k->dx = 0; }
}

/* The button a character stands for, or 0. */
static uint8_t button(uint8_t c)
{
    switch (c) {
    case 'a':           return PAD_A;
    case 'b': case 'p': return PAD_B;
    case 's':           return PAD_SEL;
    default:            return 0;
    }
}

/* The commands a character stands for, or KBD_NONE.  Digits set the knob. */
static int command(kbd_t *k, uint8_t c)
{
    switch (c) {
    case '?':           return KBD_HELP;
    case '-':           return KBD_SLOWER;
    case '+': case '=': return KBD_FASTER;             /* = is + unshifted */
    default: break;
    }
    if (c >= '1' && c <= '5') {                      /* level n = the middle of its band */
        k->knob = (int16_t)((c - '1') * 200 + 100);
        k->knob_ref = -1;
    }
    return KBD_NONE;
}

static int is_command(uint8_t c)
{
    return c == '?' || c == '-' || c == '+' || c == '=' || (c >= '1' && c <= '5');
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
    /* Inside a $ line: collect it whole, then hand it to kbd_frame(). */
    if (k->llen) {
        if (c == '\r' || c == '\n') {
            k->line[k->llen] = 0;
            k->llen = 0;
            return kbd_is_key(k->line) ? kbd_frame(k, k->line, now) : KBD_FRAME;
        }
        if (k->llen < KBD_LINE_MAX) { k->line[k->llen++] = (char)c; }
        else                        { k->llen = 0; }      /* too long: not a $KEY */
        return KBD_NONE;
    }

    if (key_timeout(&k->dec, now) != KEY_NONE) { k->wlen = 0; }   /* Esc: forget the word */

    keycode_t key = key_feed(&k->dec, c, now);
    if (key == KEY_NONE) { return KBD_NONE; }        /* inside an escape sequence */
    if (key != KEY_CHAR) {                           /* an arrow, Esc or other    */
        arrow_down(k, key, now + KBD_HOLD_MS);
        k->wlen = 0;
        return KBD_NONE;
    }

    if (c == '$') { k->line[0] = '$'; k->llen = 1; k->wlen = 0; return KBD_NONE; }
    if (c >= 'A' && c <= 'Z') { c = (uint8_t)(c - 'A' + 'a'); }
    if (c == '\r' || c == '\n')  { return enter(k); }
    if (c == 0x08 || c == 0x7F)  { if (k->wlen) { k->wlen--; } return KBD_NONE; }   /* Backspace */
    uint8_t b = button(c);
    if (b) { k->pressed |= b; k->wlen = 0; return KBD_NONE; }
    if (is_command(c)) { k->wlen = 0; return command(k, c); }
    if (c >= 'a' && c <= 'z' && k->wlen < KBD_WORD_MAX) { k->word[k->wlen++] = (char)c; }
    return KBD_NONE;
}

int kbd_is_key(const char *line)
{
    return strncmp(line, "$KEY,", 5) == 0;
}

/* $KEY,<key>,<1|0>*CS - the station's real key-down and key-up. */
int kbd_frame(kbd_t *k, const char *line, uint32_t now)
{
    const char *body;
    size_t len;
    if (!kbd_is_key(line) || proto_check(line, &body, &len) != PROTO_OK) { return KBD_NONE; }

    const char *name = body + 4;                     /* past "KEY," */
    const char *comma = memchr(name, ',', len - 4u);
    if (comma == 0 || comma + 2 != body + len) { return KBD_NONE; }
    size_t nlen = (size_t)(comma - name);
    int down = comma[1] == '1';
    if (!down && comma[1] != '0') { return KBD_NONE; }

    keycode_t arrow = KEY_NONE;
    if      (nlen == 2 && strncmp(name, "UP", 2) == 0)    { arrow = KEY_UP; }
    else if (nlen == 4 && strncmp(name, "DOWN", 4) == 0)  { arrow = KEY_DOWN; }
    else if (nlen == 4 && strncmp(name, "LEFT", 4) == 0)  { arrow = KEY_LEFT; }
    else if (nlen == 5 && strncmp(name, "RIGHT", 5) == 0) { arrow = KEY_RIGHT; }
    if (arrow != KEY_NONE) {
        if (down) { arrow_down(k, arrow, now + KBD_LIVE_MS); }
        else      { arrow_up(k, arrow); }
        return KBD_NONE;
    }
    if (nlen != 1) { return KBD_NONE; }

    uint8_t c = (uint8_t)name[0];
    if (c >= 'A' && c <= 'Z') { c = (uint8_t)(c - 'A' + 'a'); }
    uint8_t b = button(c);
    if (b) {
        if (down) {
            if (!(k->held & b)) { k->pressed |= b; }  /* a repeat renews, never re-presses */
            k->held |= b;
            k->held_until = now + KBD_LIVE_MS;
        } else {
            k->held &= (uint8_t)~b;
        }
        return KBD_NONE;
    }
    return down ? command(k, c) : KBD_NONE;
}

void kbd_apply(kbd_t *k, pad_t *p, uint32_t now)
{
    /* The stick: an arrow overrides the real stick on its own axis only. */
    if (k->dx && (int32_t)(k->x_until - now) > 0) { p->x = (int16_t)(k->dx * 100); }
    else                                          { k->dx = 0; }
    if (k->dy && (int32_t)(k->y_until - now) > 0) { p->y = (int16_t)(k->dy * 100); }
    else                                          { k->dy = 0; }

    /* Buttons: a terminal key is a press that lasts one update; a station key
     * stays down until its key-up (or until it is not renewed). */
    if (k->held && (int32_t)(k->held_until - now) <= 0) { k->held = 0; }
    uint8_t was = p->down;
    p->down    |= (uint8_t)(k->pressed | k->held);
    p->pressed |= (uint8_t)(k->pressed & ~was);
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
const char *kbd_line(const kbd_t *k) { return k->line; }
