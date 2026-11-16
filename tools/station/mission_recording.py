import csv
import math
from collections import deque
from pathlib import Path


class MissionRecording:
    """Lightweight mission recorder for lesson 14 station telemetry."""

    def __init__(self, max_samples: int = 20000):
        self.max_samples = max_samples
        self.samples = deque(maxlen=max_samples)
        self.events = deque(maxlen=2000)

    def add_sample(self, sample):
        if sample is None:
            return
        item = dict(sample)
        self.samples.append(item)

    def add_event(self, timestamp, description, severity="info", **extra):
        event = {"timestamp": timestamp, "description": description, "severity": severity}
        event.update(extra)
        self.events.append(event)

    def total_distance(self):
        total = 0.0
        prev = None
        for sample in self.samples:
            x = sample.get("estimated_x")
            y = sample.get("estimated_y")
            if x is None or y is None:
                continue
            if prev is not None:
                total += math.hypot(float(x) - prev[0], float(y) - prev[1])
            prev = (float(x), float(y))
        return total

    def summary(self):
        samples = list(self.samples)
        if not samples:
            return {
                "mission_result": "no-data",
                "flight_duration": 0.0,
                "total_distance": 0.0,
                "max_altitude": 0.0,
                "average_ground_speed": 0.0,
                "max_ground_speed": 0.0,
                "max_vertical_speed": 0.0,
                "start_battery": None,
                "ending_battery": None,
                "battery_consumption": 0.0,
                "landing_position": None,
            }

        start_ts = float(samples[0].get("timestamp", 0.0))
        end_ts = float(samples[-1].get("timestamp", 0.0))
        flight_duration = max(end_ts - start_ts, 0.0)

        values = [float(s["estimated_z"]) for s in samples if s.get("estimated_z") is not None]
        max_altitude = max(values) if values else 0.0

        ground_speeds = [float(s["ground_speed"]) for s in samples if s.get("ground_speed") is not None]
        vertical_speeds = [abs(float(s["vertical_speed"])) for s in samples if s.get("vertical_speed") is not None]
        avg_ground_speed = sum(ground_speeds) / len(ground_speeds) if ground_speeds else 0.0
        max_ground_speed = max(ground_speeds) if ground_speeds else 0.0
        max_vertical_speed = max(vertical_speeds) if vertical_speeds else 0.0

        battery_values = [float(s["battery"]) for s in samples if s.get("battery") is not None]
        start_battery = battery_values[0] if battery_values else None
        ending_battery = battery_values[-1] if battery_values else None
        battery_consumption = 0.0 if start_battery is None or ending_battery is None else max(0.0, start_battery - ending_battery)

        landing = None
        for sample in reversed(samples):
            if sample.get("estimated_x") is not None and sample.get("estimated_y") is not None:
                landing = (sample.get("estimated_x"), sample.get("estimated_y"))
                break

        mission_result = "completed"
        for event in self.events:
            description = (event.get("description") or "").lower()
            if "failed" in description:
                mission_result = "failed"
            elif "aborted" in description:
                mission_result = "aborted"
            elif "completed" in description:
                mission_result = "completed"

        return {
            "mission_result": mission_result,
            "flight_duration": flight_duration,
            "total_distance": self.total_distance(),
            "max_altitude": max_altitude,
            "average_ground_speed": avg_ground_speed,
            "max_ground_speed": max_ground_speed,
            "max_vertical_speed": max_vertical_speed,
            "start_battery": start_battery,
            "ending_battery": ending_battery,
            "battery_consumption": battery_consumption,
            "landing_position": landing,
        }

    def export_csv(self, telemetry_path, events_path=None, summary_path=None):
        telemetry_path = Path(telemetry_path)
        telemetry_path.parent.mkdir(parents=True, exist_ok=True)
        with telemetry_path.open("w", newline="", encoding="utf-8") as fh:
            writer = csv.DictWriter(fh, fieldnames=[
                "timestamp",
                "t_ms",
                "mode",
                "estimated_x",
                "estimated_y",
                "estimated_z",
                "truth_x",
                "truth_y",
                "truth_z",
                "yaw",
                "battery",
                "wind",
                "ground_speed",
                "vertical_speed",
                "estimated_error",
            ])
            writer.writeheader()
            for sample in self.samples:
                writer.writerow(sample)

        if events_path is not None:
            events_path = Path(events_path)
            events_path.parent.mkdir(parents=True, exist_ok=True)
            with events_path.open("w", newline="", encoding="utf-8") as fh:
                writer = csv.DictWriter(fh, fieldnames=["timestamp", "description", "severity", "kind", "value"])
                writer.writeheader()
                for event in self.events:
                    writer.writerow({
                        "timestamp": event.get("timestamp"),
                        "description": event.get("description"),
                        "severity": event.get("severity"),
                        "kind": event.get("kind"),
                        "value": event.get("value"),
                    })

        if summary_path is not None:
            summary_path = Path(summary_path)
            summary_path.parent.mkdir(parents=True, exist_ok=True)
            with summary_path.open("w", newline="", encoding="utf-8") as fh:
                writer = csv.writer(fh)
                writer.writerow(["metric", "value"])
                for key, value in self.summary().items():
                    writer.writerow([key, value])

        return telemetry_path

    def load_from_csv(self, telemetry_path):
        telemetry_path = Path(telemetry_path)
        if not telemetry_path.exists():
            return []
        self.samples.clear()
        with telemetry_path.open("r", newline="", encoding="utf-8") as fh:
            reader = csv.DictReader(fh)
            for row in reader:
                self.samples.append({k: (float(v) if v not in ("", "None", "null") else None) for k, v in row.items()})
        return list(self.samples)


__all__ = ["MissionRecording"]
