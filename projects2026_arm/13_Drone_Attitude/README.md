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
| `Main.c` | five RTOS tasks (world, ctrl, pilot, shell, disp), SysTick cycle counting, MPU6050 driver and axis mapping, artificial horizon, `$ATT`/`$EST` telemetry, shell `stats alpha aw mix kick hold` |
| `host/sitl.c` | the SITL test: 24 checks; `sitl score ANGLE_KP RATE_KP [KI [KD [ALPHA]]]` for the Lab Part 1 contest |
| `host/run.bat`, `host/run.sh` | build and run it with the PC's `gcc` |
| `host/cycles.py` | the cycle budget: runs this `Main.elf`'s `world_step`, `world_sense` and `control_step` on lesson 10's Cortex-M0+ model (`10_Fixed_Point_And_DMA/host/m0sim.py`) |
| `build.bat` | `LIBS=retarget os uart proto i2c oled adc pad beep` |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board plus `wokwi-mpu6050` on PB8/PB9 at 0x68 |

## Build and run

```
build.bat          # FLASH 26292 B / 32 KB, RAM 10848 B / 12 KB, zero warnings
host\run.bat       # PASSED: 0 of 24 checks failed
python host\cycles.py         # needs Main.elf; about 35 s
simulate.bat
python ..\08_UART_And_Python_Host\host.py --file cap.txt --chart ATT,2,-300,300
```

RAM is 88 % used: five task stacks (256–384 words) are 6.4 KB of it, the
OLED framebuffer 1 KB. `stats` prints each stack's high-water mark; nobody
has read those numbers yet, so the stack sizes are lesson 09's experience,
not a measurement.

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

Builds with **zero warnings** (FLASH 26 292 B, RAM 10 848 B). `host/run.bat`
passes **24 of 24** checks; `host/cycles.py` passes (48.8 % at 1 wait state).

**Not yet watched running in Wokwi.** Nothing on the OLED, no button, no
slider and no `stats` output has been seen. When someone does, look first at:

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
