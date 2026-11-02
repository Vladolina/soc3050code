/*
 * mirror.c - shared library, LIBS=mirror oled proto: the OLED, copied to the
 *            station on the PC in 32-byte segments.  See mirror.h.
 */
#include <stdio.h>
#include <string.h>
#include "oled.h"
#include "proto.h"
#include "mirror.h"

#define SEG_W      32u                          /* columns per segment          */
#define SEG_PER_PG (OLED_W / SEG_W)             /* 4                            */
#define SEGS       (OLED_PAGES * SEG_PER_PG)    /* 32: one bit each in sent_mask */

static uint16_t sent_hash[SEGS];   /* what the station was last sent, hashed    */
static uint32_t sent_mask;         /* bit s: segment s has been sent since live */
static uint8_t  live, next_seg, refresh_seg;
static uint32_t live_until, next_refresh, sent;

/* FNV-1a, folded to 16 bits: a few cycles a byte, and any one changed pixel
 * changes it. */
static uint16_t hash(const uint8_t *b)
{
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < SEG_W; i++) { h = (h ^ b[i]) * 16777619u; }
    return (uint16_t)(h ^ (h >> 16));
}

static const uint8_t *seg_bytes(uint32_t s)
{
    return oled_page((int)(s / SEG_PER_PG)) + (s % SEG_PER_PG) * SEG_W;
}

int mirror_frame(const char *line, uint32_t now)
{
    const char *body;
    size_t len;
    if (strncmp(line, "$MIRROR,", 8) != 0) { return 0; }
    if (proto_check(line, &body, &len) != PROTO_OK || len != 8u) { return 0; }
    if (body[7] == '1') {
        if (!mirror_live(now)) {                       /* a new viewer: send it all, */
            sent_mask = 0;                             /* then refresh from scratch  */
            next_refresh = now + MIRROR_REFRESH_MS;
        }
        live = 1;
        live_until = now + MIRROR_LIVE_MS;
    } else {
        live = 0;
    }
    return 1;
}

int mirror_live(uint32_t now)
{
    if (live && (int32_t)(live_until - now) <= 0) { live = 0; }
    return live;
}

static size_t build(char *buf, size_t size, uint32_t s)
{
    static const char hex[] = "0123456789ABCDEF";
    const uint8_t *b = seg_bytes(s);
    char body[80];
    int n = snprintf(body, sizeof body, "FB,%u,%u,", (unsigned)(s / SEG_PER_PG),
                     (unsigned)((s % SEG_PER_PG) * SEG_W));
    for (uint32_t i = 0; i < SEG_W; i++) {
        body[n++] = hex[b[i] >> 4];
        body[n++] = hex[b[i] & 15u];
    }
    body[n] = 0;
    int m = snprintf(buf, size, "$%s*%02X\n", body, (unsigned)proto_checksum(body, (size_t)n));
    sent_hash[s] = hash(b);
    sent_mask |= 1u << s;
    sent++;
    return (m > 0 && (size_t)m < size) ? (size_t)m : 0u;
}

size_t mirror_next(char *buf, size_t size, uint32_t now)
{
    if (!mirror_live(now) || size < MIRROR_FRAME_MAX) { return 0; }

    /* A changed segment first, searching on from the last one sent so a
     * busy corner of the screen cannot starve the rest. */
    for (uint32_t i = 0; i < SEGS; i++) {
        uint32_t s = (next_seg + i) % SEGS;
        if (!(sent_mask & (1u << s)) || hash(seg_bytes(s)) != sent_hash[s]) {
            next_seg = (uint8_t)((s + 1u) % SEGS);
            return build(buf, size, s);
        }
    }
    /* Nothing changed: the slow refresh that bounds a missed (same-hash) change. */
    if ((int32_t)(now - next_refresh) >= 0) {
        next_refresh = now + MIRROR_REFRESH_MS;
        uint32_t s = refresh_seg;
        refresh_seg = (uint8_t)((refresh_seg + 1u) % SEGS);
        return build(buf, size, s);
    }
    return 0;
}

uint32_t mirror_sent(void) { return sent; }
