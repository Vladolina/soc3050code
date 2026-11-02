"""
l17_balance.py - lesson 17, the balancing robot, seen from the side.

    $BAL,<ms>,<mode>,<tilt mdeg>,<estimate mdeg>,<x mm>,<speed mm/s>,<mV>,<fallen>,<payload g>

mode is 0 PID tilt, 1 cascade, 2 LQR (control.c's names); tilt is + leaning
forward, toward + x (world.h); mV is the motor voltage the controller asked
for - the control effort - clipped by the world at the 7.4 V supply.

The body is drawn at the TRUE tilt, solid; the controller's ESTIMATE of it is
a dashed ghost on the same axle, so a complementary filter that trusts the gyro
too much (alpha 100) is seen leaning away from the real body.

$BAL arrives at 10 Hz and the canvas redraws at ~30 Hz.  The picture is NOT
interpolated between frames (that would need the next frame, i.e. 100 ms of
lag): it eases toward the latest one with a ~50 ms time constant, so the
robot moves smoothly and is never more than about one frame behind.  The
charts plot the frames exactly as they arrived.
"""

import math
import time

from .base import BG, DIM, FG, FONT, FONT_BIG, GRID, Strip, View, num, rot

MODES = ("PID tilt", "cascade", "LQR")
WHEEL_R = 0.035          # m, params.h BOT_R
BODY_L = 0.22            # m, drawn length of the body (the payload sits at 0.20)
VBAT = 7.4               # V, params.h - the effort chart's limits
EASE_S = 0.05            # s, the picture's time constant toward the latest frame


class Balance(View):
    name = "balance"
    title = "Lesson 17 - balancing robot"
    frames = ("BAL",)
    help = "arrows = drive, a = stand up, b = push, s = next controller, 1-5 = payload, - + = push size"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.tilt = Strip(["true", "estimate"], span=20, unit=" deg")
        self.effort = Strip(["volts"], span=20, lo=-VBAT, hi=VBAT, unit=" V")
        self.pos = Strip(["x"], span=20, unit=" m")
        self.latest = None       # dict of the newest good frame
        self.shown = None        # the eased values the picture draws
        self.t_draw = None
        self.cam = 0.0           # m, the world x at the centre of the scene
        self.frames_in = 0
        self.bad = 0

    # ---- frames ---------------------------------------------------------
    def frame(self, name, f, t):
        if name != "BAL":
            return
        v = [num(s) for s in f[:9]]
        if len(v) < 9 or any(x is None or not math.isfinite(x) for x in v):
            self.bad += 1
            return
        ms, mode, th, est, x, xd, mv, fallen, pay = v
        d = {"ms": ms, "mode": int(mode), "th": th / 1000.0, "est": est / 1000.0,
             "x": x / 1000.0, "xd": xd / 1000.0, "u": mv / 1000.0,
             "fallen": bool(fallen), "pay": pay / 1000.0}
        self.latest = d
        self.frames_in += 1
        self.tilt.add(t, true=d["th"], estimate=d["est"])
        self.effort.add(t, volts=d["u"])
        self.pos.add(t, x=d["x"])

    def _ease(self):
        now = time.monotonic()
        dt = 0.0 if self.t_draw is None else min(now - self.t_draw, 1.0)
        self.t_draw = now
        L = self.latest
        if self.shown is None or L["fallen"] != self.shown["fallen"] or abs(L["x"] - self.shown["x"]) > 1.0:
            self.shown = dict(L)                 # first frame, a fall, a stand-up: jump
            return
        k = 1.0 - math.exp(-dt / EASE_S)
        for key in ("th", "est", "x"):
            self.shown[key] += (L[key] - self.shown[key]) * k
        for key in ("mode", "fallen", "pay", "u", "xd", "ms"):
            self.shown[key] = L[key]

    # ---- the picture ----------------------------------------------------
    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        if self.latest is None:
            c.create_text(w / 2, h / 2, text="waiting for $BAL ...  (type loud if it was quieted)",
                          fill=DIM, font=FONT_BIG)
            return
        self._ease()
        s = self.shown
        mode = MODES[s["mode"]] if 0 <= s["mode"] < len(MODES) else f"mode {s['mode']}"

        # header
        c.create_text(10, 8, anchor="nw", fill=FG, font=FONT_BIG,
                      text=f"{mode}    tilt {s['th']:+.1f} deg    estimate {s['est']:+.1f} deg")
        c.create_text(10, 34, anchor="nw", fill=DIM, font=FONT,
                      text=f"x {s['x']:+.3f} m   v {s['xd']:+.3f} m/s   u {s['u']:+.2f} V   "
                           f"payload {s['pay'] * 1000:.0f} g   t {s['ms'] / 1000:.1f} s   "
                           f"frames {self.frames_in}" + (f"  bad {self.bad}" if self.bad else ""))

        # layout: the scene on the left, three charts on the right (or below, if narrow)
        top = 56
        if w >= 700:
            sx, sy, sw, sh = 10, top, int(w * 0.55), h - top - 10
            cx, cy, cw, ch = sx + sw + 10, top, w - sw - 30, (h - top - 10) / 3
            charts = [(cx, cy + i * ch, cw, ch - 8) for i in range(3)]
        else:
            sx, sy, sw, sh = 10, top, w - 20, (h - top) * 0.5
            ch = (h - top - sh - 10) / 3
            charts = [(10, sy + sh + 6 + i * ch, w - 20, ch - 6) for i in range(3)]
        if sw > 40 and sh > 40:
            self._scene(c, sx, sy, sw, sh, s)
        titles = ["tilt: true and estimated", "control effort (motor volts)", "position"]
        for strip, (x, y, cw_, ch_), tt in zip((self.tilt, self.effort, self.pos), charts, titles):
            if cw_ > 40 and ch_ > 20:
                strip.draw(c, x, y, cw_, ch_, tt)

    def _scene(self, c, x0, y0, sw, sh, s):
        c.create_rectangle(x0, y0, x0 + sw, y0 + sh, outline=GRID)
        ppm = min(sh / 0.34, sw / 0.6)                  # pixels per metre
        gy = y0 + sh - 30                               # the ground line
        half = sw / 2 / ppm                             # metres from centre to edge
        # the camera recentres when the robot nears an edge, as the OLED's does,
        # so the ground ticks visibly slide under a drifting robot
        if abs(s["x"] - self.cam) > half * 0.7:
            self.cam = s["x"]

        def px(xm):
            return x0 + sw / 2 + (xm - self.cam) * ppm

        c.create_line(x0, gy, x0 + sw, gy, fill=FG, width=2)
        k = math.floor((self.cam - half) * 10) - 1
        while True:
            gx = px(k / 10)
            if gx > x0 + sw:
                break
            if gx >= x0:
                big = k % 10 == 0
                c.create_line(gx, gy, gx, gy + (10 if big else 5), fill=DIM)
                if big:
                    c.create_text(gx, gy + 12, anchor="n", text=f"{k // 10} m", fill=DIM, font=FONT)
            k += 1

        ax = px(s["x"])
        r = WHEEL_R * ppm
        ay = gy - r                                     # the axle
        if not (x0 - r < ax < x0 + sw + r):
            return

        def body(th, **kw):
            th = max(-1.66, min(1.66, th))              # fallen: lying, not through the floor
            tx, ty = rot(0.0, -BODY_L * ppm, th)        # + tilt leans toward + x
            bw = 0.045 * ppm
            nx, ny = rot(bw / 2, 0.0, th)
            pts = [ax - nx, ay - ny, ax + nx, ay + ny, ax + tx + nx, ay + ty + ny, ax + tx - nx, ay + ty - ny]
            c.create_polygon(*pts, **kw)
            return ax + tx, ay + ty

        rad = math.radians
        # the estimate first, so the true body draws over it
        body(rad(s["est"]), fill="", outline="#ffb454", dash=(4, 3), width=2)
        tipx, tipy = body(rad(s["th"]), fill="#3b6f8f", outline="#5ec4ff", width=2)
        if s["pay"] > 0.01:
            b = (0.02 + 0.05 * s["pay"] / 0.6) * ppm
            c.create_rectangle(tipx - b / 2, tipy - b, tipx + b / 2, tipy, fill="#9ad36a", outline="")
        c.create_oval(ax - r, ay - r, ax + r, ay + r, fill="#1c2329", outline=FG, width=2)
        spin = s["x"] / WHEEL_R                          # rolling: clockwise as x grows
        c.create_line(ax, ay, ax + r * 0.85 * math.cos(spin), ay + r * 0.85 * math.sin(spin), fill=FG, width=2)

        # the upright reference and the legend
        c.create_line(ax, ay, ax, ay - BODY_L * ppm * 1.1, fill=GRID, dash=(2, 4))
        c.create_text(x0 + 8, y0 + 6, anchor="nw", fill="#5ec4ff", font=FONT, text="body: true tilt")
        c.create_text(x0 + 8, y0 + 22, anchor="nw", fill="#ffb454", font=FONT, text="dashed: controller's estimate")
        if s["fallen"]:
            c.create_text(x0 + sw / 2, y0 + sh / 2 - 40, text="FALLEN  -  press a to stand it up",
                          fill="#f07178", font=FONT_BIG)
