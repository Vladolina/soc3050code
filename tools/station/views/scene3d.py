"""
scene3d.py - just enough 3D for a station view, on a plain Tk canvas.

No OpenGL, no numpy: a pinhole camera, a few vectors and the painter's
algorithm (draw far things first).  A few hundred canvas items a frame at
30 Hz is well within Tk.  World axes: x east, y north, z up, metres.

    cam = Camera(pos=(0, -3, 1.5), pitch_deg=12)     # looks north, tipped down
    cam.fit(w, h)                                   # once per draw
    p = cam.project((x, y, z))                       # (sx, sy, depth) or None

Lesson 13's view (l13_attitude.py) is the first user; anything that moves in
a plane - lessons 14-17 - can reuse it.
"""

import math


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a, k):
    return (a[0] * k, a[1] * k, a[2] * k)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def norm(a):
    n = math.sqrt(dot(a, a))
    return mul(a, 1.0 / n) if n > 1e-12 else (0.0, 0.0, 0.0)


def attitude(roll_deg, pitch_deg, yaw_deg=0.0):
    """A function body -> world for an aircraft at this attitude.  Body axes
    here: x right, y forward, z up.  Signs as the board's FRD telemetry:
    roll > 0 = right side down, pitch > 0 = nose up, yaw > 0 = nose right."""
    r, p, yw = math.radians(roll_deg), math.radians(pitch_deg), math.radians(yaw_deg)
    cr, sr, cp, sp, cy, sy = math.cos(r), math.sin(r), math.cos(p), math.sin(p), math.cos(yw), math.sin(yw)

    def f(v):
        x, y, z = v
        x, z = x * cr + z * sr, -x * sr + z * cr          # roll about forward
        y, z = y * cp - z * sp, y * sp + z * cp           # pitch about right
        x, y = x * cy + y * sy, -x * sy + y * cy          # yaw about up
        return (x, y, z)
    return f


def blend(c1, c2, k):
    """Colour c1 -> c2 by k in 0..1, both '#rrggbb'."""
    k = max(0.0, min(1.0, k))
    a = [int(c1[i:i + 2], 16) for i in (1, 3, 5)]
    b = [int(c2[i:i + 2], 16) for i in (1, 3, 5)]
    return "#%02x%02x%02x" % tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))


class Camera:
    """A camera facing north (or any heading), tipped down by pitch_deg."""

    NEAR = 0.08

    def __init__(self, pos=(0.0, -3.0, 1.5), pitch_deg=12.0, heading_deg=0.0, fov_deg=62.0):
        self.pos, self.pitch_deg, self.heading_deg, self.fov_deg = pos, pitch_deg, heading_deg, fov_deg
        self.x0 = self.y0 = 0
        self.w, self.h = 640, 480
        self._axes()

    def _axes(self):
        c, s = math.radians(self.pitch_deg), math.radians(self.heading_deg)
        flat = (math.sin(s), math.cos(s), 0.0)
        self.right = (math.cos(s), -math.sin(s), 0.0)
        self.fwd = norm(add(mul(flat, math.cos(c)), (0.0, 0.0, -math.sin(c))))
        self.up = cross(self.right, self.fwd)

    def fit(self, w, h, x0=0, y0=0):
        """The canvas rectangle this camera draws into."""
        self.x0, self.y0, self.w, self.h = x0, y0, w, h
        self.f = (w / 2.0) / math.tan(math.radians(self.fov_deg) / 2.0)
        self.cx, self.cy = x0 + w / 2.0, y0 + h / 2.0
        self._axes()

    def to_cam(self, p):
        d = sub(p, self.pos)
        return dot(d, self.right), dot(d, self.up), dot(d, self.fwd)

    def project(self, p):
        """World point -> (sx, sy, depth), or None behind the near plane."""
        x, y, z = self.to_cam(p)
        if z < self.NEAR:
            return None
        return self.cx + self.f * x / z, self.cy - self.f * y / z, z

    def horizon_y(self):
        """Screen y of the horizon (flat ground, level camera roll)."""
        return self.cy - self.f * math.tan(math.radians(self.pitch_deg))

    def segment(self, a, b):
        """A 3D segment cut at the near plane and projected: (x1, y1, x2, y2,
        mean depth) or None."""
        ca, cb = self.to_cam(a), self.to_cam(b)
        n = self.NEAR
        if ca[2] < n and cb[2] < n:
            return None
        if ca[2] < n or cb[2] < n:
            k = (n - ca[2]) / (cb[2] - ca[2])
            cut = tuple(ca[i] + k * (cb[i] - ca[i]) for i in range(3))
            if ca[2] < n:
                ca = cut
            else:
                cb = cut
        f = self.f
        return (self.cx + f * ca[0] / ca[2], self.cy - f * ca[1] / ca[2],
                self.cx + f * cb[0] / cb[2], self.cy - f * cb[1] / cb[2], (ca[2] + cb[2]) / 2.0)

    def polyline(self, pts):
        """A 3D polyline as screen runs: a list of flat [x, y, x, y, ...] lists,
        broken wherever it passes behind the camera."""
        runs, cur = [], []
        for p in pts:
            q = self.project(p)
            if q is None:
                if len(cur) >= 4:
                    runs.append(cur)
                cur = []
            else:
                cur += [q[0], q[1]]
        if len(cur) >= 4:
            runs.append(cur)
        return runs


def circle(centre, u, v, r, n=24):
    """n points of a circle of radius r in the plane spanned by unit u, v."""
    return [add(centre, add(mul(u, r * math.cos(2 * math.pi * i / n)), mul(v, r * math.sin(2 * math.pi * i / n))))
            for i in range(n + 1)]
