#!/usr/bin/env python3
"""
sprite.py - draw a sprite in ASCII, get the bytes oled_sprite() wants.  lesson 12

The SSD1306 - and so oled_sprite() - stores a picture as COLUMNS: one byte
per column per 8 rows, bit 0 at the top.  Nobody draws like that, so draw
rows of '#' and '.' in a text file and let this script turn it round:

    python sprite.py ship.txt

ship.txt:
    ..##....
    .####...
    ########
    .####...
    ..##....

prints the C array (width 8, height 5) ready to paste.  Blank lines separate
animation frames.

    python sprite.py --rows doom_art.txt

is the second format, for DOOM's scaled sprites (doom.c): 16 x 16, stored as
16 ROW words per plane, bit 15 = the leftmost pixel, in two planes -

    MASK   which pixels the sprite owns (it hides the wall behind them)
    INK    which of those are lit

so a pixel can be lit ('#'), black but opaque ('-'), or see-through ('.').
A frame header line  "name  halo"  adds a one-pixel black outline around the
body (every '.' touching a '#' or '-'), which is what keeps a white imp
visible against a lit wall.  Standard library only.
"""
import sys


def convert(rows):
    w = max(len(r) for r in rows)
    rows = [r.ljust(w, ".") for r in rows]
    h = len(rows)
    out = []
    for page in range((h + 7) // 8):            # sprites taller than 8 rows
        for c in range(w):                      # are stored page by page
            b = 0
            for bit in range(8):
                r = page * 8 + bit
                if r < h and rows[r][c] not in ". ":
                    b |= 1 << bit
            out.append(b)
    return w, h, out


def rows16(rows, halo):
    """16 rows of '#', '-', '.'  ->  (ink words, mask words)"""
    rows = [r.ljust(16, ".")[:16] for r in rows]
    rows += ["." * 16] * (16 - len(rows))
    body = [[ch in "#-" for ch in r] for r in rows]
    mask = [row[:] for row in body]
    if halo:
        for y in range(16):
            for x in range(16):
                if body[y][x]:
                    continue
                for dy in (-1, 0, 1):
                    for dx in (-1, 0, 1):
                        if 0 <= y + dy < 16 and 0 <= x + dx < 16 and body[y + dy][x + dx]:
                            mask[y][x] = True
    ink = [sum(1 << (15 - x) for x in range(16) if rows[y][x] == "#") for y in range(16)]
    msk = [sum(1 << (15 - x) for x in range(16) if mask[y][x]) for y in range(16)]
    return ink, msk


def main_rows(path):
    with open(path) as f:
        text = f.read()
    for blk in text.split("\n\n"):
        lines = [ln.rstrip() for ln in blk.splitlines() if ln.strip() and not ln.startswith("//")]
        if not lines:
            continue
        head = lines[0].split()
        name, halo = head[0], "halo" in head[1:]
        art = lines[1:]
        ink, msk = rows16(art, halo)
        print("/* %s%s */" % (name, "  (halo)" if halo else ""))
        for y in range(16):
            r = (art[y] if y < len(art) else "").ljust(16, ".")[:16]
            print("/*   %s */" % r)
        print("{ { " + ", ".join("0x%04X" % v for v in ink) + " },")
        print("  { " + ", ".join("0x%04X" % v for v in msk) + " } },")
    return 0


def main(path):
    with open(path) as f:
        text = f.read()
    frames = []
    for blk in text.split("\n\n"):
        rows = [ln.rstrip() for ln in blk.splitlines() if ln.strip() and not ln.startswith("//")]
        if not rows:
            continue
        name = ""
        if rows[0][0].isalpha():                  # a bare NAME line labels the frame
            name, rows = rows[0].split()[0], rows[1:]
        frames.append((name, rows))
    for i, (name, rows) in enumerate(frames):
        w, h, data = convert(rows)
        print("/* frame %d: %s%d x %d */" % (i, name + ", " if name else "", w, h))
        for r in rows:
            print("/*   %s */" % r)
        print("{ " + ", ".join("0x%02X" % b for b in data) + " },")
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--rows":
        sys.exit(main_rows(sys.argv[2]))
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    sys.exit(main(sys.argv[1]))
