"""
l13_attitude.py - lesson 13, the drone on a gimbal.  An artificial horizon the
size of the window: what the controller BELIEVES (the filled sky and ground),
what the world really did (dashed white), and what the pilot asked for
(dotted magenta), with the four motors and strip charts beside it.

    $ATT,<ms>,<roll>,<pitch>,<roll_sp>,<pitch_sp>,<roll_true>,<pitch_true>,
         <m0>,<m1>,<m2>,<m3>,<armed>,<kp x100>               SIM mode, 25 Hz
    $EST,<ms>,<roll>,<pitch>,<roll_acc>,<pitch_acc>          MPU mode, 25 Hz

Angles are 0.1 degree (Main.c sends rad * DEG * 10), motors 0..1000 (x1000),
kp is the rate-loop gain scale x100.  Signs are FRD: roll > 0 = right side
down, pitch > 0 = nose up.  Motors: M0 FL, M1 FR, M2 RR, M3 RL.
"""

import math
import time

from .base import BG, COLOURS, DIM, FG, FONT, FONT_BIG, GRID, Strip, View, num

SKY = "#2a5f8f"
GROUND = "#7a5532"
TRUE_COL = "#ffffff"
SP_COL = "#ff6ad5"
ACC_COL = "#e6db74"
PLANE = "#ffd700"
MOTOR_NAMES = ("M0 FL", "M1 FR", "M2 RR", "M3 RL")


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
    help = ("arrows = stick, a = arm, b = gust, s = SIM/MPU, 1-5 = Kp;  "
            "type kick, hold, alpha 980, aw 0, mix -1, quiet / loud")

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
        self.roll_s = Strip(["est", "true", "sp"], span=self.SPAN, unit=" deg")
        self.pitch_s = Strip(["est", "true", "sp"], span=self.SPAN, unit=" deg")
        self.err_s = Strip(["roll err", "pitch err"], span=self.SPAN, unit=" deg")
        self.mot_s = Strip(list(MOTOR_NAMES), span=self.SPAN, lo=0, hi=100, unit=" %")
        self.eroll_s = Strip(["est", "accel"], span=self.SPAN, unit=" deg")
        self.epitch_s = Strip(["est", "accel"], span=self.SPAN, unit=" deg")

    # ---- frames ------------------------------------------------------------
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
            self.err_s.add(t, **{"roll err": r - rt, "pitch err": p - pt})
            self.mot_s.add(t, **dict(zip(MOTOR_NAMES, self.motors)))
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
        self._line(cx, cy, half, k, self.roll, self.pitch, fill=FG, width=2)

        # pitch ladder on the estimate, every 10 degrees
        for lad in (-30, -20, -10, 10, 20, 30):
            dd = (self.pitch - lad) * k
            px, py = cx + nx * dd, cy + ny * dd
            L = s * (0.10 if lad % 20 else 0.16)
            ex, ey = math.cos(a) * L, -math.sin(a) * L
            if abs(px - cx) < half - 4 and abs(py - cy) < half - 4:
                c.create_line(px - ex, py - ey, px + ex, py + ey, fill=FG)
                c.create_text(px + ex * 1.3, py + ey * 1.3, text=str(abs(lad)), fill=FG, font=FONT)

        # roll scale: ticks that turn with the horizon, a fixed pointer
        r = half * 0.86
        for tick in (-60, -45, -30, -20, -10, 0, 10, 20, 30, 45, 60):
            ang = math.radians(tick - self.roll)
            ln = 14 if tick % 30 == 0 else 8
            x1, y1 = cx + r * math.sin(ang), cy - r * math.cos(ang)
            x2, y2 = cx + (r - ln) * math.sin(ang), cy - (r - ln) * math.cos(ang)
            c.create_line(x1, y1, x2, y2, fill=FG, width=2)
        c.create_polygon(cx, cy - r + 16, cx - 8, cy - r + 30, cx + 8, cy - r + 30, fill=PLANE)

        # the other opinions
        if self.true:
            self._line(cx, cy, half, k, *self.true, fill=TRUE_COL, width=2, dash=(10, 6))
        if self.sp:
            self._line(cx, cy, half, k, *self.sp, fill=SP_COL, width=2, dash=(2, 5))
        if self.acc:
            self._line(cx, cy, half, k, *self.acc, fill=ACC_COL, width=2, dash=(10, 6))

        # the aircraft, fixed in the middle
        wgt = s * 0.17
        c.create_line(cx - 2 * wgt, cy, cx - wgt * 0.6, cy, cx - wgt * 0.3, cy + wgt * 0.25, fill=PLANE, width=4)
        c.create_line(cx + 2 * wgt, cy, cx + wgt * 0.6, cy, cx + wgt * 0.3, cy + wgt * 0.25, fill=PLANE, width=4)
        c.create_oval(cx - 4, cy - 4, cx + 4, cy + 4, fill=PLANE, outline="")
        c.create_rectangle(cx - half, cy - half, cx + half, cy + half, outline=DIM, width=2)

        # legend
        items = [("estimate", FG, None)]
        if self.true:
            items.append(("true", TRUE_COL, (10, 6)))
        if self.sp:
            items.append(("setpoint", SP_COL, (2, 5)))
        if self.acc:
            items.append(("accel only", ACC_COL, (10, 6)))
        lx = x + 8
        for label, col, dash in items:
            ly = y + s - 14
            c.create_rectangle(lx - 3, ly - 9, lx + 26 + 8 * len(label), ly + 9, fill=BG, outline="")
            c.create_line(lx, ly, lx + 20, ly, fill=col, width=2, dash=dash or ())
            c.create_text(lx + 24, ly, anchor="w", text=label, fill=col, font=FONT)
            lx += 34 + 8 * len(label)

    # ---- the motors --------------------------------------------------------
    def _motors(self, x, y, w, h):
        c = self.c
        c.create_rectangle(x, y, x + w, y + h, outline=GRID)
        c.create_text(x + 6, y + 4, anchor="nw", text="motors, seen from above (nose up)", fill=DIM, font=FONT)
        if not self.motors:
            c.create_text(x + w / 2, y + h / 2, text="no motors in MPU mode", fill=DIM, font=FONT)
            return
        # FL FR / RL RR, matching the OLED
        cells = ((0, 0, 0), (1, 1, 0), (2, 1, 1), (3, 0, 1))
        cw, chh = (w - 30) / 2.0, (h - 30) / 2.0
        for i, gx, gy in cells:
            bx, by = x + 10 + gx * (cw + 10), y + 22 + gy * (chh + 4)
            pct = self.motors[i]
            col = COLOURS[i]
            c.create_rectangle(bx, by, bx + cw, by + chh - 18, outline=col)
            c.create_rectangle(bx + 1, by + 1, bx + 1 + (cw - 2) * pct / 100.0, by + chh - 19, fill=col, outline="")
            c.create_text(bx, by + chh - 16, anchor="nw", text=f"{MOTOR_NAMES[i]}  {pct:5.1f} %", fill=col, font=FONT)

    # ---- the whole window --------------------------------------------------
    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        if self.mode is None:
            c.create_text(w / 2, h / 2, fill=DIM, font=FONT_BIG,
                          text="waiting for $ATT / $EST  (typed quiet? type loud)")
            return

        stale = self.t_last is None or time.monotonic() - self.t_last > 1.5
        if self.mode == "ATT":
            state = "ARMED" if self.armed else "disarmed"
            head = f"SIM   {state}   Kp x{self.kp:.2f}" if self.kp is not None else f"SIM   {state}"
        else:
            head = "MPU6050 - estimator only, nothing flies"
        if stale:
            head += "   (no telemetry - quiet?)"
        c.create_text(10, 6, anchor="nw", fill=FG, font=FONT_BIG, text=head)

        top = 36
        s = max(120, min(h - top - 10, int(w * 0.55)))
        self._horizon(10, top, s)

        # numbers under the title, right of the horizon
        x0 = 10 + s + 16
        rw = max(160, w - x0 - 10)
        lines = [f"estimate  roll {self.roll:7.1f}   pitch {self.pitch:7.1f}  deg"]
        if self.true:
            lines.append(f"true      roll {self.true[0]:7.1f}   pitch {self.true[1]:7.1f}")
            lines.append(f"est-true  roll {self.roll - self.true[0]:7.1f}   pitch {self.pitch - self.true[1]:7.1f}")
        if self.sp:
            lines.append(f"setpoint  roll {self.sp[0]:7.1f}   pitch {self.sp[1]:7.1f}")
        if self.acc:
            lines.append(f"accel     roll {self.acc[0]:7.1f}   pitch {self.acc[1]:7.1f}")
        for i, ln in enumerate(lines):
            c.create_text(x0, top + 16 * i, anchor="nw", fill=FG, font=FONT, text=ln)

        y = top + 16 * len(lines) + 8
        avail = top + s - y
        if self.mode == "ATT":
            mh = max(70, int(avail * 0.24))
            self._motors(x0, y, rw, mh)
            y += mh + 8
            ch = (top + s - y) / 3.0
            self.roll_s.draw(c, x0, y, rw, ch - 6, "roll (deg)")
            self.pitch_s.draw(c, x0, y + ch, rw, ch - 6, "pitch (deg)")
            self.err_s.draw(c, x0, y + 2 * ch, rw, ch - 6, "estimate - true (deg)")
        else:
            ch = avail / 2.0
            self.eroll_s.draw(c, x0, y, rw, ch - 6, "roll: filter vs accelerometer (deg)")
            self.epitch_s.draw(c, x0, y + ch, rw, ch - 6, "pitch: filter vs accelerometer (deg)")
