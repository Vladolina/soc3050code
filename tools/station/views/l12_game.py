"""
l12_game.py - lesson 12, the arcade.  The game itself is on the OLED mirror;
this view is the profiler: $PERF as charts, and every $SCORE.

    $PERF,<fps>,<ups>,<flush_us>,<max_us>,<bytes/frame>,<busy %>,<DIRTY|FULL>
    $SCORE,<game>,<score>
"""

from .base import BG, DIM, FG, FONT, FONT_BIG, Strip, View, num


class Game(View):
    name = "game"
    title = "Lesson 12 - arcade profiler"
    frames = ("PERF", "SCORE")
    help = "arrows = stick, a = A, p = pause, s = DIRTY/FULL, 1-5 = speed, - + = pace"

    def __init__(self, canvas, station):
        super().__init__(canvas, station)
        self.rates = Strip(["fps", "ups"], span=30, lo=0, hi=60)
        self.flush = Strip(["flush_us", "max_us"], span=30, lo=0, unit=" us")
        self.busy = Strip(["busy %"], span=30, lo=0, hi=100)
        self.mode = "?"
        self.bytes = None
        self.scores = []

    def frame(self, name, f, t):
        if name == "PERF" and len(f) >= 7:
            self.rates.add(t, fps=num(f[0]), ups=num(f[1]))
            self.flush.add(t, flush_us=num(f[2]), max_us=num(f[3]))
            self.busy.add(t, **{"busy %": num(f[5])})
            self.bytes, self.mode = num(f[4]), f[6]
        elif name == "SCORE" and len(f) >= 2:
            self.scores.append((f[0], f[1]))
            self.scores = self.scores[-12:]

    def draw(self):
        c, w, h = self.c, self.w, self.h
        c.delete("all")
        c.configure(bg=BG)
        c.create_text(10, 8, anchor="nw", fill=FG, font=FONT_BIG,
                      text=f"mode {self.mode}   {self.bytes if self.bytes is not None else '-'} B/frame")
        sw = w - 200 if w > 500 else w - 20
        ch = (h - 60) / 3
        self.rates.draw(c, 10, 40, sw, ch - 8, "frames and updates per second")
        self.flush.draw(c, 10, 40 + ch, sw, ch - 8, "oled_flush() time")
        self.busy.draw(c, 10, 40 + 2 * ch, sw, ch - 8, "CPU busy")
        if w > 500:
            x = w - 180
            c.create_text(x, 40, anchor="nw", fill=FG, font=FONT_BIG, text="$SCORE")
            for i, (g, s) in enumerate(reversed(self.scores)):
                c.create_text(x, 70 + 18 * i, anchor="nw", font=FONT,
                              fill=FG if i == 0 else DIM, text=f"{g:<9}{s:>6}")
