"""
l18_arena.py - lesson 18, the sumo tournament.  The dohyo drawn large from
$ARENA, and the standings kept from every $MATCH and $RESULT.

    $ARENA,<round>,<phase>,<t_ms>,<won0>,<won1>,<x0>,<y0>,<th0>,<x1>,<y1>,<th1>,<name0>,<name1>
                     mm and milliradians; sent every 80 ms during a match, only
                     while the station is connected (mirror_live() on the board)
    $MATCH,<robot0>,<robot1>,<seed>,<winner 0/1/-1>,<rounds0>,<rounds1>,<hash>
    $RESULT,<name>,<W>,<D>,<L>,<hash>,<max cycles>,<build>

Robot 0 is drawn OPEN and robot 1 FILLED, as scene.c draws them on the OLED.
"""

import time

from .base import BG, COLOURS, DIM, FG, FONT, FONT_BIG, GRID, View, num, rot

RING_MM = 385          # world.h RING_R
LINE_MM = 25           # world.h LINE_W, the white border inside the ring
ROBOT_MM = 55          # world.h ROBOT_R
PHASES = ["countdown", "FIGHT", "round over", "match over"]   # referee.h PHASE_*
STALE_S = 1.0          # no $ARENA for this long: the match is not on the screen


class Arena(View):
    name = "arena"
    title = "Lesson 18 - sumo tournament"
    frames = ("ARENA", "MATCH", "RESULT")
    help = "a = start/abort, b = next opponent, s = driver/cones, 1-5 = speed x1..MAX, arrows = joystick driver"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.arena = None           # the last $ARENA, parsed
        self.arena_t = 0.0
        self.table = {}             # name -> [W, D, L], from $MATCH
        self.matches = []           # recent $MATCH, newest last
        self.results = []           # recent $RESULT, newest last

    # ---- frames -------------------------------------------------------------
    def frame(self, name, f, t):
        if name == "ARENA" and len(f) >= 11:
            v = [num(x) for x in f[:11]]
            if any(not isinstance(x, int) for x in v):
                return
            self.arena = {
                "round": v[0], "phase": v[1], "t_ms": v[2], "won": (v[3], v[4]),
                "pose": ((v[5], v[6], v[7] / 1000.0), (v[8], v[9], v[10] / 1000.0)),
                "names": (f[11] if len(f) > 11 else "robot 0", f[12] if len(f) > 12 else "robot 1"),
            }
            self.arena_t = t
        elif name == "MATCH" and len(f) >= 6:
            winner = num(f[3])
            if winner not in (-1, 0, 1):
                return
            a, b = f[0], f[1]
            for i, who in enumerate((a, b)):
                row = self.table.setdefault(who, [0, 0, 0])
                row[0 if winner == i else 1 if winner < 0 else 2] += 1
            self.matches.append((a, b, winner, f[4], f[5]))
            self.matches = self.matches[-10:]
        elif name == "RESULT" and len(f) >= 5:
            if all(isinstance(num(x), int) for x in f[1:4]):
                self.results.append((f[0], f[1], f[2], f[3], f[4], f[5] if len(f) > 5 else "?"))
                self.results = self.results[-4:]

    # ---- drawing ------------------------------------------------------------
    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        side = 300 if w > 700 else 0          # the standings column, when there is room
        aw, ah = w - side, h
        self._arena(c, 0, 0, aw, ah)
        if side:
            self._standings(c, aw + 10, 10, side - 20, h - 20)

    def _arena(self, c, x0, y0, w, h):
        a = self.arena
        live = a is not None and time.monotonic() - self.arena_t < STALE_S
        top = 50                                   # room for the score line
        r = max(20, min(w, h - top) / 2 - 20)      # ring radius, px
        cx, cy = x0 + w / 2, y0 + top + (h - top) / 2
        k = r / RING_MM                            # px per mm

        def px(xm, ym):
            return cx + xm * k, cy - ym * k        # y up, as world.h

        c.create_oval(cx - r, cy - r, cx + r, cy + r, fill="#1a1f26", outline=FG, width=2)
        ri = (RING_MM - LINE_MM) * k               # inner edge of the white border
        c.create_oval(cx - ri, cy - ri, cx + ri, cy + ri, outline=DIM, dash=(3, 4))
        c.create_line(cx - 6, cy, cx + 6, cy, fill=GRID)
        c.create_line(cx, cy - 6, cx, cy + 6, fill=GRID)

        if a is None:
            c.create_text(cx, y0 + 20, text="waiting for a match ($ARENA comes only while one is on)",
                          fill=DIM, font=FONT_BIG)
            return
        names = a["names"]
        won = a["won"]
        phase = a["phase"]
        pname = PHASES[phase] if isinstance(phase, int) and 0 <= phase < len(PHASES) else f"phase {phase}"
        t = a["t_ms"]
        clock = f"{-t / 1000.0:.1f} s to go" if t < 0 else f"{t / 1000.0:.1f} s"
        c.create_text(x0 + w / 2, y0 + 8, anchor="n", font=FONT_BIG, fill=FG if live else DIM,
                      text=f"o {names[0]}  {won[0]} : {won[1]}  {names[1]} *")
        c.create_text(x0 + w / 2, y0 + 32, anchor="n", font=FONT, fill=FG if live else DIM,
                      text=f"round {a['round']}   {pname}   {clock}" + ("" if live else "   (stale)"))

        rr = ROBOT_MM * k
        for i, (xm, ym, th) in enumerate(a["pose"]):
            col = COLOURS[i] if live else DIM
            bx, by = px(xm, ym)
            if i == 0:
                c.create_oval(bx - rr, by - rr, bx + rr, by + rr, outline=col, width=3)
            else:
                c.create_oval(bx - rr, by - rr, bx + rr, by + rr, outline=col, fill=col)
            hx, hy = rot(rr * 1.5, 0, th)          # heading, out through the nose
            c.create_line(bx, by, bx + hx, by - hy, fill=FG if i else col, width=3, arrow="last")
            c.create_text(bx, by + rr + 4, anchor="n", text=names[i], fill=col, font=FONT)

    def _standings(self, c, x, y, w, h):
        c.create_text(x, y, anchor="nw", fill=FG, font=FONT_BIG, text="Standings")
        y += 30
        c.create_text(x, y, anchor="nw", fill=DIM, font=FONT, text=f"{'robot':<12}{'W':>3}{'D':>3}{'L':>3}{'pts':>5}")
        y += 18
        rows = sorted(self.table.items(), key=lambda kv: (-(3 * kv[1][0] + kv[1][1]), kv[0]))
        for name, (W, D, L) in rows[:12]:
            c.create_text(x, y, anchor="nw", fill=FG, font=FONT,
                          text=f"{name[:12]:<12}{W:>3}{D:>3}{L:>3}{3 * W + D:>5}")
            y += 16
        if not rows:
            c.create_text(x, y, anchor="nw", fill=DIM, font=FONT, text="no $MATCH yet")
            y += 16

        y += 14
        c.create_text(x, y, anchor="nw", fill=FG, font=FONT_BIG, text="Matches")
        y += 28
        for a, b, win, r0, r1 in reversed(self.matches):
            mark = "draw" if win < 0 else (a if win == 0 else b)
            c.create_text(x, y, anchor="nw", fill=DIM, font=FONT,
                          text=f"{a[:8]} {r0}-{r1} {b[:8]}  {mark[:8]}")
            y += 16
            if y > h - 100:
                break

        if self.results:
            y = max(y + 14, h - 80)
            c.create_text(x, y, anchor="nw", fill=FG, font=FONT_BIG, text="$RESULT")
            y += 26
            for name, W, D, L, hsh, cyc in reversed(self.results[-2:]):
                c.create_text(x, y, anchor="nw", fill=COLOURS[2], font=FONT,
                              text=f"{name[:10]} {W}-{D}-{L}  hash {hsh}  {cyc} cyc")
                y += 16
