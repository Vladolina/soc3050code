/*
 * mirror.h - the OLED, copied to the station on the PC   LIBS=mirror oled proto
 *
 * The board's 128 x 64 panel is tiny in a simulator window.  The station,
 * tools/station/station.py, draws it as big as you like - but only if the
 * picture reaches the PC.  This module sends it, a piece at a time:
 *
 *   $FB,<page>,<col>,<64 hex digits>*CS     32 columns of one page = 32 bytes
 *
 * The panel is 8 pages x 4 such segments.  A segment is sent only when it has
 * changed since it was last sent, so a game that redraws one small area pays
 * for that area, not the screen - lesson 12's dirty pages, one level finer.
 *
 * WHAT CHANGED, IN 64 BYTES.  Knowing that needs a copy of what was sent -
 * 1 KB, which lesson 16 does not have.  So each segment keeps a 16-bit hash
 * instead (32 x 2 = 64 B).  Two different pictures can share a hash; then a
 * change is missed until the segment changes again.  To bound that, one
 * segment is re-sent every MIRROR_REFRESH_MS whether it changed or not: the
 * whole screen is refreshed every 32 x that, about 3 s.
 *
 * ONLY WHEN ASKED.  A terminal does not want 64 hex digits per line.  The
 * station sends $MIRROR,1*CS every second; the mirror runs only while those
 * keep coming, and stops MIRROR_LIVE_MS after the last.
 *
 * NEVER BLOCKS.  mirror_next() only builds a frame; the caller sends it, and
 * calls only when the UART has MIRROR_FRAME_MAX bytes free.  When the link is
 * busy the mirror simply falls behind - it is a view, not the system.
 */
#ifndef MIRROR_H
#define MIRROR_H

#include <stddef.h>
#include <stdint.h>

#define MIRROR_FRAME_MAX   96u      /* "$FB,7,96," + 64 hex + "*CS\r\n", rounded up */
#define MIRROR_LIVE_MS   3000u      /* no $MIRROR,1 for this long: stop             */
#define MIRROR_REFRESH_MS 100u      /* one unchanged segment re-sent this often     */

/* A line from the station.  1 if it was $MIRROR,<0|1>*CS (and acted on). */
int    mirror_frame(const char *line, uint32_t now_ms);

/* 1 while the station keeps asking. */
int    mirror_live(uint32_t now_ms);

/* After oled_flush(), and only with MIRROR_FRAME_MAX bytes of UART room:
 * builds the next $FB frame, newline included, into buf.  Returns its length,
 * or 0 if nothing needs sending (or the mirror is not live). */
size_t mirror_next(char *buf, size_t size, uint32_t now_ms);

/* Frames sent so far - the station shows its own count beside it. */
uint32_t mirror_sent(void);

#endif
