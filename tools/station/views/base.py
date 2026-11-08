"""
base.py - what every station view is built from.

A VIEW is one lesson's picture of its own telemetry.  The station hands it
every checksum-verified $ frame and asks it to redraw about 30 times a second;
the view never touches the serial port.  To add one, drop a module in this
folder that defines a View subclass with `frames` set - views/__init__.py
finds it.  views/l13_attitude.py is a complete example, ~100 lines.

    class Attitude(View):
        name   = "attitude"                    # --view attitude
        title  = "Lesson 13 - attitude"
        frames = ("ATT", "EST")                # the first of these to arrive picks this view

        def frame(self, name, f, t):           # f: the fields after the name, as strings
            ...
        def draw(self):                        # self.c is a tkinter Canvas; self.w, self.h its size
            ...

Units stay the board's: if a frame says milli-degrees, the view divides.
"""

import math
import time
from collections import deque

BG = "#101418"
FG = "#d8dee9"
DIM = "#5c6773"
GRID = "#252c35"
COLOURS = ["#5ec4ff", "#ffb454", "#9ad36a", "#f07178", "#c792ea", "#e6db74", "#7fdbca", "#ff9e64"]
FONT = ("Consolas", 10)
FONT_BIG = ("Consolas", 16, "bold")


def num(s, default=None):
    """A field as a number: decimal ints and floats, or None."""
    try:
        return int(s)
    except ValueError:
        try:
            return float(s)
        except ValueError:
            return default


class View:
    name = "base"
    title = "view"
    frames = ()
    help = ""            # one line shown under the picture: what to press, what to look at
    layout = "oled"      # "oled": the OLED large above the view (the game IS the OLED)
                         # "view": this view gets the whole left side, the OLED goes small

    def __init__(self, canvas, station):
        self.c = canvas
        self.station = station            # .send_line(text), .send_key(key, down)
        self.w = int(canvas.winfo_width()) or 640
        self.h = int(canvas.winfo_height()) or 480

    def resize(self, w, h):
        self.w, self.h = w, h

    def frame(self, name, fields, t):
        pass

    def text(self, line):
        """A line the board printed that is not a $ frame ("failsafe: ...")."""

    def draw(self):
        pass

    def fnt(self, size, bold=False):
        """A font that grows with the picture, so a projector can read it:
        `size` is in points at a 1100 x 700 canvas."""
        k = max(0.8, min(2.4, min(self.w / 1100.0, self.h / 700.0)))
        return ("Consolas", max(8, int(size * k)), "bold") if bold else ("Consolas", max(8, int(size * k)))


class Strip:
    """A strip chart: the last `span` seconds of a few named series.

        s = Strip(["roll", "pitch"], span=10)
        s.add(t, roll=1.5, pitch=-0.2)          # t: time.monotonic()
        s.draw(canvas, x, y, w, h)              # autoscaled unless lo/hi given
    """

    def __init__(self, names, span=10.0, lo=None, hi=None, unit=""):
        self.names = list(names)
        self.span, self.lo, self.hi, self.unit = span, lo, hi, unit
        self.data = {n: deque() for n in self.names}

    def add(self, t, **values):
        for n, v in values.items():
            if n in self.data and v is not None:
                q = self.data[n]
                q.append((t, v))
                while q and q[0][0] < t - self.span:
                    q.popleft()

    def last(self, name):
        q = self.data.get(name)
        return q[-1][1] if q else None

    def draw(self, c, x, y, w, h, title="", font=FONT, width=2):
        c.create_rectangle(x, y, x + w, y + h, outline=GRID)
        now = time.monotonic()
        vals = [v for q in self.data.values() for t, v in q if now - t <= self.span]
        lo = self.lo if self.lo is not None else (min(vals) if vals else 0.0)
        hi = self.hi if self.hi is not None else (max(vals) if vals else 1.0)
        if hi - lo < 1e-9:
            lo, hi = lo - 1, hi + 1
        pad = (hi - lo) * 0.08 if self.lo is None else 0
        lo, hi = lo - pad, hi + pad
        if lo < 0 < hi:                               # the zero line
            zy = y + h - (0 - lo) / (hi - lo) * h
            c.create_line(x, zy, x + w, zy, fill=GRID, dash=(2, 3))
        c.create_text(x + 4, y + 2, anchor="nw", text=f"{hi:.4g}{self.unit}", fill=DIM, font=font)
        c.create_text(x + 4, y + h - 2, anchor="sw", text=f"{lo:.4g}{self.unit}", fill=DIM, font=font)
        if title:
            c.create_text(x + w / 2, y + 2, anchor="n", text=title, fill=FG, font=font)
        lx = x + w - 6
        for i, n in enumerate(self.names):
            q = self.data[n]
            col = COLOURS[i % len(COLOURS)]
            pts = []
            for t, v in q:
                if now - t > self.span:            # older than the window: off the left edge
                    continue
                pts += [x + w - (now - t) / self.span * w, y + h - (v - lo) / (hi - lo) * h]
            if len(pts) >= 4:
                c.create_line(*pts, fill=col, width=width)
            last = f" {q[-1][1]:.4g}" if q else ""
            step = int(font[1] * 1.6) if isinstance(font, tuple) else 14
            item = c.create_text(lx, y + h - 4 - step * i, anchor="se", text=f"{n}{last}", fill=col, font=font)
            del item


def rot(px, py, a):
    """(px, py) rotated by a radians."""
    ca, sa = math.cos(a), math.sin(a)
    return px * ca - py * sa, px * sa + py * ca
