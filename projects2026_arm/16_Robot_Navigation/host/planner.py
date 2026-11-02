#!/usr/bin/env python3
"""planner.py - the Python tier of SOC3050 lesson 16: Robot Navigation.

The robot plans on the board, in C (astar.c).  This is the SAME A* in Python:
same costs (10 straight, 14 diagonal, plus the costmap), same neighbour
order, same tie-break (lowest f, then lowest h, then lowest cell index), no
corner cutting.  So for the same map it must find the same path - cell for
cell - and this script checks that it does.

Levels come from ../levels.c itself (the string literals), so the robot and
this script can never disagree about what a level looks like.

  python planner.py --level 3                 print level 3 and the A* path on it
  python planner.py --level 4 --inflate 0     ... with no keep-away cost
  python planner.py --map mymaze.txt          your own map: 14 lines of 32 chars,
                                              '#' wall '.' floor 'S' start 'G' goal
  python planner.py --level 2 --frames        the frames that load it into the robot:
                                              paste them into Wokwi's serial monitor
  python planner.py --goal 20,5 --frames      just a goal: $GOAL,20,5*..
  python planner.py --map m.txt --port rfc2217://localhost:4000
                                              send the map live (needs pyserial)
  python planner.py --file capture.txt        read a serial capture: $NAV telemetry,
                                              ARRIVED lines, and a "dump" of the
                                              robot's map and plan, re-planned here
                                              and compared cell by cell
  python planner.py --compare paths.txt       check host/sitl.c's paths (run.sh does)

Where the lines come from mirrors 08_UART_And_Python_Host/host.py: --file for
a log copied out of Wokwi's serial monitor, --port for a live line
(rfc2217://localhost:4000 through the Wokwi VS Code extension, COM5 for a
real board, socket://localhost:3456 for Renode).  Standard library only,
plus pyserial for --port.

Cells are (x, y) with y UP: (0, 0) is the bottom-left; a printed map shows
its top row, y = 13, first.
"""
import argparse
import heapq
import os
import re
import sys
import time

W, H = 32, 14
LETHAL = 255
STRAIGHT, DIAG = 10, 14
COST_UNKNOWN, COST_INFLATE1, COST_INFLATE2 = 3, 12, 4      # nav.h
FREE, UNKNOWN, OCC = 0, 1, 2
# E, N, W, S, NE, NW, SW, SE - astar.c's order
DX = (1, 0, -1, 0, 1, -1, -1, 1)
DY = (0, 1, 0, -1, 1, 1, -1, -1)

HERE = os.path.dirname(os.path.abspath(__file__))
LEVELS_C = os.path.join(HERE, "..", "levels.c")


# --------------------------------------------------------------------------
# The protocol - lesson 08's: $BODY*CS, CS = XOR of BODY's bytes
# --------------------------------------------------------------------------
def checksum(body):
    cs = 0
    for ch in body.encode("ascii"):
        cs ^= ch
    return cs


def make_frame(body):
    return f"${body}*{checksum(body):02X}"


def check_frame(line):
    line = line.strip()
    if not line.startswith("$") or "*" not in line:
        return None
    star = line.index("*")
    body, cs = line[1:star], line[star + 1:star + 3]
    try:
        return body if len(cs) == 2 and int(cs, 16) == checksum(body) else None
    except ValueError:
        return None


# --------------------------------------------------------------------------
# Maps
# --------------------------------------------------------------------------
def load_levels():
    """Every string literal of 32 map characters in levels.c is a row; the
    literal before a run of 14 rows is that level's name."""
    text = open(LEVELS_C, encoding="utf-8").read()
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    levels, name, rows = [], None, []
    for lit in re.findall(r'"([^"]*)"', text):
        if len(lit) == W and set(lit) <= set("#.SGD"):
            rows.append(lit)
            if len(rows) == H:
                levels.append((name, rows))
                rows = []
        else:
            name = lit
    return levels


def load_map_file(path):
    rows = [l.rstrip("\r\n") for l in open(path, encoding="utf-8") if l.strip()]
    if len(rows) != H or any(len(r) != W for r in rows):
        sys.exit(f"{path}: need {H} lines of {W} characters, got {len(rows)} lines")
    return os.path.basename(path), rows


def parse_rows(rows):
    """rows: top line first.  Returns (wall[y][x], start, goal, door cells)."""
    wall = [[False] * W for _ in range(H)]
    start = goal = None
    door = []
    for r, line in enumerate(rows):
        y = H - 1 - r
        for x, c in enumerate(line):
            wall[y][x] = c == "#"
            if c == "S":
                start = (x, y)
            elif c == "G":
                goal = (x, y)
            elif c == "D":
                door.append((x, y))
    return wall, start, goal, door


# --------------------------------------------------------------------------
# The planner - astar.c and nav.c's costmap_build(), line for line
# --------------------------------------------------------------------------
def costmap(cls, inflate):
    """cls[y][x] in FREE/UNKNOWN/OCC -> cost per cell, LETHAL for walls."""
    cm = [[LETHAL if cls[y][x] == OCC else (COST_UNKNOWN if cls[y][x] == UNKNOWN else 0)
           for x in range(W)] for y in range(H)]
    if inflate:
        for y in range(H):
            for x in range(W):
                if cls[y][x] != OCC:
                    continue
                for dy in range(-2, 3):
                    for dx in range(-2, 3):
                        nx, ny = x + dx, y + dy
                        if not (0 <= nx < W and 0 <= ny < H):
                            continue
                        ring = max(abs(dx), abs(dy))
                        if ring == 0 or ring > inflate or cm[ny][nx] == LETHAL:
                            continue
                        base = COST_UNKNOWN if cls[ny][nx] == UNKNOWN else 0
                        want = base + (COST_INFLATE1 if ring == 1 else COST_INFLATE2)
                        cm[ny][nx] = max(cm[ny][nx], want)
    return cm


def astar(cm, start, goal):
    """Returns (path, cost, expanded); path is None if there is none."""
    def lethal(x, y):
        return not (0 <= x < W and 0 <= y < H) or cm[y][x] == LETHAL

    gx, gy = goal

    def h(x, y):
        dx, dy = abs(x - gx), abs(y - gy)
        return 10 * max(dx, dy) + 4 * min(dx, dy)

    if lethal(gx, gy):
        return None, None, 0
    g = {start: 0}
    parent = {}
    closed = set()
    idx = lambda c: c[0] + c[1] * W
    heap = [(h(*start), h(*start), idx(start), start)]
    expanded = 0
    while heap:
        f, hh, _, cur = heapq.heappop(heap)
        if cur in closed or f != g[cur] + hh:        # a stale entry: skip it
            continue
        closed.add(cur)
        expanded += 1
        if cur == goal:
            path = [cur]
            while path[-1] != start:
                path.append(parent[path[-1]])
            return path[::-1], g[goal], expanded
        cx, cy = cur
        for d in range(8):
            nx, ny = cx + DX[d], cy + DY[d]
            if lethal(nx, ny):
                continue
            if d >= 4 and (lethal(nx, cy) or lethal(cx, ny)):
                continue                              # no corner cutting
            nb = (nx, ny)
            if nb in closed:
                continue
            ng = g[cur] + (STRAIGHT if d < 4 else DIAG) + cm[ny][nx]
            if ng >= g.get(nb, 0xFFFF):
                continue
            g[nb] = ng
            parent[nb] = cur
            hn = h(nx, ny)
            heapq.heappush(heap, (ng + hn, hn, idx(nb), nb))
    return None, None, expanded


# --------------------------------------------------------------------------
# Printing
# --------------------------------------------------------------------------
def show(rows_or_cls, path=(), start=None, goal=None, title=""):
    on = set(path or ())
    if title:
        print(title)
    print("     " + "".join(str(x // 10) if x % 10 == 0 else " " for x in range(W)))
    print("     " + "".join(str(x % 10) for x in range(W)))
    for y in range(H - 1, -1, -1):
        line = []
        for x in range(W):
            c = rows_or_cls(x, y)
            if (x, y) == start:
                c = "S"
            elif (x, y) == goal:
                c = "G"
            elif (x, y) in on:
                c = "*"
            line.append(c)
        print(f"  {y:2d} " + "".join(line))


# --------------------------------------------------------------------------
# Frames for the robot
# --------------------------------------------------------------------------
def map_frames(rows):
    wall, start, goal, _ = parse_rows(rows)
    out = []
    for y in range(H):
        bits = 0
        for x in range(W):
            if wall[y][x]:
                bits |= 1 << (31 - x)
        out.append(make_frame(f"MAP,{y},{bits:08X}"))
    if start:
        out.append(make_frame(f"START,{start[0]},{start[1]}"))
    out.append(make_frame("LEVEL,6"))                 # load the custom level...
    if goal:
        out.append(make_frame(f"GOAL,{goal[0]},{goal[1]}"))   # ...then aim it
    return out


def open_port(url):
    try:
        import serial
    except ImportError:
        sys.exit("pyserial is needed for --port:  uv sync  at the repository root")
    return serial.serial_for_url(url, baudrate=115200, timeout=0.2)


def lines_from_port(ser, seconds):
    buf, t0 = b"", time.time()
    while not seconds or time.time() - t0 < seconds:
        chunk = ser.read(256)
        if not chunk:
            continue
        buf += chunk
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            yield raw.decode("ascii", errors="replace").rstrip("\r")


# --------------------------------------------------------------------------
# Reading what the robot sent
# --------------------------------------------------------------------------
NAV_FIELDS = "t level mode ex ey eth tx ty tth coll replans expanded plan_us".split()
MODES = ["IDLE", "CALIB", "RUN", "ARRIVED", "NOPATH"]


def read_capture(lines):
    nav, dump, steps, bad, events = [], {}, {}, 0, []
    for line in lines:
        if line.startswith(("ARRIVED", "NOPATH", "GO ")):
            events.append(line.strip())
        if not line.startswith("$"):
            continue
        body = check_frame(line)
        if body is None:
            bad += 1
            continue
        f = body.split(",")
        if f[0] == "NAV" and len(f) == 14:
            nav.append(dict(zip(NAV_FIELDS, map(int, f[1:]))))
        elif f[0] == "RPATH":
            dump = {"rpath": tuple(map(int, f[1:4])), "rows": {}, "veto": []}
            steps = {}
        elif f[0] == "RSTEP" and dump:
            steps[int(f[1])] = f[2]
        elif f[0] == "RMAP" and dump:
            dump["rows"][int(f[1])] = (int(f[2], 16), int(f[3], 16))
        elif f[0] == "RVETO" and dump:
            dump["veto"].append((int(f[1]), int(f[2])))
        elif f[0] == "RGOAL" and dump:
            dump["goal"] = tuple(map(int, f[1:6]))
            dump["steps"] = dict(steps)
            dump["done"] = True
    return nav, dump, bad, events


def check_dump(dump):
    """Re-plan the robot's own map in Python and compare with the robot's plan."""
    sx, sy, gx, gy, inflate = dump["goal"]
    cost_mcu, expanded_mcu, len_mcu = dump["rpath"]
    cls = [[UNKNOWN] * W for _ in range(H)]
    for y, (occ, fre) in dump["rows"].items():
        for x in range(W):
            b = 1 << (31 - x)
            cls[y][x] = OCC if occ & b else (FREE if fre & b else UNKNOWN)
    cm = costmap(cls, inflate)
    for vx, vy in dump["veto"]:
        cm[vy][vx] = LETHAL                         # the safety layer's vetoes
    cm[sy][sx] = 0                                  # "wherever I am, I can leave from"
    path, cost, expanded = astar(cm, (sx, sy), (gx, gy))
    hexs = "".join(dump["steps"][k] for k in sorted(dump["steps"]))
    mcu = [(int(hexs[i:i + 2], 16), int(hexs[i + 2], 16)) for i in range(0, len(hexs), 3)]
    sym = {OCC: "#", FREE: ".", UNKNOWN: " "}
    show(lambda x, y: sym[cls[y][x]], path or (), (sx, sy), (gx, gy),
         "The robot's map as it dumped it (# wall, . floor, blank unknown), Python's path *:")
    print(f"\n  board : cost {cost_mcu if cost_mcu != 0xFFFF else 'none'}, expanded {expanded_mcu}, {len_mcu} cells")
    print(f"  Python: cost {cost if cost is not None else 'none'}, expanded {expanded}, {len(path) if path else 0} cells")
    if cost_mcu == 0xFFFF and path is None:
        print("  agree: no path")
        return 0
    same = path == mcu and cost == cost_mcu
    print("  SAME PATH, cell for cell" if same else f"  DIFFERENT\n  board : {mcu}\n  Python: {path}")
    return 0 if same else 1


def summarise(nav, bad, events):
    if events:
        print("\n".join("  " + e for e in events))
    if not nav:
        print("  no $NAV frames")
        return
    last = nav[-1]
    err = max(((n["ex"] - n["tx"]) ** 2 + (n["ey"] - n["ty"]) ** 2) ** 0.5 for n in nav)
    print(f"  {len(nav)} $NAV frames ({bad} damaged), last at t = {last['t'] / 1000:.1f} s: "
          f"level {last['level']}, {MODES[last['mode']] if last['mode'] < 5 else last['mode']}, "
          f"{last['coll']} collision(s), {last['replans']} replans")
    print(f"  estimate vs truth: worst {err:.0f} mm, last "
          f"{((last['ex'] - last['tx']) ** 2 + (last['ey'] - last['ty']) ** 2) ** 0.5:.0f} mm")
    print(f"  slowest plan reported: {max(n['plan_us'] for n in nav)} us, "
          f"most nodes: {max(n['expanded'] for n in nav)}")
    print("  chart one field:  python ..\\..\\08_UART_And_Python_Host\\host.py --file CAPTURE --chart NAV,4,0,1400")


# --------------------------------------------------------------------------
def compare_paths(path_file, levels):
    """host/sitl.c wrote the C planner's paths on every level; plan each again."""
    fails = n = 0
    for line in open(path_file):
        p = line.split()
        if not p or p[0] != "PATH":
            continue
        lv, inflate, cost_c, exp_c = map(int, p[1:5])
        cells_c = [tuple(map(int, c.split(","))) for c in p[5:]]
        wall, start, goal, door = parse_rows(levels[lv][1])
        cls = [[OCC if wall[y][x] else FREE for x in range(W)] for y in range(H)]
        path, cost, expanded = astar(costmap(cls, inflate), start, goal)
        ok = path == cells_c and cost == cost_c and expanded == exp_c
        n += 1
        fails += not ok
        print(f"  level {lv + 1} {levels[lv][0]:<9} inflate {inflate}: C cost {cost_c:4} expanded {exp_c:4} | "
              f"Python cost {cost:4} expanded {expanded:4} | {'same path' if ok else 'DIFFERENT'}")
    print(f"planner.py vs astar.c: {n - fails} of {n} identical (path, cost and nodes expanded)")
    return 1 if fails or n == 0 else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--level", type=int, help="1..5: a built-in level from levels.c")
    ap.add_argument("--map", help="a text file: 14 lines of 32 characters")
    ap.add_argument("--inflate", type=int, default=1, choices=(0, 1, 2))
    ap.add_argument("--goal", help="X,Y: plan to (or send) this goal instead")
    ap.add_argument("--frames", action="store_true", help="print frames to paste into the serial monitor")
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--port", help="send the frames live, then show what comes back")
    src.add_argument("--file", help="a serial capture to read")
    ap.add_argument("--seconds", type=float, default=10, help="how long to listen on --port")
    ap.add_argument("--compare", help="paths.txt from host/sitl.c")
    a = ap.parse_args()

    levels = load_levels()
    if len(levels) != 5:
        sys.exit(f"levels.c: expected 5 levels, parsed {len(levels)}")

    if a.compare:
        return compare_paths(a.compare, levels)

    if a.file:
        nav, dump, bad, events = read_capture(open(a.file, encoding="utf-8", errors="replace"))
        summarise(nav, bad, events)
        if dump.get("done"):
            print()
            return check_dump(dump)
        print("  (no complete dump in the capture: type  dump  in the serial monitor first)")
        return 0

    rows = None
    if a.map:
        name, rows = load_map_file(a.map)
    elif a.level:
        name, rows = levels[a.level - 1]
    frames = []
    if rows:
        wall, start, goal, door = parse_rows(rows)
        if a.goal:
            goal = tuple(int(v) for v in a.goal.split(","))
        cls = [[OCC if wall[y][x] else FREE for x in range(W)] for y in range(H)]
        t0 = time.perf_counter()
        path, cost, expanded = astar(costmap(cls, a.inflate), start, goal)
        us = (time.perf_counter() - t0) * 1e6
        show(lambda x, y: "#" if wall[y][x] else ("D" if (x, y) in door else "."), path or (), start, goal,
             f"{name}  (inflate {a.inflate}; full knowledge of the map - the robot starts with none)")
        if path:
            print(f"\n  cost {cost}  ({cost * 10} mm if every cell were plain floor), {len(path)} cells, "
                  f"{expanded} nodes expanded, {us:.0f} us in Python")
        else:
            print(f"\n  NO PATH after expanding {expanded} nodes")
        if a.frames or a.port:
            frames = map_frames(rows) if a.map else [make_frame(f"LEVEL,{a.level}")]
            if a.goal:
                frames.append(make_frame(f"GOAL,{goal[0]},{goal[1]}"))
    elif a.goal:
        frames = [make_frame("GOAL," + a.goal)]
    else:
        ap.print_help()
        return 1

    if a.frames:
        print("\nPaste these into the serial monitor, one line at a time, then press A:")
        print("\n".join(frames))
    if a.port:
        ser = open_port(a.port)
        for fr in frames:
            ser.write((fr + "\r\n").encode("ascii"))
            print(f">>> {fr}")
            time.sleep(0.05)
        nav, dump, bad, events = read_capture(lines_from_port(ser, a.seconds))
        summarise(nav, bad, events)
    return 0


if __name__ == "__main__":
    sys.exit(main())
