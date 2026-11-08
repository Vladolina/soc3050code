# 13 — Drone Attitude

**Part 3, Applications — lesson 2 of 8: the first drone.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz, no FPU) on the
app board, plus a Wokwi MPU6050.

A quadcopter flies on the chip — a simulated one. A **world** task (500 Hz)
steps a rigid-body X-quad on a gimbal and synthesises an IMU with noise, a
drifting gyro bias, motor vibration and the MPU6050's low-pass filter. A
**controller** task (250 Hz) — the student's `control.c` — sees only a
`sensors_t` and writes only an `actuators_t` (`control.h`): complementary
filter, angle P → rate PID cascade with anti-windup and saturation, X mixer,
arming rules and failsafes. That is **Software-In-The-Loop**, and the same
two C files link into `host/sitl.c`, which flies them on the PC and grades
them. The OLED shows an artificial horizon (estimate solid, truth dotted),
four motor bars and the measured loop rate; the joystick sets roll and pitch,
A arms, B is a gust, the knob is the rate loop's Kp, and SEL swaps the world
for the real MPU6050 to study the estimator alone. `$ATT` frames go to lesson
08's `host.py`.

The syllabus proposed Webots. It is not installed here and could not be
verified, so the plant runs inside the firmware instead — Slide 1 explains
why that is the course's answer to "how do we fly a drone without a drone",
and Slide 22 measures what it costs.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 30 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: Parts 0–8, ~2 hours, a tuning contest with a score, **nothing handed in** |
| `control.h` | **the contract**: `sensors_t`, `actuators_t`, `control_step()`, tuning struct, failsafe codes, frames and signs |
| `control.c` | **subject — the student's file.** Complementary filter, angle and rate loops, D on measurement, anti-windup, X mixer, saturation, arming and failsafes. Pure C |
| `world.h`, `world.c` | **subject — the plant.** Euler's equations, quaternion kinematics, motor lag and saturation, drag torque; IMU with bias walk, noise, vibration (integer phase + sine table), DLPF. Pure C |
| `Main.c` | five RTOS tasks (world, ctrl, pilot, shell, disp), SysTick cycle counting, MPU6050 driver and axis mapping, artificial horizon, `$ATT`/`$EST` telemetry, shell `stats alpha aw mix kick hold quiet loud`, the station's keys (`$KEY`) and OLED mirror (`$FB`) |
| `host/sitl.c` | the SITL test: 24 checks; `sitl score ANGLE_KP RATE_KP [KI [KD [ALPHA]]]` for the Lab Part 1 contest |
| `host/run.bat`, `host/run.sh` | build and run it with the PC's `gcc` |
| `host/cycles.py` | the cycle budget: runs this `Main.elf`'s `world_step`, `world_sense` and `control_step` on lesson 10's Cortex-M0+ model (`10_Fixed_Point_And_DMA/host/m0sim.py`) |
| `build.bat` | `LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror` |
| `_lib/kbd.h`, `_lib/kbd.c`, `_lib/keys.c` | (shared) the station's `$KEY` down/up frames, written into the same `pad_t` the pins fill |
| `_lib/mirror.h`, `_lib/mirror.c` | (shared) the OLED, copied to the station as `$FB` frames while it asks |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board plus `wokwi-mpu6050` on PB8/PB9 at 0x68; `wokwi.toml` also forwards the serial port to the station (`rfc2217ServerPort = 4000`) |

## Build and run

```
build.bat          # FLASH 28524 B / 32 KB, RAM 11120 B / 12 KB, zero warnings
host\run.bat       # PASSED: 0 of 24 checks failed
uv run host\cycles.py         # needs Main.elf; about 35 s
simulate.bat
uv run ..\08_UART_And_Python_Host\host.py --file cap.txt --chart ATT,2,-300,300
```

RAM is 90 % used: five task stacks (256–384 words) are 6.4 KB of it, the
OLED framebuffer 1 KB, the station's key state and mirror under 0.3 KB. `stats` prints each stack's high-water mark; nobody
has read those numbers yet, so the stack sizes are lesson 09's experience,
not a measurement.

## Run it from the station

The OLED is 128 × 64 pixels in a simulator window. The ground station draws
it large and gives every control a key:

```
uv run tools/station/station.py        # from the repository root
```

Start the simulation from the **Wokwi VS Code extension** (it reads
`wokwi.toml`, whose `rfc2217ServerPort = 4000` forwards USART2 to the
station; the browser route cannot). The first `$ATT` frame selects the
station's lesson 13 view, `tools/station/views/l13_attitude.py`, which takes
the whole left of a maximised window (the OLED moves to a small panel above
the console): ARMED / disarmed in a coloured chip; an artificial horizon as
tall as the window - the estimate filled, the truth dashed, your stick dotted
(in MPU mode, the accelerometer-only tilt dashed) - with roll and pitch
printed large; the quad from above with each motor filled by its thrust;
charts of roll, pitch and estimate − truth; the board's `failsafe:` and
`CRASH` lines as a red banner; and the keys along the bottom.

| Key | Does | | Key | Does |
|---|---|---|---|---|
| arrows | the joystick (roll / pitch setpoint) | | `s` | SEL: SIM ↔ MPU |
| `a` | A: arm / disarm | | `1`–`5` | the knob: Kp ×0.19, ×0.44, ×1, ×2.3, ×5.3 |
| `b` or `p` | B: gust | | `?` | print this list |

`-` and `+` do nothing here (the digits already step the gain). The shell
still works from the station's command line; `quiet` stops the 25 Hz
`$ATT`/`$EST` frames and `loud` starts them again. Text lines (`mode:`,
`failsafe:`, `CRASH`) are never silenced. The station's `$KEY` and `$MIRROR`
lines are routed before the shell's own parser and are never answered.

**Not yet watched in Wokwi.** The code builds and the view was exercised on
the PC with frames in the firmware's format; nobody has yet seen the station
drive this lesson.

## Class tasks - one change, predict, run, explain

Your instructor gives you **one task number**. For that task:

1. **Predict.** Before you build, write down what you expect to see - in the
   station (the horizon, the motors, the charts) and in the host test.
2. **Change.** Make only the change the task asks for.
3. **Run.** `host\run.bat` first: which checks fail, and what do its tables
   say? Then `build.bat`, restart the simulation, and fly it from the station.
4. **Explain.** In one sentence: why did it behave that way? If your
   prediction was wrong, say which line you misread.
5. **Show.** `git diff` - the instructor reads your change there.
   Afterwards `git checkout -- .` puts the lesson back.

| Task | Change | Ask yourself |
|---|---|---|
| **T1** Lazy angle loop | `control.c`: `angle_kp = 9.0f` -> `4.5f` | Faster or slower to reach a 20 deg tilt? More or less overshoot? |
| **T2** No D | `control.c`: `rate_kd = 0.0008f` -> `0.0f` | How much does such a tiny number matter? |
| **T3** No I | `control.c`: `rate_ki = 0.15f` -> `0.0f` | Which host check fails - and why *that* one? |
| **T4** A slow wrist | `control.c`: `max_rate = 3.5f` -> `1.0f` (rad/s) | How long must a 20 deg step now take, at most 57 deg/s? |
| **T5** Weak arms | `control.c`: `max_out = 0.25f` -> `0.08f` | Press B for a gust: how far does it tilt now? |
| **T6** A nervous failsafe | `control.h`: `CTRL_TILT_LIMIT 1.0472f` (60 deg) -> `0.5236f` (30 deg) | Push the stick all the way. What happens, and why? (`STICK_MAX` in `Main.c`) |
| **T7** Slow motors | `world.c`: `tau_motor = 0.030f` -> `0.100f` | The controller did not change. Why does the drone? |
| **T8** A heavy wing | `world.c`: `ixx = 0.004f` -> `0.016f` | Roll and pitch both step 20 deg: which one changes? |
| **T9** A shaking frame | `world.c`: `vib_accel = 6.0f` -> `24.0f` | Which suffers - the estimate, the flight, or both? |
| **T10** A drifting gyro | `world.c`: `gyro_walk = 0.0005f` -> `0.005f` | Ten times the drift: does the drone notice? |

### Answers - read them after you have written your prediction

Measured on the host (`host/run.sh`: the real `control.c` and `world.c`), each
change applied alone and then undone; every one builds with zero warnings.
Without any change: roll +20 deg rises in 0.200 s, 0.3 % overshoot, settles in
0.336 s; a gust peaks at 11.2 deg and is back within 3 deg in 0.166 s; the
estimate's error is 0.20 deg RMS; 24 of 24 checks pass.

| Task | What happens | Why, in one sentence | Host test |
|---|---|---|---|
| **T1** | Slower and gentler: roll +20 rises in 0.442 s (was 0.200), no overshoot, settles in 0.706 s; a gust takes 0.286 s to recover (was 0.166). | The angle loop asks for half the tilt rate per degree of error, so the drone is pulled back half as hard. | **2 checks fail** (rise < 0.35 s, roll and pitch) |
| **T2** | Barely different: roll settles in 0.470 s (was 0.336), pitch rises in 0.226 s (was 0.140). | D acts on how fast the rate error changes; on this well-damped stand it is a small polish, not what holds the drone up. | passes |
| **T3** | Flies as well as before (roll settles 0.302 s) - but the anti-windup comparison fails. | With no integrator there is nothing to wind up, so switching anti-windup off can no longer make things "far worse": the test was measuring the I term. | **1 check fails** |
| **T4** | Slower steps: roll rises in 0.282 s; the held-then-released test peaks at 32.6 deg and needs 0.780 s (was 0.282). | The angle loop may only ask for 1 rad/s (57 deg/s), so every large correction is rate-limited. | passes |
| **T5** | Steps barely change, but **a gust tilts it twice as far**: 22.6 deg (was 11.2), back in 0.364 s. | The rate loop can only push 8 % differential thrust: enough to steer gently, not to fight a hard kick. | passes |
| **T6** | Full stick can disarm the drone: on the host, a held 30 deg setpoint ends in TILT although the true tilt peaked at 29.9 deg - the estimate's noise crossed the line. The 15 rad/s kick is caught in 40 ms instead of 84. | `STICK_MAX` asks for 30 deg and the failsafe now fires at 30 deg: a limit must sit clearly above anything the pilot can command. | **1 check fails** (the 30 deg held setpoint ends in TILT) |
| **T7** | Wild: roll +20 overshoots 33 %, oscillates 52 deg/s and never settles in 3 s; a gust takes 2 s. | The motors answer three times later, so the controller keeps pushing after it should have stopped - delay in the loop destabilises gains tuned for fast motors. | **8 checks fail** |
| **T8** | Roll overshoots 23 % and a gust tilts it 28.8 deg - **pitch is exactly as before** (0.140 s, 1.8 %). | Four times the roll inertia, same gains: the roll axis now needs gains of its own. | **3 checks fail** |
| **T9** | The estimate gets noisier (0.264 deg RMS noise, was 0.064) and the flight with it: pitch and roll -20 no longer settle within 1 s. | Vibration reaches the accelerometer, the filter passes 2 % of it each step, and the controller chases it. | **2 checks fail** |
| **T10** | Nothing visible: every check passes; the drone still holds level within 1.0 deg over 30 s. | The complementary filter's accelerometer half keeps pulling the gyro's drifting integral back - that is what it is for (compare Lab Part 2, gyro only). | passes |

Worth comparing side by side: **T7 and T8** - the controller is unchanged and
the drone changed; **T6** - a safety limit that fires on a legal command;
**T3** - a test that fails because it measured something that is gone.

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| `SysTick->LOAD`, `SysTick->VAL` (24-bit `RELOAD`, `CURRENT`), SysTick at `0xE000E010` | `core_cm0plus.h`: `SysTick_LOAD_RELOAD_Msk 0xFFFFFF`, `SysTick_VAL_CURRENT_Msk`, `SysTick_BASE (SCS_BASE + 0x0010)` |
| `__get_PRIMASK`, `__set_PRIMASK`, `__disable_irq` | `cmsis_gcc_m.h` |
| the kernel's SysTick is 1 kHz from `SystemCoreClock`: `LOAD` = 47 999 | `_lib/os.c`: `SysTick_Config(SystemCoreClock / OS_TICK_HZ)`, `OS_TICK_HZ 1000u` |
| no other register is touched in this lesson's own files | grep: pins, I²C, ADC, TIM3 are in `_lib/` |
| `wokwi-mpu6050`: pins VCC GND SCL SDA (XDA/XCL unimplemented), address 0x68 (0x69 with AD0 high), attributes `accelX/Y/Z` in g (default 0, 0, 1), `rotationX/Y/Z` in °/s, `temperature` | docs.wokwi.com/parts/wokwi-mpu6050 |
| MPU6050 registers `CONFIG 0x1A`, `GYRO_CONFIG 0x1B`, `ACCEL_CONFIG 0x1C`, data at `0x3B`, `PWR_MGMT_1 0x6B`, `WHO_AM_I 0x75` | Adafruit_MPU6050.h |
| 14-byte read: accel X,Y,Z, temperature, gyro X,Y,Z; 16384 LSB/g at ±2 g; 131 LSB/(°/s) at 250 °/s; both ranges the reset default | Adafruit_MPU6050.cpp / .h |
| `DLPF_CFG` = `CONFIG` bits 2:0, value 3 = 44 Hz; `FS_SEL` bits 4:3; `SLEEP` = `PWR_MGMT_1` bit 6 | Adafruit_MPU6050.cpp (`setFilterBandwidth`, `setGyroRange`, `enableSleep`), `mpu6050_bandwidth_t` |
| USART2 PA2/PA3 AF1, I²C1 PB8/PB9 AF6, TIM3_CH1 PA6 AF1, app-board pins | `_lib/pad.h`, `_lib/i2c.c`, `_lib/beep.h` (lessons 09, 12) |
| `host.py --chart NAME,INDEX` counts INDEX from the frame name (field 0) | `08_UART_And_Python_Host/host.py` |
| every `regs` block renders | `_build/regdiag.py render()` run on both blocks |

Physical parameters (mass, inertia, thrust, motor lag, noise levels) are
**chosen to be typical of a 250-class quad, not measured from one**. They
are stated on Slide 4 and Slide 6 as a model, not as facts.

## Verified by execution — on the host

`host/run.bat` (MinGW gcc 15.2, `-O2`), seed 12345, world 500 Hz, control
250 Hz, **the same `world.c` and `control.c` as the firmware**:

| Test | Result |
|---|---|
| roll +20° step | rise 0.200 s, overshoot 0.3 %, settled (±1°) at 0.336 s |
| pitch +20° step | rise 0.140 s, overshoot 1.8 %, settled at 0.284 s |
| roll −20° step | rise 0.134 s, overshoot 1.7 %, settled at 0.290 s |
| gust 229 °/s roll, −115 °/s pitch | peak 11.2°, back inside 3° after 0.166 s |
| arm with throttle 0.5 / tilted 30° | refused: THROTTLE / TILT |
| kick 15 rad/s | tilt failsafe 84 ms later (true tilt 63°) |
| one NaN from the gyro / frozen `seq` | disarm on the first step / after 20 ms |
| re-arm | only after the switch is cycled |
| alpha 0 / 0.9 / **0.98** / 0.995 / 1 | error RMS 3.18 / 0.33 / **0.20** / 0.60 / 14.54°; jitter 5.97 / 0.33 / 0.064 / 0.016 / 0.002° |
| Kp ×0.125 / ×0.25 / ×0.5 / **×1** / ×2 / ×4 / ×8 | settle never / 2.17 / 0.57 / **0.34** / 0.35 / 0.37 / 0.37 s; rate RMS 235 / 7.2 / 1.0 / 1.1 / 1.0 / 1.1 / **19.2** °/s |
| anti-windup on / off (held 2 s at 30°) | peak 29.8° / **179.5°, tilt failsafe** |
| mixer roll sign flipped in hover | tilt failsafe after 0.360 s, then crashed |
| `sitl score 9 0.12` (defaults) / `score 18 0.35 0.3 0.002` | 0.620 s / 0.278 s |

**Cycle budget**, `host/cycles.py` — this `Main.elf` run through lesson 10's
Cortex-M0+ instruction model (TRM timing table), take-off and a 20° step:

| | 1 wait state | 0 wait states |
|---|---|---|
| `world_step` mean | 17 402 cycles (363 µs) | 12 205 |
| `world_sense` mean | 16 489 (344 µs) | 11 512 |
| `control_step` flying | 25 938 (540 µs) | 18 023 |
| `control_step` disarmed | 14 118 | 9 775 |
| world + control CPU | **48.8 %** | 34.1 % |

The first draft measured 72.8 % (`world_sense` 38 097 cycles: float
Gaussians and `f_sin`/`f_cos` per step). Integer Gaussians, hoisted divides
and a 64-entry sine table brought it to 48.8 % without changing a single
host result beyond noise. Slide 22 tells it.

**Three things the host test found in the lesson's own drafts, all fixed and
now taught:**

1. **Too much gain did nothing.** The first world had no sensor lag, so the
   rate loop stayed stable even at ×12 — the knob's "too high" end was a
   lie. The MPU6050's 44 Hz filter was added to the sensor model (a real lag
   the first world lacked), the default rate Kp doubled to 0.12, and the knob
   now scales Kp alone (scaling Kd with it added phase lead). ×8 now buzzes.
2. **The controller armed a drone held at 30°**: a few milliseconds after
   reset, the sensor filter and the estimate both still read "level". Arming
   now waits for 0.5 s of good samples (Slide 17).
3. Raising the rate gain to where it belongs showed the **cascade rule** from
   the other side: at ×0.125 the inner loop is slower than the outer and the
   loop oscillates rather than crawls (Slide 19).

## Status

**Run in Wokwi on 2026-11-08: it freezes right after the banner.** The banner
prints in full (`OLED : 0x3C ready`, `MPU6050 : not found`), then nothing:
no `$ATT`, no OLED picture, no sound, no response to A. Paused in the
debugger (VS Code: **ARM: Attach to a lesson in Wokwi (pick)**, then
`-exec p 'os.c'::tasks[0]@6` in the Debug Console), the task table read:

| task | CPU ticks | state |
|---|---|---|
| world (prio 5) | 12 734 | ready, `wake = 2` |
| ctrl, pilot, disp | 0 | ready, never ran |
| shell | 0 | blocked on the UART queue |
| idle | 2 | |

So **the world task never sleeps**: in Wokwi one `world_step` + `world_sense`
takes longer than its 2 ms period, `os_delay_until()` then returns at once
every time (it only sleeps when ahead of schedule), and every lower-priority
task starves forever. On silicon the same step is 0.36 ms (`host/cycles.py`,
48.8 % for world + control together), so Wokwi's emulated CPU is evidently
much slower than real time relative to its SysTick - possibly worse on a
busy PC. Ruled out on the way: deadlock (no task holds two locks), stack
overflow (world and control need at most 144 B of 1 KB, measured on the
M0+ model), the clock (48 MHz is set), the RTOS itself (lesson 08 ran on it).

**Not yet fixed.** The plan: (1) an overrun guard in `task_world` and
`task_ctrl` - when behind schedule, drop the lost time and still yield, so
lower tasks always run; (2) if Wokwi needs it, a slow-motion pace as in
lesson 12 - the world and control periods both multiplied by one factor, so
their 2:1 ratio holds (if control outran the world it would see a stale
IMU and trip `FS_STALE`). Still to measure: `-exec p world.t` against
`-exec p 'os.c'::ticks` after a pause - their ratio is the factor.

Builds with **zero warnings** (FLASH 28 524 B, RAM 11 120 B). `host/run.bat`
passes **24 of 24** checks; `host/cycles.py` passes (48.8 % at 1 wait state).

**Not yet watched running in Wokwi.** Nothing on the OLED, no button, no
slider and no `stats` output has been seen, and neither has the station
route (keys, `quiet`/`loud`, the OLED mirror, the attitude view). When someone does, look first at:

1. **The loop rate on the OLED and `stats`.** It must say 250 Hz with no
   late wake-ups in SIM mode. If the world and controller do not fit the CPU
   in Wokwi's timing, everything else is moot. Compare `stats`' cycle counts
   with the table above — Wokwi's are a third opinion.
2. **The signs.** Joystick right must make the horizon's right end rise and
   the left motor bars (FL, RL) grow; in MPU mode, `accelY` = +0.5 g must
   read roll **+26.6°** (Lab Part 7). The mounting assumption (chip Y left)
   is the lesson's, not Wokwi's documentation's.
3. **The display's cost.** The OLED flush polls the I²C bus for ~25 ms a
   frame at the lowest priority; check the frame rate it actually reaches
   (target 15 fps) and, in MPU mode, how late the control task's reads run
   behind it.
