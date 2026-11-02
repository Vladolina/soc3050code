/*
 * keys.h - shared copy of lesson 08's terminal key decoder.   LIBS=keys
 *
 * A terminal sends a letter as one byte, but an arrow key as THREE:
 *
 *   Up  ESC [ A     Down  ESC [ B     Right  ESC [ C     Left  ESC [ D
 *
 * (ESC is 0x1B.  Some terminals send ESC O A instead; both are accepted.)
 * So a key is a tiny protocol, framed like lesson 08's $...*CS lines: a start
 * byte, a body, and a final byte that says "done".  This decoder is the
 * state machine that finds those frames in the byte stream.
 *
 * The Esc key alone is the hard case: it is the same byte that STARTS every
 * sequence.  The only way to tell them apart is time - a real sequence arrives
 * all at once, a person pressing Esc is followed by silence.  key_timeout()
 * makes that call.
 *
 * Pure C, no hardware: the same file would compile on a PC - and does, to
 * test it.
 */
#ifndef KEYS_H
#define KEYS_H

#include <stddef.h>
#include <stdint.h>

#define KEY_ESC_BYTE   0x1Bu
#define KEY_SEQ_MAX    8u          /* longer than this is not a key we know   */
#define KEY_ESC_WAIT   50u         /* ms of silence that make ESC the Esc key */

typedef enum {
    KEY_NONE,       /* byte swallowed: a sequence is still arriving         */
    KEY_CHAR,       /* an ordinary byte - the caller uses it as it is       */
    KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT,
    KEY_ESC,        /* the Esc key on its own                                */
    KEY_OTHER       /* a complete sequence this decoder has no name for     */
} keycode_t;

typedef struct {
    uint8_t  seq[KEY_SEQ_MAX];     /* the sequence so far, ESC first        */
    uint8_t  len;
    uint8_t  done;                 /* seq[] holds a finished sequence       */
    uint32_t t0;                   /* when its ESC arrived, ms              */
} keydec_t;

/* Feed one received byte, with the time it arrived. */
keycode_t key_feed(keydec_t *d, uint8_t c, uint32_t now_ms);

/* Call before key_feed().  If a sequence was left unfinished for more than
 * KEY_ESC_WAIT ms, it is given up on here: KEY_ESC for a lone ESC, KEY_OTHER
 * for anything longer.  Otherwise KEY_NONE. */
keycode_t key_timeout(keydec_t *d, uint32_t now_ms);

/* The last sequence, spelt out for a person: "ESC [ 1 ~". */
void key_spell(const keydec_t *d, char *buf, size_t size);

const char *key_name(keycode_t k);

#endif
