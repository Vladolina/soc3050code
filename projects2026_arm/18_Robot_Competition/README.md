# 18 — Robot Competition

**Part 3, Applications — a sumo tournament.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz), the app board.

Two differential-drive sumo robots on a 77 cm dohyo, simulated **inside the
firmware** (software-in-the-loop): `world.c` owns the physics — DC-motor
torque-speed lines, tyre grip that slips, circle-circle pushing contact with
friction, the ring edge — and synthesises each robot's sensors (three noisy
distance cones, four corner line sensors, wheel encoders, a bump switch).
Each robot's brain sees only its own `robot_view_t` through `strategy.h`'s
contract, keeps state only in a private 64-byte block, and answers with two
wheel commands at 50 Hz. Five built-in opponents (Bull, Matador, Spinner,
Turtle, Coward) and a deliberately weak `student.c` starter. The OLED shows the
ring, both robots and their sensor cones; the joystick can drive your robot
(fun mode); the knob sets x1–x8/MAX. A firmware tournament prints a
`$RESULT` line (hash, worst-case cycles, build ID) that a student submits, and
`host/league.c` re-runs the identical tournament — or a whole class round
robin — on a PC.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 30 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: nine parts, ~2 h, then the class tournament — **`student.c` and one `$RESULT` line handed in** |
| `strategy.h` | **the contract**: `robot_view_t`, `robot_cmd_t`, `strategy_mem_t`, the `STRATEGY_PREFIX` renaming macro |
| `student.c` | **yours**: the starter strategy, with the class rules in its header |
| `world.h`, `world.c` | physics and sensor synthesis; pure C, float, `fmath.h` |
| `referee.h`, `referee.c` | rules, seeded start positions, same-instant sensing, cycle measurement hook, the roster, `tour_seed()` |
| `bots.c` | the five opponents |
| `scene.h`, `scene.c` | the OLED picture, drawn from a snapshot |
| `Main.c` | RTOS tasks (sim / shell / oled), menu, SysTick cycle clock, build ID, `$MATCH`/`$RESULT` frames; for the station: `$ARENA` poses, `$KEY` keys, the OLED mirror |
| `build.bat` | `LIBS=retarget os uart proto i2c oled pad adc beep keys kbd mirror` |
| `_lib/kbd.h`, `_lib/kbd.c` | (shared, `LIBS=keys kbd`) the station's `$KEY` down/up frames, written into the same `pad_t` the pins fill |
| `_lib/mirror.h`, `_lib/mirror.c` | (shared, `LIBS=mirror`) the OLED, copied to the station as `$FB` frames while it asks |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board, unchanged from `_targets/app-board-diagram.json`; `wokwi.toml` also forwards the serial port to the station (`rfc2217ServerPort = 4000`) |
| `tools/station/views/l18_arena.py` | the station's view: the dohyo drawn large from `$ARENA`, standings from `$MATCH`/`$RESULT` |
| `host/league.c` | physics checks, round-robin league, determinism check, the firmware's tournament, OLED picture check |
| `host/run.bat`, `host/run.sh` | build + run; compile extra strategy files with name prefixes; reject files with globals (`nm`); compare `-O0`/`-O2`/`-Os` |
| `host/register.h` | force-included into extra files: a constructor that registers `<name>_step` |
| `host/strategies/alice.c`, `bob.c` | example submissions; `cheater.c` — one the league rejects |

## Build and run

```
build.bat        # FLASH 29244 B / 32 KB, RAM 9872 B / 12 KB, zero warnings
simulate.bat
host\run.bat                                        # built-ins + student.c
host\run.bat host\strategies\alice.c host\strategies\bob.c -- -n 100 --tour alice
host\run.bat -- -s 4711 --draw                      # another seed; print the OLED as text
```

On the board: **B** next opponent (last item TOURNAMENT), **A** start/abort,
**SEL** driver student.c ↔ joystick in the menu, sensor cones in a match;
**knob** speed. Serial at 115200: `tour [seed]`, `seed N`, `stats`.

Result line: `$RESULT,<name>,<W>,<D>,<L>,<hash>,<max cycles>,<build>*CS`
(NMEA XOR checksum, lesson 08). Per match: `$MATCH,<robot0>,<robot1>,<seed>,<winner 0/1/-1>,<rounds0>,<rounds1>,<hash>*CS`.

**Run it from the station.** With the simulation running in VS Code,
`python tools/station/station.py` (repository root) draws the dohyo large —
both robots with their headings, names, round score and clock — keeps the
standings from every `$MATCH` and `$RESULT`, shows the OLED copied from
`_lib/mirror.c`, and sends real key-down/up for every control: arrows = the
stick (fun mode), **a** = A, **b** = B, **s** = SEL, **1**–**5** = speed
x1/x2/x4/x8/MAX, **?** = the key list (`-` and `+` do nothing here: 1–5
already pick every speed). While the station is connected, and only then,
the display task also sends, during a match,
`$ARENA,<round>,<phase>,<t_ms>,<won0>,<won1>,<x0>,<y0>,<th0>,<x1>,<y1>,<th1>,<name0>,<name1>*CS`
(mm, mrad) every 80 ms — about 70 B, under 1 KB/s of the link's ~11. It is
built from the display's copy of the snapshot, so it cannot touch the
simulation: the host hashes are unchanged, and the board's `$RESULT` should
be the same with or without the station. `$KEY` and `$MIRROR` lines never
produce output. **Not yet watched in Wokwi**: the view has run only against
frames built from the firmware's format strings.

**Flash is the tight resource.** A first draft that parsed the shell with
`sscanf()` was 30 068 B — over the 29 KB line; replacing it with an 11-line
parser saved 2.7 KB, and one out-of-line copy of `f_sin`/`f_cos` 0.5 KB more
(with bit-identical host results). The station link (keys, OLED mirror,
`$ARENA`) then cost 2.1 KB: about 0.4 KB remain under the 29 KB line, 3.4 KB
under the 32 KB the chip has.

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| `SysTick->LOAD`, `SysTick->VAL` (24-bit `RELOAD`/`CURRENT`), `SCB->ICSR`, `SCB_ICSR_PENDSTSET_Msk` (bit 26) | `core_cm0plus.h` |
| no `DWT`/`CYCCNT` on the M0+ | `core_cm0plus.h` defines neither |
| the kernel's `SysTick_Config(SystemCoreClock / OS_TICK_HZ)` → `LOAD` = 47 999 | `_lib/os.c` line 221 |
| `FLASH_BASE` = `0x08000000` | `stm32c031xx.h` |
| `_sidata`, `_sdata`, `_edata` (build ID span) | `_startup/c031c6/link.ld` |
| joystick: HORZ 0 V = right, VERT 0 V = bottom; SEL shorts to GND | Wokwi `wokwi-analog-joystick` page (and `pad.h`) |
| pushbutton pins `1.l/1.r/2.l/2.r`, `key` attribute | Wokwi `wokwi-pushbutton` page |
| SSD1306 pins GND VCC SCL SDA, default `0x3c`, `i2cAddress` attribute | Wokwi `board-ssd1306` page |
| mini-sumo: 77 cm ring, 2.5 cm white border, 10 × 10 cm, 500 g | the common mini-sumo class rules (no single authority; values chosen to match) |

## Verified by execution — on the host

`host\run.bat` (MinGW-w64 gcc 15.2.0, `-std=c11 -ffp-contract=off`), all checks
pass, exit 0. Every number below is **measured on the host**:

| Check | Result |
|---|---|
| full throttle from rest: top speed / time to half speed | 0.800 m/s / **50.0 ms** (motor model's closed form: 47.7 ms) |
| launch traction-limited | wheels slip: yes |
| push from the centre: target nose-on braking / side-on braking / nose-on pushing back 100 % | 1030 ms / 1085 ms / **held, 1 mm in 6 s** |
| distance sensor, target 300 mm ahead, 1000 reads | mean 299 mm, 39 misses, side cones 0 |
| target 30° left at 250 mm | L = 155 mm, C and R none |
| edge bits: nose on the line / centre | `0x3` / `0x0` |
| league, 6 strategies × 100 matches a pair (1500 matches) | 19.3 h of match time in 1.9 s (~36 000×) |
| same league again / next seed | `4C26E6F2` both times / `B9662C8B` |
| built at `-O0`, `-O2`, `-Os` | identical league and tournament hashes |
| firmware tournament, seed 2026, starter | **5-4-11**, hash `3AA81370`, repeats |
| `scene.c` + `oled.c` drawing 600 ms into a match | ring at radius 31, opponent disc 26/29 pixels lit |
| `cheater.c` (a `static` local) | rejected: `b calls.0` |

The league (seed 2026, 100 matches a pair):

| # | Strategy | P | W | D | L | rounds W | rounds L | self-outs | Pts |
|---|---|---|---|---|---|---|---|---|---|
| 1 | Spinner | 500 | 390 | 21 | 89 | 808 | 248 | 4 | 1191 |
| 2 | Bull | 500 | 375 | 42 | 83 | 771 | 246 | 2 | 1167 |
| 3 | Turtle | 500 | 221 | 69 | 210 | 440 | 475 | 6 | 732 |
| 4 | Matador | 500 | 201 | 36 | 263 | 476 | 589 | 1 | 639 |
| 5 | student (starter) | 500 | 152 | 105 | 243 | 329 | 455 | 27 | 561 |
| 6 | Coward | 500 | 4 | 41 | 455 | 19 | 830 | 20 | 53 |

With `alice.c` and `bob.c` added (8 strategies, 2800 matches): alice 3rd
(415-47-238), above Turtle and Matador; her firmware-tournament line at seed
2026 is 8-5-7, hash `E5D4AA49`. Bob is last: 823 of his 1313 lost rounds are
self-outs — his edge-walking idea, measured.

## Status

**Builds with zero warnings** (FLASH 29 244 B, RAM 9 872 B). The world,
referee, strategies and drawing code are **verified on the host** as above.

**Not yet watched running in Wokwi**, and nothing here has executed on a
Cortex-M0+. When someone does, look first at:

1. **`stats` after a match at x8**: the mean/worst `match_step()` cycles and
   the achieved speed. All CPU-cost statements in the deck are estimates until
   then; if a step costs much more than ~20 000 cycles, x8 will run slower than
   asked (by design, the 14 ms cap slows the match rather than skipping steps).
2. **The tournament hash at seed 2026**: the board's `$RESULT` should carry
   `3AA81370`, the host's. If it does not, ARM soft-float and the PC disagree
   somewhere, and the submission check (host re-run) cannot be used as is.
3. **The OLED and controls**: the ring and both robots drawn (open = student,
   filled = bot), HORZ direction on the joystick in fun mode (right should
   turn right), the countdown beeps, and that the display keeps updating at x8.
4. **The station** (`tools/station/station.py`): the arena view follows the
   OLED, the keys drive the menu, and `$RESULT` at seed 2026 still carries
   `3AA81370` with the station connected. Not yet tried against Wokwi.
