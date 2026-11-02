"""
plot.py - the view for any lesson without its own: every numeric field of the
busiest $ frame, one strip chart each.  Click the picture to step to the next
frame name.
"""

from .base import BG, DIM, FG, FONT_BIG, Strip, View, num


class Plot(View):
    name = "plot"
    title = "Telemetry"
    frames = ()
    help = "click the picture to step through the frame names"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.strips = {}          # frame name -> (Strip, field names)
        self.counts = {}
        self.pick = None
        canvas.bind("<Button-1>", self._next, add="+")

    def _next(self, _e=None):
        names = sorted(self.strips)
        if names:
            i = names.index(self.pick) if self.pick in names else -1
            self.pick = names[(i + 1) % len(names)]

    def frame(self, name, f, t):
        if name in ("FB", "KEY", "MIRROR"):
            return
        vals = [num(x) for x in f]
        cols = [i for i, v in enumerate(vals) if v is not None]
        if not cols:
            return
        self.counts[name] = self.counts.get(name, 0) + 1
        if name not in self.strips:
            fields = [f"f{i + 1}" for i in cols[:8]]
            self.strips[name] = (Strip(fields, span=15), cols[:8])
        s, idx = self.strips[name]
        s.add(t, **{f"f{i + 1}": vals[i] for i in idx if i < len(vals) and vals[i] is not None})
        if self.pick is None or self.counts[name] > 3 * self.counts.get(self.pick, 0):
            self.pick = name

    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        if not self.pick:
            c.create_text(w / 2, h / 2, text="waiting for $ frames...", fill=DIM, font=FONT_BIG)
            return
        s, idx = self.strips[self.pick]
        c.create_text(10, 8, anchor="nw", fill=FG, font=FONT_BIG,
                      text=f"${self.pick}   ({len(self.strips)} frame name(s); click for the next)")
        n = len(s.names)
        top, gap = 40, 6
        ch = (h - top - gap * n) / max(n, 1)
        for i, fname in enumerate(s.names):
            one = Strip([fname], span=s.span)
            one.data[fname] = s.data[fname]
            one.draw(c, 10, top + i * (ch + gap), w - 20, ch, title=f"field {idx[i] + 1}")
