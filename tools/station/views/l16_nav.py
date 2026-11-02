"""
l16_nav.py - lesson 16, robot navigation.  The robot's own map, large: what
it has seen as free and as wall, the path A* planned on it, where the robot
thinks it is beside where it really is, and the counters.

    $NAV,<t_ms>,<level>,<mode>,<est x mm>,<est y mm>,<est th mdeg>,
         <true x mm>,<true y mm>,<true th mdeg>,<collisions>,<replans>,
         <expanded>,<plan us>                                    4 per second

and, after the word "dump" (this view sends it), one burst:

    $RPATH,<cost>,<expanded>,<len>       cost 65535 = no path
    $RSTEP,<k>,<xxy xxy ...>             16 cells a frame: 2 hex digits x, 1 hex y
    $RMAP,<y>,<occ>,<free>               one row, 32 bits each, bit (31 - x) = cell x
    $RVETO,<x>,<y>                       a cell the safety layer has vetoed
    $RGOAL,<start x>,<start y>,<goal x>,<goal y>,<inflate>    last: complete

Units are the firmware's (sim.h): cells of CELL_MM = 100 mm, 32 x 14 of them,
cell (0, 0) at the BOTTOM-left with y up; poses in mm from that corner,
headings in milli-degrees counter-clockwise from +x.

"dump" re-plans from where the robot is, and holds the robot's lock while it
sends.  So this view asks for one only when the robot is not driving: when the
view starts, when the level changes, and when a run ends or is reset.  Type
dump in the console for a fresh map mid-run.
"""

import math
import time

from .base import BG, COLOURS, DIM, FG, FONT, FONT_BIG, GRID, View, num

GRID_W, GRID_H, CELL_MM = 32, 14, 100          # sim.h
ROBOT_R_MM = 35                                 # sim.h: a 7 cm round robot
LEVELS = ["Pillars", "Corridors", "Trap", "Maze", "Door", "Custom"]   # levels.c, then custom
MODES = ["IDLE", "CALIB", "RUN", "ARRIVED", "NOPATH"]               # nav.h
TRAIL = 600                                     # 2.5 minutes of $NAV at 4 Hz
ASK_GAP = 1.0                                   # s: at most one "dump" this often

FREE = "#1f2a33"
WALL = "#c8d0da"
UNKNOWN = BG
TRUE_C = COLOURS[2]      # green: where the robot really is
EST_C = COLOURS[0]       # blue: where it thinks it is
PATH_C = COLOURS[1]      # orange: the plan
GOAL_C = COLOURS[5]
VETO_C = COLOURS[3]


def _hexnum(s):
    try:
        return int(s, 16)
    except (TypeError, ValueError):
        return None


class Nav(View):
    name = "nav"
    title = "Lesson 16 - robot navigation"
    frames = ("NAV", "RPATH", "RSTEP", "RMAP", "RVETO", "RGOAL")
    help = "a = go/restart, b = next level, s = true walls on OLED, 1-5 = speed, arrows = goal (idle); type dump to refresh the map"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.nav = None              # the last $NAV, as a dict
        self.trail_true, self.trail_est = [], []
        self.map = None              # committed dump: dict(occ, free, path, cost, ...)
        self._new = None             # a dump being received
        self.dumps = 0
        self.asked_at, self.pending = -1e9, None
        self._ask("view started")

    # ---- the board's frames ------------------------------------------------
    def _ask(self, why):
        """Send "dump" - or, if one went out under ASK_GAP ago, send it later
        (a level stepped through quickly must not queue a burst of dumps)."""
        now = time.monotonic()
        if now - self.asked_at < ASK_GAP:
            self.pending = why
            return
        self.asked, self.asked_at, self.pending = why, now, None
        self.station.send_line("dump")

    def frame(self, name, f, t):
        if name == "NAV":
            self._nav(f)
        elif name == "RPATH":                          # a dump begins
            v = [num(x) for x in f[:3]]
            if len(v) == 3 and None not in v and all(isinstance(x, int) for x in v):
                self._new = dict(cost=v[0], expanded=v[1], length=v[2], path={},
                                 occ=[0] * GRID_H, free=[0] * GRID_H, veto=[])
        elif self._new is None:
            return                                     # the middle of a dump we missed the start of
        elif name == "RSTEP" and len(f) >= 2:
            k, txt = num(f[0]), f[1]
            if not isinstance(k, int):
                return
            for j in range(len(txt) // 3):
                x, y = _hexnum(txt[3 * j:3 * j + 2]), _hexnum(txt[3 * j + 2])
                if x is not None and y is not None and x < GRID_W and y < GRID_H:
                    self._new["path"][k + j] = (x, y)
        elif name == "RMAP" and len(f) >= 3:
            y, occ, free = num(f[0]), _hexnum(f[1]), _hexnum(f[2])
            if isinstance(y, int) and 0 <= y < GRID_H and occ is not None and free is not None:
                self._new["occ"][y], self._new["free"][y] = occ, free
        elif name == "RVETO" and len(f) >= 2:
            x, y = num(f[0]), num(f[1])
            if isinstance(x, int) and isinstance(y, int):
                self._new["veto"].append((x, y))
        elif name == "RGOAL" and len(f) >= 5:          # the last frame: commit
            v = [num(x) for x in f[:5]]
            if len(v) < 5 or not all(isinstance(x, int) for x in v):
                return
            d = self._new
            d["start"], d["goal"], d["inflate"] = (v[0], v[1]), (v[2], v[3]), v[4]
            d["path"] = [d["path"][i] for i in sorted(d["path"])]
            d["level"] = self.nav["level"] if self.nav else None
            self.map, self._new = d, None
            self.dumps += 1

    def _nav(self, f):
        v = [num(x) for x in f[:13]]
        if len(v) < 13 or None in v:
            return
        v = [int(x) for x in v]
        n = dict(t=v[0], level=v[1], mode=v[2], ex=v[3], ey=v[4], eth=v[5],
                 tx=v[6], ty=v[7], tth=v[8], coll=v[9], replans=v[10], expanded=v[11], plan_us=v[12])
        old = self.nav or n
        self.nav = n
        if n["level"] != old["level"]:
            self.trail_true, self.trail_est = [], []
            self.map = None                            # the old level's map is not this one's
            self._ask("level changed")
        elif n["mode"] != old["mode"]:
            if n["mode"] == 0:                         # reset: a fresh run from the start
                self.trail_true, self.trail_est = [], []
            if n["mode"] in (0, 3, 4):                 # the robot has stopped: safe to re-plan
                self._ask(f"mode {MODES[n['mode']] if n['mode'] < len(MODES) else n['mode']}")
        if not self.trail_true or (n["tx"], n["ty"]) != self.trail_true[-1]:   # moved
            self.trail_true = (self.trail_true + [(n["tx"], n["ty"])])[-TRAIL:]
            self.trail_est = (self.trail_est + [(n["ex"], n["ey"])])[-TRAIL:]
        if self.pending and time.monotonic() - self.asked_at >= ASK_GAP:
            self._ask(self.pending)                    # one put off by ASK_GAP

    # ---- the picture ------------------------------------------------------
    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        side = 210 if w > 700 else 0                   # counters at the right when there is room
        mw, mh = w - side - 20, h - 40 - (0 if side else 90)
        cell = max(4.0, min(mw / GRID_W, mh / GRID_H))
        ox, oy = 10, 34                                # top-left of the arena
        aw, ah = cell * GRID_W, cell * GRID_H

        def px(x_mm, y_mm):                            # mm, y up -> canvas, y down
            return ox + x_mm / CELL_MM * cell, oy + ah - y_mm / CELL_MM * cell

        def cell_box(x, y):
            return ox + x * cell, oy + ah - (y + 1) * cell, ox + (x + 1) * cell, oy + ah - y * cell

        n, m = self.nav, self.map
        lv = n["level"] if n else None
        lvname = LEVELS[lv - 1] if lv and 1 <= lv <= len(LEVELS) else "?"
        mode = MODES[n["mode"]] if n and 0 <= n["mode"] < len(MODES) else "?"
        c.create_text(ox, 6, anchor="nw", fill=FG, font=FONT_BIG,
                      text=f"level {lv if lv else '-'} {lvname}   {mode}")

        # the robot's map: unknown is the background, free a little lighter, walls bright
        c.create_rectangle(ox, oy, ox + aw, oy + ah, outline=GRID, fill=UNKNOWN)
        if m:
            for y in range(GRID_H):
                occ, free = m["occ"][y], m["free"][y]
                for x in range(GRID_W):
                    b = 1 << (31 - x)
                    if occ & b:
                        c.create_rectangle(*cell_box(x, y), fill=WALL, outline="")
                    elif free & b:
                        c.create_rectangle(*cell_box(x, y), fill=FREE, outline="")
            for x, y in m["veto"]:
                x0, y0, x1, y1 = cell_box(x, y)
                c.create_line(x0, y0, x1, y1, fill=VETO_C, width=2)
                c.create_line(x0, y1, x1, y0, fill=VETO_C, width=2)
        if cell >= 12:                                 # the 10 cm grid, when it is not noise
            for x in range(1, GRID_W):
                c.create_line(ox + x * cell, oy, ox + x * cell, oy + ah, fill=GRID)
            for y in range(1, GRID_H):
                c.create_line(ox, oy + y * cell, ox + aw, oy + y * cell, fill=GRID)
        c.create_rectangle(ox, oy, ox + aw, oy + ah, outline=DIM)

        if m:
            pts = []
            for x, y in m["path"]:
                pts += px(x * CELL_MM + CELL_MM / 2, y * CELL_MM + CELL_MM / 2)
            if len(pts) >= 4:
                c.create_line(*pts, fill=PATH_C, width=max(2, cell / 8), dash=(6, 3))
            for (x, y), col, txt in ((m["start"], DIM, "S"), (m["goal"], GOAL_C, "G")):
                x0, y0, x1, y1 = cell_box(x, y)
                c.create_rectangle(x0 + 2, y0 + 2, x1 - 2, y1 - 2, outline=col, width=2)
                c.create_text((x0 + x1) / 2, (y0 + y1) / 2, text=txt, fill=col, font=FONT)

        # the trails, then the two robots: true solid green, estimate blue
        for trail, col, dash in ((self.trail_true, TRUE_C, ()), (self.trail_est, EST_C, (3, 3))):
            pts = [p for xy in trail for p in px(*xy)]
            if len(pts) >= 4:
                c.create_line(*pts, fill=col, width=1, dash=dash)
        if n:
            r = max(3.0, ROBOT_R_MM / CELL_MM * cell)
            for x, y, th, col in ((n["tx"], n["ty"], n["tth"], TRUE_C), (n["ex"], n["ey"], n["eth"], EST_C)):
                cx, cy = px(x, y)
                a = th / 1000.0 * math.pi / 180.0
                c.create_oval(cx - r, cy - r, cx + r, cy + r, outline=col, width=2)
                c.create_line(cx, cy, cx + 2 * r * math.cos(a), cy - 2 * r * math.sin(a),
                              fill=col, width=2, arrow="last")

        # the counters
        lines = self._counters()
        if side:
            x0, y0 = w - side, oy
        else:
            x0, y0 = ox, oy + ah + 8
        for i, (txt, col) in enumerate(lines):
            if side:
                c.create_text(x0, y0 + 17 * i, anchor="nw", fill=col, font=FONT, text=txt)
            else:
                c.create_text(x0 + (i // 5) * 260, y0 + 16 * (i % 5), anchor="nw", fill=col, font=FONT, text=txt)

    def _counters(self):
        n, m = self.nav, self.map
        out = []
        if n:
            err = math.hypot(n["ex"] - n["tx"], n["ey"] - n["ty"])
            dth = (n["eth"] - n["tth"]) / 1000.0
            dth = (dth + 180.0) % 360.0 - 180.0
            out += [(f"time      {n['t'] / 1000:8.1f} s", FG),
                    (f"collisions {n['coll']:7d}", COLOURS[3] if n["coll"] else FG),
                    (f"replans    {n['replans']:7d}", FG),
                    (f"A* nodes   {n['expanded']:7d}", FG),
                    (f"A* time    {n['plan_us']:7d} us", FG),
                    (f"true  {n['tx']:5d},{n['ty']:5d} mm {n['tth'] / 1000:6.1f} deg", TRUE_C),
                    (f"est   {n['ex']:5d},{n['ey']:5d} mm {n['eth'] / 1000:6.1f} deg", EST_C),
                    (f"error {err:5.0f} mm    {dth:6.1f} deg", FG)]
        else:
            out.append(("waiting for $NAV - type loud?", DIM))
        if m:
            cost = "no path" if m["cost"] == 65535 else f"cost {m['cost']}"
            out += [(f"map #{self.dumps}: {cost}, {len(m['path'])} cells", PATH_C),
                    (f"  {m['expanded']} nodes, inflate {m['inflate']}, {len(m['veto'])} veto", DIM)]
        else:
            out.append((f"map: asked ({self.asked})", DIM))
        return out
