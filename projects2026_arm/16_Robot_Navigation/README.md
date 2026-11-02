# 16 — Robot Navigation

**Part 3, Applications — the first robot lesson after the line follower.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz), app board.
Simulation only.

A round robot with two wheels, a gyro and five rangefinders is dropped into an
arena it has never seen — open room, corridors, a U-shaped trap, a maze, a
door that slams — and has to reach a flag. It **estimates** its pose by dead
reckoning, **maps** the walls into an occupancy grid, **plans** with A* on its
own half-built map, **follows** the plan with pure pursuit, and lets a
**reactive** safety layer brake when a wall is closer than the map says. The
robot and the room are both simulated on the chip, in two halves that meet
only in `control_step(const sensors_t *, actuators_t *)` — software in the
loop. The OLED shows the robot's belief (unknown checker, walls, plan, true
disc vs estimated ring); `host/planner.py` runs the same A* in Python, sends
goals and whole maps as lesson 08 frames, and checks the robot's plan cell for
cell.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 29 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: Parts 0–9, ~2 hours, **nothing handed in** |
| `sim.h` | **the contract**: `sensors_t`, `actuators_t`, `control_step()`, the robot's nominal datasheet |
| `world.c`, `world.h` | the world: true map, motor lag, wrong wheel sizes, gyro bias, ray-marched noisy beams, collisions, the door |
| `nav.c`, `nav.h` | **subject.** The robot: dead reckoning + gyro calibration, integer occupancy grid, costmap with inflation, replanning with recovery, pure pursuit with line of sight, reactive brake and vetoes |
| `astar.c`, `astar.h` | **subject.** A* in 2112 B: integer octile costs, no corner cutting, direction parents, decrease-key heap, overflow reported |
| `levels.c`, `levels.h` | the five arenas as text — also parsed by `planner.py` |
| `view.c`, `view.h` | the OLED frame (pure C over `oled.h`'s framebuffer) |
| `mathx.c`, `mathx.h` | one out-of-line copy of `fmath.h`'s functions (saves 552 B) |
| `Main.c` | the harness: RTOS tasks world / robot / ui / shell, SysTick cycle counter, `$NAV` telemetry, one dispatcher for typed words and `$frames`, `stats`, `dump`, `quiet` / `loud`; the station's `$KEY` and `$MIRROR` |
| `build.bat` | `LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror` |
| `simulate.bat`, `diagram.json` | the app board, unchanged from `_targets/app-board-diagram.json` |
| `wokwi.toml` | the VS Code extension's settings: GDB on port 3333, the serial port on `rfc2217://localhost:4000` for the station |
| `tools/station/views/l16_nav.py` | (outside this folder) the station's view of this lesson: the robot's map, plan and poses, large |
| `host/sitl.c`, `host/run.bat`, `host/run.sh` | the host test: every level, the lab's switches, "no path", open-list sizing, OLED frames as ASCII |
| `host/planner.py` | the Python tier: print/plan levels, make frames, send them (`--port`), read captures (`--file`), compare with C (`--compare`) |

## Build and run

```
build.bat          # FLASH 31100 B / 32 KB, RAM 11584 B / 12 KB, zero warnings
simulate.bat       # paste diagram.json, upload Main.elf, press A
host\run.bat       # or: bash host/run.sh - the whole robot on the PC, ~1 s
uv run host\planner.py --level 4
uv run host\planner.py --file capture.txt
```

RAM includes the linker's 1.5 KB heap + main-stack reserve. The robot's own
state is 3288 B: map 448, costmap 448, path 256, A* 2112, vetoes 24.

**Controls.** A start / restart · B next level · SEL (space) outline the walls
not yet mapped · knob speed 100–400 mm/s · joystick moves the goal before the
start. **Shell** (typed words or `$` frames, any case): `go`, `reset`,
`level N` (6 = the custom map), `goal X Y`, `start X Y`, `map Y HEX`, `speed`,
`inflate 0-2`, `react 0|1`, `cal 0|1`, `wheels 0|1`, `stats`, `dump`,
`quiet` / `loud` (stop / restart `$NAV`). Every change is answered with
`$ACK,...`.

**Telemetry**, 4 Hz: `$NAV,t_ms,level,mode,est_x,est_y,est_th_mdeg,true_x,true_y,true_th_mdeg,collisions,replans,expanded,plan_us`
(chart any field with `08_UART_And_Python_Host/host.py --chart NAV,N,...`).
`dump` sends `$RPATH`, `$RSTEP` (3 hex digits per cell), `$RMAP,y,occ,free`,
`$RVETO`, `$RGOAL,startx,starty,goalx,goaly,inflate`. Map rows are 32 bits,
bit 31 = x 0; y = 0 is the bottom row.

### Run it from the station

```
uv run tools/station/station.py        # from the repository root, Wokwi running in VS Code
```

`wokwi.toml` forwards the simulated USART2 to `rfc2217://localhost:4000`, and
the station (`tools/station/`) connects there. The first `$NAV` picks this
lesson's view, `l16_nav.py`: the robot's own map as large as the window - walls,
cells seen free, vetoed cells - the planned path, start and goal, the true pose
(green) and the estimate (blue) with heading arrows and trails, and the
counters. The map comes from `dump`, which the view sends when it starts, when
the level changes, and when a run ends or is reset - never mid-run, because a
`dump` re-plans and holds the robot's lock while it sends. Type `dump` in the
station's console for a fresh map mid-run.

Click the view and the keyboard is the pad (`_lib/kbd.h`): arrows = stick (the
goal, before the start), `a` = A, `b`/`p` = B, `s` = SEL, `1`-`5` = knob
(speed), `?` lists them. `-` and `+` do nothing here: speed is already the
knob's. The station sends real key-down and key-up frames, `$KEY,<key>,<1|0>`;
it also asks for the OLED (`$MIRROR,1` every second), which `task "ui"` sends
as `$FB` frames right after each flush - at most two a pass, and only while
the UART's ring has room for a whole one. The shell takes both frames before
its own parser: they are never answered, never `$NAK`ed, never echoed.

Cost: 1884 B of flash and 256 B of RAM (`kbd_t`, the mirror's 32 segment
hashes, a static 96 B frame buffer). **Not yet watched in Wokwi**: built,
sized, and the view tested on the PC against frames made from the firmware's
format strings, but no station has yet talked to this firmware.

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| `SysTick->LOAD`, `SysTick->VAL`, `RELOAD`/`CURRENT` 23:0 | `core_cm0plus.h` (`SysTick_LOAD_RELOAD_Msk`, `SysTick_VAL_CURRENT_Msk` = 0xFFFFFF) |
| `RCC->IOPENR`, `RCC_IOPENR_GPIOAEN`, `GPIOA` | `stm32c031xx.h` |
| the kernel sets SysTick to `SystemCoreClock / 1000` (LOAD 47999 = 0xBB7F) | `_lib/os.c`, `SysTick_Config()` |
| joystick: HORZ 0 V = right, VERT 0 V = down, space = SEL | Wokwi `wokwi-analog-joystick` page |
| pushbutton `key` attribute, pins `1.l 1.r 2.l 2.r` | Wokwi `wokwi-pushbutton` page |
| SSD1306 at 0x3C, `i2cAddress` attribute, pins GND VCC SCL SDA | Wokwi `board-ssd1306` page |
| USART2 PA2/PA3 AF1, I²C1 PB8/PB9 AF6, TIM3_CH1 PA6 AF1 | `_lib/` drivers (lessons 08, 09, 12) |
| the frame format `$BODY*CS` | lesson 08, `_lib/proto.c` |

Algorithms (A*, octile heuristic, log-odds occupancy, pure pursuit, subsumption)
are textbook; every constant in them is in `nav.c` / `astar.c`, measured by
the host test, not quoted from anywhere.

## Verified by execution

### On the host — `host\run.bat` (passes, exit 0)

Defaults (250 mm/s, inflate 1, reactive on, gyro heading), 5 seeds per level:

| Level | arrived | time | driven / optimal | replans | collisions | estimate error end / worst | A* peak nodes / open |
|---|---|---|---|---|---|---|---|
| 1 Pillars | 5/5 | 13.9 s | 0.99 | 4.4 | 0 | 7 / 13 mm | 304 / 72 |
| 2 Corridors | 5/5 | 26.6 s | 1.17 | 10.4 | 0 | 10 / 18 mm | 307 / 65 |
| 3 Trap | 5/5 | 19.3 s | 1.25 | 2.8 | 0 | 8 / 14 mm | 244 / 77 |
| 4 Maze | 5/5 | 39.9 s | 1.07 | 44.6 | 0 | 13 / 20 mm | 336 / 81 |
| 5 Door | 5/5 | 32.9 s | 1.04 | 17.2 | 0 | 7 / 16 mm | 349 / 65 |

The lab's switches, all levels × 5 seeds:

| setting | arrived | collisions / run | estimate error end / worst |
|---|---|---|---|
| defaults | 25/25 | 0.00 | 9 / 20 mm |
| reactive layer off | 23/25 | 10.56 | 412 / 8374 mm |
| inflation 0 | 25/25 | 0.04 | 8 / 21 mm |
| inflation 0 + reactive off | 23/25 | 18.52 | 506 / 7407 mm |
| heading from wheels | 0/25 | 0.00 | 692 / 1172 mm |
| gyro not calibrated | 16/25 | 0.04 | 144 / 476 mm |
| speed 400 mm/s (17.2 s a run) | 25/25 | 0.04 | 8 / 18 mm |
| speed 150 mm/s (47.6 s a run) | 25/25 | 0.04 | 13 / 42 mm |

"No path": a goal sealed in a box → no path after expanding exactly the 335
cells a flood fill finds reachable; goal inside a wall → no path, 0 expanded;
a diagonal wall whose cells touch at corners → no path (no corner cutting);
the robot itself on the sealed map → `NOPATH` after 23.9 s. Open list: peak 83
over every robot run, 94 over 2000 random arenas, never near the 160 slots.
`planner.py --compare`: **15 of 15** level × inflation cases identical to
`astar.c` in path, cost and nodes expanded. Host A*: 50–75 µs per worst plan.

### In Renode — the firmware itself

Lesson 07's harness (Renode 1.17.0, STM32F072 model — a Cortex-M0 — with a
test-only `SystemInit()` and the C0's GPIO block mapped as RAM). Built from
this folder unchanged except that test `startup.c` and one harness edit:
the knob is ignored, because Renode's F0 ADC has no `CCRDY` and the knob would
read 0 (100 mm/s), so the default 250 mm/s applied. Each level: `go`, `dump`
at 8 s, `stats` at 55 s.

| Level | result | replans | A* peak | A* peak time | control_step peak |
|---|---|---|---|---|---|
| 1 | ARRIVED 14.6 s, 0 collisions | 3 | 304 nodes | 499 684 cycles = 10.4 ms | 13.8 ms |
| 2 | ARRIVED 27.6 s, 0 collisions | 12 | 309 | 492 051 = 10.3 ms | 15.2 ms |
| 3 | ARRIVED 19.0 s, 0 collisions | 4 | 239 | 395 708 = 8.2 ms | 12.6 ms |
| 4 | ARRIVED 40.0 s, 0 collisions | 43 | 336 | 536 794 = 11.2 ms | 15.3 ms |
| 5 | ARRIVED 29.0 s, 0 collisions | 14 | 341 | 502 343 = 10.5 ms | 16.0 ms |

- `world_step` peak 2.3–2.7 ms of its 20 ms.
- Stack high-water marks (words): world 54 / 96, robot 174 / 224, ui 216 / 272,
  shell 254 / 304, idle 16 / 64. No HardFault.
- `gyro bias measured` 3380–4420 µrad/s against the true 4000.
- Each mid-run `dump` re-planned by `planner.py --file`: **5 of 5 the same
  path, cell for cell**, on the robot's half-built maps (e.g. level 1: cost
  187, 74 nodes, 16 cells).
- Renode runs one instruction per tick, so "cycles" are **instruction counts**
  (≈1600 per A* node) and include preemption by the world task. Silicon will
  be somewhat slower. The OLED was absent (`bus timeout`), so the display
  code ran but nothing was flushed.

### Found by these tests, and fixed

1. Pure pursuit's carrot behind a wall: the robot drove at it for ever (level 4).
   The carrot now needs line of sight through the map.
2. Carrot 2 mm away: bearing = noise, the robot spun for 120 s. Now skipped.
3. A one-cell gap in level 5 the planner liked and the brake refused: a
   deadlock. Level fixed, and the brake now **vetoes** the cell it refuses.
4. False "no path" from noisy maps: recovery (drop vetoes, then weak walls)
   before `NOPATH`.
5. 1 s of gyro calibration sometimes left 1.6 mrad/s of bias: now 2 s.
6. Lazy-deletion A* overflowed its heap on the sealed-goal case and said "no
   path" to a reachable goal: now decrease-key, measured heap, overflow reported.
7. A lost robot's estimate off the grid indexed past `cost[]`: clamped.
8. Built at 34 932 B flash / 15 136 B RAM — did not fit. Slide 23 lists what
   got it to 29 216 / 11 328 (31 100 / 11 584 since the station's keys and
   mirror).

## Status

Builds with **zero warnings**: FLASH 31 100 B, RAM 11 584 B. The host test
passes (25/25 arrivals, 0 collisions at defaults, "no path" correct, C and
Python A* identical 15/15), and the firmware ran all five levels to the goal
in Renode with 0 collisions.

**Not yet watched running in Wokwi.** The first three things to look at when
someone does:

1. **The OLED picture** (Lab Part 1): does the unknown checker clear in fans,
   do the disc and ring sit where `$NAV` says, and is the frame readable at
   10 Hz? Nothing has ever been flushed to a real SSD1306 model from this code.
2. **`stats` after level 4** (Lab Part 7): the real A* time and
   `control_step` peak. Renode says 11.2 ms and 16 ms in instructions; silicon
   cycles should be somewhat more. If `control_step` comes near 40 ms, lower
   the planning load before anything else.
3. **The controls**: A/B/SEL, the knob as speed (Renode could not read it),
   the joystick moving the goal before the start, and the buzzer on collision
   and arrival.
4. **The station** (above): do held arrow keys move the goal, does the
   mirrored OLED keep up at 10 Hz beside `$NAV`, and does `stats` show the ui
   and shell stacks still clear of their ends with the mirror running? By
   `-fstack-usage` the mirror's call chain (168 B under `task_ui`, then
   `snprintf`) is shallower than the `$NAV` send it already makes (256 B, then
   `vsnprintf`), but no run has measured it.
