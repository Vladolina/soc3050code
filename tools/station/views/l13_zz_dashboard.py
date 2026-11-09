"""Professional dashboard layout for the Lesson 13 attitude station.

This view deliberately subclasses the established attitude view: telemetry
parsing, input handling, race simulation, attitude transforms and 3D renderer
remain owned by l13_attitude.  No board protocol or firmware code changes.
"""

import time
from collections import deque

from .base import BG, DIM, FG, GRID
from .l13_attitude import Attitude as AttitudeView

# Restrained graphite/cyan aerospace palette.
PANEL = "#171e25"
PANEL_EDGE = "#303b46"
CYAN = "#50c8e8"
GREEN = "#62c98a"
AMBER = "#f2b45e"
RED = "#f06b70"
MUTED = "#81909e"
MOTOR_COLS = ("#50c8e8", "#f2b45e", "#62c98a", "#c792ea")
MOTOR_LABELS = ("MOTOR 1  /  FRONT LEFT", "MOTOR 2  /  FRONT RIGHT",
                "MOTOR 3  /  REAR RIGHT", "MOTOR 4  /  REAR LEFT")


class ProfessionalAttitude(AttitudeView):
    """Modern dashboard shell around the existing simulation and renderer."""

    # The views loader registers classes by name.  This late-sorting module
    # intentionally replaces the legacy attitude layout while inheriting its
    # parsing, race, keyboard controls, horizon and 3D scene unchanged.
    name = "attitude"
    title = "Lesson 13 — Flight telemetry"
    help = "arrows: pitch/roll   a: arm   b: gust   s: SIM/MPU   1–5: gain"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.frame_times = deque(maxlen=32)
        self.loop_hz = None

    def frame(self, name, fields, t):
        super().frame(name, fields, t)
        if name in self.frames and self.t_last == t:
            self.frame_times.append(t)
            if len(self.frame_times) >= 2:
                elapsed = self.frame_times[-1] - self.frame_times[0]
                if elapsed > 0:
                    self.loop_hz = (len(self.frame_times) - 1) / elapsed

    def _panel(self, x, y, w, h, title, accent=CYAN):
        c = self.c
        c.create_rectangle(x, y, x + w, y + h, fill=PANEL, outline=PANEL_EDGE, width=1)
        c.create_rectangle(x, y, x + 3, y + h, fill=accent, outline="")
        c.create_text(x + 12, y + 8, anchor="nw", text=title.upper(), fill=MUTED,
                      font=self.fnt(10, bold=True))

    def _metric(self, x, y, w, label, value, detail="", color=FG):
        c = self.c
        c.create_text(x + 10, y + 5, anchor="nw", text=label.upper(), fill=MUTED,
                      font=self.fnt(9, bold=True))
        c.create_text(x + 10, y + 23, anchor="nw", text=value, fill=color,
                      font=self.fnt(20, bold=True))
        if detail:
            c.create_text(x + 10, y + 48, anchor="nw", text=detail, fill=DIM, font=self.fnt(9))

    def _motor_panel(self, x, y, w, h):
        self._panel(x, y, w, h, "Motor outputs", CYAN)
        if self.motors is None:
            self.c.create_text(x + 12, y + 38, anchor="nw", text="No actuator telemetry in MPU mode",
                               fill=MUTED, font=self.fnt(10))
            return
        c = self.c
        top = y + 32
        row_h = max(31, (h - 38) / 4)
        bar_x = x + w * 0.53
        bar_w = max(20, w * 0.30)
        for i, pct in enumerate(self.motors):
            ry = top + row_h * i
            col = MOTOR_COLS[i]
            c.create_text(x + 12, ry + 2, anchor="nw", text=MOTOR_LABELS[i], fill=FG,
                          font=self.fnt(8, bold=True))
            c.create_rectangle(bar_x, ry + 5, bar_x + bar_w, ry + 15, fill="#0c1116", outline=PANEL_EDGE)
            if pct > 0:
                c.create_rectangle(bar_x + 1, ry + 6, bar_x + 1 + (bar_w - 2) * pct / 100.0,
                                   ry + 14, fill=col, outline="")
            c.create_text(x + w - 10, ry + 2, anchor="ne", text=f"{pct:5.1f}%", fill=FG,
                          font=self.fnt(10, bold=True))

    def _status_panel(self, x, y, w, h, stale):
        self._panel(x, y, w, h, "System status", GREEN if not stale else RED)
        c = self.c
        mode = "SIM / ATTITUDE" if self.mode == "ATT" else "MPU / ESTIMATOR"
        state = "ARMED" if self.armed else "DISARMED" if self.armed is not None else "UNKNOWN"
        state_col = GREEN if self.armed else MUTED
        if stale:
            link, link_col = "TELEMETRY STALE", RED
        else:
            link, link_col = "TELEMETRY LIVE", GREEN
        c.create_text(x + 12, y + 34, anchor="nw", text=mode, fill=CYAN, font=self.fnt(10, bold=True))
        c.create_text(x + 12, y + 57, anchor="nw", text=f"FLIGHT STATE   {state}", fill=state_col,
                      font=self.fnt(10, bold=True))
        c.create_text(x + 12, y + 80, anchor="nw", text=f"LINK   {link}", fill=link_col,
                      font=self.fnt(9, bold=True))
        hz = f"{self.loop_hz:.1f} Hz" if self.loop_hz is not None else "25 Hz nominal"
        c.create_text(x + w - 12, y + 34, anchor="ne", text=f"STREAM RATE  {hz}", fill=FG,
                      font=self.fnt(9))
        c.create_text(x + w - 12, y + 57, anchor="ne", text="YAW  NOT REPORTED", fill=MUTED,
                      font=self.fnt(9))
        fail = bool(self.banner and self.banner[0].lower().startswith("failsafe:"))
        c.create_text(x + w - 12, y + 80, anchor="ne", text="FAILSAFE  ACTIVE" if fail else "FAILSAFE  NONE REPORTED",
                      fill=RED if fail else MUTED, font=self.fnt(9, bold=fail))

    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        now = time.monotonic()
        stale = self.t_last is None or now - self.t_last > 1.5
        margin, header_h, footer_h, gap = 12, 52, 34, 10
        c.create_rectangle(0, 0, w, header_h, fill="#121920", outline="")
        c.create_text(margin, 9, anchor="nw", text="FLIGHT CONTROL  /  ATTITUDE MONITOR",
                      fill=FG, font=self.fnt(15, bold=True))
        c.create_text(margin, 32, anchor="nw", text="SOC3050  •  LAB 13  •  TELEMETRY VIEW",
                      fill=MUTED, font=self.fnt(8))
        badge = "LINK LOST" if stale else ("SIM" if self.mode == "ATT" else "MPU")
        badge_col = RED if stale else CYAN
        c.create_rectangle(w - 112, 13, w - 12, 39, fill=PANEL, outline=PANEL_EDGE)
        c.create_text(w - 62, 26, text=badge, fill=badge_col, font=self.fnt(10, bold=True))

        content_top, content_bottom = header_h + gap, h - footer_h
        avail_h = max(80, content_bottom - content_top)
        side_w = max(260, int(w * 0.30))
        scene_w = max(160, w - side_w - 3 * margin)
        scene_x, scene_y = margin, content_top
        self._panel(scene_x, scene_y, scene_w, avail_h, "Simulated flight scene", CYAN)
        # The existing 3D scene is retained as the central visual focus.
        self._scene(scene_x + 5, scene_y + 27, scene_w - 10, avail_h - 32)
        # Cover the original scene's own HUD text with a quiet caption strip.
        c.create_rectangle(scene_x + 5, content_bottom - 28, scene_x + scene_w - 5,
                           content_bottom - 2, fill="#121920", outline="")
        c.create_text(scene_x + 12, content_bottom - 15, anchor="w",
                      text="TRUE ATTITUDE: SOLID AIRFRAME     ESTIMATOR: WHITE WIRE OVERLAY",
                      fill=MUTED, font=self.fnt(8))
        c.create_text(scene_x + scene_w - 10, content_bottom - 15, anchor="e",
                      text="Position / speed are station-side race estimates", fill=MUTED, font=self.fnt(8))

        px = scene_x + scene_w + gap
        pw = max(120, w - px - margin)
        # A compact roll/pitch/clock block followed by instruments and motors.
        card_h = 70
        self._panel(px, content_top, pw, card_h, "Attitude / session", CYAN)
        col_w = pw / 3.0
        self._metric(px, content_top + 23, col_w, "Roll", f"{self.roll:+.1f}°",
                     f"true {self.true[0]:+.1f}°" if self.true else "true unavailable", CYAN)
        self._metric(px + col_w, content_top + 23, col_w, "Pitch", f"{self.pitch:+.1f}°",
                     f"true {self.true[1]:+.1f}°" if self.true else "true unavailable", CYAN)
        elapsed = self.race.elapsed()
        timer = f"{elapsed:0.1f}s" if elapsed is not None else "--.-s"
        self._metric(px + 2 * col_w, content_top + 23, col_w, "Flight timer", timer,
                     f"speed {__import__('math').hypot(*self.race.vel):.1f} m/s", FG)

        y = content_top + card_h + gap
        instrument_h = max(145, int(avail_h * 0.34))
        self._panel(px, y, pw, instrument_h, "Artificial horizon", AMBER)
        hs = max(80, min(int(pw * 0.72), instrument_h - 43))
        self._horizon(px + (pw - hs) / 2, y + 31, hs, full=False)
        c.create_text(px + 10, y + instrument_h - 9, anchor="sw",
                      text=f"EST {self.roll:+.1f}° / {self.pitch:+.1f}°",
                      fill=FG, font=self.fnt(8))
        c.create_text(px + pw - 10, y + instrument_h - 9, anchor="se",
                      text=f"SET {self.sp[0]:+.1f}° / {self.sp[1]:+.1f}°" if self.sp else "SETPOINT N/A",
                      fill=MUTED, font=self.fnt(8))

        y += instrument_h + gap
        remaining = max(80, content_bottom - y)
        motor_h = int(remaining * 0.56)
        self._motor_panel(px, y, pw, motor_h)
        y += motor_h + gap
        status_h = max(80, content_bottom - y)
        self._status_panel(px, y, pw, status_h, stale)

        c.create_rectangle(0, h - footer_h, w, h, fill="#121920", outline="")
        c.create_text(margin, h - footer_h / 2, anchor="w",
                      text="INPUT  ARROWS: PITCH / ROLL     A: ARM     B: GUST     S: SIM / MPU     1–5: GAIN",
                      fill=MUTED, font=self.fnt(9))
        c.create_text(w - margin, h - footer_h / 2, anchor="e",
                      text="Failsafe shown only when reported by board text; yaw is not in $ATT / $EST",
                      fill=DIM, font=self.fnt(8))
