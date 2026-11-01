# 14 — Drone Missions

**Part 3, Applications — lesson 3 of 8: position hold, missions from Python,
failsafes.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz), the app board,
in Wokwi. Simulation only: the drone is simulated **inside the firmware**.

A drone flies a five-waypoint mission in a gusty wind, holds its position
when told to, and comes home on its own when it crosses the geofence or its
battery runs low. The firmware carries the aircraft: a `world` task steps a 3D
point-mass model (attitude abstracted as a first-order lag — lesson 13's loop,
justified by time-scale separation and measured), with drag, wind and gusts
from a deterministic generator, a draining battery, a 10 Hz GPS that is 200 ms
late, and a noisy barometer. A `ctl` task runs the flight computer through
`control_step(const sensors_t*, actuators_t*)`: an alpha-beta estimator with
delayed-state GPS fusion, cascaded position → velocity → tilt control,
six flight modes and the failsafes. The mission comes from
`host/mission.py` as "MAVLink-lite" — lesson 08's `$BODY*CS` frames — or from
button A with no host at all. The OLED is a ground station: map, trail,
waypoints, altitude, battery, wind.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 31 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: Parts 0–8, ~2 hours, three contests, **nothing handed in** |
| `Main.c` | five RTOS tasks (world, ctl, link, tel, ui), three locks, the shell (`stats params mission tel`), SysTick cycle stamps |
| `control.h` | the plant/controller contract: `sensors_t`, `actuators_t`, `control_step()` |
| `world.c`, `world.h` | **pure C.** The simulated drone: tilted thrust, linear drag on airspeed, OU gusts from xorshift32, battery ∝ thrust^1.5, delayed GPS, baro, biased accelerometer |
| `flight.c`, `flight.h` | **pure C.** Estimator, cascade, modes (DISARMED TAKEOFF HOLD MISSION RTL LAND), line following, mission validation, pre-arm, failsafes, `$PARAM` table |
| `mavlite.c`, `mavlite.h` | **pure C.** Strict frame parser (`$WP $MISSION $MODE $ARM $DISARM $PARAM $PING`) and telemetry builders (`$POS $TRU $EVT $MIS $ACK`) |
| `ui.c`, `ui.h` | **pure C.** The OLED map and panel, drawn into `oled.c`'s RAM framebuffer |
| `fm.c`, `fm.h` | `fmath.h`'s sin/cos/atan2/sqrt/wrap compiled once (saves 720 B of flash — slide 25) |
| `build.bat` | `LIBS=retarget os uart proto i2c oled adc pad beep` |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board, unchanged from `_targets/app-board-diagram.json`; `wokwi.toml` adds lesson 08's `rfc2217ServerPort = 4000` |
| `host/sitl.c` | software-in-the-loop: flies the four pure-C files on the PC; every test, `--parse`, `--fly`, `--hold` |
| `host/run.bat`, `host/run.sh` | build and run all host tests; exit non-zero on any failure |
| `host/mission.py` | the ground station, standard library only: mission file → frames, `--gen`, `--port` (pyserial, as lesson 08), `--score`, `--race`, `--verify-mis` |
| `host/default.mission` | button A's mission as a text file |

**No shared file was changed.** The lesson links `_lib` as it is.

## Build and run

```
build.bat        # FLASH 27496 B / 32 KB (83.9 %), RAM 10520 B / 12 KB (85.6 %), zero warnings
simulate.bat     # paste diagram.json, upload Main.elf, press a
host\run.bat     # the host tests (or: sh host/run.sh)
python host\mission.py host\default.mission          # frames to type/paste into Wokwi
python host\mission.py --score capture.txt --race    # score a captured flight
```

Task stacks are 160/224/320/288/288 words (world/ctl/link/tel/ui), sized from
`-fstack-usage` plus the C library's prologues; `stats` prints the measured
high-water marks. The first build used 384 words each and **did not link**
(RAM 12 576 B, 102 %); see slide 25.

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| `SysTick->LOAD` RELOAD and `SysTick->VAL` CURRENT are bits 23:0 (`0xFFFFFF`) | `core_cm0plus.h`, `SysTick_LOAD_RELOAD_Msk`, `SysTick_VAL_CURRENT_Msk` |
| the kernel loads SysTick with `SystemCoreClock / 1000` → LOAD = 47 999 = `0xBB7F` | `_lib/os.c` (`SysTick_Config`) |
| no other register is touched by this lesson's own code (all I/O is in `_lib`) | grep of the lesson's `*.c` |
| joystick pins `VCC VERT HORZ SEL GND`; HORZ "0 volts (right) to VCC (left)", VERT 0 V bottom | Wokwi `wokwi-analog-joystick` page |
| pushbutton pins `1.l 1.r 2.l 2.r`; `key` attribute active while the diagram has focus | Wokwi `wokwi-pushbutton` page |
| SSD1306 pins `GND VCC SCL SDA`, default address `0x3c` | Wokwi `board-ssd1306` page |
| `rfc2217ServerPort` → `serial_for_url('rfc2217://localhost:4000', baudrate=115200)` | Wokwi VS Code project-config page |
| serial monitor sends a typed line with `lf` by default; **pasting is not documented** | Wokwi serial-monitor guide |
| `MISSION_COUNT` 44, `MISSION_CLEAR_ALL` 45, `MISSION_ACK` 47, `MISSION_REQUEST_INT` 51, `MISSION_ITEM_INT` 73, `MAV_CMD_NAV_WAYPOINT` 16, `MAV_CMD_NAV_RETURN_TO_LAUNCH` 20, `MAV_CMD_NAV_LAND` 21, `MAV_CMD_MISSION_START` 300, `MAV_CMD_COMPONENT_ARM_DISARM` 400 | MAVLink `common.xml` (GitHub master) |
| `PARAM_VALUE` 22, `PARAM_SET` 23, `GLOBAL_POSITION_INT` 33, `MISSION_ITEM_REACHED` 46, `COMMAND_ACK` 77 | mavlink.io common messages page |
| `MISSION_ITEM_INT` local x/y are metres × 1e4; upload is vehicle-driven (`MISSION_REQUEST_INT`) | `common.xml`; mavlink.io mission protocol page |
| fmath accuracy and "50-100 cycles" per soft-float op | `_lib/fmath.h` header comment (lesson 10 measures) |

Modelling constants (drag 0.35 /s, attitude lag 80 ms, GPS 200 ms and 0.4 m
wander, 0.35 %/s at hover) are **choices**, stated at the top of `world.c` —
typical of a 1–2 kg quadcopter, not of any particular one.

## Verified by execution — on the host

`host/run.sh` (MinGW gcc 15.2, Python 3.12) — 27 flight checks, then
`mission.py` scoring checked against C, then the parser round trip.
**All pass.** Every number below is from that run.

| Test | Result |
|---|---|
| default mission, still air | 57.7 s START→landed; cross-track max 1.75 / RMS 0.56 m; worst waypoint 1.73 m; landed 0.64 m from home at 0.41 m/s; 79.3 % left |
| default mission, 8 m/s + gusts | 61.0 s; cross-track 2.07 / 1.00 m; worst waypoint 1.74 m; landed 0.45 m from home at 0.43 m/s; 76.6 % left |
| estimate error vs GPS's own error | 0.69 m RMS vs 0.65 m (still), 0.68 vs 0.64 (wind) |
| same, without latency compensation (`GPS_LAG_STEPS 0`, a lab variant) | 1.08 m / 0.98 m |
| line following vs aim-at-waypoint, 8 m/s | max cross-track 2.07 vs 2.34 m, RMS 1.00 vs 1.13 m |
| determinism | same seed twice: bit-identical flight |
| hold 30 s, 8 m/s | true RMS 1.01 m (max 2.19) = seen 0.76 + blind 0.67; mean offset 0.23 m; tilt 12.9°; integrator reads 7.1 m/s of 8.0 |
| hold with `KI_VEL = 0` | mean offset 1.26 m downwind (algebra predicts 1.4 m) |
| attitude lag 40/80/200/400/800 ms | seen hold RMS 0.74/0.76/0.83/1.30/3.37 m |
| geofence (joystick east 3 m/s) | RTL `FENCE` at 29.9 s; farthest 51.6 m (fence 50); landed 0.45 m from home |
| low battery (start 32 %) | RTL `BAT` at 20.1 s, mission abandoned; landed at home (0.19 m), 17.1 % left |
| critical battery (drops to 9 % at 25 s) | `LAND` in place, no RTL; 0.6 m from where it fired; 0.39 m/s; 4.5 % left |
| mission validation | empty → `EMPTY`; gap → `EMPTY`; 60 m out → `FENCE`; fixed → `OK`, `TAKEOFF` |
| parser: `mission.py default.mission` → C | 9/9 `OK`; 0/9 one-bit corruptions accepted; 11/11 malformed-but-checksummed refused |
| round trip C `$MIS` → `mission.py --verify-mis` | 5 waypoints identical to the file |
| `mission.py --score` vs C, windy flight | time 61.00/60.96 s; cross-track 2.07/2.06 m; worst wp 1.74/1.75 m; landing 0.45/0.44 m — agree |
| OLED drawn by `ui.c` + `oled.c` on the PC | `host/out/oled_wind.txt`, quoted as slide 24 |
| flash cost, by building variants | inline fmath +720 B; stdio stream (`fputs`) +1860 B |

Found while writing and recorded in the Lab rather than fixed: with the
battery draining six times faster (`--fly 4 ... DRAIN=210`) both battery
failsafes fire correctly and the drone still hits the ground at 8.7 m/s,
because a 10 % threshold is ~5 s at that drain. The flight computer then
reports `LANDED` — it cannot tell a crash from a landing.

## Status

- Builds with **zero warnings**: FLASH 27 496 B, RAM 10 520 B.
- Host tests: **all pass** (above).
- **Not yet watched running in Wokwi.** Nothing in this lesson has run on the
  simulated chip. When someone does, look first at:
  1. **`stats` after a minute of flight** — the per-step cost of `ctl` and
     `world` (slide 26 predicts a few hundred µs for `ctl`) and every task's
     stack high-water mark; any task within ~20 words of its size needs a
     bigger stack.
  2. **The OLED and the buttons** — does `a` (after the 1 s GPS lock) take
     off and draw the map as `host/out/oled_wind.txt` shows, does the
     joystick in HOLD move the drone the right way (east = right, north =
     up), and does the knob change the `WND` figure?
  3. **The frames** — does Wokwi's serial monitor accept `mission.py`'s
     frames typed or pasted one line at a time, does each get its `$ACK`,
     and does `mission.py --score` on a captured flight land near the host's
     57.7 s (5 m/s, the knob's default)?
