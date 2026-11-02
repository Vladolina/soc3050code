#!/usr/bin/env python3
"""
station.py - the ground station: the board's screen, keyboard and telemetry,
on the PC.

    python tools/station/station.py                         Wokwi in VS Code (the default port)
    python tools/station/station.py --port COM5             a real Nucleo on USB
    python tools/station/station.py --port socket://localhost:4001   tools/station/fakeboard.py

One window, three parts:

  the OLED      the board's 128 x 64 panel, drawn large.  The board sends it
                as $FB frames (_lib/mirror.c) while the station asks, with a
                $MIRROR,1 every second.
  the view      the lesson's own picture of its telemetry, at the PC's
                resolution - an artificial horizon, a map, a pendulum.  Chosen
                by the first $ frame that a view in views/ claims; the generic
                strip chart (views/plot.py) otherwise.
  the console   every line the board prints, and a command line for its shell.

Click the OLED or the view and the KEYBOARD belongs to the board: arrows are
the stick, a / b / p / s the buttons, 1-5 the knob, - and + whatever the lesson
says.  Each key is sent as $KEY,<key>,1 when pressed and $KEY,<key>,0 when
released - which a terminal cannot do - so a held key is simply held.  A held
key's down frame is repeated every 200 ms; if the window loses the keyboard,
every held key is released.  The board side is _lib/kbd.c.

Wokwi: the lesson's wokwi.toml must carry  rfc2217ServerPort = 4000  (all of
12-19 do).  Start the simulation first, or start this first - it keeps
retrying.  Python and pyserial come from the course's uv environment: run
uv sync once at the repository root, then  uv run tools/station/station.py.
"""

import argparse
import os
import queue
import sys
import threading
import time
import tkinter as tk

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

try:
    import serial  # pyserial, from the uv environment (pyproject.toml)
except ImportError:
    sys.exit("pyserial is missing: run  uv sync  at the repository root, then\n"
             "start the station with  uv run tools/station/station.py")

import views  # noqa: E402
from views.base import BG, DIM, FG, FONT  # noqa: E402

ACCENT = "#ffb454"
OLED_ON, OLED_OFF = "#9fe8ff", "#05080b"
HOLDABLE = {"UP", "DOWN", "LEFT", "RIGHT", "a", "b", "p", "s"}
KEY_CHARS = set("abps12345-+=?")
ARROWS = {"Up": "UP", "Down": "DOWN", "Left": "LEFT", "Right": "RIGHT"}
REPEAT_MS = 200


def checksum(body):
    x = 0
    for b in body.encode("ascii", "replace"):
        x ^= b
    return x


def make_frame(body):
    return f"${body}*{checksum(body):02X}"


def parse_frame(line):
    """'$NAME,a,b*CS' -> ['NAME', 'a', 'b'], or None if it is not a valid frame."""
    if not line.startswith("$") or "*" not in line:
        return None
    body, cs = line[1:].rsplit("*", 1)
    try:
        if int(cs[:2], 16) != checksum(body):
            return None
    except ValueError:
        return None
    return body.split(",")


class Link:
    """The serial port, on its own thread: lines in through a queue, bytes out."""

    def __init__(self, url, baud):
        self.url, self.baud = url, baud
        self.port = None
        self.lines = queue.Queue()
        self.lock = threading.Lock()
        self.rx_bytes = 0
        self.state = "connecting"
        threading.Thread(target=self._run, daemon=True).start()

    def _run(self):
        buf = b""
        while True:
            if self.port is None:
                try:
                    self.port = serial.serial_for_url(self.url, baudrate=self.baud, timeout=0.05)
                    self.state = "connected"
                    self.lines.put(("info", f"connected to {self.url}"))
                except Exception as e:  # noqa: BLE001 - any failure means "try again"
                    self.state = f"waiting for {self.url} ({type(e).__name__})"
                    time.sleep(1.0)
                    continue
            try:
                data = self.port.read(self.port.in_waiting or 1)
            except Exception as e:  # noqa: BLE001
                self.lines.put(("info", f"link lost: {e}"))
                self._drop()
                continue
            if not data:
                continue
            self.rx_bytes += len(data)
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                self.lines.put(("line", raw.decode("ascii", "replace").rstrip("\r")))

    def _drop(self):
        try:
            self.port.close()
        except Exception:  # noqa: BLE001
            pass
        self.port = None
        self.state = "reconnecting"
        time.sleep(1.0)

    def write(self, text):
        with self.lock:
            if self.port is None:
                return False
            try:
                self.port.write(text.encode("ascii", "replace"))
                return True
            except Exception:  # noqa: BLE001
                return False


class Oled:
    """The board's framebuffer as the $FB frames have drawn it: 8 pages x 128
    columns, a byte per column of 8 pixels, bit 0 on top (lesson 12 slide 3)."""

    def __init__(self):
        self.fb = [bytearray(128) for _ in range(8)]
        self.segments = 0
        self.dirty = True

    def apply(self, f):
        try:
            page, col, hexs = int(f[0]), int(f[1]), f[2]
            data = bytes.fromhex(hexs)
        except (ValueError, IndexError):
            return
        if 0 <= page < 8 and 0 <= col and col + len(data) <= 128:
            self.fb[page][col:col + len(data)] = data
            self.segments += 1
            self.dirty = True

    def rows(self):
        """The picture as Tk PhotoImage.put() data: 64 rows of 128 colours."""
        out = []
        for y in range(64):
            page, bit = self.fb[y >> 3], 1 << (y & 7)
            out.append("{" + " ".join(OLED_ON if page[x] & bit else OLED_OFF for x in range(128)) + "}")
        return " ".join(out)


class Station:
    def __init__(self, root, args):
        self.root, self.args = root, args
        self.link = Link(args.port, args.baud)
        self.oled = Oled()
        self.view = None
        self.forced = args.view
        self.held = {}                 # key -> time it was pressed
        self.release_pending = {}      # key -> after() id (X11 auto-repeat sends up+down pairs)
        self.seen = {}                 # frame name -> recent arrival times, for the console filter
        self.frames_in = 0
        self.view_fed = 0.0            # when the current view last got one of its own frames
        self.rate_t, self.rate_bytes, self.rate_frames, self.rate_segs = time.monotonic(), 0, 0, 0
        self.rate_text = ""
        self.history, self.hist_i = [], 0

        root.title("SOC3050 station")
        root.configure(bg=BG)
        root.geometry(args.geometry)

        s = args.scale
        left = tk.Frame(root, bg=BG)
        left.grid(row=0, column=0, sticky="nsew")
        right = tk.Frame(root, bg=BG)
        right.grid(row=0, column=1, sticky="nsew")
        root.columnconfigure(0, weight=3)
        root.columnconfigure(1, weight=2)
        root.rowconfigure(0, weight=1)

        self.oled_canvas = tk.Canvas(left, width=128 * s, height=64 * s, bg=OLED_OFF, highlightthickness=3,
                                     highlightbackground=BG, highlightcolor=ACCENT, takefocus=1)
        self.oled_canvas.pack(side="top", padx=8, pady=(8, 4))
        self.oled_img = tk.PhotoImage(width=128, height=64)
        self.oled_big = self.oled_img.zoom(s)
        self.oled_item = self.oled_canvas.create_image(3, 3, anchor="nw", image=self.oled_big)

        self.view_title = tk.Label(left, text="", bg=BG, fg=FG, font=FONT, anchor="w")
        self.view_title.pack(side="top", fill="x", padx=10)
        self.view_canvas = tk.Canvas(left, bg=BG, highlightthickness=3, highlightbackground=BG,
                                     highlightcolor=ACCENT, takefocus=1)
        self.view_canvas.pack(side="top", fill="both", expand=True, padx=8, pady=(0, 8))
        self.view_canvas.bind("<Configure>", lambda e: self.view and self.view.resize(e.width, e.height))

        self.status = tk.Label(right, text="", bg=BG, fg=DIM, font=FONT, anchor="w", justify="left")
        self.status.pack(side="top", fill="x", padx=6, pady=(8, 2))
        opts = tk.Frame(right, bg=BG)
        opts.pack(side="top", fill="x", padx=6)
        self.show_tel = tk.BooleanVar(value=False)
        self.mirror_on = tk.BooleanVar(value=True)
        for text, var in (("show telemetry lines", self.show_tel), ("mirror the OLED", self.mirror_on)):
            tk.Checkbutton(opts, text=text, variable=var, bg=BG, fg=FG, selectcolor=BG, activebackground=BG,
                           activeforeground=FG, font=FONT).pack(side="left")
        self.console = tk.Text(right, bg="#0b0e11", fg=FG, insertbackground=FG, font=FONT, wrap="none",
                               state="disabled", height=10)
        self.console.pack(side="top", fill="both", expand=True, padx=6)
        self.console.tag_configure("info", foreground=ACCENT)
        self.console.tag_configure("me", foreground="#9ad36a")
        self.console.tag_configure("frame", foreground=DIM)
        self.entry = tk.Entry(right, bg="#0b0e11", fg=FG, insertbackground=FG, font=FONT)
        self.entry.pack(side="top", fill="x", padx=6, pady=(4, 8))
        self.entry.bind("<Return>", self._send_entry)
        self.entry.bind("<Up>", lambda e: self._hist(-1))
        self.entry.bind("<Down>", lambda e: self._hist(1))

        for w in (self.oled_canvas, self.view_canvas):
            w.bind("<Button-1>", lambda e, w=w: w.focus_set(), add="+")
            w.bind("<KeyPress>", self._key_down)
            w.bind("<KeyRelease>", self._key_up)
            w.bind("<FocusOut>", self._release_all)

        if self.forced:
            self._set_view(views.all_views()[self.forced])
        else:
            self._set_view(views.all_views()["plot"])
        self.oled_canvas.focus_set()
        self._say("info", "click the OLED or the picture: the keyboard then drives the board. "
                          "Type shell commands in the box below.")
        root.protocol("WM_DELETE_WINDOW", self._close)
        root.after(15, self._poll)
        root.after(33, self._redraw)
        root.after(REPEAT_MS, self._repeat)
        root.after(200, self._heartbeat)
        if args.snapshot:
            root.after(int(args.after * 1000), self._snapshot)

    # ---- views ----------------------------------------------------------
    def _set_view(self, cls):
        self.view_canvas.unbind("<Button-1>")
        self.view_canvas.bind("<Button-1>", lambda e: self.view_canvas.focus_set())
        self.view = cls(self.view_canvas, self)
        self.view.resize(max(self.view_canvas.winfo_width(), 200), max(self.view_canvas.winfo_height(), 200))
        self.view_title.config(text=f"{cls.title}   -   {cls.help}")

    # ---- what the board sent ---------------------------------------------
    def _poll(self):
        for _ in range(400):
            try:
                kind, line = self.link.lines.get_nowait()
            except queue.Empty:
                break
            if kind == "info":
                self._say("info", line)
                continue
            f = parse_frame(line)
            if f is None:
                self._say(None, line)
                continue
            name = f[0]
            self.frames_in += 1
            if name == "FB":
                self.oled.apply(f[1:])
                continue
            now = time.monotonic()
            if self.view is not None and name in self.view.frames:
                self.view_fed = now
            elif not self.forced and now - self.view_fed > 3.0:
                # Switch only when the current view has gone quiet: a stray frame
                # from another lesson must not flip the picture back and forth.
                cls = views.for_frame(name)
                if cls is not None and not isinstance(self.view, cls):
                    self.view_fed = now
                    self._set_view(cls)
                    self._say("info", f"${name} -> view '{cls.name}'")
            try:
                self.view.frame(name, f[1:], time.monotonic())
            except Exception as e:  # noqa: BLE001 - a bad frame must not kill the window
                self._say("info", f"view error on ${name}: {e}")
            if self._show_frame(name):
                self._say("frame", line)
        self.root.after(15, self._poll)

    def _show_frame(self, name):
        """A frame name arriving more than ~1/s is telemetry: hidden unless asked."""
        now = time.monotonic()
        q = [t for t in self.seen.get(name, []) if now - t < 3.0] + [now]
        self.seen[name] = q
        return self.show_tel.get() or len(q) <= 3

    def _say(self, tag, text):
        c = self.console
        c.configure(state="normal")
        c.insert("end", text + "\n", tag or ())
        if int(c.index("end-1c").split(".")[0]) > 3000:
            c.delete("1.0", "500.0")
        c.configure(state="disabled")
        c.see("end")

    # ---- drawing ---------------------------------------------------------
    def _redraw(self):
        if self.oled.dirty:
            self.oled.dirty = False
            self.oled_img.put(self.oled.rows(), to=(0, 0))
            self.oled_big = self.oled_img.zoom(self.args.scale)
            self.oled_canvas.itemconfigure(self.oled_item, image=self.oled_big)
        try:
            self.view.draw()
        except Exception as e:  # noqa: BLE001
            self.view_canvas.delete("all")
            self.view_canvas.create_text(10, 10, anchor="nw", fill=ACCENT, font=FONT, text=f"view error: {e}")
        now = time.monotonic()
        if now - self.rate_t >= 1.0:
            dt = now - self.rate_t
            self.rate_text = (f"{(self.link.rx_bytes - self.rate_bytes) / dt:6.0f} B/s in   "
                              f"{(self.frames_in - self.rate_frames) / dt:4.0f} frames/s   "
                              f"{(self.oled.segments - self.rate_segs) / dt:4.0f} OLED segments/s")
            self.rate_t, self.rate_bytes = now, self.link.rx_bytes
            self.rate_frames, self.rate_segs = self.frames_in, self.oled.segments
        focus = self.root.focus_get()
        keys = "KEYS -> BOARD" if focus in (self.oled_canvas, self.view_canvas) else "keys -> console"
        held = " ".join(sorted(self.held)) or "-"
        self.status.config(text=f"{self.link.state}   view: {self.view.name}\n{self.rate_text}\n"
                                f"{keys}   held: {held}")
        self.root.after(33, self._redraw)

    # ---- the keyboard ----------------------------------------------------
    @staticmethod
    def _key_name(e):
        if e.keysym in ARROWS:
            return ARROWS[e.keysym]
        ch = (e.char or "").lower()
        return ch if ch in KEY_CHARS else None

    def send_key(self, key, down):
        self.link.write(make_frame(f"KEY,{key},{1 if down else 0}") + "\r\n")

    def send_line(self, text):
        self.link.write(text + "\r\n")

    def _key_down(self, e):
        k = self._key_name(e)
        if k is None:
            return None
        pend = self.release_pending.pop(k, None)
        if pend:                                  # X11 auto-repeat: up+down that never happened
            self.root.after_cancel(pend)
            return "break"
        if k not in self.held:                    # Windows auto-repeat: down, down, down... once
            self.held[k] = time.monotonic()
            self.send_key(k, True)
        return "break"

    def _key_up(self, e):
        k = self._key_name(e)
        if k is None or k not in self.held:
            return None
        self.release_pending[k] = self.root.after(30, lambda: self._release(k))
        return "break"

    def _release(self, k):
        self.release_pending.pop(k, None)
        if self.held.pop(k, None) is not None:
            self.send_key(k, False)

    def _release_all(self, _e=None):
        for k in list(self.held):
            self._release(k)

    def _repeat(self):
        for k in self.held:
            if k in HOLDABLE:
                self.send_key(k, True)            # "still down": the board lets go without it
        self.root.after(REPEAT_MS, self._repeat)

    def _heartbeat(self):
        self.link.write(make_frame(f"MIRROR,{1 if self.mirror_on.get() else 0}") + "\r\n")
        self.root.after(1000, self._heartbeat)

    # ---- the console's command line ---------------------------------------
    def _send_entry(self, _e=None):
        text = self.entry.get()
        self.entry.delete(0, "end")
        if text:
            self.history.append(text)
        self.hist_i = len(self.history)
        self._say("me", f"> {text}")
        self.send_line(text)

    def _hist(self, d):
        if not self.history:
            return "break"
        self.hist_i = max(0, min(len(self.history), self.hist_i + d))
        self.entry.delete(0, "end")
        if self.hist_i < len(self.history):
            self.entry.insert(0, self.history[self.hist_i])
        return "break"

    def _close(self):
        self._release_all()                     # never leave the board with a key down
        self.link.write(make_frame("MIRROR,0") + "\r\n")
        self.root.destroy()

    # ---- a picture of the window, for the docs ------------------------------
    def _snapshot(self):
        try:
            from PIL import ImageGrab
            self.root.update()
            x, y = self.root.winfo_rootx(), self.root.winfo_rooty()
            w, h = self.root.winfo_width(), self.root.winfo_height()
            ImageGrab.grab(bbox=(x, y, x + w, y + h)).save(self.args.snapshot)
            print(f"snapshot: {self.args.snapshot}")
        except Exception as e:  # noqa: BLE001
            print(f"snapshot failed: {e} (needs Pillow)")
        self._close()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="rfc2217://localhost:4000",
                    help="rfc2217://localhost:4000 (Wokwi, default), COM5, socket://localhost:4001")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--view", choices=sorted(views.all_views()), help="force a view instead of choosing by frame")
    ap.add_argument("--scale", type=int, default=5, help="OLED pixels per board pixel (default 5)")
    ap.add_argument("--geometry", default="1400x860")
    ap.add_argument("--snapshot", help=argparse.SUPPRESS)       # docs: save a PNG after --after s, then quit
    ap.add_argument("--after", type=float, default=4.0, help=argparse.SUPPRESS)
    args = ap.parse_args()
    root = tk.Tk()
    Station(root, args)
    root.mainloop()


if __name__ == "__main__":
    main()
