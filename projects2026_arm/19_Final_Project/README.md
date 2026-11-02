# 19 — Final Project

**Part 3, the last lesson.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz) on the app board.

The project briefing and a **template** to build it on. The template wires
every app-board subsystem together the way a product would: six RTOS tasks
(input 100 Hz, app 50 Hz, telemetry 10 Hz, display 20 Hz, health 4 Hz, a
shell), a print mutex, an I²C bus mutex, an app-state mutex, sticky button
edges, a sound mailbox, the **IWDG** fed only when every periodic task has
beaten its own heartbeat counter, reset-cause reporting, TIM14 response-time
profiling, and a `stats` shell that prints each task's CPU %, stack
high-water mark, response time, lateness and overruns. The student's project
plugs in behind four functions in `app.h`; the demo app, **steer**, is a
fixed-point ball you steer onto targets. The pure-C half — app and health
logic — is tested on the PC by `host/test_app.c`, which is itself the
template for every project's host tests.

## Files

| | |
|---|---|
| `Slide.md` | the project briefing - 31 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | **the handbook**: a guided first session (8 parts, plus 1b: the station), then the milestone checklist, proposal template, design-review checklist, report template, demo checklist and rubric |
| `app.h` | **the plug-in interface**: `app_init`, `app_step(dt, const pad_t*)`, `app_draw`, `app_telemetry`; `app_sound` provided by the platform |
| `app.c` | the demo app, *steer* — **replace this with your project**. Pure C |
| `Main.c` | the platform: the task/rate/priority table, shared data and its protection, the five periodic tasks, `main()`; `input` applies the station's keys, `display` sends the OLED mirror |
| `shell.c` | `stats`, `health`, `scan`, `hang <task>`, `spin <task>`, `reset`, `quiet`, `loud`; routes the station's `$KEY` and `$MIRROR` lines, silently |
| `sys.h` | what `Main.c` and `shell.c` share |
| `tools/station/views/l19_final.py` | this lesson's view in the PC station: `$APP` as the arena, `$SYS` as a health dashboard. **Your project's view starts as a copy** |
| `health.c`, `health.h` | heartbeat counters → missing bitmap → feed / starve / force. Pure C |
| `wdog.c`, `wdog.h` | IWDG start and refresh (ST's HAL sequence), debug freeze, `RCC->CSR2` reset cause |
| `prof.c`, `prof.h` | TIM14 at 1 MHz; per-task response time, lateness, overruns |
| `host/test_app.c` | host test harness template: fake platform, scripted inputs and a bot, `CHECK`, exit status |
| `host/run.bat`, `host/run.sh` | compile `app.c`, `health.c`, `_lib/oled.c` with the PC's gcc and run the tests |
| `build.bat` | `LIBS=retarget os uart proto i2c oled adc pad beep keys kbd mirror` |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board unchanged from `_targets/app-board-diagram.json`; `wokwi.toml` forwards the serial port to TCP 4000 for the station |

**Why `retarget` and `uart` together**, as in lesson 09: `uart.c` provides
the interrupt-driven `_write()` and `uart_getc()` the shell blocks on;
`retarget.c` provides the other C-library stubs (`_sbrk`, `_close`, …), and
its weak polled `_write()` and `uart2_init()` are replaced at link time.
`retarget` alone would give a shell with no way to read input without
polling.

## Build and run

```
build.bat        # FLASH 18780 B / 32 KB, RAM 10552 B / 12 KB, zero warnings
host\run.bat     # 15 checks, 0 failed, exit code 0
simulate.bat
uv run ..\08_UART_And_Python_Host\host.py --file capture.txt --chart SYS,2,0,1000
```

`$APP,<ms>,<x>,<y>,<vx>,<vy>,<score>` at 10 Hz; once a second
`$SYS,<ms>,<idle per mille>,<context switches>,<i2c errors>,<oled bytes>,<watchdog feeds>`.
`quiet` in the shell stops both, `loud` restarts them - so you can read
`stats` without frames scrolling through it.

## Run it from the station

```
simulate.bat                              # start the simulation in VS Code (Wokwi)
uv run tools/station/station.py           # from the repository root
```

The station is one window on the PC: the board's OLED drawn large, this
lesson's view (`tools/station/views/l19_final.py`) and a console for the
shell. `wokwi.toml`'s `rfc2217ServerPort = 4000` is how it reaches the
simulated USART2. Click the OLED or the view and the keyboard is the board's:

| Key | Does |
|---|---|
| arrows | the stick - held exactly as long as your finger is |
| `a` | button A: brake |
| `b` or `p` | button B: restart |
| `s` | stick press (SEL) - steer ignores it |
| `1` .. `5` | the knob in fifths: the steering gain |
| `?` | prints the key list |
| `-` `+` | **nothing here**, silently - steer has no pace. `shell.c`'s `station_frame()` is where your project gives them a meaning |

How it is wired, in the order a byte travels:

- **Keys in.** The station sends `$KEY,<key>,<1|0>*CS` for every press and
  release. `shell.c` sees a line starting with `$`, hands a `$KEY` to
  `kbd_frame()` under `kbd_lock`, and says nothing. `input`, right after
  `pad_read()`, calls `kbd_apply()` under the same lock: the keys land in the
  same `pad_t` as the buttons, so `app.c` cannot tell them apart and the host
  tests are untouched.
- **Screen out.** The station sends `$MIRROR,1*CS` once a second. While those
  keep coming, `display` follows every `oled_flush()` with up to two
  `$FB` frames (`_lib/mirror.c`: only the 32-byte segments that changed),
  and only while the UART ring has room for a whole one - so the mirror never
  waits for the serial port. They go out through `send_raw()`, under
  `print_lock` like `say()`, but with no printf. A terminal that never sends
  `$MIRROR` never sees a `$FB`.
- **Telemetry.** `$APP` and `$SYS` are unchanged. The view draws the arena
  at the board's 128 x 64 pixels with the ball's last 3 s as a trail and its
  velocity as an arrow (Q8 per 20 ms step, shown in px/s), the score, and
  from `$SYS` idle %, context switches per second, OLED bytes per second,
  watchdog feeds per second and I2C errors, each with a strip chart.

**Not yet watched in Wokwi.** The station route builds with zero warnings and
the view was fed realistic and garbage frames on the PC; nobody has yet run
this lesson with the station attached.

### Your project's view

1. Copy `tools/station/views/l19_final.py` to
   `tools/station/views/l19_<yourname>.py`. The station finds every view in
   that folder; there is nothing to register.
2. Rename the class and set `name` (short, unique: `--view <name>` picks it)
   and `title`.
3. Give your `app_telemetry()` frame **its own name** - `$ROBO`, not `$APP` -
   and set `frames = ("ROBO", "SYS")`. The first of those to arrive selects
   the view; keeping `"SYS"` in the list stops the station switching back to
   this lesson's view when your `$SYS` arrives.
4. Rewrite `_app()` (parse your fields; ignore a frame that is too short) and
   `_draw_app()`. Keep `_sys()` and the dashboard: your project sends the
   same `$SYS`.
5. Test it without the board: feed it frames built from your own format
   string and call `draw()` - the view must survive a short or garbled frame.

## How to start your project

1. **Copy the folder.** `19_Final_Project` → `19_Project_<yourname>` (the
   build engine needs it beside `_build`, `_lib` and `_startup`). Keep the
   original as a reference.
2. **Build both halves once, unchanged.** `build.bat` (zero warnings) and
   `host\run.bat` (all PASS). If either fails, fix that first.
3. **Write the proposal** (`Lab.md` B2) before any code. Requirements are
   numbers.
4. **Empty `app.c`** down to the four functions of `app.h`; change `APP_NAME`
   and `APP_PERIOD_MS` if your logic wants another rate.
5. **Write one host test first**, in your copy of `host/test_app.c`, for the
   first rule of your MVP. Keep the fake platform and `CHECK`. Make it pass.
6. **Grow `app.c`**, a test at a time. No `stm32c031xx.h`, no `os_*()`, no
   clocks in it — or the host build breaks, which is the point.
7. **Hardware your project adds** (a sensor, an LED) gets its own task or
   joins `input`; it goes in `Main.c`'s table with a period, a priority, a
   heartbeat and a line in `prof[]`. Anything on I²C takes `bus_lock`.
8. **Give it a station view** - "Your project's view" above.
9. **Measure** with `stats` and `$SYS` against every requirement, and keep the
   output for the report.

Tracks B and C (drone, robot) keep their controller behind lessons 13–18's
`control_step()` instead; the platform half of this template — health,
watchdog, profiling, shell — carries over unchanged.

## Memory budget (this build)

| Item | Bytes |
|---|---|
| FLASH | 18 780 of 32 768 |
| RAM | 10 552 of 12 288 |
| task stacks (input 160, app 256, telem 288, display 224, health 256, shell 320 words) | 6 016 |
| idle stack | 256 |
| OLED framebuffer | 1 032 |
| UART rings | 320 |
| newlib `__sf` + `_impure_data` | 388 |
| heap + main stack (`link.ld`) | 1 536 |
| station: `kbd` 76, mirror state 83, `$FB` buffer 96, two mutexes + `tel_quiet` 9 | 264 |

Stack sizes are a **static estimate**: `-fstack-usage` for this lesson's and
`_lib`'s functions (deepest: `task_shell` 224 B, `task_tel` 160 B), plus
newlib's printf frames read from the disassembly (`_vfprintf_r` 144 B,
`_printf_i` 64 B; ~650 B from `say()` down to the UART ring). A first draft
with 256–384 words per task came to **11 944 B RAM** — over the course's
11 KB ceiling — and was cut to these sizes on that evidence. They have not
been checked by running: `stats` prints the real high-water marks.

The station added **no stack size and, by the same estimate, no depth**.
`display` now also runs `mirror_next()` (40 B, then its builder 128 B) and
`send_raw()` → `_write()` - about 210 B, well under the ~650 B printf chain it
already had through `app_draw()` → `oled_printf()` → `vsnprintf()`. The
`$FB` buffer is `static` for that reason. `input` gains `kbd_apply()`, 12 B;
the shell's `kbd_frame()` path (~90 B) is shallower than its `say()`.
Adding the station cost **2 132 B of flash and 272 B of RAM**.

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| `IWDG->KR/PR/RLR/SR/WINR`, `IWDG_PR_PR_0/1`, `IWDG_SR_PVU/RVU/WVU` | `stm32c031xx.h` |
| key values `0xCCCC` enable, `0x5555` write access, `0xAAAA` reload; `IWDG_PRESCALER_32 = PR_1 | PR_0` | ST's `stm32c0xx_hal_iwdg.h` (GitHub, STMicroelectronics/stm32c0xx-hal-driver) |
| init order: start, unlock, PR, RLR, wait `SR` flags (bounded), reload | ST's `HAL_IWDG_Init()`, `stm32c0xx_hal_iwdg.c` |
| `LSI_VALUE = 32000`, "typical", varies with voltage and temperature | ST's `stm32c0xx_hal_conf_template.h` |
| `RCC->CSR2` and `RCC_CSR2_IWDGRSTF/SFTRSTF/PWRRSTF/PINRSTF/OBLRSTF/WWDGRSTF/LPWRRSTF/RMVF` | `stm32c031xx.h` |
| `DBG->APBFZ1`, `DBG_APB_FZ1_DBG_IWDG_STOP`, `RCC_APBENR1_DBGEN` | `stm32c031xx.h` |
| `TIM14`, `RCC_APBENR2_TIM14EN`, `TIM_CR1_CEN`, `TIM_EGR_UG` | `stm32c031xx.h` |
| `NVIC_SystemReset()` | `core_cm0plus.h` |
| Wokwi Nucleo-C031C6: IWDG, DMA, DBG, PWR **not implemented**; RCC partial; TIM1/3/14/16/17 supported | docs.wokwi.com, `board-st-nucleo-c031c6` page |
| `os_task_t.ticks`, `os_stack_used()`, `os_switches()`, `os_task()` | `_lib/os.h` |
| app-board pins | `_lib/pad.h`, `_targets/app-board-diagram.json` |
| printf frame sizes | `arm-none-eabi-objdump -d Main.elf` (prologue `sub sp`) |

## Verified by execution — on the host

`host\run.bat` compiles the same `app.c` and `health.c` the board runs, plus
`_lib/oled.c`'s drawing code, with MinGW gcc 15.2, `-Wall -Wextra`, zero
warnings. **Measured on the host:**

| Test | Result |
|---|---|
| [1] full right stick → right wall | 34 steps = **680 ms** |
| [2] release stick → ball stops | **1840 ms** (friction rounded away from zero; plain `v/32` would never stop) |
| [3] fuzz: 180 000 steps = 1 h of random controls | **0** escapes from the arena; fastest 1.44 px/step (cap 3.00) |
| [4] determinism: same seed and inputs twice | identical `$APP` line |
| [5] PD autopilot, 60 s of game time | **83 points**, 0 wall hits |
| [6] one frame through `oled_flush()` | **1088 B = 24.5 ms** at 400 kHz; a still frame also 1088 B |
| [7] longest `$APP` body over 50 000 steps | **29 B** of the 80-byte buffer |
| [8] health: display hangs at 1000 ms | missing bitmap `0x04`; first starved check **1250 ms**; software reset forced **2500 ms** (1500 ms after the last feed) |

**One bug found by the first run — in the harness.** `CHECK` evaluated its
condition twice; a check whose condition called `health_check()` passed on
the first call and failed on the second. It now evaluates once. Slide 21
uses this.

**One design finding:** test [6] showed the template's display costs a full
1088-byte repaint every frame, moving or not, because the border and HUD make
`oled_clear()` dirty all eight pages. That is ~49 % of the I²C bus, polled.
It is left in deliberately: Lab Part 5 is a contest to cut it.

## Status

- Builds with **zero warnings**: FLASH 18 780 B, RAM 10 552 B.
- Host tests: **15 checks, 0 failed** — numbers above.
- **Not yet watched running in Wokwi.** Nothing in this lesson has run on a
  simulated or real STM32: not the tasks, the shell, the profiling or the
  watchdog path. The IWDG itself cannot be observed in Wokwi (not
  implemented there); the template's software fallback stands in for it. The
  first three things to look at:
  1. **The boot banner**: does the OLED answer at 0x3C, and what reset cause
     does Wokwi's partial RCC report (`CSR2` is printed raw)?
  2. **`stats` after a minute of play**: are all stacks inside their sizes
     (they are a static estimate), is `over` zero, and is `display` the
     biggest CPU user as the budget predicts?
  3. **`hang display`**: does `health` name it within ~500 ms, does LD4 stop,
     and does the board reset itself ~1.5 s after the last refresh?
- **The station route is not yet watched in Wokwi either**: whether the keys
  steer the ball, the `$FB` mirror keeps up beside `$APP`, and `stats` shows
  `display`'s stack still inside its 224 words with the mirror running.
