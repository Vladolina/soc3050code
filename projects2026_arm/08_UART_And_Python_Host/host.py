#!/usr/bin/env python3
"""host.py - the PC side of SOC3050 lesson 08.

The board sends frames like   $TEL,12000,11,500,2,0*75
and accepts frames like       $LED,170*57
Both are NMEA-style: "$" BODY "*" CS, where CS is the XOR of every byte of BODY
as two hex digits (proto.c on the board does the same arithmetic).

Where the lines come from - pick one:

  --file capture.txt            a log you copied out of Wokwi's serial monitor
  --port rfc2217://localhost:4000   live, through the Wokwi VS Code extension
                                    (wokwi.toml turns that port on)
  --port COM5                   a real Nucleo board on USB
  --port socket://localhost:3456    Renode's UART-to-TCP bridge
  --demo                        no board at all: made-up telemetry, to try the tools

Other things it does:

  --frame "LED,170"             print the checksummed frame, to paste into Wokwi
  --send  "LED,170"             send that frame on a live --port, print the reply
  --plot                        plot `tri` with matplotlib if you have it,
                                otherwise a text strip chart
  --chart IMU,2,-16384,16384    chart field 2 of every $IMU frame, scaled
                                between MIN and MAX (default 0..1000)
  --selftest                    check the checksum against a real GPS sentence

Needs only the standard library, plus pyserial for --port (uv sync at the repository root).
"""
import argparse
import math
import sys
import time

# A real sentence from a GPS receiver, as printed in countless NMEA references.
# If our checksum disagrees with its *47, our checksum is wrong - not the GPS.
NMEA_EXAMPLE = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47"


# --------------------------------------------------------------------------
# The protocol - the same rules as proto.c
# --------------------------------------------------------------------------
def checksum(body: str) -> int:
    cs = 0
    for ch in body.encode("ascii"):
        cs ^= ch
    return cs


def make_frame(body: str) -> str:
    return f"${body}*{checksum(body):02X}"


def check_frame(line: str):
    """Return (body, None) if the line is a good frame, else (None, reason)."""
    line = line.strip()
    if not line.startswith("$"):
        return None, "not a frame"
    star = line.find("*")
    if star < 0:
        return None, "no *"
    body, cs_text = line[1:star], line[star + 1:star + 3]
    try:
        cs = int(cs_text, 16)
    except ValueError:
        return None, "bad hex"
    if len(cs_text) != 2:
        return None, "bad hex"
    if checksum(body) != cs:
        return None, f"checksum {checksum(body):02X} != {cs:02X}"
    return body, None


# --------------------------------------------------------------------------
# Where lines come from
# --------------------------------------------------------------------------
def lines_from_file(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            yield raw.rstrip("\r\n")


def lines_from_port(ser):
    buf = b""
    while True:
        chunk = ser.read(256)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            yield raw.decode("ascii", errors="replace").rstrip("\r")


def lines_demo(count=40):
    """Telemetry exactly as task_tel makes it, with one damaged line and one
    missing sequence number thrown in - so the checks have something to find."""
    for seq in range(count):
        t = 1000 * (seq + 1)
        ph = t % 4000
        tri = ph // 2 if ph < 2000 else (4000 - ph) // 2
        frame = make_frame(f"TEL,{t},{seq},{tri},{seq // 10},0")
        if seq == 7:
            frame = frame.replace(",7,", ",9,")          # corrupted in transit
        if seq == 23:
            continue                                     # lost entirely
        yield frame
        time.sleep(0.02)


def open_port(url):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is needed for --port:  uv sync  at the repository root")
    return serial.serial_for_url(url, baudrate=115200, timeout=0.2)


# --------------------------------------------------------------------------
# Reading telemetry
# --------------------------------------------------------------------------
class Telemetry:
    def __init__(self, chart=None):
        self.good = self.bad = self.other = self.lost = 0
        self.last_seq = None
        self.t, self.tri = [], []
        # --chart NAME,INDEX[,MIN,MAX]: chart one numeric field of any frame
        self.chart = None
        if chart:
            p = chart.split(",")
            lo, hi = (float(p[2]), float(p[3])) if len(p) >= 4 else (0.0, 1000.0)
            self.chart = (p[0], int(p[1]), lo, hi)

    def feed(self, line):
        if not line.startswith("$"):
            self.other += 1                   # banner, shell replies: not frames
            return None
        body, why = check_frame(line)
        if body is None:
            self.bad += 1
            return f"BAD   {line}   ({why})"
        self.good += 1
        f = body.split(",")
        if self.chart and f[0] == self.chart[0] and len(f) > self.chart[1]:
            name, idx, lo, hi = self.chart
            v = float(f[idx])
            self.t.append(len(self.t))
            self.tri.append(v)
            n = int(40 * (min(max(v, lo), hi) - lo) / (hi - lo)) if hi > lo else 0
            return f"{name}[{idx}] = {f[idx]:>8}  |{'#' * n}"
        if f[0] == "TEL" and len(f) >= 6:
            t, seq, tri = int(f[1]), int(f[2]), int(f[3])
            if self.last_seq is not None and seq != self.last_seq + 1:
                self.lost += seq - self.last_seq - 1
            self.last_seq = seq
            self.t.append(t / 1000.0)
            self.tri.append(tri)
            bar = "#" * (tri // 25)
            return f"t={t / 1000:7.2f}s seq={seq:<5} tri={tri:4}  A={f[4]} B={f[5]}  |{bar}"
        return f"FRAME {body}"

    def summary(self):
        return (f"\nframes good {self.good}, bad {self.bad}, "
                f"lost (sequence gaps) {self.lost}, other lines {self.other}")


def plot(tel):
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n(matplotlib not installed - text chart above; uv sync  at the repository root to plot)")
        return
    plt.plot(tel.t, tel.tri, marker=".")
    plt.xlabel("board time (s)")
    plt.ylabel("tri")
    plt.title("SOC3050 lesson 08 telemetry")
    plt.show()


# --------------------------------------------------------------------------
def selftest():
    body, why = check_frame(NMEA_EXAMPLE)
    ok_gps = body is not None
    print(f"real GPS sentence   : {'accepted' if ok_gps else 'REJECTED: ' + why}")
    ok_ours = make_frame("LED,170") == "$LED,170*57"
    print(f"$LED,170 -> {make_frame('LED,170')}   {'as expected' if ok_ours else 'UNEXPECTED'}")
    body, why = check_frame("$LED,171*57")          # "0" -> "1": one bit
    print(f"one bit flipped     : {'rejected (' + why + ')' if body is None else 'ACCEPTED - broken'}")
    return 0 if ok_gps and ok_ours and body is None else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--file")
    src.add_argument("--port")
    src.add_argument("--demo", action="store_true")
    ap.add_argument("--frame", help='print the frame for a body, e.g. "LED,170"')
    ap.add_argument("--send", help="send this body as a frame on --port")
    ap.add_argument("--seconds", type=float, default=0, help="stop a live --port after this long")
    ap.add_argument("--plot", action="store_true")
    ap.add_argument("--chart", help="NAME,INDEX[,MIN,MAX] - chart one field of any frame")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        return selftest()
    if a.frame:
        print(make_frame(a.frame))
        return 0

    tel = Telemetry(a.chart)
    if a.file:
        source = lines_from_file(a.file)
    elif a.demo:
        source = lines_demo()
    elif a.port:
        ser = open_port(a.port)
        if a.send:
            ser.write((make_frame(a.send) + "\r\n").encode("ascii"))
            print(f">>> {make_frame(a.send)}")
        source = lines_from_port(ser)
    else:
        ap.print_help()
        return 1

    start = time.time()
    try:
        for line in source:
            out = tel.feed(line)
            if out:
                print(out)
            if a.seconds and time.time() - start > a.seconds:
                break
    except KeyboardInterrupt:
        pass
    print(tel.summary())
    if a.plot:
        plot(tel)
    return 0


if __name__ == "__main__":
    sys.exit(main())
