"""
l13_attitude.py - lesson 13, the drone on a gimbal, drawn for a projector.

The view takes the whole left side of the station (layout = "view"; the
OLED goes small, top right):

  - a status bar: SIM or MPU, ARMED / disarmed in a coloured chip, the gain;
  - an artificial horizon as tall as the window - what the controller
    BELIEVES (the filled sky and ground), what the world really did (dashed
    white), what the pilot asked for (dotted magenta) - with roll and pitch
    printed large on it;
  - the quad from above, each motor where it sits on the frame, filled by
    its thrust;
  - strip charts of roll, pitch and estimate - truth;
  - the board's events ("failsafe: ...", "CRASH ...") as a banner;
  - the keys, along the bottom.

    $ATT,<ms>,<roll>,<pitch>,<roll_sp>,<pitch_sp>,<roll_true>,<pitch_true>,
         <m0>,<m1>,<m2>,<m3>,<armed>,<kp x100>               SIM mode, 25 Hz
    $EST,<ms>,<roll>,<pitch>,<roll_acc>,<pitch_acc>          MPU mode, 25 Hz

Angles are 0.1 degree (Main.c sends rad * DEG * 10), motors 0..1000 (x1000),
kp is the rate-loop gain scale x100.  Signs are FRD: roll > 0 = right side
down, pitch > 0 = nose up.  Motors: M0 FL, M1 FR, M2 RR, M3 RL.
"""

import math
import time

from .base import BG, COLOURS, DIM, FG, GRID, Strip, View, num

SKY = "#2a5f8f"
GROUND = "#7a5532"
TRUE_COL = "#ffffff"
SP_COL = "#ff6ad5"
ACC_COL = "#e6db74"
PLANE = "#ffd700"
OK_COL = "#2e7d32"
OFF_COL = "#455a64"
MPU_COL = "#9e7d00"
ALARM = "#c62828"
MOTOR_NAMES = ("M0 FL", "M1 FR", "M2 RR", "M3 RL")
BANNER_S = 4.0                       # how long a board event stays on screen


def _fin(v):
    return v is not None and math.isfinite(v)


def _clip_poly(pts, nx, ny, d):
    """The part of polygon pts where nx*x + ny*y >= d (one half-plane)."""
    out = []
    for i, (x1, y1) in enumerate(pts):
        x2, y2 = pts[(i + 1) % len(pts)]
        s1, s2 = nx * x1 + ny * y1 - d, nx * x2 + ny * y2 - d
        if s1 >= 0:
            out.append((x1, y1))
        if (s1 >= 0) != (s2 >= 0):
            k = s1 / (s1 - s2)
            out.append((x1 + k * (x2 - x1), y1 + k * (y2 - y1)))
    return out


def _clip_line(px, py, dx, dy, x0, y0, x1, y1):
    """The infinite line p + t*d cut to the box, or None."""
    lo, hi = -1e9, 1e9
    for p, dd, a, b in ((px, dx, x0, x1), (py, dy, y0, y1)):
        if abs(dd) < 1e-12:
            if p < a or p > b:
                return None
            continue
        t1, t2 = (a - p) / dd, (b - p) / dd
        lo, hi = max(lo, min(t1, t2)), min(hi, max(t1, t2))
    if lo >= hi:
        return None
    return px + lo * dx, py + lo * dy, px + hi * dx, py + hi * dy


class Attitude(View):
    name = "attitude"
    title = "Lesson 13 - drone attitude"
    frames = ("ATT", "EST")
    layout = "view"
    help = ("arrows = stick, a = arm, b = gust, s = SIM/MPU, 1-5 = Kp;  "
            "type kick, hold, alpha 980, aw 0, mix -1, quiet / loud")
    KEYS = (("arrows", "tilt"), ("a", "arm / disarm"), ("b", "gust"),
            ("s", "SIM / MPU"), ("1-5", "Kp"), ("?", "keys"))

    SPAN = 15.0

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.mode = None                     # "ATT" (SIM) or "EST" (MPU)
        self.t_last = None
        self.roll = self.pitch = 0.0         # degrees, the estimate
        self.true = None                     # (roll, pitch), SIM only
        self.sp = None                       # (roll, pitch) setpoint, SIM only
        self.acc = None                      # (roll, pitch) accel-only, MPU only
        self.motors = None                   # four percentages
        self.armed = None
        self.kp = None
        self.board_ms = None
        self.banner = None                   # (text, colour, until)
        self.roll_s = Strip(["est", "true", "sp"], span=self.SPAN, unit="°")
        self.pitch_s = Strip(["est", "true", "sp"], span=self.SPAN, unit="°")
        self.err_s = Strip(["roll", "pitch"], span=self.SPAN, unit="°")
        self.eroll_s = Strip(["accel", "est"], span=self.SPAN, unit="°")
        self.epitch_s = Strip(["accel", "est"], span=self.SPAN, unit="°")

    # ---- what the board sends ----------------------------------------------
    def frame(self, name, f, t):
        if name == "ATT" and len(f) >= 13:
            v = [num(x) for x in f[:13]]
            if not all(_fin(x) for x in v):
                return
            self.board_ms = v[0]
            r, p, rs, ps, rt, pt = (x / 10.0 for x in v[1:7])
            self.mode, self.t_last = "ATT", t
            self.roll, self.pitch = r, p
            self.sp, self.true, self.acc = (rs, ps), (rt, pt), None
            self.motors = [min(max(x / 10.0, 0.0), 100.0) for x in v[7:11]]
            self.armed, self.kp = bool(v[11]), v[12] / 100.0
            self.roll_s.add(t, est=r, true=rt, sp=rs)
            self.pitch_s.add(t, est=p, true=pt, sp=ps)
            self.err_s.add(t, roll=r - rt, pitch=p - pt)
        elif name == "EST" and len(f) >= 5:
            v = [num(x) for x in f[:5]]
            if not all(_fin(x) for x in v):
                return
            self.board_ms = v[0]
            r, p, ra, pa = (x / 10.0 for x in v[1:5])
            self.mode, self.t_last = "EST", t
            self.roll, self.pitch = r, p
            self.acc, self.true, self.sp, self.motors = (ra, pa), None, None, None
            self.eroll_s.add(t, est=r, accel=ra)
            self.epitch_s.add(t, est=p, accel=pa)

    def text(self, line):
        """The board's own words, large: failsafes and crashes in red."""
        s = line.strip()
        if s.startswith("failsafe:") or s.startswith("CRASH"):
            self.banner = (s, ALARM, time.monotonic() + BANNER_S)
        elif s.startswith("mode:"):
            self.banner = (s, MPU_COL, time.monotonic() + BANNER_S)

    # ---- the horizon -------------------------------------------------------
    def _line(self, cx, cy, half, k, roll, pitch, **kw):
        """A horizon line for (roll, pitch) in degrees, cut to the box.  Roll
        right: the right end rises.  Pitch up: the line drops, k px/deg."""
        a = math.radians(roll)
        dx, dy = math.cos(a), -math.sin(a)          # along the horizon
        nx, ny = math.sin(a), math.cos(a)           # its normal, toward the ground
        d = max(-90.0, min(90.0, pitch)) * k
        seg = _clip_line(cx + nx * d, cy + ny * d, dx, dy, cx - half, cy - half, cx + half, cy + half)
        if seg:
            self.c.create_line(*seg, **kw)

    def _horizon(self, x, y, s):
        c = self.c
        half, cx, cy = s / 2.0, x + s / 2.0, y + s / 2.0
        k = s / 100.0                                # px per degree of pitch
        a = math.radians(self.roll)
        nx, ny = math.sin(a), math.cos(a)
        d = max(-90.0, min(90.0, self.pitch)) * k
        box = [(cx - half, cy - half), (cx + half, cy - half), (cx + half, cy + half), (cx - half, cy + half)]
        c.create_rectangle(cx - half, cy - half, cx + half, cy + half, fill=SKY, outline="")
        ground = _clip_poly(box, nx, ny, nx * cx + ny * cy + d)
        if len(ground) >= 3:
            c.create_polygon(*[v for pt in ground for v in pt], fill=GROUND, outline="")
        lw = max(2, int(s / 250))
        self._line(cx, cy, half, k, self.roll, self.pitch, fill=FG, width=lw + 1)

        # pitch ladder on the estimate, every 10 degrees
        f_small = self.fnt(11)
        for lad in (-30, -20, -10, 10, 20, 30):
            dd = (self.pitch - lad) * k
            px, py = cx + nx * dd, cy + ny * dd
            L = s * (0.10 if lad % 20 else 0.16)
            ex, ey = math.cos(a) * L, -math.sin(a) * L
            if abs(px - cx) < half - 4 and abs(py - cy) < half - 4:
                c.create_line(px - ex, py - ey, px + ex, py + ey, fill=FG, width=lw)
                c.create_text(px + ex * 1.35, py + ey * 1.35, text=str(abs(lad)), fill=FG, font=f_small)

        # roll scale: ticks that turn with the horizon, a fixed pointer
        r = half * 0.86
        for tick in (-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60):
            ang = math.radians(tick - self.roll)
            ln = s * (0.035 if tick % 30 == 0 else 0.02)
            x1, y1 = cx + r * math.sin(ang), cy - r * math.cos(ang)
            x2, y2 = cx + (r - ln) * math.sin(ang), cy - (r - ln) * math.cos(ang)
            c.create_line(x1, y1, x2, y2, fill=FG, width=lw)
        tp = s * 0.04
        c.create_polygon(cx, cy - r + tp * 0.5, cx - tp * 0.5, cy - r + tp * 1.4, cx + tp * 0.5, cy - r + tp * 1.4,
                         fill=PLANE)

        # the other opinions
        if self.true:
            self._line(cx, cy, half, k, *self.true, fill=TRUE_COL, width=lw, dash=(12, 8))
        if self.sp:
            self._line(cx, cy, half, k, *self.sp, fill=SP_COL, width=lw + 1, dash=(3, 6))
        if self.acc:
            self._line(cx, cy, half, k, *self.acc, fill=ACC_COL, width=lw, dash=(12, 8))

        # the aircraft, fixed in the middle
        wgt = s * 0.17
        pw = max(4, int(s / 140))
        c.create_line(cx - 2 * wgt, cy, cx - wgt * 0.6, cy, cx - wgt * 0.3, cy + wgt * 0.25, fill=PLANE, width=pw)
        c.create_line(cx + 2 * wgt, cy, cx + wgt * 0.6, cy, cx + wgt * 0.3, cy + wgt * 0.25, fill=PLANE, width=pw)
        c.create_oval(cx - pw, cy - pw, cx + pw, cy + pw, fill=PLANE, outline="")
        c.create_rectangle(cx - half, cy - half, cx + half, cy + half, outline=DIM, width=2)

        # roll and pitch, large, in the top corners of the instrument
        big, mid = self.fnt(30, bold=True), self.fnt(13)
        pad = s * 0.03
        for corner, label, est, tru in (("w", "ROLL", self.roll, self.true and self.true[0]),
                                        ("e", "PITCH", self.pitch, self.true and self.true[1])):
            tx = x + pad if corner == "w" else x + s - pad
            anchor = "n" + corner
            c.create_text(tx, y + pad, anchor=anchor, text=label, fill=FG, font=mid)
            c.create_text(tx, y + pad + mid[1] * 1.7, anchor=anchor, text=f"{est:+.1f}°", fill=FG, font=big)
            if tru is not None:
                c.create_text(tx, y + pad + mid[1] * 1.7 + big[1] * 1.7, anchor=anchor,
                              text=f"true {tru:+.1f}°", fill=TRUE_COL, font=mid)

        # legend, bottom of the instrument
        items = [("estimate", FG, None)]
        if self.true:
            items.append(("true", TRUE_COL, (12, 8)))
        if self.sp:
            items.append(("your stick", SP_COL, (3, 6)))
        if self.acc:
            items.append(("accel only", ACC_COL, (12, 8)))
        f_leg = self.fnt(12)
        cw = f_leg[1] * 0.85                         # roughly one character, px
        lx, ly = x + pad, y + s - pad - f_leg[1]
        for label, col, dash in items:
            wtxt = cw * len(label)
            c.create_rectangle(lx - 4, ly - f_leg[1], lx + 36 + wtxt + 8, ly + f_leg[1], fill=BG, outline="")
            c.create_line(lx, ly, lx + 30, ly, fill=col, width=lw + 1, dash=dash or ())
            c.create_text(lx + 36, ly, anchor="w", text=label, fill=col, font=f_leg)
            lx += 36 + wtxt + 26

    # ---- the quad from above ------------------------------------------------
    def _quad(self, x, y, w, h):
        """The X-quad seen from above, nose up: each motor where it sits on the
        frame, its disc filled by its thrust."""
        c = self.c
        cx, cy = x + w / 2.0, y + h / 2.0
        arm = min(w, h) * 0.28
        rr = min(w, h) * 0.15                        # motor disc radius
        lw = max(3, int(min(w, h) / 60))
        # body and arms
        spots = ((-1, -1), (1, -1), (1, 1), (-1, 1))   # M0 FL, M1 FR, M2 RR, M3 RL
        for sx, sy in spots:
            c.create_line(cx, cy, cx + sx * arm, cy + sy * arm, fill=DIM, width=lw)
        c.create_rectangle(cx - arm * 0.25, cy - arm * 0.25, cx + arm * 0.25, cy + arm * 0.25, fill=GRID, outline=DIM)
        c.create_polygon(cx, cy - arm * 0.55, cx - arm * 0.12, cy - arm * 0.3, cx + arm * 0.12, cy - arm * 0.3,
                         fill=PLANE)                                  # the nose
        if not self.motors:
            c.create_text(cx, cy + arm + rr * 0.6, text="no motors in MPU mode", fill=DIM, font=self.fnt(12))
            return
        f_pct, f_name = self.fnt(17, bold=True), self.fnt(10)
        for i, (sx, sy) in enumerate(spots):
            mx, my = cx + sx * arm, cy + sy * arm
            pct = self.motors[i]
            col = COLOURS[i]
            c.create_oval(mx - rr, my - rr, mx + rr, my + rr, outline=col, width=lw)
            rf = rr * math.sqrt(max(0.0, pct) / 100.0)   # filled area = thrust
            if rf > 1:
                c.create_oval(mx - rf, my - rf, mx + rf, my + rf, fill=col, outline="")
            c.create_text(mx, my, text=f"{pct:.0f}%", fill=FG, font=f_pct)
            c.create_text(mx, my + sy * (rr + f_name[1] * 1.2), text=MOTOR_NAMES[i], fill=col, font=f_name)

    # ---- the whole window --------------------------------------------------
    def _chip(self, x, y, text, bg, font):
        c = self.c
        w = font[1] * 0.85 * len(text) + font[1] * 1.6
        h = font[1] * 2.1
        c.create_rectangle(x, y, x + w, y + h, fill=bg, outline="")
        c.create_text(x + w / 2, y + h / 2, text=text, fill="#ffffff", font=font)
        return x + w + font[1]

    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        if self.mode is None:
            c.create_text(w / 2, h / 2, fill=DIM, font=self.fnt(20),
                          text="waiting for $ATT / $EST\n(start the simulation; typed quiet? type loud)",
                          justify="center")
            return

        # ---- status bar
        fb = self.fnt(18, bold=True)
        bar_h = fb[1] * 2.6
        stale = self.t_last is None or time.monotonic() - self.t_last > 1.5
        x = 10
        if self.mode == "ATT":
            x = self._chip(x, 8, "SIM", OFF_COL, fb)
            x = self._chip(x, 8, "ARMED" if self.armed else "DISARMED", OK_COL if self.armed else OFF_COL, fb)
            if self.kp is not None:
                c.create_text(x, 8 + fb[1] * 1.05, anchor="w", text=f"Kp ×{self.kp:.2f}", fill=FG, font=fb)
        else:
            x = self._chip(x, 8, "MPU6050", MPU_COL, fb)
            c.create_text(x, 8 + fb[1] * 1.05, anchor="w", text="estimator only - nothing flies",
                          fill=FG, font=self.fnt(14))
        right = (f"t {self.board_ms / 1000.0:.1f} s" if self.board_ms is not None else "")
        if stale:
            right = "no telemetry - typed quiet?   " + right
        c.create_text(w - 10, 8 + fb[1] * 1.05, anchor="e", text=right, fill=ALARM if stale else DIM,
                      font=self.fnt(13))

        # ---- the keys, bottom
        fk = self.fnt(12)
        key_h = fk[1] * 2.6
        kx, ky = 10, h - key_h + 4
        for key, what in self.KEYS:
            kw = fk[1] * 0.85 * len(key) + fk[1] * 1.2
            c.create_rectangle(kx, ky, kx + kw, ky + fk[1] * 1.9, fill=GRID, outline=DIM)
            c.create_text(kx + kw / 2, ky + fk[1] * 0.95, text=key, fill=FG, font=fk)
            c.create_text(kx + kw + 6, ky + fk[1] * 0.95, anchor="w", text=what, fill=DIM, font=fk)
            kx += kw + 12 + fk[1] * 0.85 * len(what) + fk[1]

        # ---- horizon left, quad and charts right
        top, bottom = bar_h + 6, h - key_h - 6
        s = max(150, min(bottom - top, int(w * 0.6)))
        self._horizon(10, top, s)
        x0 = 10 + s + 14
        rw = max(160, w - x0 - 10)
        avail = bottom - top
        fs = self.fnt(11)
        if self.mode == "ATT":
            qh = min(rw, avail * 0.42)
            self._quad(x0, top, rw, qh)
            y = top + qh + 8
            ch = (bottom - y) / 3.0
            self.roll_s.draw(c, x0, y, rw, ch - 8, "roll (°)", font=fs, width=3)
            self.pitch_s.draw(c, x0, y + ch, rw, ch - 8, "pitch (°)", font=fs, width=3)
            self.err_s.draw(c, x0, y + 2 * ch, rw, ch - 8, "estimate - true (°)", font=fs, width=3)
        else:
            ch = avail / 2.0
            self.eroll_s.draw(c, x0, top, rw, ch - 8, "roll: filter vs accelerometer (°)", font=fs, width=3)
            self.epitch_s.draw(c, x0, top + ch, rw, ch - 8, "pitch: filter vs accelerometer (°)", font=fs, width=3)

        # ---- a board event, across the instrument
        if self.banner and time.monotonic() < self.banner[2]:
            text, col, _ = self.banner
            fbn = self.fnt(20, bold=True)
            by = top + s * 0.62
            c.create_rectangle(10, by - fbn[1] * 1.4, 10 + s, by + fbn[1] * 1.4, fill=col, outline="")
            c.create_text(10 + s / 2, by, text=text, fill="#ffffff", font=fbn, width=s - 20, justify="center")
