"""
l19_final.py - lesson 19, the final-project template.  Two halves, one per
frame the template sends (projects2026_arm/19_Final_Project/Main.c, app.c):

    $APP,<ms>,<x px>,<y px>,<vx Q8>,<vy Q8>,<score>                   10 Hz
    $SYS,<ms>,<idle per mille>,<switches>,<i2c errors>,<oled bytes>,<feeds>   1 Hz

Left, the app: steer's arena at the board's own 128 x 64 pixels, drawn large -
the ball, its last few seconds as a trail, its velocity as an arrow, the score.
Right, the system: the $SYS counters as rates (switches per second, OLED bytes
per second, watchdog feeds per second) with a strip chart each.

YOUR PROJECT'S VIEW starts as a copy of this file:
  1. copy it to views/l19_<yourname>.py, rename the class, set `name` and `title`;
  2. give your app_telemetry() frame its own name ($ROBO, not $APP) and put it
     FIRST in `frames`, keeping "SYS":  frames = ("ROBO", "SYS").  Listing SYS
     keeps the station from switching to this view when a $SYS arrives;
  3. rewrite _app() and _draw_app() for your fields; keep _sys() and the
     dashboard - your project sends the same $SYS.
Units stay the board's: Q8 velocities are divided here, not on the board.
"""

import time
from collections import deque

from .base import BG, COLOURS, DIM, FG, FONT, FONT_BIG, GRID, Strip, View, num

OLED_W, OLED_H = 128, 64        # the panel, px
HUD_H = 9                       # app.c: rows 0..8 are the score line
STEP_HZ = 50                    # app.h: APP_PERIOD_MS = 20 -> velocities are per 20 ms step
TRAIL_S = 3.0                   # seconds of path drawn behind the ball
ARROW_S = 0.25                  # the velocity arrow: where the ball is in this many seconds
BAD = "#f07178"
GOOD = "#9ad36a"
FONT_HUGE = ("Consolas", 28, "bold")


def ints(f, n):
    """The first n fields as integers, or None if any is missing or not one."""
    if len(f) < n:
        return None
    v = [num(x) for x in f[:n]]
    return None if any(not isinstance(x, int) for x in v) else v


class Final(View):
    name = "final"
    title = "Lesson 19 - final project: app and health"
    frames = ("APP", "SYS")
    help = "arrows = stick, a = brake, b = restart, 1-5 = knob gain;  shell: stats, health, quiet, loud"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        # the app
        self.app = None                       # [ms, x, y, vx, vy, score]
        self.app_t = None                     # when it arrived (monotonic)
        self.trail = deque()                  # (t, x, y)
        self.restarts = 0                     # <ms> went backwards: B, or a reset
        self.speed = Strip(["px/s"], span=20, lo=0)
        # the system
        self.sys = None                       # [ms, idle, switches, errors, bytes, feeds]
        self.sys_t = None
        self.rate = {}                        # per second, from two $SYS in a row
        self.idle = Strip(["idle %"], span=60, lo=0, hi=100)
        self.switch = Strip(["switches/s"], span=60, lo=0)
        self.oled = Strip(["kB/s"], span=60, lo=0)
        self.feeds = Strip(["feeds/s"], span=60, lo=0, hi=8)
        self.bad = 0                          # frames too short or not numbers

    # ---- frames ------------------------------------------------------------
    def frame(self, name, f, t):
        if name == "APP":
            self._app(f, t)
        elif name == "SYS":
            self._sys(f, t)

    def _app(self, f, t):
        v = ints(f, 6)
        if v is None:
            self.bad += 1
            return
        if self.app is not None and v[0] < self.app[0]:   # time ran backwards
            self.restarts += 1
            self.trail.clear()
        self.app, self.app_t = v, t
        self.trail.append((t, v[1], v[2]))
        while self.trail and self.trail[0][0] < t - TRAIL_S:
            self.trail.popleft()
        vx, vy = v[3] / 256 * STEP_HZ, v[4] / 256 * STEP_HZ        # px per second
        self.speed.add(t, **{"px/s": (vx * vx + vy * vy) ** 0.5})

    def _sys(self, f, t):
        v = ints(f, 6)
        if v is None:
            self.bad += 1
            return
        p = self.sys
        if p is not None and v[0] > p[0] and all(a >= b for a, b in zip(v[2:], p[2:])):
            k = 1000.0 / (v[0] - p[0])                    # counters -> per second
            self.rate = {"switches": (v[2] - p[2]) * k, "bytes": (v[4] - p[4]) * k,
                         "feeds": (v[5] - p[5]) * k}
            self.switch.add(t, **{"switches/s": self.rate["switches"]})
            self.oled.add(t, **{"kB/s": self.rate["bytes"] / 1000})
            self.feeds.add(t, **{"feeds/s": self.rate["feeds"]})
        else:
            self.rate = {}                                # first one, or the board reset
        self.idle.add(t, **{"idle %": v[1] / 10})
        self.sys, self.sys_t = v, t

    # ---- drawing -----------------------------------------------------------
    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        if w >= 760:                                      # side by side
            lw = int(w * 0.58)
            self._draw_app(10, 10, lw - 20, h - 20)
            self._draw_sys(lw, 10, w - lw - 10, h - 20)
        else:                                             # one above the other
            top = int(h * 0.5)
            self._draw_app(10, 10, w - 20, top - 20)
            self._draw_sys(10, top, w - 20, h - top - 10)

    def _age(self, t):
        return f"{time.monotonic() - t:.0f} s ago" if t is not None else "never"

    def _draw_app(self, x0, y0, w, h):
        c = self.c
        a = self.app
        if a is None:
            c.create_text(x0 + w / 2, y0 + h / 2, fill=DIM, font=FONT_BIG,
                          text="waiting for $APP ...  (quiet?  type loud)")
            return
        ms, bx, by, vx, vy, score = a
        c.create_text(x0, y0, anchor="nw", fill=FG, font=FONT_HUGE, text=f"SCORE {score}")
        c.create_text(x0 + w, y0 + 6, anchor="ne", fill=DIM, font=FONT_BIG,
                      text=f"{ms / 1000:7.1f} s")
        stale = time.monotonic() - self.app_t > 1.0
        if stale:
            c.create_text(x0 + w, y0 + 30, anchor="ne", fill=BAD, font=FONT,
                          text=f"no $APP for {self._age(self.app_t)} - quiet, or a task hung?")

        # The arena, at the board's pixels: s screen px per board px, 2:1.
        top = y0 + 48
        sh = max(h - 48 - 120, 60)
        s = max(min(w / OLED_W, sh / OLED_H), 0.5)
        ox, oy = x0 + (w - OLED_W * s) / 2, top

        def P(px, py):
            return ox + px * s, oy + py * s

        c.create_rectangle(*P(0, 0), *P(OLED_W, OLED_H), outline=GRID, fill="#05080b")
        c.create_rectangle(*P(0, HUD_H), *P(OLED_W, OLED_H), outline=DIM)   # app.c's border
        c.create_text(*P(2, HUD_H / 2), anchor="w", fill=DIM, font=FONT, text="HUD")

        pts = []                                          # the trail, oldest faint
        for _, tx, ty in self.trail:
            pts += P(tx + 0.5, ty + 0.5)
        if len(pts) >= 4:
            c.create_line(*pts, fill=COLOURS[0], width=2, dash=(4, 3))
        cx, cy = P(bx + 0.5, by + 0.5)
        r = 3 * s                                         # app.c: R = 3 px
        c.create_oval(cx - r, cy - r, cx + r, cy + r, fill=COLOURS[1], outline="")

        pxs, pys = vx / 256 * STEP_HZ, vy / 256 * STEP_HZ  # Q8 per step -> px/s
        ex, ey = P(bx + 0.5 + pxs * ARROW_S, by + 0.5 + pys * ARROW_S)
        if abs(ex - cx) + abs(ey - cy) > 2:
            c.create_line(cx, cy, ex, ey, fill=COLOURS[2], width=3, arrow="last")
        c.create_text(x0, oy + OLED_H * s + 6, anchor="nw", fill=FG, font=FONT,
                      text=f"x {bx:3d}  y {by:3d} px    v ({pxs:+6.1f}, {pys:+6.1f}) px/s"
                           f"    restarts {self.restarts}")
        sy = oy + OLED_H * s + 26
        if y0 + h - sy > 40:
            self.speed.draw(c, x0, sy, w, y0 + h - sy, "speed")

    def _draw_sys(self, x0, y0, w, h):
        c = self.c
        v = self.sys
        c.create_text(x0, y0, anchor="nw", fill=FG, font=FONT_BIG, text="health  ($SYS)")
        if v is None:
            c.create_text(x0, y0 + 30, anchor="nw", fill=DIM, font=FONT,
                          text="waiting for $SYS - one a second")
            return
        stale = time.monotonic() - self.sys_t > 2.5
        r = self.rate

        def rate(k, fmt):
            return fmt.format(r[k]) if k in r else "-"

        rows = [
            ("idle", f"{v[1] / 10:5.1f} %", FG),
            ("switches", rate("switches", "{:7.0f} /s"), FG),
            ("OLED", rate("bytes", "{:7.0f} B/s"), FG),
            ("watchdog", f"{rate('feeds', '{:4.1f} /s')}  ({v[5]} feeds)",
             BAD if r.get("feeds", 1) == 0 else GOOD),
            ("I2C errors", f"{v[3]}", BAD if v[3] else GOOD),
            ("last $SYS", self._age(self.sys_t), BAD if stale else DIM),
        ]
        if self.bad:
            rows.append(("bad frames", f"{self.bad}", BAD))
        y = y0 + 30
        for label, text, col in rows:
            c.create_text(x0, y, anchor="nw", fill=DIM, font=FONT, text=label)
            c.create_text(x0 + 110, y, anchor="nw", fill=col, font=FONT, text=text)
            y += 18
        y += 6
        n = 4
        ch = (y0 + h - y) / n
        if ch < 30:
            return
        for i, (strip, title) in enumerate(((self.idle, "idle - CPU left over"),
                                            (self.switch, "context switches"),
                                            (self.oled, "OLED bytes on I2C"),
                                            (self.feeds, "watchdog feeds"))):
            strip.draw(c, x0, y + i * ch, w, ch - 6, title)
