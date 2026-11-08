"""
l13_attitude.py - lesson 13, the drone, drawn for a projector.

SIM mode (the board sends $ATT): a 3D chase view and a race.

  - the quad in 3D at the attitude the board's world REALLY has, each
    propeller disc lit by its motor's thrust, blades turning; a white
    wireframe at the attitude the ESTIMATOR believes - the gap between the
    two is the filter's error, visible;
  - a course of rings.  Arm (a) and the clock starts; fly through them in
    order with the arrows.  Every tilt is the board's: a sluggish gain, a
    gust (b) or an oscillating loop costs time, so tuning gets a score;
  - the trajectory: a fading trail in 3D and the whole path on a map;
  - the artificial horizon as a small instrument, the motors from above,
    strip charts of roll, pitch and estimate - truth.

  The drone on the board is on a gimbal: it tilts, it cannot travel.  The
  STATION turns tilt into travel (a = g tan(tilt), minus drag), using the
  board's own clock, and says so on screen.  Altitude is held at ring height.

MPU mode (the board sends $EST): the horizon, large, and filter-vs-
accelerometer charts - nothing flies, so there is nothing to race.

    $ATT,<ms>,<roll>,<pitch>,<roll_sp>,<pitch_sp>,<roll_true>,<pitch_true>,
         <m0>,<m1>,<m2>,<m3>,<armed>,<kp x100>               SIM mode, 25 Hz
    $EST,<ms>,<roll>,<pitch>,<roll_acc>,<pitch_acc>          MPU mode, 25 Hz

Angles are 0.1 degree (Main.c sends rad * DEG * 10), motors 0..1000 (x1000),
kp is the rate-loop gain scale x100.  Signs are FRD: roll > 0 = right side
down, pitch > 0 = nose up.  Motors: M0 FL, M1 FR, M2 RR, M3 RL.
"""

import math
import time
from collections import deque

from .base import BG, COLOURS, DIM, FG, GRID, Strip, View, num
from . import scene3d as s3

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

# ---- the 3D scene ----------------------------------------------------------
SKY_TOP, SKY_LOW = "#0b1a2e", "#4a7fae"
GRASS_NEAR, GRASS_FAR = "#24452c", "#3c5f4a"
GRID_NEAR, GRID_FAR = "#3f6a4a", "#46685a"
SHADOW = "#16301d"
RING_NEXT, RING_LATER, RING_DONE = "#ffb454", "#5ec4ff", "#4caf50"
TRAIL = "#7fdbca"
BODY = "#3a4250"
NOSE = "#ff7043"

# ---- the race (station physics, not the board's) ---------------------------
G = 9.81
DRAG = 0.7                           # 1/s: top speed at 20 deg ~ 5 m/s
TILT_CAP = 40.0                      # deg: tan() kept finite
ALT = 1.5                            # m: ring height, held
RING_R = 0.9                         # m: ring radius - generous
COURSE = ((0, 8), (4, 16), (-2, 25), (-7, 33), (-3, 42), (4, 49), (8, 58), (2, 66), (-4, 74), (0, 83))
TRAIL_S = 300.0                      # s of trajectory kept: the whole run, for the map
TRAIL_3D = 750                       # points of it drawn in 3D: the last 30 s at 25 Hz
ARM_A = 0.30                         # m: arm reach in the drawing (larger than the model's, to be seen)
SPOTS = ((-1, 1), (1, 1), (1, -1), (-1, -1))   # M0 FL, M1 FR, M2 RR, M3 RL: (right, forward)


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


class Race:
    """The station's half of the flight: tilt -> travel, the rings, the clock.
    Advanced by the BOARD's milliseconds, so a slow simulator slows it too."""

    def __init__(self):
        self.rings = []
        prev = (0.0, 0.0)
        for x, y in COURSE:
            n = s3.norm((x - prev[0], y - prev[1], 0.0))
            self.rings.append(((float(x), float(y), ALT), n))
            prev = (x, y)
        self.best = None
        self.reset()

    def reset(self):
        self.pos = (0.0, 0.0, 0.0)
        self.vel = (0.0, 0.0)
        self.next = 0
        self.t0 = None                       # board ms when the clock started
        self.t_now = None
        self.finished = None                 # seconds, once through the last ring
        self.trail = deque()                 # (board ms, (x, y, z))
        self.crash_until = None
        self.note = None                     # ("missed ring 3", until wall time)

    def elapsed(self):
        if self.finished is not None:
            return self.finished
        if self.t0 is None or self.t_now is None:
            return None
        return (self.t_now - self.t0) / 1000.0

    def step(self, ms, armed, roll, pitch):
        """One $ATT frame: ms = board clock, roll/pitch = TRUE attitude, deg."""
        dt = 0.0 if self.t_now is None else (ms - self.t_now) / 1000.0
        if dt < 0 or dt > 0.5:                  # board reset, or a long gap
            dt = 0.0
        self.t_now = ms
        x, y, z = self.pos
        vx, vy = self.vel
        now = time.monotonic()
        if self.crash_until is not None:        # falling, then back to the pad
            z = max(0.0, z - 6.0 * dt)
            vx *= 0.9
            vy *= 0.9
            if now > self.crash_until:
                self.reset()
                return
        elif armed:
            if self.t0 is None:
                self.t0 = ms
            z += (ALT - z) * min(1.0, dt / 0.5)   # climb to ring height, then hold
            if z > 0.3:
                r = math.radians(max(-TILT_CAP, min(TILT_CAP, roll)))
                p = math.radians(max(-TILT_CAP, min(TILT_CAP, pitch)))
                ax, ay = G * math.tan(r), -G * math.tan(p)    # roll right -> east; nose up -> back
                vx += (ax - DRAG * vx) * dt
                vy += (ay - DRAG * vy) * dt
        else:                                   # disarmed: settle to the ground
            z += (0.0 - z) * min(1.0, dt / 0.4)
            k = min(1.0, dt * 3.0)
            vx -= vx * k
            vy -= vy * k
        if z < 0.05:
            vx = vy = 0.0
        new = (x + vx * dt, y + vy * dt, z)
        if self.finished is None and self.t0 is not None and self.next < len(self.rings):
            self._gate(self.pos, new, now)
        self.pos, self.vel = new, (vx, vy)
        if dt > 0 and z > 0.05:
            self.trail.append((ms, new))
        while self.trail and self.trail[0][0] < ms - TRAIL_S * 1000:
            self.trail.popleft()

    def _gate(self, a, b, now):
        c, n = self.rings[self.next]
        s0, s1 = s3.dot(s3.sub(a, c), n), s3.dot(s3.sub(b, c), n)
        if not (s0 < 0.0 <= s1):
            return
        k = s0 / (s0 - s1)
        hit = s3.add(a, s3.mul(s3.sub(b, a), k))
        off = s3.sub(hit, c)
        miss = math.sqrt(max(0.0, s3.dot(off, off) - s3.dot(off, n) ** 2))
        if miss <= RING_R:
            self.next += 1
            if self.next == len(self.rings):
                self.finished = self.elapsed()
                if self.best is None or self.finished < self.best:
                    self.best = self.finished
        else:
            self.note = (f"missed ring {self.next + 1} by {miss - RING_R:.1f} m - go round", now + 2.5)

    def crash(self):
        if self.crash_until is None:
            self.crash_until = time.monotonic() + 2.0


class Attitude(View):
    name = "attitude"
    title = "Lesson 13 - drone attitude"
    frames = ("ATT", "EST")
    layout = "view"
    help = ("a = arm and start the clock, arrows = fly through the rings, b = gust, 1-5 = Kp;  "
            "type kick, hold, alpha 980, aw 0, mix -1, quiet / loud")
    KEYS = (("arrows", "fly"), ("a", "arm: clock starts"), ("b", "gust"),
            ("1-5", "Kp"), ("s", "SIM / MPU"), ("?", "keys"))

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
        self.race = Race()
        self.cam = s3.Camera(pitch_deg=13.0, fov_deg=64.0)
        self.cam_pos = None
        self.spin = [0.0, 0.0, 0.0, 0.0]     # propeller blade angles, radians
        self.t_draw = time.monotonic()

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
            was_armed = self.armed
            self.armed, self.kp = bool(v[11]), v[12] / 100.0
            if self.armed and not was_armed and self.race.crash_until is None:
                self.race.reset()                       # every arming is a new run
            self.race.step(v[0], self.armed, rt, pt)
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
            if s.startswith("CRASH"):
                self.race.crash()
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

    def _horizon(self, x, y, s, full=True):
        """The artificial horizon in an s x s box.  full=False: the small
        instrument inside the 3D view - no big numbers, no legend."""
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
        f_small = self.fnt(11 if full else 8)
        for lad in (-30, -20, -10, 10, 20, 30):
            dd = (self.pitch - lad) * k
            px, py = cx + nx * dd, cy + ny * dd
            L = s * (0.10 if lad % 20 else 0.16)
            ex, ey = math.cos(a) * L, -math.sin(a) * L
            if abs(px - cx) < half - 4 and abs(py - cy) < half - 4:
                c.create_line(px - ex, py - ey, px + ex, py + ey, fill=FG, width=lw)
                if full:
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
        pw = max(3, int(s / 140))
        c.create_line(cx - 2 * wgt, cy, cx - wgt * 0.6, cy, cx - wgt * 0.3, cy + wgt * 0.25, fill=PLANE, width=pw)
        c.create_line(cx + 2 * wgt, cy, cx + wgt * 0.6, cy, cx + wgt * 0.3, cy + wgt * 0.25, fill=PLANE, width=pw)
        c.create_oval(cx - pw, cy - pw, cx + pw, cy + pw, fill=PLANE, outline="")
        c.create_rectangle(cx - half, cy - half, cx + half, cy + half, outline=DIM, width=2)
        if not full:
            return

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
        for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):   # M0 FL, M1 FR, M2 RR, M3 RL
            c.create_line(cx, cy, cx + sx * arm, cy + sy * arm, fill=DIM, width=lw)
        c.create_rectangle(cx - arm * 0.25, cy - arm * 0.25, cx + arm * 0.25, cy + arm * 0.25, fill=GRID, outline=DIM)
        c.create_polygon(cx, cy - arm * 0.55, cx - arm * 0.12, cy - arm * 0.3, cx + arm * 0.12, cy - arm * 0.3,
                         fill=PLANE)                                  # the nose
        if not self.motors:
            c.create_text(cx, cy + arm + rr * 0.6, text="no motors in MPU mode", fill=DIM, font=self.fnt(12))
            return
        f_pct, f_name = self.fnt(14, bold=True), self.fnt(9)
        for i, (sx, sy) in enumerate(((-1, -1), (1, -1), (1, 1), (-1, 1))):
            mx, my = cx + sx * arm, cy + sy * arm
            pct = self.motors[i]
            col = COLOURS[i]
            c.create_oval(mx - rr, my - rr, mx + rr, my + rr, outline=col, width=lw)
            rf = rr * math.sqrt(max(0.0, pct) / 100.0)   # filled area = thrust
            if rf > 1:
                c.create_oval(mx - rf, my - rf, mx + rf, my + rf, fill=col, outline="")
            c.create_text(mx, my, text=f"{pct:.0f}%", fill=FG, font=f_pct)
            c.create_text(mx, my + sy * (rr + f_name[1] * 1.2), text=MOTOR_NAMES[i], fill=col, font=f_name)

    # ---- the 3D view -------------------------------------------------------
    def _ground(self, x0, y0, w, h):
        c, cam = self.c, self.cam
        hy = max(y0, min(y0 + h, cam.horizon_y()))
        bands = 8
        for i in range(bands):                       # sky: dark above, pale at the horizon
            ya, yb = y0 + (hy - y0) * i / bands, y0 + (hy - y0) * (i + 1) / bands
            c.create_rectangle(x0, ya, x0 + w, yb + 1, fill=s3.blend(SKY_TOP, SKY_LOW, (i + 0.5) / bands), outline="")
        for i in range(bands):                       # grass: haze toward the horizon
            ya, yb = hy + (y0 + h - hy) * i / bands, hy + (y0 + h - hy) * (i + 1) / bands
            c.create_rectangle(x0, ya, x0 + w, yb + 1, fill=s3.blend(GRASS_FAR, GRASS_NEAR, (i + 0.5) / bands),
                               outline="")
        px, py, _ = cam.pos
        step, reach = 2.0, 90.0
        gx0 = math.floor((px - 50) / step) * step
        for i in range(int(100 / step) + 1):         # north-south lines
            gx = gx0 + i * step
            seg = cam.segment((gx, py - 1.0, 0.0), (gx, py + reach, 0.0))
            if seg:
                c.create_line(*seg[:4], fill=GRID_NEAR)
        gy0 = math.ceil(py / step) * step
        for i in range(int(reach / step)):           # east-west lines, fading with distance
            gy = gy0 + i * step
            seg = cam.segment((px - 60, gy, 0.0), (px + 60, gy, 0.0))
            if seg:
                c.create_line(*seg[:4], fill=s3.blend(GRID_NEAR, GRID_FAR, (gy - py) / reach))
        pad = [cam.project(p) for p in ((-1, -1, 0), (1, -1, 0), (1, 1, 0), (-1, 1, 0))]   # the start pad
        if all(pad):
            c.create_polygon(*[v for q in pad for v in q[:2]], fill="#33404d", outline="#8fa3b5", width=2)
            q = cam.project((0, -1.3, 0))
            if q:
                c.create_text(q[0], q[1], text="START", fill="#c6d3df", font=self.fnt(10, bold=True))

    def _ring(self, i, centre, n):
        c, cam, race = self.c, self.cam, self.race
        q = cam.project(centre)
        if q is None:
            return
        depth = q[2]
        if depth < 1.6:                              # just passed, or about to: it would fill the screen
            return
        done, nxt = i < race.next, i == race.next and race.finished is None
        col = RING_DONE if done else RING_NEXT if nxt else RING_LATER
        u = s3.norm((-n[1], n[0], 0.0))
        pts = s3.circle(centre, u, (0.0, 0.0, 1.0), RING_R, 32)
        width = max(2, min(18, int(cam.f * (0.10 if nxt else 0.07) / depth)))
        post = cam.segment(s3.add(centre, (0, 0, -RING_R)), (centre[0], centre[1], 0.0))
        if post:
            c.create_line(*post[:4], fill=s3.blend(col, "#000000", 0.45), width=max(1, width // 2))
        for run in cam.polyline(pts):
            if nxt:
                c.create_line(*run, fill="#5a3500", width=width + 4)
            c.create_line(*run, fill=col, width=width)
        if not done and depth < 60:
            top = cam.project(s3.add(centre, (0, 0, RING_R + 0.35)))
            if top:
                size = max(8, min(28, int(cam.f * 0.32 / depth)))
                c.create_text(top[0], top[1], text=str(i + 1), fill=col, font=("Consolas", size, "bold"))

    def _drone(self, pos, roll, pitch, ghost=None):
        """The quad at pos.  ghost = (roll, pitch) of the estimate: drawn as a
        white wireframe over the solid one."""
        c, cam = self.c, self.cam
        R = s3.attitude(roll, pitch)

        def W(v):
            return s3.add(pos, R(v))

        # body: a flat box, faces facing the camera only, shaded by the light
        hx, hy_, hz = 0.10, 0.15, 0.045
        corners = [(sx * hx, sy * hy_, sz * hz) for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
        faces = (((0, 1, 3, 2), (-1, 0, 0)), ((4, 6, 7, 5), (1, 0, 0)), ((0, 4, 5, 1), (0, -1, 0)),
                 ((2, 3, 7, 6), (0, 1, 0)), ((0, 2, 6, 4), (0, 0, -1)), ((1, 5, 7, 3), (0, 0, 1)))
        light = s3.norm((-0.4, -0.5, 1.0))
        wc = [W(p) for p in corners]
        arms, props = [], []
        for i, (sx, sy) in enumerate(SPOTS):
            m = (sx * ARM_A, sy * ARM_A, 0.0)
            arms.append((W((0, 0, 0)), W(m)))
            props.append((i, W((m[0], m[1], 0.05)), m))
        lw = max(2, int(cam.f * 0.025 / max(0.3, s3.dot(s3.sub(pos, cam.pos), cam.fwd))))
        for a, b in arms:
            seg = cam.segment(a, b)
            if seg:
                c.create_line(*seg[:4], fill="#20262e", width=lw + 2)
        for idx, nrm in faces:
            wn = R(nrm)
            centre = s3.mul(s3.add(wc[idx[0]], wc[idx[2]]), 0.5)
            if s3.dot(wn, s3.sub(cam.pos, centre)) <= 0:
                continue
            pts = [cam.project(wc[k]) for k in idx]
            if not all(pts):
                continue
            base = NOSE if nrm == (0, 1, 0) else BODY
            shade = 0.35 + 0.65 * max(0.0, s3.dot(wn, light))
            c.create_polygon(*[v for q in pts for v in q[:2]], fill=s3.blend("#000000", base, shade),
                             outline="#11151a")
        props.sort(key=lambda t: -s3.dot(s3.sub(t[1], cam.pos), cam.fwd))
        ux, uy = R((1, 0, 0)), R((0, 1, 0))
        for i, hub, _m in props:
            pct = self.motors[i] / 100.0 if self.motors else 0.0
            col = COLOURS[i]
            ring = [cam.project(p) for p in s3.circle(hub, ux, uy, 0.19, 20)]
            if all(ring):
                c.create_polygon(*[v for q in ring for v in q[:2]], fill=s3.blend("#151a20", col, 0.15 + 0.75 * pct),
                                 outline=col, width=2)
            a = self.spin[i]
            for blade in (0.0, math.pi / 2):
                d = s3.add(s3.mul(ux, math.cos(a + blade) * 0.18), s3.mul(uy, math.sin(a + blade) * 0.18))
                seg = cam.segment(s3.sub(hub, d), s3.add(hub, d))
                if seg:
                    c.create_line(*seg[:4], fill="#e8eef4", width=max(1, lw - 1))
        if ghost:
            Rg = s3.attitude(*ghost)
            gx, gy = Rg((1, 0, 0)), Rg((0, 1, 0))
            for sx, sy in SPOTS:
                m = s3.add(pos, Rg((sx * ARM_A, sy * ARM_A, 0.0)))
                seg = cam.segment(pos, m)
                if seg:
                    c.create_line(*seg[:4], fill="#ffffff", width=2, dash=(4, 3))
                for run in cam.polyline(s3.circle(s3.add(m, Rg((0, 0, 0.05))), gx, gy, 0.19, 16)):
                    c.create_line(*run, fill="#ffffff", width=2)

    def _shadow(self, pos):
        c, cam = self.c, self.cam
        R = s3.attitude(*(self.true or (0.0, 0.0)))
        pts = []
        for sx, sy in ((-1, 1), (1, 1), (1, -1), (-1, -1)):
            p = s3.add(pos, R((sx * ARM_A * 1.5, sy * ARM_A * 1.5, 0.0)))
            q = cam.project((p[0], p[1], 0.005))
            if q is None:
                return
            pts += q[:2]
        c.create_polygon(*pts, fill=SHADOW, outline="")

    def _trail(self):
        c, cam, trail = self.c, self.cam, self.race.trail
        if len(trail) < 2:
            return
        pts = [p for _, p in list(trail)[-TRAIL_3D:]]
        chunks = 6
        n = len(pts)
        for k in range(chunks):                     # old = dim and thin, new = bright
            seg = pts[max(0, k * n // chunks - 1):(k + 1) * n // chunks]
            for run in cam.polyline(seg):
                c.create_line(*run, fill=s3.blend("#1d3a37", TRAIL, (k + 1) / chunks), width=1 + k // 2)

    def _scene(self, x0, y0, w, h):
        c, race, cam = self.c, self.race, self.cam
        now = time.monotonic()
        dt, self.t_draw = max(0.0, min(0.1, now - self.t_draw)), now
        pos = race.pos
        target = (pos[0], pos[1] - 2.4, max(0.7, pos[2] + 0.85))
        if self.cam_pos is None:
            self.cam_pos = target
        k = 1.0 - math.exp(-dt * 5.0)
        self.cam_pos = s3.add(self.cam_pos, s3.mul(s3.sub(target, self.cam_pos), k))
        cam.pos = self.cam_pos
        cam.fit(w, h, x0, y0)
        for i in range(4):                          # blades: faster with thrust; M0/M2 one way, M1/M3 the other
            pct = self.motors[i] / 100.0 if self.motors else 0.0
            self.spin[i] += dt * (4.0 + 50.0 * pct) * (1 if i in (0, 2) else -1)

        self._ground(x0, y0, w, h)
        self._shadow(pos)
        self._trail()
        dd = s3.dot(s3.sub(pos, cam.pos), cam.fwd)
        order = sorted(range(len(race.rings)), key=lambda i: -s3.dot(s3.sub(race.rings[i][0], cam.pos), cam.fwd))
        drone_drawn = False
        roll, pitch = self.true or (self.roll, self.pitch)
        for i in order:
            centre, n = race.rings[i]
            if not drone_drawn and s3.dot(s3.sub(centre, cam.pos), cam.fwd) < dd:
                self._drone(pos, roll, pitch, ghost=(self.roll, self.pitch))
                drone_drawn = True
            self._ring(i, centre, n)
        if not drone_drawn:
            self._drone(pos, roll, pitch, ghost=(self.roll, self.pitch))

        # ---- heads-up: rings, clock, best, speed
        fbig, fmid, fsm = self.fnt(24, bold=True), self.fnt(14), self.fnt(10)
        pad = 12
        total = len(race.rings)
        t = race.elapsed()
        lines = [(f"RING {min(race.next + 1, total)}/{total}" if race.finished is None else "FINISHED",
                  RING_NEXT if race.finished is None else RING_DONE)]
        c.create_text(x0 + pad, y0 + pad, anchor="nw", text=lines[0][0], fill=lines[0][1], font=fbig)
        clock = f"{t:6.2f} s" if t is not None else "  --.-- s"
        c.create_text(x0 + w / 2, y0 + pad, anchor="n", text=clock, fill="#ffffff", font=fbig)
        best = f"best {race.best:.2f} s" if race.best is not None else "best --"
        c.create_text(x0 + w - pad, y0 + pad, anchor="ne", text=best, fill=RING_DONE, font=fmid)
        spd = math.hypot(*race.vel)
        c.create_text(x0 + w - pad, y0 + pad + fmid[1] * 2.0, anchor="ne", text=f"{spd:4.1f} m/s",
                      fill=FG, font=fmid)

        msg = None
        if race.crash_until is not None:
            msg = ("CRASH - back to the start", ALARM)
        elif race.finished is not None:
            msg = (f"{race.finished:.2f} s" + ("  - best!" if race.best == race.finished else "")
                   + "   disarm (a) and arm again to fly again", OK_COL)
        elif not self.armed:
            msg = ("press a to arm - the clock starts - then fly through the rings with the arrows", OFF_COL)
        elif race.note and now < race.note[1]:
            msg = (race.note[0], MPU_COL)
        if msg:
            fm = self.fnt(15, bold=True)
            my = y0 + h * 0.62
            c.create_rectangle(x0 + w * 0.10, my - fm[1] * 2.0, x0 + w * 0.90, my + fm[1] * 2.0, fill=msg[1],
                               outline="")
            c.create_text(x0 + w / 2, my, text=msg[0], fill="#ffffff", font=fm, width=w * 0.76, justify="center")

        # the small horizon, bottom left; the honesty line, bottom right
        s = max(90, int(min(w, h) * 0.26))
        self._horizon(x0 + pad, y0 + h - s - pad - fsm[1] * 2, s, full=False)
        c.create_text(x0 + pad, y0 + h - pad, anchor="sw", fill=FG, font=fsm,
                      text=f"roll {self.roll:+.1f}°  pitch {self.pitch:+.1f}°")
        c.create_text(x0 + w - pad, y0 + h - pad, anchor="se", fill=DIM, font=fsm, justify="right",
                      text="solid quad: the true attitude    white wire: what the estimator believes\n"
                           "the board flies the attitude; the station turns tilt into travel")

    # ---- the trajectory from above ------------------------------------------
    def _map(self, x, y, w, h):
        c, race = self.c, self.race
        c.create_rectangle(x, y, x + w, y + h, fill="#0d1a14", outline=DIM)
        xs = [r[0][0] for r in race.rings] + [0.0]
        ys = [r[0][1] for r in race.rings] + [0.0]
        if race.trail:
            xs += [p[0] for _, p in race.trail]
            ys += [p[1] for _, p in race.trail]
        xs.append(race.pos[0])
        ys.append(race.pos[1])
        lo_x, hi_x, lo_y, hi_y = min(xs) - 3, max(xs) + 3, min(ys) - 3, max(ys) + 3
        k = min((w - 12) / (hi_x - lo_x), (h - 30) / (hi_y - lo_y))
        ox = x + w / 2 - (lo_x + hi_x) / 2 * k
        oy = y + 22 + (h - 30) / 2 + (lo_y + hi_y) / 2 * k

        def P(px, py):
            return ox + px * k, oy - py * k
        c.create_text(x + w / 2, y + 4, anchor="n", text="trajectory", fill=FG, font=self.fnt(10))
        sx, sy = P(0, 0)
        c.create_rectangle(sx - k, sy - k, sx + k, sy + k, outline="#8fa3b5")
        for i, (centre, n) in enumerate(race.rings):
            u = (-n[1], n[0])
            a = P(centre[0] - u[0] * RING_R, centre[1] - u[1] * RING_R)
            b = P(centre[0] + u[0] * RING_R, centre[1] + u[1] * RING_R)
            col = RING_DONE if i < race.next else RING_NEXT if i == race.next else RING_LATER
            c.create_line(*a, *b, fill=col, width=4 if i == race.next else 3)
        if len(race.trail) >= 2:
            pts = [v for _, p in race.trail for v in P(p[0], p[1])]
            c.create_line(*pts, fill=TRAIL, width=2)
        dx, dy = P(race.pos[0], race.pos[1])
        c.create_oval(dx - 5, dy - 5, dx + 5, dy + 5, fill=PLANE, outline="")

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

        fb = self.fnt(18, bold=True)
        bar_h = fb[1] * 2.6
        fk = self.fnt(12)
        key_h = fk[1] * 2.6
        top, bottom = bar_h + 6, h - key_h - 6
        avail = bottom - top
        fs = self.fnt(11)
        sw = int(w * 0.64)
        if self.mode == "ATT":
            # the 3D scene first, then everything outside it painted over:
            # a Tk canvas cannot clip, and ground lines run to the horizon
            self._scene(10, top, sw - 10, avail)
            for box in ((0, 0, w, top), (0, bottom, w, h), (0, 0, 10, h), (sw, 0, w, h)):
                c.create_rectangle(*box, fill=BG, outline="")
            c.create_rectangle(10, top, sw, bottom, outline=DIM, width=2)

        # ---- status bar
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
        kx, ky = 10, h - key_h + 4
        for key, what in self.KEYS:
            kw = fk[1] * 0.85 * len(key) + fk[1] * 1.2
            c.create_rectangle(kx, ky, kx + kw, ky + fk[1] * 1.9, fill=GRID, outline=DIM)
            c.create_text(kx + kw / 2, ky + fk[1] * 0.95, text=key, fill=FG, font=fk)
            c.create_text(kx + kw + 6, ky + fk[1] * 0.95, anchor="w", text=what, fill=DIM, font=fk)
            kx += kw + 12 + fk[1] * 0.85 * len(what) + fk[1]

        if self.mode == "ATT":
            # ---- trajectory map, motors and charts right of the 3D view
            x0 = sw + 12
            mw = max(90, int(w * 0.11))
            self._map(x0, top, mw, avail)
            x1 = x0 + mw + 12
            rw = max(150, w - x1 - 10)
            qh = min(rw * 0.8, avail * 0.30)
            self._quad(x1, top, rw, qh)
            y = top + qh + 8
            ch = (bottom - y) / 3.0
            self.roll_s.draw(c, x1, y, rw, ch - 8, "roll (°)", font=fs, width=2)
            self.pitch_s.draw(c, x1, y + ch, rw, ch - 8, "pitch (°)", font=fs, width=2)
            self.err_s.draw(c, x1, y + 2 * ch, rw, ch - 8, "estimate - true (°)", font=fs, width=2)
            sx, sw_b = 10, sw - 10                   # where a board event goes
        else:
            s = max(150, min(avail, int(w * 0.6)))
            self._horizon(10, top, s)
            x0 = 10 + s + 14
            rw = max(160, w - x0 - 10)
            ch = avail / 2.0
            self.eroll_s.draw(c, x0, top, rw, ch - 8, "roll: filter vs accelerometer (°)", font=fs, width=3)
            self.epitch_s.draw(c, x0, top + ch, rw, ch - 8, "pitch: filter vs accelerometer (°)", font=fs, width=3)
            sx, sw_b = 10, s

        # ---- a board event, across the picture
        if self.banner and time.monotonic() < self.banner[2]:
            text, col, _ = self.banner
            fbn = self.fnt(20, bold=True)
            by = top + avail * 0.45
            c.create_rectangle(sx, by - fbn[1] * 1.4, sx + sw_b, by + fbn[1] * 1.4, fill=col, outline="")
            c.create_text(sx + sw_b / 2, by, text=text, fill="#ffffff", font=fbn, width=sw_b - 20, justify="center")
