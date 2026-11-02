"""
l15_line.py - lesson 15, the line follower, seen from above.

The firmware sends (Main.c, task_ui; 10 Hz while racing, `quiet` stops it):

    $LINE,<ms>,<est x10>,<true x10>,<cmd L>,<cmd R>,<speed>,<s>,<slip>,
          <x>,<y>,<heading>,<track>,<line 0>,...,<line 6>
    $LAP,<track>,<ms>                       every AUTO lap (manual laps send none)

  ms         world time, ms (5 ms steps; resets on every start)
  est x10    where control.c THINKS the line is, 0.1 mm, + = line to the LEFT
             (measured at the sensor bar, 70 mm ahead of the axle)
  true x10   the world's TRUE lateral error of the axle, 0.1 mm, + = ROBOT to
             the left of the line - so it is mostly the opposite sign of est.
             The charts plot -true, so both read "+ = line to the left".
  cmd L, R   wheel speed commands, mm/s, +-2000
  speed      the robot's real speed over the ground, mm/s
  s          distance along the track, mm;   slip  1 while the tyres slide
  x, y       axle centre, mm (truth);   heading  degrees, 0 = east, + = anticlockwise
  track      1..4;   line 0..6  reflectance 0..1000, 1000 = tape, line 0 = RIGHTMOST

Frames from before the last eight fields were added (8 fields) still chart;
the map then has nothing to draw.

THE TRACK SHAPE is not sent: it is rebuilt here from the same turtle commands
as ../../projects2026_arm/15_Robot_Line_Follower/track.c.  THE TWO MUST STAY IN
SYNC - edit a track there (Lab Part 8) and this map is wrong for it.  A track
id this file does not know is drawn from the robot's own trail instead.
"""

import math
from collections import deque

from .base import BG, COLOURS, DIM, FG, FONT, FONT_BIG, GRID, Strip, View, num

# ---- track.c, in Python ------------------------------------------------------
# (mm, deg, gap): deg == 0 is a straight of mm; else an arc of radius mm turning
# deg degrees, + = left.  (name, x0, y0, heading0 in degrees, commands)
TRACKS = {
    1: ("OVAL", 0.0, 0.0, 0.0, [
        (600, 0, 0), (400, 180, 0), (1200, 0, 0), (400, 180, 0), (600, 0, 0)]),
    2: ("S-BENDS", 0.0, 0.0, 0.0, [
        (150, 0, 0), (220, 60, 0), (220, -120, 0), (220, 60, 0), (150, 0, 0),
        (350, 180, 0),
        (150, 0, 0), (220, 60, 0), (220, -120, 0), (220, 60, 0), (150, 0, 0),
        (350, 180, 0)]),
    3: ("HAIRPINS", 0.0, 0.0, 0.0, [
        (750, 0, 0), (120, 180, 0), (1000, 0, 0), (120, -180, 0),
        (1000, 0, 0), (120, 180, 0), (1500, 0, 0), (200, 90, 0),
        (320, 0, 0), (200, 90, 0), (750, 0, 0)]),
    4: ("FIGURE-8", 148.49, 148.49, 45.0, [
        (210, 0, 0), (420, -270, 0),
        (105, 0, 0), (126, 0, 1), (609, 0, 0),
        (420, 270, 0), (630, 0, 0)]),
}
TAPE_MM = 19.0                 # track.h: TAPE_HALF_MM 9.5
WHEELBASE_MM = 100.0           # world.h: ROBOT_TRACK_MM
BAR_AHEAD_MM = 70.0            # world.h: BAR_AHEAD_MM
PITCH_MM = 13.0                # control.h: SENSOR_PITCH_MM


def build_track(tid):
    """track_build() from track.c: [(x, y, gap_after)], in mm."""
    if tid not in TRACKS:
        return None
    _, x, y, th0, cmds = TRACKS[tid]
    th = math.radians(th0)
    pts = [(x, y, 0)]
    for mm, deg, gap in cmds:
        if deg == 0:                                  # straights in <= 100 mm pieces
            k = (mm + 99) // 100
            step = mm / k
            for _ in range(k):
                x += step * math.cos(th)
                y += step * math.sin(th)
                pts.append((x, y, gap))
        else:                                         # arcs in chords of sqrt(8 R)
            r, ang = float(mm), math.radians(deg)
            rs = r if ang > 0 else -r
            cx, cy = x - rs * math.sin(th), y + rs * math.cos(th)
            k = int(r * abs(ang) / math.sqrt(8.0 * r)) + 1
            for j in range(1, k + 1):
                ph = th + ang * j / k
                x, y = cx + rs * math.sin(ph), cy - rs * math.cos(ph)
                pts.append((x, y, gap))
            th += ang
    pts[-1] = (pts[0][0], pts[0][1], pts[-1][2])      # snapped shut, as in C
    # gap belongs to the SEGMENT that ends at a point: segment i = pts[i] -> pts[i+1]
    return [(round(px), round(py), pts[i + 1][2] if i + 1 < len(pts) else 0)
            for i, (px, py, _) in enumerate(pts)]


def fnum(f, i):
    """Field i as a finite number, or None if missing or garbled."""
    v = num(f[i]) if i < len(f) else None
    return v if v is not None and math.isfinite(v) else None


class LineFollower(View):
    name = "line"
    title = "Lesson 15 - line follower"
    frames = ("LINE", "LAP")
    help = ("menu: up/down + a.  race: a start/stop, b next track, s manual/auto "
            "(arrows drive), 1-5 or - + speed.  Shell: quiet / loud")

    TRAIL = 900                # points: 90 s at 10 Hz

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.pos = Strip(["est", "-true"], span=10, unit=" mm")
        self.motors = Strip(["L", "R", "speed"], span=10, lo=-2000, hi=2000)
        self.track_id = None
        self.track = None
        self.pose = None               # (x, y, heading rad)
        self.sens = None               # 7 readings, line 0 (rightmost) first
        self.trail = deque(maxlen=self.TRAIL)   # (x, y, slipping)
        self.last_ms = None
        self.info = {}
        self.laps = deque(maxlen=12)   # (track, ms)
        self.best = {}                 # track -> ms

    # ---- frames ----------------------------------------------------------
    def frame(self, name, f, t):
        if name == "LAP":
            tr, ms = fnum(f, 0), fnum(f, 1)
            if isinstance(tr, int) and 0 < tr < 100 and isinstance(ms, int) and ms > 0:
                self.laps.append((tr, ms))
                if tr not in self.best or ms < self.best[tr]:
                    self.best[tr] = ms
            return
        if name != "LINE" or len(f) < 8:
            return
        ms, est, tru = fnum(f, 0), fnum(f, 1), fnum(f, 2)
        cl, cr, v = fnum(f, 3), fnum(f, 4), fnum(f, 5)
        self.pos.add(t, est=None if est is None else est / 10.0,
                     **{"-true": None if tru is None else -tru / 10.0})
        self.motors.add(t, L=cl, R=cr, speed=v)
        self.info = {"ms": ms, "est": est, "true": tru, "v": v, "s": fnum(f, 6), "slip": fnum(f, 7)}

        if isinstance(ms, int) and self.last_ms is not None and ms < self.last_ms:
            self.trail.clear()                         # a new run: the clock restarted
        if isinstance(ms, int):
            self.last_ms = ms

        x, y, hd, tid = fnum(f, 8), fnum(f, 9), fnum(f, 10), fnum(f, 11)
        if isinstance(tid, int) and tid != self.track_id:
            self.track_id = tid
            self.track = build_track(tid)
            self.trail.clear()
        if None not in (x, y, hd):
            self.pose = (float(x), float(y), math.radians(hd))
            self.trail.append((float(x), float(y), self.info["slip"] == 1))
        if len(f) >= 19:
            vals = [fnum(f, 12 + i) for i in range(7)]
            if None not in vals:
                self.sens = [max(0, min(1000, v)) for v in vals]

    # ---- drawing ---------------------------------------------------------
    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        name = TRACKS[self.track_id][0] if self.track_id in TRACKS else (
            f"track {self.track_id}" if self.track_id is not None else "waiting for $LINE...")
        i = self.info
        head = name
        if i.get("ms") is not None:
            head += f"   t {i['ms'] / 1000:6.2f} s"
        if i.get("v") is not None:
            head += f"   {i['v']} mm/s"
        if i.get("slip") == 1:
            head += "   SLIP"
        c.create_text(10, 8, anchor="nw", fill=FG, font=FONT_BIG, text=head)

        top = 40
        if w >= 1.2 * h:                               # side by side
            mw = int(w * 0.58)
            self._map(10, top, mw - 20, h - top - 10)
            self._panel(mw, top, w - mw - 10, h - top - 10)
        else:                                          # stacked
            mh = int((h - top) * 0.55)
            self._map(10, top, w - 20, mh - 10)
            self._panel(10, top + mh, w - 20, h - top - mh - 10)

    def _bounds(self):
        if self.track:
            xs = [p[0] for p in self.track]
            ys = [p[1] for p in self.track]
        elif self.trail:
            xs = [p[0] for p in self.trail]
            ys = [p[1] for p in self.trail]
        else:
            return None
        return min(xs), max(xs), min(ys), max(ys)

    def _map(self, x0, y0, w, h):
        c = self.c
        c.create_rectangle(x0, y0, x0 + w, y0 + h, outline=GRID)
        b = self._bounds()
        if b is None or w < 40 or h < 40:
            c.create_text(x0 + w / 2, y0 + h / 2, fill=DIM, font=FONT,
                          text="no position yet - start a race (a)")
            return
        xmin, xmax, ymin, ymax = b
        m = 160.0                                      # mm of margin: the robot overhangs
        sx = (w - 20) / max(xmax - xmin + 2 * m, 1.0)
        sy = (h - 20) / max(ymax - ymin + 2 * m, 1.0)
        k = min(sx, sy)
        cx = x0 + w / 2 - (xmin + xmax) / 2 * k
        cy = y0 + h / 2 + (ymin + ymax) / 2 * k

        def P(px, py):
            return cx + px * k, cy - py * k            # y up, as in track.c

        if self.track:
            tape = max(2, TAPE_MM * k)
            for a, bb in zip(self.track, self.track[1:]):
                (ax, ay), (bx, by) = P(a[0], a[1]), P(bb[0], bb[1])
                if a[2]:                               # a gap: no tape here
                    c.create_line(ax, ay, bx, by, fill=DIM, dash=(3, 4))
                else:
                    c.create_line(ax, ay, bx, by, fill="#3b4652", width=tape, capstyle="round")
            # the start line, across the track at point 0
            (sx0, sy0, _), (sx1, sy1, _) = self.track[0], self.track[1]
            d = math.hypot(sx1 - sx0, sy1 - sy0) or 1.0
            nx, ny = -(sy1 - sy0) / d * 40, (sx1 - sx0) / d * 40
            c.create_line(*P(sx0 - nx, sy0 - ny), *P(sx0 + nx, sy0 + ny), fill=FG, width=2)
        elif self.track_id is not None:
            c.create_text(x0 + 6, y0 + h - 6, anchor="sw", fill=DIM, font=FONT,
                          text=f"track {self.track_id} is not in l15_line.py: drawn from the trail")

        # the trail: blue on the line, red where the tyres slid
        run, col = [], None
        for px, py, slip in self.trail:
            cc = COLOURS[3] if slip else COLOURS[0]
            if cc != col and len(run) >= 4:
                c.create_line(*run, fill=col, width=2)
                run = run[-2:]
            col = cc
            run += P(px, py)
        if len(run) >= 4:
            c.create_line(*run, fill=col, width=2)

        if self.pose:
            self._robot(P, k, *self.pose)
            self._inset(x0 + w - 10, y0 + 10, min(w, h) * 0.36)

    def _robot(self, P, k, x, y, th):
        """Body, wheels and the sensor bar, to scale (but never under ~12 px)."""
        c = self.c
        g = max(1.0, 14.0 / (WHEELBASE_MM * k))       # grow it if the map is small
        ca, sa = math.cos(th), math.sin(th)

        def R(fwd, left):                              # robot frame -> canvas
            return P(x + (fwd * ca - left * sa) * g, y + (fwd * sa + left * ca) * g)

        body = [R(-40, -50), R(85, -50), R(85, 50), R(-40, 50)]
        c.create_polygon(*[v for p in body for v in p], fill="", outline=FG, width=2)
        for side in (-1, 1):                           # the wheels, on the axle
            a, b = R(-30, side * 55), R(30, side * 55)
            c.create_line(*a, *b, fill=FG, width=max(2, 8 * k * g))
        self._sensor_dots(R, max(2.0, 4 * k * g))

    def _sensor_dots(self, R, r):
        c = self.c
        for i in range(7):
            px, py = R(BAR_AHEAD_MM, (i - 3) * PITCH_MM)
            v = self.sens[i] if self.sens else 0
            c.create_oval(px - r, py - r, px + r, py + r, fill=self._shade(v), outline=DIM)

    @staticmethod
    def _shade(v):
        """Reflectance 0..1000 as a colour: dark grey (floor) to amber (tape)."""
        f = max(0.0, min(1.0, v / 1000.0))
        r = int(0x30 + f * (0xff - 0x30))
        g = int(0x30 + f * (0xb4 - 0x30))
        b = int(0x30 + f * (0x54 - 0x30))
        return f"#{r:02x}{g:02x}{b:02x}"

    def _inset(self, right, top, size):
        """The robot's view: 400 mm around it, heading UP, tape and sensor spots."""
        if size < 80:
            return
        c = self.c
        x0, y0 = right - size, top
        c.create_rectangle(x0, y0, x0 + size, y0 + size, fill=BG, outline=GRID)
        span = 400.0
        k = size / span
        rx, ry, th = self.pose
        ca, sa = math.cos(th), math.sin(th)
        ox, oy = x0 + size / 2, y0 + size * 0.62         # the axle, a little low

        def W(px, py):                                 # world -> inset, heading up
            dx, dy = px - rx, py - ry
            fwd, left = dx * ca + dy * sa, -dx * sa + dy * ca
            return ox - left * k, oy - fwd * k

        if self.track:
            for a, b in zip(self.track, self.track[1:]):
                if max(abs(a[0] - rx), abs(a[1] - ry)) > span and max(abs(b[0] - rx), abs(b[1] - ry)) > span:
                    continue
                if a[2]:
                    c.create_line(*W(a[0], a[1]), *W(b[0], b[1]), fill=DIM, dash=(3, 4))
                else:
                    c.create_line(*W(a[0], a[1]), *W(b[0], b[1]), fill="#3b4652",
                                  width=TAPE_MM * k, capstyle="round")

        def R(fwd, left):
            return ox - left * k, oy - fwd * k

        body = [R(-40, -50), R(85, -50), R(85, 50), R(-40, 50)]
        c.create_polygon(*[v for p in body for v in p], fill="", outline=FG, width=2)
        for side in (-1, 1):
            c.create_line(*R(-30, side * 55), *R(30, side * 55), fill=FG, width=max(2, 8 * k))
        self._sensor_dots(R, max(3.0, 5 * k))
        c.create_text(x0 + 4, y0 + 2, anchor="nw", fill=DIM, font=FONT, text="robot's view, 400 mm")
        c.create_text(x0 + size - 4, y0 + size - 2, anchor="se", fill=DIM, font=FONT,
                      text="true err " + (f"{self.info['true'] / 10:+.1f} mm"
                                          if isinstance(self.info.get("true"), (int, float)) else "-"))

    def _panel(self, x, y, w, h):
        c = self.c
        laps_w = 190 if w > 520 else 0
        cw = w - laps_w
        gap = 8
        rh = (h - 2 * gap) / 3
        self._bars(x, y, cw, rh)
        self.pos.draw(c, x, y + rh + gap, cw, rh, "line position, mm, + = left  (est: bar; -true: axle)")
        self.motors.draw(c, x, y + 2 * (rh + gap), cw, rh, "wheel commands and speed, mm/s")
        if laps_w:
            self._laps(x + cw + 10, y, laps_w - 10, h)

    def _bars(self, x, y, w, h):
        """The seven readings, leftmost sensor on the LEFT (line 6 ... line 0)."""
        c = self.c
        c.create_rectangle(x, y, x + w, y + h, outline=GRID)
        c.create_text(x + w / 2, y + 2, anchor="n", fill=FG, font=FONT,
                      text="sensor bar, 0..1000  (left ... right, as the robot sees it)")
        if not self.sens:
            c.create_text(x + w / 2, y + h / 2, fill=DIM, font=FONT, text="no readings yet")
            return
        top, bot = y + 20, y + h - 18
        bw = w / 7
        for j in range(7):
            v = self.sens[6 - j]
            bx = x + j * bw
            by = bot - (bot - top) * v / 1000.0
            c.create_rectangle(bx + bw * 0.15, by, bx + bw * 0.85, bot, fill=self._shade(v), outline="")
            c.create_text(bx + bw / 2, bot + 2, anchor="n", fill=DIM, font=FONT, text=str(v))

    def _laps(self, x, y, w, h):
        c = self.c
        c.create_text(x, y, anchor="nw", fill=FG, font=FONT_BIG, text="$LAP")
        yy = y + 30
        for tr in sorted(self.best):
            nm = TRACKS[tr][0] if tr in TRACKS else f"track {tr}"
            c.create_text(x, yy, anchor="nw", fill=COLOURS[2], font=FONT,
                          text=f"best {nm:<9}{self.best[tr] / 1000:6.2f} s")
            yy += 16
        yy += 8
        for n, (tr, ms) in enumerate(reversed(self.laps)):
            if yy > y + h - 16:
                break
            c.create_text(x, yy, anchor="nw", fill=FG if n == 0 else DIM, font=FONT,
                          text=f"track {tr}   {ms / 1000:6.2f} s")
            yy += 16
        if not self.laps:
            c.create_text(x, yy, anchor="nw", fill=DIM, font=FONT, text="auto laps only")
