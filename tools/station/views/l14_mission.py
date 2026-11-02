"""
l14_mission.py - lesson 14, drone missions: the flight seen from above.

The OLED's map is 64 x 64 pixels at 1.6 m a pixel; this one is the PC's
window.  Two tracks, because the lesson is about the difference: where the
drone BELIEVES it is ($POS, the flight computer's estimate from late, noisy
GPS) and where it really is ($TRU, the simulator's truth inside the chip).

    $POS,t_ms,mode,x_dm,y_dm,alt_dm,yaw_deg,wp,bat      5 Hz   estimate; wp -1 = no mission
    $TRU,t_ms,x_cm,y_cm,alt_cm,wind_dms                 5 Hz   truth; wind in dm/s
    $EVT,t_ms,MODE,name,reason | $EVT,t_ms,WP,idx | $EVT,t_ms,MISSION,count
    $MIS,idx,count,x_dm,y_dm,alt_dm                     on START: the mission flown
    $ACK,...   $PARAM,NAME,value                        replies (FENCE_R is in m)

x is east, y north, of home; yaw is counter-clockwise from east (mavlite.c,
world.c).  "quiet" stops $POS and $TRU, so the map stops too; "loud" resumes.
"""

import math
from collections import deque

from .base import BG, COLOURS, DIM, FG, FONT, FONT_BIG, GRID, Strip, View, num

EST, TRUE, WPC, FENCE, HOME = COLOURS[0], COLOURS[1], COLOURS[2], COLOURS[3], COLOURS[5]
MODE_COLOUR = {"DISARMED": DIM, "TAKEOFF": COLOURS[5], "HOLD": COLOURS[4],
               "MISSION": COLOURS[2], "RTL": COLOURS[7], "LAND": COLOURS[6]}
TRACK = 3000                      # points kept per track: 10 minutes at 5 Hz


def _ok(*vals):
    """Every field parsed, and finite: num() turns "nan" and "inf" into floats."""
    return all(v is not None and math.isfinite(v) for v in vals)


class Mission(View):
    name = "mission"
    title = "Lesson 14 - drone missions"
    frames = ("POS", "TRU", "MIS", "EVT")
    help = "a = fly / resume, b = RTL, s = HOLD + arrows, 1-5 = wind, - + = cruise; quiet / loud"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.est = deque(maxlen=TRACK)        # (x, y) m
        self.tru = deque(maxlen=TRACK)
        self.pos = None                       # (x, y, alt, yaw_rad)
        self.truth = None                     # (x, y, alt)
        self.mode, self.reason = "?", ""
        self.cur, self.bat, self.wind = -1, None, None
        self.t_ms = None
        self.wps = {}                         # idx -> (x, y, alt) m
        self.n_wp = 0
        self.fence_r = 50.0                   # m, flight.c's default
        self.reached = set()
        self.log = deque(maxlen=14)
        self.alt = Strip(["est", "true"], span=60, lo=0, unit=" m")
        self.err = Strip(["est err"], span=60, lo=0, unit=" m")

    # ---- frames -----------------------------------------------------------
    def frame(self, name, f, t):
        if name == "POS" and len(f) >= 8:
            tm, x, y, z, yaw = num(f[0]), num(f[2]), num(f[3]), num(f[4]), num(f[5])
            if not _ok(tm, x, y, z, yaw):
                return
            self.t_ms, self.mode = tm, f[1][:10]
            self.pos = (x / 10.0, y / 10.0, z / 10.0, math.radians(yaw))
            self.est.append(self.pos[:2])
            cur, bat = num(f[6]), num(f[7])
            self.cur = cur if isinstance(cur, int) else -1
            self.bat = bat if _ok(bat) else None
            self.alt.add(t, est=self.pos[2])
        elif name == "TRU" and len(f) >= 5:
            x, y, z, w = num(f[1]), num(f[2]), num(f[3]), num(f[4])
            if not _ok(x, y, z):
                return
            self.truth = (x / 100.0, y / 100.0, z / 100.0)
            self.tru.append(self.truth[:2])
            self.wind = w / 10.0 if _ok(w) else None
            self.alt.add(t, true=self.truth[2])
            if self.pos:                       # $POS went out just before, same tick
                self.err.add(t, **{"est err": math.hypot(self.pos[0] - self.truth[0],
                                                         self.pos[1] - self.truth[1])})
        elif name == "MIS" and len(f) >= 5:
            i, n, x, y, z = (num(v) for v in f[:5])
            if not _ok(i, n, x, y, z) or not isinstance(i, int) or not 0 <= i < 64:
                return
            if i == 0:
                self.wps = {}
            self.n_wp = n
            self.wps[i] = (x / 10.0, y / 10.0, z / 10.0)
        elif name == "EVT" and len(f) >= 3:
            tm = num(f[0])
            ts = f"{tm / 1000.0:7.1f}s" if isinstance(tm, (int, float)) else "      ?"
            kind = f[1]
            if kind == "MODE" and len(f) >= 4:
                self.mode, self.reason = f[2][:10], f[3][:10]
                self._log(ts, f"{f[2]:<8} ({f[3]})", MODE_COLOUR.get(f[2], FG))
                if f[2] == "TAKEOFF":                  # a new flight: a clean map
                    self.est.clear()
                    self.tru.clear()
                    self.reached.clear()
            elif kind == "WP":
                i = num(f[2])
                if isinstance(i, int):
                    self.reached.add(i)
                self._log(ts, f"waypoint {f[2]} reached", WPC)
            elif kind == "MISSION":
                n = num(f[2])
                if isinstance(n, int):
                    self.n_wp = n
                    self.wps = {}                      # the $MIS lines follow
                    self.reached.clear()
                self._log(ts, f"mission of {f[2]} started", FG)
        elif name == "PARAM" and len(f) >= 2 and f[0] == "FENCE_R":
            r = num(f[1])
            if _ok(r) and 0 < r < 100000:
                self.fence_r = float(r)
        elif name == "ACK" and "ERR" in f:
            self._log("       ", "refused: " + ",".join(f)[:30], COLOURS[3])

    def _log(self, ts, text, col):
        self.log.append((ts, text, col))

    # ---- picture ----------------------------------------------------------
    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        side = max(120, min(h - 20, int(w * 0.62)))
        self._map(10, 10, side)
        px = 10 + side + 14
        pw = w - px - 10
        if pw >= 160:
            self._panel(px, 10, pw, h - 20)

    def _map(self, x0, y0, s):
        c = self.c
        c.create_rectangle(x0, y0, x0 + s, y0 + s, outline=GRID)
        pts = list(self.est) + list(self.tru) + [(v[0], v[1]) for v in self.wps.values()]
        reach = max([self.fence_r] + [math.hypot(px, py) for px, py in pts]) * 1.08
        k = s / (2 * reach)                                  # pixels per metre
        cx, cy = x0 + s / 2, y0 + s / 2

        def P(x, y):
            return cx + x * k, cy - y * k                    # north is up

        step = next((g for g in (5, 10, 20, 50, 100, 200, 500) if g * k >= 40), 1000)
        n = min(40, int(reach // step))                      # a garbled 1e9 m: no million lines
        for i in range(-n, n + 1):                           # the grid, every `step` m
            gx, gy = cx + i * step * k, cy - i * step * k
            c.create_line(gx, y0, gx, y0 + s, fill=GRID)
            c.create_line(x0, gy, x0 + s, gy, fill=GRID)
        c.create_text(x0 + 6, y0 + s - 6, anchor="sw", fill=DIM, font=FONT, text=f"grid {step} m")
        c.create_text(x0 + s - 10, y0 + 8, anchor="n", fill=DIM, font=FONT, text="N")
        c.create_line(x0 + s - 10, y0 + 40, x0 + s - 10, y0 + 22, fill=DIM, arrow="last")

        r = self.fence_r * k                                  # the geofence
        c.create_oval(cx - r, cy - r, cx + r, cy + r, outline=FENCE, dash=(6, 4))
        c.create_text(cx, cy - r - 2, anchor="s", fill=FENCE, font=FONT, text=f"fence {self.fence_r:.0f} m")

        hx, hy = P(0, 0)                                     # home
        c.create_rectangle(hx - 6, hy - 6, hx + 6, hy + 6, outline=HOME, width=2)
        c.create_text(hx + 9, hy + 9, anchor="nw", fill=HOME, font=FONT, text="H")

        wl = [self.wps[i] for i in sorted(self.wps)]          # the mission
        if wl:
            line = [0.0, 0.0] + [v for p in wl for v in p[:2]]
            c.create_line(*[q for i in range(0, len(line), 2) for q in P(line[i], line[i + 1])],
                          fill=WPC, dash=(3, 3))
        for i in sorted(self.wps):
            x, y, z = self.wps[i]
            qx, qy = P(x, y)
            cur = i == self.cur
            rr = 9 if cur else 6
            c.create_oval(qx - rr, qy - rr, qx + rr, qy + rr, outline=WPC,
                          width=3 if cur else 1, fill=WPC if i in self.reached else "")
            c.create_text(qx + 11, qy - 11, anchor="sw", fill=WPC, font=FONT, text=f"{i}  {z:.0f} m")

        for track, col in ((self.tru, TRUE), (self.est, EST)):
            if len(track) >= 2:
                c.create_line(*[q for p in track for q in P(*p)], fill=col, width=2)

        if self.truth:
            tx, ty = P(*self.truth[:2])
            c.create_oval(tx - 4, ty - 4, tx + 4, ty + 4, fill=TRUE, outline="")
        if self.pos:                                          # the drone, facing its yaw
            dx, dy = P(*self.pos[:2])
            a = self.pos[3]
            c.create_oval(dx - 7, dy - 7, dx + 7, dy + 7, outline=EST, width=2)
            c.create_line(dx, dy, dx + 22 * math.cos(a), dy - 22 * math.sin(a),
                          fill=EST, width=3, arrow="last")

        ly = y0 + 8
        for text, col in (("estimate ($POS)", EST), ("truth ($TRU)", TRUE), ("waypoints ($MIS)", WPC)):
            c.create_line(x0 + 8, ly + 6, x0 + 28, ly + 6, fill=col, width=3)
            c.create_text(x0 + 34, ly, anchor="nw", fill=col, font=FONT, text=text)
            ly += 16
        if not wl and not self.est:
            c.create_text(cx, cy + s / 4, fill=DIM, font=FONT,
                          text="press a to fly the built-in mission, or send mission.py's frames")

    def _panel(self, x, y, w, h):
        c = self.c
        col = MODE_COLOUR.get(self.mode, FG)
        c.create_text(x, y, anchor="nw", fill=col, font=FONT_BIG, text=self.mode)
        if self.reason:
            c.create_text(x + w, y + 4, anchor="ne", fill=DIM, font=FONT, text=f"why: {self.reason}")
        y += 30
        wp = f"WP {self.cur}/{self.n_wp}" if self.cur >= 0 else f"WP -/{self.n_wp}"
        t = f"t {self.t_ms / 1000.0:.1f} s" if isinstance(self.t_ms, (int, float)) else "t -"
        c.create_text(x, y, anchor="nw", fill=FG, font=FONT, text=f"{wp:<10}{t}")
        y += 18
        alt = f"{self.pos[2]:.1f} m" if self.pos else "-"
        wind = f"{self.wind:.1f} m/s" if self.wind is not None else "-"
        c.create_text(x, y, anchor="nw", fill=FG, font=FONT, text=f"alt {alt:<10}wind {wind}")
        y += 20
        if isinstance(self.bat, (int, float)):                # battery bar
            b = max(0.0, min(100.0, float(self.bat)))
            bc = COLOURS[3] if b <= 25 else COLOURS[2]
            c.create_text(x, y, anchor="nw", fill=FG, font=FONT, text=f"bat {b:.0f}%")
            c.create_rectangle(x + 70, y + 2, x + w, y + 14, outline=DIM)
            c.create_rectangle(x + 70, y + 2, x + 70 + (w - 70) * b / 100.0, y + 14, outline="", fill=bc)
        y += 24
        ch = max(60, (h - (y - 10)) // 3 - 8)
        self.alt.draw(c, x, y, w, ch, "altitude: estimate vs true")
        y += ch + 8
        self.err.draw(c, x, y, w, ch, "horizontal estimate error")
        y += ch + 10
        c.create_text(x, y, anchor="nw", fill=FG, font=FONT, text="events ($EVT)")
        y += 18
        rows = max(0, int((10 + h - y) // 15))
        for ts, text, colr in list(self.log)[-rows:] if rows else []:
            c.create_text(x, y, anchor="nw", fill=colr, font=FONT, text=f"{ts}  {text}")
            y += 15
