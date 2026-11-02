# 15 — Robot Line Follower

**Part 3, Applications — the first robot.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz), the app board.

A line-following robot races laps of four taped tracks on the OLED — and the
robot exists only inside the firmware. A **world task** (priority 5, 200 Hz)
steps a differential-drive robot with motor lag, a friction-circle tyre
model, wheel encoders and a seven-sensor reflectance bar, and judges laps and
DNFs. A separate **control task** (priority 4, 100 Hz) sees only the sensors
and writes only two wheel speeds, through
`control_step(const sensors_t *, actuators_t *)` in `control.h` — the
Software-In-The-Loop shape lessons 13–18 share. The joystick drives in manual
mode, the knob sets the speed, every lap beeps and prints a checksummed
`$LAP` frame for a class leaderboard, and `host/sitl.c` races the same C on a
PC to measure everything this README quotes.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 31 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: ten parts (the last, the station, optional), ~2 hours, a leaderboard, **nothing handed in** |
| `control.h` | **the contract**: `sensors_t`, `actuators_t`, `control_step()`, the tunable gains |
| `control.c` | **the student's file**: weighted-average line position, PID, corner slow-down, gap / overshoot / crossing handling |
| `world.c`, `world.h` | the robot and the judge: motors (τ 40 ms, 8 m/s²), friction circle (0.7 g), encoders, sensor synthesis with ambient and noise, laps, DNF |
| `track.c`, `track.h` | four tracks as turtle programs (OVAL, S-BENDS, HAIRPINS, FIGURE-8 with a crossing and a gap), built into ≤ 1 mm polylines |
| `view.c`, `view.h` | the OLED: track drawn once, robot as an XOR triangle, lap times, sensor bars |
| `Main.c` | five RTOS tasks, the shared-data rules, `$LINE`/`$LAP` telemetry, the shell, on-chip cycle counting, the station's keys (`$KEY`) and OLED mirror (`$FB`) |
| `build.bat` | `LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror` - the last three are the station's keyboard and OLED mirror |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board, unchanged from `_targets/app-board-diagram.json`; `wokwi.toml` forwards the serial port to TCP 4000 for the station |
| `host/sitl.c`, `host/run.bat`, `host/run.sh` | **the host test**: nine measured tables, exit 1 on failure; also `race` and `ascii` modes |
| `host/leaderboard.py` | ranks `$LAP` frames from students' serial captures, checksum-verified (standard library only) |
| `.gitignore` | keeps `host/sitl.exe` out of git |
| `tools/station/views/l15_line.py` | (outside this folder) the station's view: the race from above, sensor bar, charts, laps |

## Build and run

```
build.bat        # FLASH 29484 B / 32 KB, RAM 9744 B / 12 KB, zero warnings
simulate.bat
host\run.bat     # the races, on the PC: PASS: 0 failure(s)
python ..\08_UART_And_Python_Host\host.py --file capture.txt --chart LINE,3,-400,400
python host\leaderboard.py captures\*.txt
```

Controls: menu — joystick up/down, **A** to choose. Race — **A** start / stop
(back to the line), **B** next track, **SEL** manual ↔ auto, knob = speed
(0–2000 mm/s). Shell: `kp ki kd df slow thr N`, `noise ambient fail N`,
`gains`, `stats`, `quiet` / `loud` (stop / restart `$LINE`; `$LAP` stays).

### Run it from the station

Start the simulation from VS Code (`wokwi.toml` carries
`rfc2217ServerPort = 4000`), then `python tools/station/station.py` from
the repository root. The OLED appears large, and beside it the race from
above (`tools/station/views/l15_line.py`): the track, the robot to scale with
its sensor spots, the path it drove, a close-up in the robot's frame, the
sensor bar, error and wheel-command charts, and every `$LAP`. Keys: arrows =
stick, `a` = A, `b`/`p` = B, `s` = SEL, `1`-`5` = knob, `-`/`+` = knob -/+ 50
(100 mm/s), `?` = the list. `quiet` / `loud` in the station's command line.

`$LINE` grew eleven fields **at the end** for that map - true `x`, `y` (mm),
heading (degrees, 0 = east, + = anticlockwise), track 1-4, and the seven
readings, rightmost first - so the first eight, and `host.py --chart LINE,3`,
are unchanged; `leaderboard.py` reads only `$LAP`. The track shapes are
**not** sent: the view rebuilds them from a copy of `track.c`'s turtle
commands, checked point for point against `track_build()` (all four within
1 mm, gaps identical). Change a track and that copy is stale; a track id it
does not know is drawn from the robot's trail. **Not yet watched in Wokwi.**

### Stacks

The first build used guessed stacks (256–384 words) and linked at **11 728 B
of RAM**, 95% of the chip. The stacks were then sized from
`arm-none-eabi-gcc -fstack-usage` for this lesson's and `_lib`'s code, plus
newlib-nano's printf frames read from the disassembly (`_svfprintf_r` /
`_vfprintf_r` 144 B, `_vsnprintf_r` 120 B, `_printf_i` 64 B). Deepest chains:
`ui` ≈ 680 B (`send_frame` → `vsnprintf`; ≈ 790 B since `$LINE` grew - see below), `display` ≈ 670 B (`oled_printf`),
`world` ≈ 330 B (`world_reset` → `track_build`). Each got its chain, 64 B for
the exception and context-switch frames, and roughly a third spare:
world 160, control 160, ui 256, shell 224, display 256 words. **RAM 9 352 B.**

The station (2026-11) moved three of those figures. The longer `$LINE`
frame grew `task_ui`'s own frame 120 → 184 B and `send_frame`'s 96 → 144 B,
so `ui` went to **288 words** to keep about a quarter spare. `display` also
builds a `$FB` frame after each flush (`mirror_next` 40 B + `build` 128 B +
`snprintf`, ≈ 710 B deep with its own frame) and stayed at 256; its 96-byte
frame buffer is `static`, not on the stack. `kbd_t` and the mirror's hashes
add ~170 B of `.bss`. **RAM 9 744 B.** Read `stats` before trusting this.
`stats` prints the real high-water marks; check them before shrinking.

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| `SysTick->LOAD`, `SysTick->VAL` (24-bit `RELOAD` / `CURRENT`, counts down) | `core_cm0plus.h`: `SysTick_Type`, `SysTick_LOAD_RELOAD_Msk`, `SysTick_VAL_CURRENT_Msk` = `0xFFFFFF` |
| the kernel sets `LOAD = SystemCoreClock / 1000 − 1` (47 999) | `_lib/os.c`: `SysTick_Config(SystemCoreClock / OS_TICK_HZ)`; lesson 06's `LOAD` diagram |
| `__disable_irq()` / `__enable_irq()` | CMSIS `cmsis_gcc.h` |
| no other register is touched by this lesson's own code | `grep` of `Main.c`, `world.c`, `track.c`, `control.c`, `view.c`: only `SysTick->` and `SystemCoreClock` |
| joystick: pins VCC VERT HORZ SEL GND; HORZ 0 V = right, VERT 0 V = bottom; SEL shorts to GND; space presses it | Wokwi `wokwi-analog-joystick` page |
| SSD1306: GND VCC SCL SDA, default address `0x3c`, 128×64 | Wokwi `board-ssd1306` page |
| buzzer: pin 1 negative, pin 2 positive; `volume` attribute | Wokwi `wokwi-buzzer` page |
| app-board pins and alternate functions (PA0/PA1/PA4 analog, PB3/4/5 inputs, PA6 TIM3_CH1 AF1, PB8/PB9 I2C1 AF6, PA2/PA3 USART2 AF1) | `_lib/pad.h`, `_lib/beep.c`, `_lib/i2c.c`, lesson 09's README (Zephyr `hal_stm32` C031 pin table) |
| printf has no `%f` (newlib-nano) — gains printed as fixed point, parsed without `strtof` | brief; `--specs=nano.specs` in the build |
| corner speed limit √(0.7 g·R): 908 mm/s at R 120, 1229 at 220, 1657 at 400 | arithmetic, with `MU_G_MMS2 = 6867` |

## Verified by execution — on the host

`host\run.bat` (MinGW-w64 gcc 15.2, the same `world.c`, `track.c`,
`control.c`, `view.c` and `_lib/oled.c` with the I²C call stubbed) — **exit 0,
`PASS: 0 failure(s)`**. Default gains: kp 20, ki 0, kd 1.0, dfilt 0.5, slow
0.5, thr 300; sensor noise 15.

| Test | Result, measured on the host |
|---|---|
| 1. tracks close | all four: closure miss **0.00 mm**; 71 / 97 / 111 / 92 points; 4911 / 4638 / 7076 / 5634 mm |
| 2. default gains, knob 500 (1000 mm/s), 5 laps | OVAL **5.12 s** (lap 1 5.20), S-BENDS **5.07**, HAIRPINS **7.46**, FIGURE-8 **5.99**; max true error 2.1 / 4.8 / 3.6 / 3.1 mm; **no DNF** |
| 3. speed sweep, fastest knob that finishes 3 laps | OVAL 1000 (2.81 s), S-BENDS **775** (3.72 s), HAIRPINS **675** (6.03 s), FIGURE-8 1000 (3.29 s). Without corner slow-down: S-BENDS 675 (4.26 s), HAIRPINS 575 (6.68 s) |
| 4. P vs PD vs PID, S-BENDS knob 700 | P only kp 20: **12.56 s**, RMS 27.5 mm (weaves, slides); kp 10: 13.19 s; **PD 3.80 s, RMS 5.4 mm**; PID (ki 2) 3.76 s, RMS 7.0 mm |
| 5. noise vs D filter, S-BENDS knob 700 | noise 80: chatter **602** mm/s unfiltered, 256 at dfilt 0.5, 162 at 0.25 — but RMS error 8.5 mm at 0.25 vs 5.7 at 0.5; lap times within 40 ms |
| 6. dead sensor, knob 500 | edge sensor: no effect; centre sensor: OVAL 6.48 s (vs 5.12), S-BENDS 6.07 s |
| (race mode) ambient 400 | HAIRPINS **DNF** at thr 300; **7.46 s** at thr 650 |
| 7. determinism | FIGURE-8 raced twice: identical results |
| 8. OLED | frame drawn by `view.c` printed as text (slide 22) |
| 9. shortcuts | box test leaves 2.8–3.7 segments per step on average (max 6) of 70–110; OLED **448–501 bytes per 10 Hz frame** vs 1032 for a full repaint |

## Status

- **Builds with zero warnings**: FLASH 29 484 B, RAM 9 744 B.
- **The station route** (`$KEY` keys, `$FB` mirror, `quiet`/`loud`, the
  `l15_line.py` view) is built and its view tested on the PC with frames
  from the real `world.c`; **not yet watched in Wokwi**.
- **Host test passes** with the numbers above.
- **Not yet watched running in Wokwi.** Nothing on the chip side — timing,
  the display, the buttons, the beeps — has been seen. The first three things
  to look at when someone does:
  1. **`stats`, the world step's cycles.** It must fit well inside 240 000
     (5 ms at 48 MHz) or world time falls behind real time and every lap
     time differs from the host's. This is the lesson's biggest unmeasured
     assumption: ~3 exact distance checks × 7 sensors plus the judge's 11, in
     soft float, per step.
  2. **Does OVAL at the default knob lap in about 5.1 s** (Lab Part 0)? Close
     agreement with the host says the SITL chain holds on the chip; a
     difference of seconds says the world task is late.
  3. **The menu and the joystick direction.** `pad.c` flips HORZ; the menu
     uses VERT (up = previous track). If up moves the cursor down, or manual
     steering is mirrored, that is a sign error in this lesson, not in Wokwi.
