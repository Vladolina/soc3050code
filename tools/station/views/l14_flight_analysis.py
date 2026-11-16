"""The dedicated post-flight analysis view for lesson 14."""

import math
from collections import deque

from .base import BG, COLOURS, DIM, FG, FONT, FONT_BIG, GRID, View
from tools.station.mission_recording import MissionRecording


class FlightAnalysis(View):
    name = "flight_analysis"
    title = "Lesson 14 - flight analysis"
    frames = ("POS", "TRU", "MIS", "EVT")
    help = "mission summary, trajectory review, chart history and export"
    layout = "view"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.recording = MissionRecording(max_samples=20000)
        self.chart_names = ["Altitude", "Ground speed", "Battery", "Error", "Wind"]
        self.chart_index = 0
        self.samples = deque(maxlen=3000)
        self.events = deque(maxlen=500)
        self.mode = "--"
        self.pos = None
        self.truth = None
        self.wps = {}
        self.cur = -1
        self.bat = None
        self.wind = None
        canvas.bind("<Button-1>", self._next_chart, add="+")

    def _next_chart(self, _event=None):
        self.chart_index = (self.chart_index + 1) % len(self.chart_names)

    def _record_snapshot(self, name, values):
        row = {"timestamp": values.get("timestamp"), "mode": self.mode, "estimated_x": values.get("estimated_x"),
               "estimated_y": values.get("estimated_y"), "estimated_z": values.get("estimated_z"),
               "truth_x": values.get("truth_x"), "truth_y": values.get("truth_y"), "truth_z": values.get("truth_z"),
               "yaw": values.get("yaw"), "battery": self.bat, "wind": self.wind, "ground_speed": values.get("ground_speed"),
               "vertical_speed": values.get("vertical_speed"), "estimated_error": values.get("estimated_error")}
        self.recording.add_sample(row)

    def frame(self, name, fields, t):
        if name == "POS" and len(fields) >= 8:
            tm = fields[0]
            try:
                tm = float(tm)
            except ValueError:
                return
            self.mode = fields[1] if len(fields) > 1 else "--"
            x = float(fields[2]) / 10.0
            y = float(fields[3]) / 10.0
            z = float(fields[4]) / 10.0
            yaw = float(fields[5])
            self.cur = int(fields[6]) if fields[6] not in ("", "-1") else -1
            self.bat = float(fields[7]) if len(fields) > 7 else None
            self.pos = (x, y, z, yaw)
            self._record_snapshot("POS", {"timestamp": tm / 1000.0, "estimated_x": x, "estimated_y": y, "estimated_z": z,
                                          "yaw": yaw, "ground_speed": 0.0, "vertical_speed": 0.0,
                                          "estimated_error": None})
        elif name == "TRU" and len(fields) >= 5:
            x = float(fields[1]) / 100.0
            y = float(fields[2]) / 100.0
            z = float(fields[3]) / 100.0
            self.wind = float(fields[4]) / 10.0 if len(fields) > 4 else None
            self.truth = (x, y, z)
            last = self.recording.samples[-1] if self.recording.samples else None
            if last is not None:
                last["truth_x"] = x
                last["truth_y"] = y
                last["truth_z"] = z
                last["wind"] = self.wind
        elif name == "MIS" and len(fields) >= 5:
            i = int(fields[0]); n = int(fields[1]); x = float(fields[2]) / 10.0; y = float(fields[3]) / 10.0; z = float(fields[4]) / 10.0
            self.wps[i] = (x, y, z)
        elif name == "EVT" and len(fields) >= 3:
            ts = float(fields[0]) / 1000.0 if fields[0] else 0.0
            self.events.append((ts, fields[1], fields[2] if len(fields) > 2 else ""))

    def draw(self):
        c = self.c
        c.delete("all")
        c.configure(bg=BG)

        summary = self.recording.summary()
        top = 10
        left = 10
        width = max(140, (self.w - 40) / 4)
        cards = [
            ("Result", summary.get("mission_result", "--")),
            ("Duration", f"{summary.get('flight_duration', 0):.1f}s" if summary.get("flight_duration") is not None else "--"),
            ("Distance", f"{summary.get('total_distance', 0):.1f}m"),
            ("Max alt", f"{summary.get('max_altitude', 0):.1f}m" if summary.get("max_altitude") is not None else "--"),
        ]
        for i, (label, value) in enumerate(cards):
            x = left + i * width
            c.create_rectangle(x, top, x + width - 8, top + 50, outline=GRID, fill="#111820")
            c.create_text(x + 10, top + 8, anchor="nw", fill=DIM, font=FONT, text=label)
            c.create_text(x + 10, top + 26, anchor="nw", fill=FG, font=FONT_BIG, text=str(value))

        map_y = 70
        map_h = max(220, self.h * 0.42)
        map_w = self.w - 30
        c.create_rectangle(10, map_y, map_w, map_y + map_h, outline=GRID, fill="#111820")
        c.create_text(20, map_y + 8, anchor="nw", fill=FG, font=FONT_BIG, text="Trajectory")

        if self.pos or self.truth or self.wps:
            xs = []
            ys = []
            for row in self.recording.samples:
                if row.get("estimated_x") is not None and row.get("estimated_y") is not None:
                    xs.append(float(row["estimated_x"]))
                    ys.append(float(row["estimated_y"]))
                if row.get("truth_x") is not None and row.get("truth_y") is not None:
                    xs.append(float(row["truth_x"]))
                    ys.append(float(row["truth_y"]))
            for item in self.wps.values():
                xs.append(item[0]); ys.append(item[1])
            if xs and ys:
                min_x, max_x = min(xs), max(xs)
                min_y, max_y = min(ys), max(ys)
                span_x = max(1.0, max_x - min_x)
                span_y = max(1.0, max_y - min_y)
                for row in self.recording.samples:
                    ex = row.get("estimated_x")
                    ey = row.get("estimated_y")
                    if ex is not None and ey is not None:
                        px = 20 + (float(ex) - min_x) / span_x * (map_w - 40)
                        py = map_y + map_h - 20 - (float(ey) - min_y) / span_y * (map_h - 40)
                        c.create_oval(px - 1.5, py - 1.5, px + 1.5, py + 1.5, fill=COLOURS[0], outline="")
                    tx = row.get("truth_x"); ty = row.get("truth_y")
                    if tx is not None and ty is not None:
                        px = 20 + (float(tx) - min_x) / span_x * (map_w - 40)
                        py = map_y + map_h - 20 - (float(ty) - min_y) / span_y * (map_h - 40)
                        c.create_oval(px - 1.5, py - 1.5, px + 1.5, py + 1.5, fill=COLOURS[1], outline="")
                for idx, point in self.wps.items():
                    px = 20 + (point[0] - min_x) / span_x * (map_w - 40)
                    py = map_y + map_h - 20 - (point[1] - min_y) / span_y * (map_h - 40)
                    c.create_oval(px - 4, py - 4, px + 4, py + 4, fill=COLOURS[2], outline="")
                    c.create_text(px + 7, py - 8, anchor="nw", fill=FG, font=FONT, text=str(idx))

        chart_y = map_y + map_h + 15
        chart_h = max(120, self.h - chart_y - 30)
        c.create_rectangle(10, chart_y, self.w - 10, chart_y + chart_h, outline=GRID, fill="#111820")
        title = self.chart_names[self.chart_index]
        c.create_text(20, chart_y + 8, anchor="nw", fill=FG, font=FONT_BIG, text=title)
        c.create_text(self.w - 60, chart_y + 8, anchor="ne", fill=DIM, font=FONT, text="click to cycle")

        data = []
        for row in self.recording.samples:
            if title == "Altitude":
                value = row.get("estimated_z")
            elif title == "Ground speed":
                value = row.get("ground_speed")
            elif title == "Battery":
                value = row.get("battery")
            elif title == "Error":
                value = row.get("estimated_error")
            elif title == "Wind":
                value = row.get("wind")
            else:
                value = None
            if value is not None:
                data.append((row.get("timestamp", 0), float(value)))

        if data:
            min_t = min(v for v, _ in [(a, b) for a, b in []]) if False else min(t for t, _ in data)
            max_t = max(t for t, _ in data)
            min_v = min(v for _, v in data)
            max_v = max(v for _, v in data)
            if max_v == min_v:
                min_v -= 1.0; max_v += 1.0
            for idx in range(len(data) - 1):
                t1, v1 = data[idx]
                t2, v2 = data[idx + 1]
                x1 = 20 + (t1 - min_t) / max(1.0, max_t - min_t) * (self.w - 40)
                x2 = 20 + (t2 - min_t) / max(1.0, max_t - min_t) * (self.w - 40)
                y1 = chart_y + chart_h - 20 - (v1 - min_v) / max(1e-6, max_v - min_v) * (chart_h - 40)
                y2 = chart_y + chart_h - 20 - (v2 - min_v) / max(1e-6, max_v - min_v) * (chart_h - 40)
                c.create_line(x1, y1, x2, y2, fill=COLOURS[0], width=2)

        list_y = chart_y + chart_h + 10
        c.create_text(10, list_y, anchor="nw", fill=FG, font=FONT_BIG, text="Mission event report")
        event_y = list_y + 22
        for ts, kind, text in list(self.events)[-8:]:
            c.create_text(10, event_y, anchor="nw", fill=FG, font=FONT, text=f"{ts:5.1f}s  {kind}: {text[:50]}")
            event_y += 16


__all__ = ["FlightAnalysis"]
