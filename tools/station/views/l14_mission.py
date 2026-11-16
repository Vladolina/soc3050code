"""l14_mission.py - lesson 14, drone missions: the flight seen from above.

This view keeps the original mission-map architecture, but improves the visual
polish and the live telemetry readout. It remains a Tkinter Canvas-based View and
uses the same incoming $POS, $TRU, $MIS and $EVT frames already used by the
base lesson.
"""

import math
from collections import deque

from .base import BG, COLOURS, DIM, FG, FONT, FONT_BIG, GRID, Strip, View, num

EST = COLOURS[0]
TRUE = COLOURS[1]
WPC = COLOURS[2]
FENCE = COLOURS[3]
HOME = COLOURS[5]
WARN = COLOURS[4]
ALERT = COLOURS[6]
TRACK = 3000

MODE_COLOUR = {
    "DISARMED": DIM,
    "TAKEOFF": COLOURS[5],
    "HOLD": COLOURS[4],
    "MISSION": COLOURS[2],
    "RTL": COLOURS[7],
    "LAND": COLOURS[6],
    "MANUAL": COLOURS[0],
    "AUTONOMOUS": COLOURS[2],
}


def _ok(*vals):
    return all(v is not None and math.isfinite(v) for v in vals)


class Mission(View):
    name = "mission"
    title = "Lesson 14 - drone missions"
    frames = ("POS", "TRU", "MIS", "EVT")
    help = "a = fly / resume, b = RTL, s = HOLD + arrows, 1-5 = wind, - + = cruise; quiet / loud"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.est = deque(maxlen=TRACK)
        self.tru = deque(maxlen=TRACK)
        self.pos = None
        self.truth = None
        self.mode = "?"
        self.reason = ""
        self.cur = -1
        self.bat = None
        self.wind = None
        self.t_ms = None
        self.wps = {}
        self.n_wp = 0
        self.fence_r = 50.0
        self.reached = set()
        self.log = deque(maxlen=20)
        self.alt = Strip(["est", "true"], span=60, lo=0, unit=" m")
        self.err = Strip(["est err"], span=60, lo=0, unit=" m")
        self.prev_pos = None
        self.prev_t = None
        self.ground_speed = None
        self.vertical_speed = None
        self.distance_home = None
        self.distance_next_wp = None
        self.last_event = ""
        self.events = []

    def _push_event(self, text, severity="info"):
        ts = self.t_ms / 1000.0 if isinstance(self.t_ms, (int, float)) else 0.0
        self.events.append((ts, text, severity))
        self.log.append((f"{ts:6.1f}s", text, severity))
        if len(self.events) > 50:
            self.events.pop(0)

    def frame(self, name, f, t):
        if name == "POS" and len(f) >= 8:
            tm = num(f[0])
            x = num(f[2])
            y = num(f[3])
            z = num(f[4])
            yaw = num(f[5])
            if not _ok(tm, x, y, z, yaw):
                return
            prev = self.pos
            self.t_ms = tm
            self.mode = f[1][:10] if len(f) > 1 else self.mode
            self.pos = (x / 10.0, y / 10.0, z / 10.0, math.radians(yaw))
            self.est.append(self.pos[:2])
            cur = num(f[6])
            bat = num(f[7])
            self.cur = cur if isinstance(cur, int) else -1
            self.bat = bat if _ok(bat) else None
            self.alt.add(t, est=self.pos[2])

            if prev is not None:
                dt = max(float(tm - self.t_ms) / 1000.0, 0.05) if self.t_ms is not None and tm is not None else 0.05
                if self.prev_pos is not None:
                    dx = self.pos[0] - self.prev_pos[0]
                    dy = self.pos[1] - self.prev_pos[1]
                    dz = self.pos[2] - self.prev_pos[2]
                    self.ground_speed = math.hypot(dx, dy) / max(dt, 0.05)
                    self.vertical_speed = dz / max(dt, 0.05)
                if self.truth is not None:
                    self.err.add(t, **{"est err": math.hypot(self.pos[0] - self.truth[0], self.pos[1] - self.truth[1])})
            self.prev_pos = self.pos[:3]
            self.distance_home = math.hypot(self.pos[0], self.pos[1]) if self.pos else None
            if self.wps:
                target = self.wps.get(self.cur)
                if target is not None:
                    self.distance_next_wp = math.hypot(self.pos[0] - target[0], self.pos[1] - target[1])
                else:
                    self.distance_next_wp = None

        elif name == "TRU" and len(f) >= 5:
            x = num(f[1])
            y = num(f[2])
            z = num(f[3])
            w = num(f[4])
            if not _ok(x, y, z):
                return
            self.truth = (x / 100.0, y / 100.0, z / 100.0)
            self.tru.append(self.truth[:2])
            self.wind = w / 10.0 if _ok(w) else None
            self.alt.add(t, true=self.truth[2])
            if self.pos:
                self.err.add(t, **{"est err": math.hypot(self.pos[0] - self.truth[0], self.pos[1] - self.truth[1])})

        elif name == "MIS" and len(f) >= 5:
            i = num(f[0])
            n = num(f[1])
            x = num(f[2])
            y = num(f[3])
            z = num(f[4])
            if not _ok(i, n, x, y, z) or not isinstance(i, int) or not 0 <= i < 64:
                return
            if i == 0:
                self.wps = {}
            self.n_wp = int(n)
            self.wps[i] = (x / 10.0, y / 10.0, z / 10.0)

        elif name == "EVT" and len(f) >= 3:
            tm = num(f[0])
            ts = f"{tm / 1000.0:7.1f}s" if isinstance(tm, (int, float)) else "      ?"
            kind = f[1]
            if kind == "MODE" and len(f) >= 4:
                self.mode, self.reason = f[2][:10], f[3][:10]
                self._push_event(f"mode {f[2]} ({f[3]})", "warning" if f[2] in {"HOLD", "RTL", "LAND"} else "info")
                if f[2] == "TAKEOFF":
                    self.est.clear()
                    self.tru.clear()
                    self.reached.clear()
            elif kind == "WP":
                i = num(f[2])
                if isinstance(i, int):
                    self.reached.add(i)
                self._push_event(f"waypoint {f[2]} reached", "info")
            elif kind == "MISSION":
                n = num(f[2])
                if isinstance(n, int):
                    self.n_wp = n
                    self.wps.clear()
                    self.reached.clear()
                self._push_event(f"mission {f[2]} started", "info")
            elif kind == "LOW_BAT":
                self._push_event("low battery warning", "warning")
            elif kind == "GPS":
                self._push_event("GPS failure", "warning")
            elif kind == "LAND":
                self._push_event("landing complete", "info")
            else:
                self._push_event(f"{kind} {f[2] if len(f) > 2 else ''}".strip(), "info")

        elif name == "PARAM" and len(f) >= 2 and f[0] == "FENCE_R":
            r = num(f[1])
            if _ok(r) and 0 < r < 100000:
                self.fence_r = float(r)
        elif name == "ACK" and "ERR" in f:
            self._push_event("refused: " + ",".join(f)[:30], "warning")

    def _draw_status(self, x, y, w):
        bar_h = 38
        c = self.c
        c.create_rectangle(x, y, x + w, y + bar_h, fill="#121a22", outline=GRID)
        status = [
            ("LINK", self.station.link.state if hasattr(self.station, "link") else "connected", "connected" if "connected" in str(self.station.link.state) else "warning"),
            ("MODE", self.mode, "ok"),
            ("MISS", f"{self.cur}/{self.n_wp}" if self.cur >= 0 else f"-/{self.n_wp}", "ok"),
            ("TIME", f"{self.t_ms / 1000.0:.1f}s" if isinstance(self.t_ms, (int, float)) else "--", "ok"),
            ("HOME", f"{self.distance_home:.1f}m" if self.distance_home is not None else "--", "ok"),
            ("WARN", "OK" if not self.reason else self.reason[:10], "warning" if self.reason else "ok"),
        ]
        cell = (w - 20) / len(status)
        for i, (label, val, kind) in enumerate(status):
            cx = x + 10 + i * cell
            c.create_text(cx, y + 8, anchor="nw", fill=DIM, font=FONT, text=label)
            c.create_text(cx, y + 20, anchor="nw", fill=FG if kind == "ok" else WARN, font=FONT_BIG, text=str(val))

    def _draw_map(self, x0, y0, s):
        c = self.c
        c.create_rectangle(x0, y0, x0 + s, y0 + s, outline=GRID, fill="#0b1015")
        pts = list(self.est) + list(self.tru) + [(v[0], v[1]) for v in self.wps.values()]
        reach = max([self.fence_r] + [math.hypot(px, py) for px, py in pts]) * 1.08 if pts else self.fence_r
        k = s / (2 * max(reach, 1.0))
        cx, cy = x0 + s / 2, y0 + s / 2

        def P(x, y):
            return cx + x * k, cy - y * k

        step = next((g for g in (5, 10, 20, 50, 100, 200, 500) if g * k >= 30), 1000)
        n = min(40, max(1, int(reach // step)))
        for i in range(-n, n + 1):
            gx, gy = cx + i * step * k, cy - i * step * k
            c.create_line(gx, y0, gx, y0 + s, fill=GRID)
            c.create_line(x0, gy, x0 + s, gy, fill=GRID)

        c.create_text(x0 + 10, y0 + 8, anchor="nw", fill=DIM, font=FONT, text=f"grid {step} m")
        c.create_text(x0 + s - 24, y0 + 10, anchor="n", fill=DIM, font=FONT, text="N")
        c.create_line(x0 + s - 18, y0 + 32, x0 + s - 18, y0 + 18, fill=DIM, arrow="last")

        r = self.fence_r * k
        c.create_oval(cx - r, cy - r, cx + r, cy + r, outline=FENCE, dash=(6, 4))

        hx, hy = P(0, 0)
        c.create_rectangle(hx - 6, hy - 6, hx + 6, hy + 6, outline=HOME, width=2)
        c.create_text(hx + 9, hy + 9, anchor="nw", fill=HOME, font=FONT, text="H")

        wl = [self.wps[i] for i in sorted(self.wps)]
        if wl:
            line = [0.0, 0.0] + [v for p in wl for v in p[:2]]
            c.create_line(*[q for i in range(0, len(line), 2) for q in P(line[i], line[i + 1])], fill=WPC, dash=(3, 3))
        for i in sorted(self.wps):
            x, y, z = self.wps[i]
            qx, qy = P(x, y)
            rr = 8 if i == self.cur else 6
            c.create_oval(qx - rr, qy - rr, qx + rr, qy + rr, outline=WPC, width=2 if i == self.cur else 1,
                          fill=WPC if i in self.reached else "")
            c.create_text(qx + 10, qy - 10, anchor="sw", fill=WPC, font=FONT, text=f"{i}")

        for track, col in ((self.tru, TRUE), (self.est, EST)):
            if len(track) >= 2:
                c.create_line(*[q for p in track for q in P(*p)], fill=col, width=2)

        if self.truth:
            tx, ty = P(*self.truth[:2])
            c.create_oval(tx - 4, ty - 4, tx + 4, ty + 4, fill=TRUE, outline="")
        if self.pos:
            dx, dy = P(*self.pos[:2])
            a = self.pos[3]
            c.create_oval(dx - 7, dy - 7, dx + 7, dy + 7, outline=EST, width=2)
            c.create_line(dx, dy, dx + 18 * math.cos(a), dy - 18 * math.sin(a), fill=EST, width=3, arrow="last")

        legend_y = y0 + 8
        for label, col in (("estimate", EST), ("truth", TRUE), ("waypoints", WPC), ("home", HOME), ("fence", FENCE)):
            c.create_line(x0 + 8, legend_y + 8, x0 + 26, legend_y + 8, fill=col, width=3)
            c.create_text(x0 + 32, legend_y, anchor="nw", fill=col, font=FONT, text=label)
            legend_y += 16

    def _draw_telemetry(self, x, y, w, h):
        c = self.c
        c.create_rectangle(x, y, x + w, y + h, outline=GRID, fill="#0d141b")
        c.create_text(x + 12, y + 8, anchor="nw", fill=FG, font=FONT_BIG, text="Telemetry")

        rows = [
            ("X", self.pos[0] if self.pos else None, "m"),
            ("Y", self.pos[1] if self.pos else None, "m"),
            ("Alt", self.pos[2] if self.pos else None, "m"),
            ("Yaw", math.degrees(self.pos[3]) if self.pos else None, "°"),
            ("Speed", self.ground_speed, "m/s"),
            ("VSpeed", self.vertical_speed, "m/s"),
            ("Home", self.distance_home, "m"),
            ("To WP", self.distance_next_wp, "m"),
        ]
        metrics = []
        for i, (label, value, unit) in enumerate(rows):
            metrics.append((label, value, unit))

        col_w = (w - 30) / 2
        cx = 0
        for i, (label, value, unit) in enumerate(metrics):
            xx = x + 12 + (i % 2) * col_w
            yy = y + 34 + (i // 2) * 54
            c.create_text(xx, yy, anchor="nw", fill=DIM, font=FONT, text=f"{label}")
            text = "--" if value is None else f"{value:.2f}" if isinstance(value, float) else str(value)
            c.create_text(xx, yy + 18, anchor="nw", fill=FG, font=FONT_BIG, text=f"{text}{unit}")

        battery_y = y + h - 110
        if self.bat is not None:
            b = max(0.0, min(100.0, float(self.bat)))
            c.create_text(x + 12, battery_y, anchor="nw", fill=FG, font=FONT, text=f"Battery {b:.0f}%")
            c.create_rectangle(x + 115, battery_y + 2, x + w - 12, battery_y + 14, outline=DIM)
            c.create_rectangle(x + 115, battery_y + 2, x + 115 + max(0.0, (w - 130) * b / 100.0), battery_y + 14,
                              fill=COLOURS[2] if b > 25 else WARN, outline="")
        else:
            c.create_text(x + 12, battery_y, anchor="nw", fill=DIM, font=FONT, text="Battery --")

        chart_y = y + h - 70
        c.create_text(x + 12, chart_y, anchor="nw", fill=DIM, font=FONT, text="Wind")
        wind_val = "--" if self.wind is None else f"{self.wind:.1f} m/s"
        c.create_text(x + 100, chart_y, anchor="nw", fill=FG, font=FONT_BIG, text=wind_val)
        if self.wind is not None:
            c.create_line(x + 180, y + h - 46, x + 220, y + h - 46, fill=COLOURS[0], width=2)

    def _draw_charts(self, x, y, w, h):
        c = self.c
        c.create_rectangle(x, y, x + w, y + h, outline=GRID, fill="#0d141b")
        c.create_text(x + 12, y + 8, anchor="nw", fill=FG, font=FONT_BIG, text="Live charts")

        series = {
            "alt": [(t, s[2]) for t, s in [(p.get("timestamp"), p) for p in list(self.est)[:0]]],
        }
        del series

        for i, label in enumerate(["Altitude", "Battery", "Error", "Wind"]):
            xx = x + 12 + (i % 2) * (w / 2 - 12)
            yy = y + 30 + (i // 2) * 86
            c.create_rectangle(xx, yy, xx + w / 2 - 20, yy + 70, outline=GRID)
            c.create_text(xx + 8, yy + 8, anchor="nw", fill=FG, font=FONT, text=label)

    def _draw_events(self, x, y, w, h):
        c = self.c
        c.create_rectangle(x, y, x + w, y + h, outline=GRID, fill="#0d141b")
        c.create_text(x + 12, y + 8, anchor="nw", fill=FG, font=FONT_BIG, text="Flight events")
        start = max(0, len(self.log) - 10)
        for i, (ts, text, colr) in enumerate(list(self.log)[start:], start=1):
            color = FG if colr == "info" else WARN if colr == "warning" else ALERT
            c.create_text(x + 12, y + 28 + i * 14, anchor="nw", fill=color, font=FONT, text=f"{ts}  {text}")

    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)

        status_h = 50
        self._draw_status(10, 10, w - 20)

        map_x, map_y = 10, 70
        map_w = w * 0.62
        map_h = h - 170
        self._draw_map(map_x, map_y, int(map_w - 20))

        telemetry_x = map_x + int(map_w - 20) + 20
        telemetry_w = max(230, w - telemetry_x - 20)
        self._draw_telemetry(telemetry_x, map_y, telemetry_w, map_h)

        event_y = map_y + map_h + 10
        event_h = max(70, h - event_y - 10)
        self._draw_events(10, event_y, w - 20, event_h)

        if not self.est and not self.tru and not self.wps:
            c.create_text(w / 2, h / 2, fill=DIM, font=FONT_BIG, text="waiting for mission telemetry…")


__all__ = ["Mission"]
