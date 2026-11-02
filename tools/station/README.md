# The station — the board's screen and keyboard on the PC

```
python tools/station/station.py                          Wokwi in VS Code (default: rfc2217://localhost:4000)
python tools/station/station.py --port COM5              a real Nucleo on USB
python tools/station/station.py --port socket://localhost:4001   with fakeboard.py, no simulator
```

The app board's OLED is 128 × 64 pixels — tiny in a simulator window, and too
small for a drone's or a robot's picture. The station is how lessons 12-19 are
**watched and driven from the PC**, the way a real drone is flown from a
ground station:

| Part of the window | What it is | Board side |
|---|---|---|
| the OLED | the board's own screen, drawn 5× (`--scale`) | `_lib/mirror.c` sends it as `$FB` frames while the station asks |
| the view | the lesson's telemetry drawn at the PC's resolution: a horizon, a map, a robot | the frames the lesson already sends (`$ATT`, `$NAV`, `$BAL`, ...) |
| the console | everything the board prints, and a command line for its shell | the lesson's shell |

**The board computes; the PC draws.** The serial link carries ~11 KB/s at
115 200 baud: plenty for numbers (a 40-byte state frame 50 times a second is
2 KB/s), far too little for pixels at PC resolution (one 640 × 480 frame would
take over 3 s). So the board sends state, and the view turns it into a picture.

**Needs:** the course's uv environment - `uv sync` at the repository root
(`pyproject.toml`: Python 3.12 with tkinter, pyserial). Then
`uv run tools/station/station.py`, or the VS Code task **ARM: Station**, which
runs `tools/cli/station.ps1`: it uses `uv run` (which syncs first if needed)
and prints how to install uv if it is missing.

## The keyboard

Click the OLED or the view: the keyboard now drives the board (the status line
says `KEYS -> BOARD`). Click the command line to type shell commands instead.

| Key | Board |
|---|---|
| arrows | the joystick |
| `a` | button A |
| `b` or `p` | button B |
| `s` | stick press (SEL) |
| `1` … `5` | the knob, in fifths |
| `-` `+` | whatever the lesson says (lesson 12: the game's pace) |
| `?` | the board prints its key list |

Typed commands worth knowing: `quiet` / `loud` stop and restart the lesson's
periodic telemetry lines.

Each key goes to the board as **`$KEY,<key>,1*CS` when pressed and
`$KEY,<key>,0*CS` when released** — lesson 08's frame format. A terminal can
only say "pressed", so `_lib/kbd.c` has to guess how long a terminal key is
held; from the station it does not guess. While a key is held the station
repeats its down frame every 200 ms; the board releases a key it has not heard
about for 600 ms, so a lost key-up or a closed window cannot leave the stick
pushed. The board feeds all of it into the same `pad_t` the pins fill, so no
application code can tell keyboard from joystick.

## The protocol

| Direction | Frame | Meaning |
|---|---|---|
| PC → board | `$KEY,<UP\|DOWN\|LEFT\|RIGHT\|char>,<1\|0>` | a key went down / up (`_lib/kbd.c`) |
| PC → board | `$MIRROR,<1\|0>` | send me the OLED; sent every second, the mirror stops 3 s after the last (`_lib/mirror.c`) |
| board → PC | `$FB,<page>,<col>,<64 hex>` | 32 columns of one OLED page, sent when they change |
| board → PC | the lesson's own frames | drawn by the lesson's view |

All frames carry lesson 08's XOR checksum; the station drops any that fail.
A plain terminal never sees `$FB`: the mirror runs only while asked.

## Views

`views/` holds one module per lesson; `views/__init__.py` finds them. The first
frame whose name is in a view's `frames` tuple selects it; `--view NAME`
forces one; `views/plot.py` (a strip chart of every numeric field) is the
fallback. To write one, copy a lesson's view and read `views/base.py`.

## Testing without a board

`fakeboard.py` is a pretend board on TCP port 4001: it answers `$MIRROR` with a
moving ball, sends `$PERF`, and moves the ball with the `$KEY` arrows — a key's
round trip, press to pixel, with no simulator. `--replay FILE` also plays a
capture of real board output.

## Wokwi

Each lesson's `wokwi.toml` carries `rfc2217ServerPort = 4000`: while the
simulation runs, the VS Code extension serves USART2 on that port. Start the
station before or after the simulation — it keeps retrying.

**Status (2026-11-03):** the station has been run against `fakeboard.py` and
seen working (mirror, view selection, key down/up/repeat, console both ways).
The firmware side has host tests (lesson 12: `$KEY` and `$MIRROR` checks). **It
has not yet been run against Wokwi**, and Wokwi's RFC2217 forwarding has never
been exercised in this repository (lesson 08 also lists it as unproven). That
is the first thing to try.
