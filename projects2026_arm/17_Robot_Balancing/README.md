# 17 — Robot Balancing: PID vs LQR

**Part 3, Applications — lesson 17.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz, no FPU), app board.

A two-wheeled self-balancing robot simulated **inside the firmware**: a 1 kHz
world task integrates the full nonlinear equations of a wheeled inverted
pendulum driven by DC motors with back-EMF and a supply limit, and synthesises
the sensors a real robot has — gyro (noise, bias), accelerometer (noise,
fooled by the robot's own acceleration, mounted 1° crooked) and wheel encoder
(relative to the body, quantised). A 200 Hz control task sees **only** those
sensors through `control_step(const sensors_t*, actuators_t*)` and answers
with a voltage, by one of three controllers chosen live with the joystick
button: PID on tilt, cascaded PID, or LQR. The LQR gain is computed by
`host/lqr.py` — standard library Python that parses the same `params.h`,
linearises, discretises, iterates the Riccati equation and proves the result
stable. The OLED draws the robot from the side with a tilt strip chart; `$BAL`
frames go to lesson 08's `host.py`, or to the PC's station
(`tools/station/station.py`), which draws the robot large and sends keys.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 30 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: nine parts (0-8), ~2 hours, a Q/R contest, **nothing handed in** |
| `params.h` | **every** physical number, timing and LQR weight; read by `world.c`, `control.c` and (parsed) by `host/lqr.py` |
| `control.h` | the SITL contract: `sensors_t`, `actuators_t`, `control_step()` |
| `control.c` | **subject.** Complementary filter, encoder + tilt position, command slew, failsafe, PID tilt / cascade / LQR (`LQR_K` pasted from `lqr.py`) |
| `world.c`, `world.h` | **subject.** The plant: Lagrange equations, motor model, semi-implicit Euler at 1 kHz, payload, push, sensor synthesis with a seeded xorshift32 |
| `view.c`, `view.h` | the OLED scene: ground ticks, wheel with spoke, body at the tilt, payload, target caret, tilt history, FALLEN box. Pure C on `oled.h` |
| `Main.c` | five RTOS tasks (world 1 kHz, control 200 Hz, ui 50 Hz, shell, display 10 Hz), single-writer request variables, TIM14 microsecond stopwatch, `$BAL` telemetry, shell `stats mode noise alpha vbat push k quiet loud`; the station's `$KEY` lines into the pad (`kbd`, one mutex for the shell and ui tasks), `$FB` OLED mirror frames from the display task |
| `host/lqr.py` | LQR design and checks: A, B from `params.h`; ZOH `expm`; DARE by iteration; eigenvalues (Faddeev-LeVerrier + Durand-Kerner) and a power-iteration cross-check; linear recovery from 6°; `--check control.c`; `--pid KP,KI,KD` tilt-only analysis; `--Q --R --payload` |
| `host/sitl.c` | the host verification: the firmware's own `world.c`, `control.c`, `view.c` and `_lib/oled.c` on a PC; `--trace MODE [push\|drive\|tilt]` CSV |
| `host/run.sh`, `host/run.bat` | run `lqr.py --check`, then compile and run `sitl` with plain `gcc`; non-zero exit on failure |
| `build.bat` | `LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror` |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board, unchanged from `_targets/app-board-diagram.json` — the robot needs no parts; `wokwi.toml` forwards USART2 to `rfc2217://localhost:4000` for the station |
| `tools/station/views/l17_balance.py` | the station's view of `$BAL` (outside this folder) |

**No shared file was changed.**

## Build and run

```
build.bat        # FLASH 26280 B / 32 KB, RAM 10408 B / 12 KB, zero warnings
simulate.bat     # paste diagram.json, upload Main.elf
host\run.bat     # or: bash host/run.sh  - exits 0, "all checks passed"
python ..\08_UART_And_Python_Host\host.py --file capture.txt --chart BAL,3,-30000,30000
```

Controls: joystick up/down drives (±0.4 m/s), **space** (SEL) cycles the
controller, **a** stands the robot up, **b** pushes it, the knob loads it
(0-0.6 kg at 20 cm). `$BAL` fields: ms, mode, true tilt (mdeg), estimated
tilt (mdeg), x (mm), speed (mm/s), motor mV, fallen, payload (g).

## Run it from the station

The PC's ground station draws the robot large instead of on a 128 x 64 panel:

```
simulate.bat                          # Wokwi in VS Code; wokwi.toml forwards USART2 to port 4000
python tools/station/station.py       # from the repository root
```

The first `$BAL` frame selects `tools/station/views/l17_balance.py`: the robot
from the side at the **true** tilt, the controller's **estimated** tilt as a
dashed ghost on the same axle, the wheel rolling along a ground with a tick
every 10 cm, and strip charts of true vs estimated tilt, the motor volts and
the position, under the controller's name. `$BAL` is only 10 Hz; the view does
not interpolate between frames (that would cost a frame of lag) but eases the
picture toward the latest one over ~50 ms, and the charts plot the frames as
they arrived. The station's OLED pane is the board's own screen, sent by
`mirror` as `$FB` frames after each `oled_flush()`.

Keys, held as long as the key is held: arrows drive, `a` stands it up, `b` or
`p` pushes, `s` cycles the controller, `1`-`5` set the payload in fifths,
`-` / `+` make button B's push 50 mN s smaller / bigger (this lesson has no
pace to slow), `?` lists them. The shell words `quiet` and `loud` stop and
restart `$BAL`. `$KEY` and `$MIRROR` lines never get an answer.

**The station route has not yet been watched in Wokwi.** The firmware builds
and the view was exercised headless with synthetic `$BAL` frames only.

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| `RCC_APBENR2_TIM14EN` (bit 15), `TIM14`, `TIM_TypeDef` fields `CR1 EGR CNT PSC ARR`, `TIM_EGR_UG` (bit 0), `TIM_CR1_CEN` (bit 0), `TIM_CR1_ARPE` (bit 7) | `stm32c031xx.h` — every register and bit name used, grepped |
| TIM14 counting at 1 MHz with `PSC = 47` | lesson 06's `Main.c` does the same on this chip |
| no `DWT->CYCCNT` on the M0+ | `core_cm0plus.h` defines no DWT cycle counter |
| SSD1306 pins `GND VCC SCL SDA`, `i2cAddress` default `0x3c` | Wokwi `board-ssd1306` page (fetched 2026-10-03) |
| joystick pins `VCC VERT HORZ SEL GND`; HORZ 0 V = right; **space** presses SEL | Wokwi `wokwi-analog-joystick` page (fetched 2026-10-03) |
| pushbutton contacts `1.l/1.r`, `2.l/2.r`; `key` attribute | Wokwi `wokwi-pushbutton` page (fetched 2026-10-03) |
| app board pins (PA0/PA1/PA4/PB3/PB4/PB5/PA6/PB8/PB9) | `_lib/pad.h`, `_targets/app-board-diagram.json` |
| deepest stack frames ~150 B in `world`/`control` | `arm-none-eabi-gcc -fstack-usage` on this lesson's sources |
| equations of motion, motor model, linearisation | derived here (Lagrange); `lqr.py` linearises the *same* expressions `world.c` integrates |

## Verified by execution — on the host

`host/run.sh` (MinGW-w64 gcc 15.2.0, Python 3.12, seed 12345, sensor noise on),
exit 0, `all checks passed`:

**lqr.py** — open-loop poles +7.04, 0, −5.68, −70.4 /s (falling: e-fold
every 142 ms). Q = diag(30, 2, 50, 0.5), R = 1, 5 ms: Riccati converged in
2729 iterations, **K = [−5.1239, −15.255, −21.262, −2.4627]**. Closed-loop
eigenvalues 0.99606, 0.97958, 0.95914, 0.66211 — **spectral radius 0.99606**
(power check 0.99655). Linear model from 6°: peak 2.23 V, settled in 2.10 s.
`--check control.c`: matches.

**sitl** — the nonlinear model, the firmware's own controller:

| controller | settle from 6° | peak V | largest push | 30 s, no command | payload through 0.1 N s | drive 0.3 m/s, 5 s |
|---|---|---|---|---|---|---|
| PID tilt | never | 7.40 | 0.62 N s | fell at 7.8 s, 3.3 m away | 0.16 kg | −1.71 m (backwards) |
| cascade | 1.70 s | 2.10 | 0.60 N s | −0.09 m | 1.88 kg | 1.72 m |
| **LQR** | 3.00 s | 1.80 | **0.78 N s** | −0.07 m | 1.36 kg | 1.72 m |

Tilt estimate error (complementary filter) 1.1–1.4° rms. Two identical runs
agree bit for bit. The OLED frame is drawn by `view.c` on the PC and printed
pixel for pixel (756 lit).

Also measured on the host for the slides and lab: `lqr.py --pid` eigenvalues
(tilt-only PD runs away; with I an 89 s swing; position always at 1); Q/R
variants (slide 19 — Q_θ = 500 beats the default on every test); the cascade
**falls** in the drive test if its lean clamp is ±0.10 rad; noise survivable to
×8, falls at ×16; alpha sweep (accelerometer-only falls at 0.2 s); a 0.20 N s
push wins at 1.8 V supply; LQR plus speed feed-forward covers exactly the
target's 1.41 m instead of 1.72 m.

**What the host cannot show:** anything about the Cortex-M0+ itself — CPU time
per step (slide 22 is an estimate, ~240 µs for the world step), stack
high-water marks, I²C timing — and anything about Wokwi.

## Status

Builds with **zero warnings** (FLASH 26 280 B, RAM 10 408 B). The host
verification passes and its numbers are quoted above and in the deck, each
labelled *measured on the host*.

**Not yet watched running in Wokwi.** When someone does, look first at:

1. **`stats`** — the world step's microseconds and the CPU share. If the world
   step is anywhere near 1000 µs the 1 kHz physics cannot keep time, and
   everything else in the lesson is off. Also the `world`/`control` stack
   high-water marks against their 160 words.
2. **The OLED**: does the robot stand under LQR with nobody touching it, and
   does `b` make it lean back and return to the caret? Does PID tilt (space,
   then `a`) roll backwards and fall after several seconds, as the host
   predicts?
3. **The display's I²C cost** while the 1 kHz world runs above it: does the
   10 Hz picture keep up, and do `$BAL` frames arrive every 100 ms with good
   checksums in `host.py`?
