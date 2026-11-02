#!/usr/bin/env python3
"""mission.py - the ground station for SOC3050 lesson 14: plan, send, score.

The drone's C owns the flying; this script owns the MISSION - which is how
real drones divide the work (QGroundControl on a laptop, PX4 on the board).
It speaks lesson 14's "MAVLink-lite": lesson 08's $BODY*CS frames.

Plan
  mission.py default.mission            print the frames: $MISSION,CLEAR, the
                                        params, one $WP per waypoint, $MISSION,START.
                                        Paste them into Wokwi's serial monitor.
  mission.py default.mission --no-start leave out $MISSION,START
  mission.py --gen circle R ALT N       print a mission FILE for N points on a circle
  mission.py --gen lawnmower W H LANES ALT
                                        a survey pattern; add your own (Lab Part 5)

Send (live)
  mission.py default.mission --port rfc2217://localhost:4000 --log flight.log
  mission.py default.mission --port COM5 --log flight.log
                                        send each frame, WAIT for its $ACK, then
                                        record telemetry until the drone lands.
                                        Needs pyserial, as host.py's --port does.

Score
  mission.py --score flight.log         time, cross-track error, waypoint
                                        accuracy, landing error, failsafes
  mission.py --score flight.log --race  add the race verdict: time counts only
                                        within the accuracy limits

Checks used by host/run.sh
  mission.py --verify-mis default.mission loaded.txt
                                        the $MIS frames the C parser produced
                                        must match the file, waypoint for waypoint
  mission.py --score out/wind.log --expect out/wind.expect
                                        Python's score must agree with the C test's

Standard library only.
"""
import argparse
import math
import sys
import time

# --------------------------------------------------------------------------
# Lesson 08's frame rules (the same code as 08_UART_And_Python_Host/host.py)
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
# Missions: a text file in, frames out
# --------------------------------------------------------------------------
FENCE_R, FENCE_ALT = 50.0, 30.0          # flight.c's defaults


def read_mission(path):
    """Returns (waypoints [(x, y, alt) in m], params [(name, int)])."""
    wps, params = [], []
    with open(path, encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].split()
            if not line:
                continue
            if line[0] == "wp" and len(line) == 4:
                x, y, z = (float(v) for v in line[1:])
                if math.hypot(x, y) > FENCE_R or not 1.0 <= z <= FENCE_ALT:
                    print(f"{path}:{n}: warning - outside the default fence, "
                          f"the drone will refuse START", file=sys.stderr)
                wps.append((x, y, z))
            elif line[0] == "param" and len(line) == 3:
                params.append((line[1].upper(), int(line[2])))
            else:
                sys.exit(f"{path}:{n}: cannot read: {raw.strip()}")
    if not wps:
        sys.exit(f"{path}: no waypoints")
    if len(wps) > 16:
        sys.exit(f"{path}: {len(wps)} waypoints - the drone holds 16")
    return wps, params


def dm(metres):
    return int(round(metres * 10))


def mission_frames(wps, params, start=True):
    out = [make_frame("MISSION,CLEAR")]
    out += [make_frame(f"PARAM,{name},{value}") for name, value in params]
    out += [make_frame(f"WP,{i},{dm(x)},{dm(y)},{dm(z)}") for i, (x, y, z) in enumerate(wps)]
    if start:
        out.append(make_frame("MISSION,START"))
    return out


def generate(kind, args):
    """Print a mission file.  Your figure-8 goes here (Lab Part 5)."""
    print(f"# generated: mission.py --gen {kind} {' '.join(args)}")
    if kind == "circle":
        r, alt, n = float(args[0]), float(args[1]), int(args[2])
        for k in range(n + 1):                       # back to the first point
            a = 2 * math.pi * k / n
            x, y = round(r * math.cos(a), 1) + 0.0, round(r * math.sin(a), 1) + 0.0   # no "-0.0"
            print(f"wp {x:7.1f} {y:7.1f} {alt:5.1f}")
    elif kind == "lawnmower":
        w, h, lanes, alt = float(args[0]), float(args[1]), int(args[2]), float(args[3])
        for k in range(lanes):
            x = -w / 2 + w * k / max(lanes - 1, 1)
            ys = (-h / 2, h / 2) if k % 2 == 0 else (h / 2, -h / 2)
            for y in ys:
                print(f"wp {x:7.1f} {y:7.1f} {alt:5.1f}")
    else:
        sys.exit(f"unknown pattern {kind}: circle, lawnmower (or write figure8)")


# --------------------------------------------------------------------------
# Live: a COM port or Wokwi's RFC 2217 server - the same route as host.py
# --------------------------------------------------------------------------
def open_port(url):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is needed for --port:  uv sync  at the repository root\n"
                 "(or print the frames and paste them into Wokwi's serial monitor)")
    return serial.serial_for_url(url, baudrate=115200, timeout=0.1)


def read_lines(ser, buf):
    chunk = ser.read(256)
    buf += chunk
    lines = []
    while b"\n" in buf:
        raw, buf = buf.split(b"\n", 1)
        lines.append(raw.decode("ascii", errors="replace").rstrip("\r"))
    return lines, buf


def send_mission(url, frames, log_path, seconds):
    ser = open_port(url)
    log = open(log_path, "w", encoding="utf-8") if log_path else None
    buf = b""

    def record(line):
        if log:
            log.write(line + "\n")

    for frame in frames:
        cmd = check_frame(frame)[0].split(",")[0]
        for attempt in range(3):                       # MAVLink retries too
            ser.write((frame + "\r\n").encode("ascii"))
            deadline, reply = time.time() + 1.0, None
            while time.time() < deadline and reply is None:
                lines, buf = read_lines(ser, buf)
                for line in lines:
                    record(line)
                    body, _ = check_frame(line)
                    if body and body.startswith("ACK," + cmd):
                        reply = body
            if reply:
                break
            print(f"  no ACK for {frame}, retrying")
        print(f">>> {frame:40} {reply or 'NO REPLY'}")
        if reply is None or ",ERR," in reply:
            sys.exit("mission upload failed")

    print("recording telemetry until the drone lands (Ctrl-C to stop)")
    start = time.time()
    try:
        while not seconds or time.time() - start < seconds:
            lines, buf = read_lines(ser, buf)
            for line in lines:
                record(line)
                body, _ = check_frame(line)
                if not body:
                    continue
                f = body.split(",")
                if f[0] == "POS":
                    print(f"\r  t={int(f[1]) / 1000:6.1f}s {f[2]:9} x={int(f[3]) / 10:6.1f} "
                          f"y={int(f[4]) / 10:6.1f} alt={int(f[5]) / 10:5.1f} wp={f[7]:>2} "
                          f"bat={f[8]}%  ", end="")
                elif f[0] == "EVT":
                    print(f"\n  EVENT {','.join(f[2:])}")
                    if f[2:5] == ["MODE", "DISARMED", "LANDED"]:
                        seconds = 0.001                # stop: it is down
    except KeyboardInterrupt:
        pass
    if log:
        log.close()
        print(f"\nsaved {log_path} - now:  mission.py --score {log_path}")


# --------------------------------------------------------------------------
# Scoring a flight from its telemetry
# --------------------------------------------------------------------------
def seg_dist(px, py, ax, ay, bx, by):
    lx, ly = bx - ax, by - ay
    l2 = lx * lx + ly * ly
    t = ((px - ax) * lx + (py - ay) * ly) / l2 if l2 > 1e-9 else 0.0
    t = min(max(t, 0.0), 1.0)
    return math.hypot(px - (ax + t * lx), py - (ay + t * ly))


LIMITS = {"wp": 2.0, "xt": 3.0, "land": 1.5}          # the race's accuracy rules


def score(path, race=False, quiet=False):
    wps, mode, cur = [], "?", -1
    t_start = t_end = None
    truth, xts, events = [], [], []
    good = bad = 0
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            i = line.find("$")                         # tolerate serial-monitor junk
            if i < 0:
                continue
            body, why = check_frame(line[i:])
            if body is None:
                bad += 1
                continue
            good += 1
            p = body.split(",")
            if p[0] == "MIS":
                idx, n = int(p[1]), int(p[2])
                if idx == 0:
                    wps = []
                wps.append((int(p[3]) / 10, int(p[4]) / 10, int(p[5]) / 10))
            elif p[0] == "EVT":
                t = int(p[1]) / 1000
                if p[2] == "MISSION" and t_start is None:
                    t_start = t
                if p[2] == "MODE":
                    events.append((t, p[3], p[4]))
                    if p[3] == "DISARMED" and p[4] == "LANDED":
                        t_end = t
            elif p[0] == "POS":
                mode, cur = p[2], int(p[7])
            elif p[0] == "TRU":
                x, y, z = int(p[2]) / 100, int(p[3]) / 100, int(p[4]) / 100
                truth.append((x, y, z))
                if mode == "MISSION" and 0 <= cur < len(wps):
                    a = wps[cur - 1] if cur > 0 else (0.0, 0.0, 0.0)
                    b = wps[cur]
                    xts.append(seg_dist(x, y, a[0], a[1], b[0], b[1]))

    if not truth:
        sys.exit(f"{path}: no $TRU frames - is this a lesson 14 log?")
    miss = [min(math.dist(w, s) for s in truth) for w in wps]   # none if no mission
    land = math.hypot(truth[-1][0], truth[-1][1]) if t_end is not None else None
    fails = [e for e in events if e[2] in ("FENCE", "BAT", "BATCRIT")]
    res = {
        "time": (t_end - t_start) if (t_end is not None and t_start is not None) else None,
        "max_xt": max(xts) if xts else 0.0,
        "rms_xt": math.sqrt(sum(e * e for e in xts) / len(xts)) if xts else 0.0,
        "wp": miss, "wp_worst": max(miss) if miss else None, "land": land, "fails": fails,
    }
    if quiet:
        return res

    print(f"{path}: {good} good frames, {bad} bad, {len(truth)} truth samples, {len(wps)} waypoints")
    if res["time"] is not None:
        print(f"  completion (START -> landed)   {res['time']:.1f} s")
    else:
        print("  completion                     " + ("no mission was started" if t_start is None
                                                     else "DID NOT LAND"))
    print(f"  cross-track error, max / RMS   {res['max_xt']:.2f} / {res['rms_xt']:.2f} m")
    if miss:
        print("  waypoint miss (closest, 3D)    " + " ".join(f"{m:.2f}" for m in miss) + " m")
    print(f"  landing error from home        {land:.2f} m" if land is not None else
          "  landing error                  -")
    for t, m, why in events:
        print(f"  t={t:7.1f}s  -> {m:9} ({why})")
    if race:
        ok = (res["time"] is not None and not fails and miss and res["wp_worst"] <= LIMITS["wp"]
              and res["max_xt"] <= LIMITS["xt"] and land is not None and land <= LIMITS["land"])
        print(f"\n  RACE: {'%.1f s' % res['time'] if ok else 'DISQUALIFIED'}   "
              f"(limits: every waypoint within {LIMITS['wp']} m, cross-track under "
              f"{LIMITS['xt']} m, land within {LIMITS['land']} m, no failsafe)")
    return res


def check_expect(res, path):
    """host/run.sh: does Python's score agree with the C test's own numbers?"""
    exp = {}
    with open(path) as f:
        for line in f:
            k, v = line.split()
            exp[k] = float(v)
    # Python sees 5 Hz telemetry, C every 20 ms step: allow for the sampling.
    tol = {"time": 0.05, "max_xt": 0.3, "wp_worst": 0.3, "land": 0.05}
    mine = {"time": res["time"], "max_xt": res["max_xt"], "wp_worst": res["wp_worst"], "land": res["land"]}
    bad = 0
    for k, t in tol.items():
        ok = mine[k] is not None and abs(mine[k] - exp[k]) <= t
        bad += not ok
        print(f"  {k:9} C {exp[k]:7.2f}   Python {mine[k]:7.2f}   {'agree' if ok else 'DISAGREE'}")
    return 1 if bad else 0


def verify_mis(mission_path, mis_path):
    wps, _ = read_mission(mission_path)
    got = []
    with open(mis_path) as f:
        for line in f:
            body, why = check_frame(line)
            if body is None:
                print(f"  bad frame: {line.strip()} ({why})")
                return 1
            p = body.split(",")
            got.append((int(p[3]), int(p[4]), int(p[5])))
    want = [(dm(x), dm(y), dm(z)) for x, y, z in wps]
    for i, (w, g) in enumerate(zip(want, got)):
        print(f"  wp {i}: file {w}  drone {g}  {'same' if w == g else 'DIFFERENT'}")
    ok = want == got
    print(f"  round trip: {len(want)} waypoints in the file, {len(got)} loaded - "
          f"{'IDENTICAL' if ok else 'MISMATCH'}")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mission", nargs="?", help="a mission file (see default.mission)")
    ap.add_argument("--no-start", action="store_true")
    ap.add_argument("--gen", nargs="+", metavar="ARG", help="circle R ALT N | lawnmower W H LANES ALT")
    ap.add_argument("--port")
    ap.add_argument("--log")
    ap.add_argument("--seconds", type=float, default=0)
    ap.add_argument("--score", metavar="LOG")
    ap.add_argument("--race", action="store_true")
    ap.add_argument("--expect")
    ap.add_argument("--verify-mis", nargs=2, metavar=("MISSION", "MIS_FRAMES"))
    a = ap.parse_args()

    if a.gen:
        generate(a.gen[0], a.gen[1:])
        return 0
    if a.verify_mis:
        return verify_mis(*a.verify_mis)
    if a.score:
        res = score(a.score, race=a.race)
        return check_expect(res, a.expect) if a.expect else 0
    if not a.mission:
        ap.print_help()
        return 1
    wps, params = read_mission(a.mission)
    frames = mission_frames(wps, params, start=not a.no_start)
    if a.port:
        send_mission(a.port, frames, a.log, a.seconds)
    else:
        print("\n".join(frames))
    return 0


if __name__ == "__main__":
    sys.exit(main())
