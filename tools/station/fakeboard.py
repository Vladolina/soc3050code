#!/usr/bin/env python3
"""
fakeboard.py - a pretend board, for trying the station without a simulator.

    python tools/station/fakeboard.py            listens on localhost:4001
    python tools/station/station.py --port socket://localhost:4001

It speaks the board's side of the station protocol: answers $MIRROR,1 with
$FB frames of a little scene, sends $PERF each second, and moves a ball with
the $KEY arrows it receives - so a key's round trip, press to pixel, can be
seen without Wokwi.  --replay FILE also plays a capture of real board output
(one line per line, $ frames and text), looping, at --rate lines a second.

Standard library only.  Not a model of any lesson: it checks the station.
"""

import argparse
import socket
import time


def checksum(body):
    x = 0
    for b in body.encode():
        x ^= b
    return x


def frame(body):
    return f"${body}*{checksum(body):02X}\r\n".encode()


class Scene:
    def __init__(self):
        self.x, self.y, self.vx, self.vy = 20.0, 20.0, 1.3, 0.9
        self.stick = [0, 0]
        self.fb = [bytearray(128) for _ in range(8)]
        self.sent = [None] * 32

    def px(self, x, y):
        if 0 <= x < 128 and 0 <= y < 64:
            self.fb[y >> 3][x] |= 1 << (y & 7)

    def step(self):
        self.vx += 0.15 * self.stick[0]
        self.vy -= 0.15 * self.stick[1]
        self.x += self.vx
        self.y += self.vy
        if not 3 <= self.x <= 124:
            self.vx = -self.vx
            self.x = min(max(self.x, 3), 124)
        if not 3 <= self.y <= 60:
            self.vy = -self.vy
            self.y = min(max(self.y, 3), 60)
        self.fb = [bytearray(128) for _ in range(8)]
        for i in range(128):
            self.px(i, 0)
            self.px(i, 63)
        for j in range(64):
            self.px(0, j)
            self.px(127, j)
        for dx in range(-3, 4):
            for dy in range(-3, 4):
                if dx * dx + dy * dy <= 9:
                    self.px(int(self.x) + dx, int(self.y) + dy)

    def changed_segments(self, most):
        """Up to `most` changed segments as $FB frames; the rest wait their turn."""
        out = []
        for s in range(32):
            p, c = s // 4, (s % 4) * 32
            seg = bytes(self.fb[p][c:c + 32])
            if seg != self.sent[s] and len(out) < most:
                self.sent[s] = seg
                out.append(frame(f"FB,{p},{c},{seg.hex().upper()}"))
        return out


def serve(args):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.port))
    srv.listen(1)
    print(f"fakeboard: listening on localhost:{args.port}")
    replay = open(args.replay, encoding="utf-8", errors="replace").read().splitlines() if args.replay else []
    while True:
        conn, _ = srv.accept()
        conn.settimeout(0.02)
        print("fakeboard: station connected")
        conn.sendall(b"\r\n=== fakeboard - not a real lesson ===\r\n  keys: arrows push the ball\r\n")
        sc, inbuf, live_until, t_sec, t_step, ri, t_rep = Scene(), b"", 0.0, time.monotonic(), 0.0, 0, 0.0
        frames = 0
        try:
            while True:
                now = time.monotonic()
                try:
                    data = conn.recv(4096)
                    if not data:
                        break
                    inbuf += data
                except socket.timeout:
                    pass
                while b"\n" in inbuf:
                    line, inbuf = inbuf.split(b"\n", 1)
                    text = line.decode(errors="replace").strip()
                    if text.startswith("$MIRROR,1"):
                        if now > live_until:
                            sc.sent = [None] * 32
                        live_until = now + 3.0
                    elif text.startswith("$KEY,"):
                        k, d = text[5:].split("*")[0].split(",")
                        v = 1 if d == "1" else 0
                        axis = {"LEFT": (0, -1), "RIGHT": (0, 1), "UP": (1, 1), "DOWN": (1, -1)}.get(k)
                        if axis:
                            sc.stick[axis[0]] = axis[1] * v
                        if axis is None or v == 0:        # arrows repeat while held: log only their release
                            print(f"fakeboard: {text}")
                    elif text and not text.startswith("$"):
                        conn.sendall(f"fakeboard got: {text}\r\n".encode())
                if now - t_step >= 0.02:
                    t_step = now
                    sc.step()
                    frames += 1
                    if now < live_until and args.scene:
                        for f in sc.changed_segments(6):
                            conn.sendall(f)
                if now - t_sec >= 1.0 and args.scene:
                    conn.sendall(frame(f"PERF,{frames},{frames},2900,3100,129,14,DIRTY"))
                    frames, t_sec = 0, now
                if replay and now - t_rep >= 1.0 / args.rate:
                    t_rep = now
                    conn.sendall((replay[ri % len(replay)] + "\r\n").encode())
                    ri += 1
        except (ConnectionError, OSError):
            pass
        print("fakeboard: station gone")
        conn.close()


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=4001)
    ap.add_argument("--replay", help="a capture of board output to play, looping")
    ap.add_argument("--rate", type=float, default=20.0, help="replay lines per second")
    ap.add_argument("--scene", action=argparse.BooleanOptionalAction, default=True,
                    help="draw the bouncing ball on the mirror (--no-scene: leave $FB to --replay)")
    serve(ap.parse_args())
