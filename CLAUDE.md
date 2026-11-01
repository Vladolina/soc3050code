# CLAUDE.md — SOC3050 Embedded Systems Course

Working notes and development tracker for this repository. Claude Code reads this
at session start; keep it current as work proceeds.

---

## 1. Repository purpose

ATmega128 embedded-systems teaching framework. Each lesson ships **code + slides +
a simulator circuit** so students can build, run and study a topic without hardware.

Self-contained: the AVR toolchain and the SimulIDE simulator are vendored under
`tools/`. No external installs required.

## 2. Layout

```
projects/             53 legacy lesson folders (previous edition, SimulIDE 1.1.0-SR1)
projects2026_avr/     intro + 24 curated lessons, SimulIDE 1.1.0-SR2  <-- frozen, complete
projects2026_arm/     STM32 edition, lessons 00-19 (Parts 0-3)  <-- live; owns the website
shared_libs/          _port, _adc, _uart, _timer, _glcd, _game, _eeprom, _pwm ...
tools/avr-toolchain/  avr-gcc 15.1.0, avr-objcopy, avr-size, avrdude
tools/arm-toolchain/  arm-none-eabi-gcc 15.2.1 + gdb  <-- ARM edition, see below
tools/cmsis/          CMSIS 6 core + ST STM32C0xx headers  <-- ARM edition
tools/simulide/       SimulIDE 0.4.15-SR10 and 1.1.0-SR1 + the original master board
tools/simulide110sr2/ SimulIDE 1.1.0-SR2  <-- the simulator the 2026 edition targets
tools/simulide200/    SimulIDE 2.x (R260501) — not targeted this cycle
python_projects/      separate Python track (out of scope here)
docs/                 framework + SimulIDE documentation
```

**This tracker documents the AVR edition.** `projects2026_arm/` arrived on
2026-09-13, is independent of everything below, and carries its own
`README.md`. Only the two things that reach outside it are recorded here: its
effect on the published site (section 9g) and its toolchain under the shared
`tools/` directory (section 9h).

`tools/simulide110sr2/SimulIDE_1.1.0-SR2_Win64/` is committed, so a fresh clone
can simulate. The `.zip` files beside it are ignored - one duplicates the
extracted folder, the other four are SimulIDE 2.x companion apps nothing here
uses. `tools/simulide200/` (159 MB) remains untracked and unused.

## 3. Toolchain facts (verified)

| Item | Value |
|---|---|
| MCU | ATmega128 at 16 MHz |
| Compiler | `tools/avr-toolchain/bin/avr-gcc.exe` — avr-gcc 15.1.0 |
| Flags | `-mmcu=atmega128 -DF_CPU=16000000UL -DBAUD=9600 -Os -Wall -Wextra -ffunction-sections -fdata-sections -Wl,--gc-sections` |
| Output | `Main.elf` → `avr-objcopy -O ihex -R .eeprom` → `Main.hex` |
| SR2 binary | `tools/simulide110sr2/SimulIDE_1.1.0-SR2_Win64/simulide.exe` |
| SR2 native circuit | `.sim1`, `<circuit version="" rev="2208">` |
| Legacy circuit | `.simu`, `rev="2005"` — SR2 opens these fine (verified by launching) |
| ATmega128 model | `data/AVR/mega128.mcu` present in SR2; `itemtype="MCU"` unchanged from SR1 |
| Shared board | `projects2026_avr/Simulator.simu` — every component in it is present in the SR2 binary |

**`avr-size` in this toolchain does not support `--format=avr`.** Use the plain
Berkeley output — but do not read its `data` and `bss` columns. This linker
marks `.data` read-only, so Berkeley folds it into `text` and reports `data 0`
and `bss 0` even for a program with hundreds of bytes of string literals in
SRAM. `avr-size -A` gives the real per-section split, and is what
`00_Introduction/Slide.md` teaches students to use.

## 4. Decisions (confirmed 2026-09-05)

| Decision | Choice |
|---|---|
| Scope | Curated core set — 22 lessons, one per topic, plus `00_Introduction` |
| Circuits | **One shared master board**; each lesson loads it against its own `Main.hex` |
| Clock | **16 MHz everywhere**, matching the board's `Frequency="16 MHz"` |

## 5. Build system

The legacy `build.bat` files each hand-maintained their own list of
`shared_libs` sources. That list cannot be replaced with "link everything",
because several lessons **deliberately define their own ISR or UART routine**
that a shared library also defines. Verified collisions:

| Lesson | Collision |
|---|---|
| `Timer0_Overflow_Blink`, `RTOS_UPGRADED` | `__vector_16` (TIMER0_OVF) vs `_timer.c` |
| `Timer1_CTC_Precision` | `__vector_12` vs `_timer.c` |
| `Timer1_Input_Capture` | `__vector_11` vs `_timer.c` |
| `Power_Sleep_Modes` | `__vector_10` vs `_timer.c` |
| `Watchdog_System_Reset` | `led_all_on` / `led_all_off` vs `_port.c` |
| `UART` | `putch_USART1` / `puts_USART1` / `getch_USART1` vs `_uart.c` |
| `23_Game_Engine_GLCD`, `24_Game_Arcade` | `_game.c`'s `__vector_12` (TIMER1_COMPA) and `__vector_9` (TIMER2_COMP) vs `_timer.c` |

**Design:** one shared engine plus a per-lesson library list.

```
projects2026_avr/
├── _build/build-lesson.bat     compile + link + hex + size
├── _build/simulate-lesson.bat  place the board next to the hex, launch SR2
├── Simulator.simu              the single maintained board
└── NN_Topic/
    ├── build.bat               "set LIBS=..." then calls the engine
    ├── simulate.bat            calls the launcher
    └── Main.c  config.h  Slide.md  README.md
```

`_build/resolve-libs.py` derives each lesson's minimal conflict-free library set
from `avr-nm` symbol tables. Re-run with `python _build/resolve-libs.py --all`.

**Batch gotcha, cost an hour:** `echo Build OK -> Main.hex` inside a `.bat` is a
*redirect* — it silently overwrote every `Main.hex` with the text `Build OK  -`.
Existence checks passed; the firmware was empty. Validate hex content
(starts with `:`, more than a few hundred bytes), never just `Test-Path`.

## 6. The shared board — traced from the circuit file

| Port | Connected to |
|---|---|
| PORTA0–7 | KS0108 GLCD data bus |
| PORTB0–7 | 8 LEDs **active low**; PB0–3 also stepper + SPI; PB0 scope; PB6–7 logic analyser |
| **PORTC** | **not connected at all** |
| PORTD0, D1, D4–D7 | push buttons, pulled up |
| PORTD2, D3 | RXD1 / TXD1 — the serial port |
| PORTE3 | joystick; PORTE4–E7 GLCD control (also INT4–INT7) |
| PORTF0 | potentiometer (ADC0); PORTF1, F2 joystick axes |
| PORTG1 | **GLCD R/W** — must be driven LOW to write; **PORTG4 → slide switch → AudioOut speaker** |

**PG1 is the GLCD R/W line, and `_glcd.h` used to deny it.** The header said
"R/W: GND (tied to ground for write-only)". Tracing the board gives
`Ks0108-237-PinRW → mega128-20-PORTG1`, and `ks0108_init()` never drove it, so
the pin floated: the panel reads R/W high as a *read* request, accepts nothing,
and the display stays blank with no error anywhere. Lessons survived only
because they also linked `_port.c`, whose `Port_init()` does `DDRG = 0xFF;
PORTG = 0x00;` and leaves R/W low by accident. `_glcd_legacy.c` always had it
right. **Fixed 2026-09-09** in `_glcd.h` (comment plus `KS0108_RW_*` defines),
`_glcd.c` (`ks0108_init()` now drives it) and `_game.c`. Found by
`23_Game_Engine_GLCD` rendering a blank screen in SR2 — it links only `_game`,
so nothing set PG1.

**PORTG4 is the speaker, not just a switch.** Traced 2026-09-08:
`mega128-20-PORTG4 → Switch-75 → AudioOut-13`. The slide switch sits *in series*
with the speaker, so it is a physical mute, and `_buzzer.h`'s
`BUZZER_PORT PORTG / BUZZER_PIN 4` is correct after all. If a tone plays and
nothing is heard, check the switch before debugging code.

Also on the board, previously unrecorded: an **Oscope**, a **LAnalizer**, a
**DcMotor** (wired), an **SR04** ultrasonic ranger, and a **Servo**.

**The Servo and the SR04 are not connected to the MCU.** Traced 2026-09-08 by
enumerating every connector whose endpoints name them:

```
Servo-1025-in1            <-> Ground-1081-Gnd
Fixed Voltage-1080-outnod <-> Servo-1025-in0      (no MCU pin anywhere)
Potentiometer-1420-PinM   <-> SR04-1349-inpin     (range set by hand, not read)
```

So **`10_PWM_Servo` has no servo that responds to it.** The lesson still builds
and still emits correct PWM — visible on the logic analyser and scope, which are
wired — but nothing on the board moves. This is the same class of gap as the
unwired DS1307 in section 6, and belongs on the same list in section 10. It has
not been fixed, for the same reason: splicing new nodes into a circuit file every
lesson shares is risky from the CLI with no way to confirm the electrical result.

LED wiring is rail → resistor → anode, cathode → pin, so **driving a pin LOW
lights its LED**. This matches `shared_libs/_port.c`, which documents PORT B as
the LED array.

**Placed but unwired:** DS1307, DS1621, DS18B20, DHT22, ESP01, KY040, TouchPadR.
Lesson 16 needs its DS1307 wired to PD1 (SDA) / PD0 (SCL) before the RTC demos
respond; those pins already carry the pull-ups an I2C bus wants. Left as a GUI
step on purpose - see section 10 for why, and `16_I2C_RTC_DS1307/README.md` for
the click-by-click instructions.

## 7. What was changed during the upgrade

- **Clock unified at 16 MHz.** Five lessons disagreed with themselves between
  `build.bat` and `config.h`; in `ADC_Basic` and `Timer_PWM_Motor_DC` the
  `#define` was unguarded and silently overrode the compiler flag, so delays and
  baud rates were computed for the wrong clock. `config.h` now guards it.
- **Status LEDs moved off PORT C.** Six lessons drove the unconnected PORT C.
  They now go through `LED_WRITE()` / `LED_TOGGLE()` / `LED_DDR` in `config.h`,
  targeting PORT B and handling the active-low inversion in one place.
  `15_SPI_Master_Basic` was deliberately left alone — PB0–PB3 carry its SPI bus.
- **Serial pin conflict.** `02_Port_Button_Debounce` used PD2 as a button; PD2 is
  RXD1 and carried the lesson's own output. Moved to PD4.
- **Real defects fixed:** `sprintf` buffer overflows in the keypad (30–34 bytes
  into `buf[20]`) and LCD lessons; `%u` given an `unsigned long` in the servo
  lesson; a `PROGMEM` array declared without `static` so the attribute was
  ignored and `lpm` read a RAM address as flash; a status print in the
  ring-buffer burst demo that sat outside the data branch and repeated forever.
- **Encoding damage repaired.** 310 sequences mangled by an old UTF-8/CP949
  round-trip, including banner lines where the `\` of `\r` had been eaten.
- **Slides completed.** `02`, `05`, `14`, `19` had none; all 22 now do.
- **`#include "config.h"` made universal** — `09`, `18`, `21` did not include it.

## 8. Status

**All 25 lessons build clean and produce valid Intel HEX**, checked by
`projects2026_avr/_build/verify-all.ps1`, which validates hex records rather
than merely testing that a file exists. SimulIDE 1.1.0-SR2 was launched against
the board and confirmed to load both circuit and firmware.

Remaining compiler warnings are only `defined but not used` on demo functions
students are meant to uncomment (`04_Inline_Assembly`, `18_GLCD_Graphics`), plus
unused-parameter warnings inside `shared_libs` stubs. Those stubs are honest
`// TODO` placeholders and **no lesson calls any of them** — checked.

Current totals: **0 warnings in lesson code, 9 in `shared_libs`** (2026-09-09),
down from 5 and 45. Two changes got there: the four `_glcd.c` warnings were real
bugs and are fixed (section 9e), and the five `defined but not used` demo
functions in `04_Inline_Assembly` and `18_GLCD_Graphics` now carry
`__attribute__((unused))`. Those are genuinely uncalled on purpose — `main()`
runs one demo and students uncomment the rest — so the attribute states the
intent instead of leaving noise that trains people to ignore warnings.

| Date | Status |
|---|---|
| 2026-09-05 | Surveyed repo; verified SR2 format, `mega128.mcu`, toolchain. |
| 2026-09-05 | Scope/circuit/clock decisions confirmed; lib-collision problem found and solved. |
| 2026-09-05 | 22 lessons imported, normalised, defects fixed, slides completed, all building and verified in SR2. |
| 2026-09-05 | `projects2026_avr/` + this tracker committed and pushed. |
| 2026-09-05 | System pass: VS Code tasks repaired, verifier and slide renderer added, both READMEs rewritten, `projects/` archived, SimulIDE SR2 committed. |
| 2026-09-05 | History squashed to a single root commit on `main` and force-pushed; all prior history deleted. See section 12. |
| 2026-09-06 | `00_Introduction` added for the first class; `gen-readme.py` taught about natively-written lessons; two mangled commands in the 2026 README repaired. See section 9a. |
| 2026-09-06 | Datasheet link pinned into the rendered deck chrome so it is reachable from every slide, not just slide 1. |
| 2026-09-06 | Decks published to GitHub Pages, rendered in CI from `Slide.md`. See section 9c. |
| 2026-09-07 | Pages confirmed live and serving all 23 decks; full rebuild re-verified. See section 9d. |
| 2026-09-08 | Application track started: `_game` engine plus lessons 23 and 24. All 25 build clean. See section 9e. |
| 2026-09-09 | GLCD debugging: R/W (PG1) was floating, `_glcd.c`'s `set_pixel` was destructive, `putchar` corrupted glyphs at column 64, and `_game.c` had dropped a settling delay the board needs. All fixed; warnings 5/45 → 0/9. SITL C-to-Python link proven. See sections 6 and 9e. |
| 2026-09-14 | GitHub Pages switched to the ARM edition; the AVR decks are no longer published. Colab link added to the ARM decks. See section 9g. |
| 2026-09-19 | ARM toolchain vendored under `tools/`; ARM Part 1 began with lesson 04. Nothing in the AVR tree touched. See section 9h. |
| 2026-09-22 | ARM lesson 04 grew an eight-LED bar students program; the site gained a generated board pin map. Nothing in the AVR tree touched. See the end of section 9g. |
| 2026-09-26 | ARM syllabus agreed (20 lessons, Game → Drone → Robot) and ARM Part 1 written: lessons 05–09, all zero warnings; 06–09 partly run in Renode, none yet watched in Wokwi. Nothing in the AVR tree touched. See `projects2026_arm/README.md`, "The syllabus". |
| 2026-09-27 | ARM lesson 05's deck rewritten step by step (every GPIO register its own slide), each of lessons 04-09 opened with generated pin-map slides, then push-pull/open-drain, the EXTI multiplexer and the EXTICR layout drawn as SVG and the debouncer split over two slides. Nothing in the AVR tree touched. |
| 2026-09-30 | ARM decks gained 62 to-scale register diagrams, written as ```` ```regs ```` blocks and drawn by the new `projects2026_arm/_build/regdiag.py`; every bit position checked against the CMSIS headers. Three factual slips in ARM slide text fixed on the way. Nothing in the AVR tree touched. See `projects2026_arm/README.md`, "Register diagrams". |
| 2026-09-30 | VS Code moved to the ARM edition: F5 debugs an ARM lesson in Wokwi through its GDB server (`gdbServerPort` in each `wokwi.toml`, vendored `arm-none-eabi-gdb`, `cppdbg`); the old AVR `launch.json` stub is gone. `tools/cli/` dispatchers now recognise `projects2026_arm/`, the ARM IntelliSense config is the default, and AVR-only tasks are labelled `AVR:`. See `projects2026_arm/README.md`, "Debugging a lesson in VS Code". |
| 2026-10-02 | ARM lesson 05 gained Lab Part 8 and slide 33b: reaching the debounce accept and the EXTI handler under GDB by latching a Wokwi button (Ctrl-click) and by raising the interrupt from the debugger through `EXTI->SWIER1` and NVIC `ISPR`. Unwatched in Wokwi, and says so. Decks re-rendered. Nothing in the AVR tree touched. |
| 2026-10-03 | ARM edition completed: lessons 10-19 written (Part 2: fixed point and DMA, faults and watchdogs; Part 3: arcade game, two drone lessons, four robot lessons, final project), all zero warnings, every host test passing, none yet watched in Wokwi. New shared `_lib/` modules (`oled`, `pad`, `beep`, `fmath.h`, and lesson 09's `i2c`/`adc` promoted) and one app board, `_targets/app-board-diagram.json`. Re-scoped from the syllabus: 10-11 run on the C031C6 rather than an F446RE in Renode, and 13-18 simulate the physics inside the firmware rather than in Webots, because neither tool is on this machine. Wokwi implements no DMA, IWDG, PWR or RTC on this board; the lessons say so. The ARM build engine now links `-lm` (no existing lesson changes by a byte). Nothing in the AVR tree touched. See `projects2026_arm/README.md`, "Parts 2 and 3". |
| 2026-10-03 | ARM decks: every register block now gets its whole map on one page before slides explain registers one at a time (the maintainer's rule; model: lesson 06 slides 1 and 5). Maps added to 00, 03-12, 17, 19; new pages take a letter suffix so no slide number or Lab reference moved. Offsets and bits checked against the CMSIS headers; all 104 `regs` diagrams render. Fixed on the way: lesson 05's GPIO table skipped LCKR, lesson 04's table missed registers it claimed to cover, several unshaded fields the code uses. Open: lesson 12's SSD1306 bit layouts are from the datasheet as remembered (no copy in the repo); lesson 05's EXTICR field width (8 bits per RM, 3-bit header masks). Rule recorded in `projects2026_arm/README.md`, Conventions. Nothing in the AVR tree touched. |
| 2026-10-04 | ARM lesson 06 watched running in Wokwi for the first time: SysTick, TIM3 PWM and the report line worked, but the echo read 0 us - **Wokwi does not implement timer input capture** (0 capture edges). Fixed by also timestamping PA7's edges through EXTI line 7 from `TIM14->CNT`; each report line names the source and counts both paths' edges. Watched working the same day: 100 cm read 5882 us = 101 cm, 151 cm read 8882 us = 153 cm, servo pulse and LED bar matched the formulas. Wokwi's HC-SR04 is 58.8 us/cm, not the documented 58 (reads ~1.4 % long; left as is). New slide 13b, Lab Part 6 rewritten around the EXTI path. Nothing in the AVR tree touched. |
| 2026-10-06 | ARM lesson 06: a lab check added to the header comment of `Main.c` - ten one-change tasks (T1-T10: bar direction, servo direction, ping rate, slew, full scale, a dot, a danger light, report rate, timer clock, 58.8 us/cm) handed out one per student, each predicted before running, explained in one sentence and shown with `git diff`. Comment only: the firmware is byte-identical (7340 B). The answer key is kept out of the repository, since students read the file. Lab.md points to it. Nothing in the AVR tree touched. |
| 2026-10-11 | ARM lesson 07's deck: slide 1 now opens with the maintainer's two-line contrast, in English with the Korean beside it - a cooperative scheduler lets one CPU hog stop the whole system; a preemptive scheduler (an RTOS) preempts it - and names the CPU hog, both schedulers and who stops a job in a table. Eight more slides (2, 4b, 9, 10, 12, 13, 15, 17) gained named-concept tables: task/context/TCB, deciding vs switching, scheduling policies and the two priority numberings, the READY/SLEEPING/BLOCKED states and busy-waiting vs sleeping, PRIMASK vs mutex, blocking vs (unbounded) priority inversion, mutex vs queue and task vs ISR APIs, and what a stack overflow really looks like. Checked against `_lib/os.c`; no slide renumbered. Nothing in the AVR tree touched. |
| 2026-10-11 | ARM lesson 07: new slide 4c, a ten-step flowchart of one task switch (task A, SysTick entry, `SysTick_Handler` decides, PendSV pended, tail-chain, PendSV saves A and loads B, exception return, task B), dashed boxes for hardware, solid for kernel code, filled for the switch, with a step-to-slide table. Checked by rendering it in headless Chrome. Nothing in the AVR tree touched. |
| 2026-10-11 | ARM lesson 07, slide 6: a stack diagram under the PendSV listing - one task's 16 words, save steps for task A on the left and restore steps for task B on the right, numbered in program order, with a nine-step instruction table that also says why r4-r7 are restored last. Checked against the listing and by rendering in headless Chrome. Nothing in the AVR tree touched. |
| 2026-10-12 | ARM lesson 07, slide 6: a table comparing a task switch in this kernel, Windows and Linux (x86-64) - trigger, deferred step (PendSV / DISPATCH_LEVEL / `TIF_NEED_RESCHED`), who picks, what is saved on entry and by the switch, FPU, address space, rough cost. The kernel column is checked against `os.c`; the Windows and Linux columns are from memory with no reference in the repo, and the cost row is typical orders of magnitude, not measured. Nothing in the AVR tree touched. |
| 2026-10-19 | ARM lesson 08 decodes keys: new pure-C `keys.c` turns terminal escape sequences back into keys (Left/Right move the LED bar, Up/Down the telemetry rate, Esc cancels a line, others spelt out), and the shell now echoes and handles Backspace - never echoing a `$` frame, and pausing telemetry while a line is half typed so a frame is never glued onto it. New Lab Part 7 and slide 9b. Zero warnings, FLASH 10840 -> 11868 B. Decoder and `task_proto()` tested on the PC only; whether the Wokwi VS Code terminal sends arrows raw is not yet watched. Nothing in the AVR tree touched. |
| 2026-10-19 | ARM lesson 08's keys were seen working in Wokwi through the VS Code extension's serial terminal, which sends each key as it is pressed: the arrows reached USART2 and `keys.c` decoded them. Lab Part 7, slide 9b and the README now say so and point to that terminal. Still open: the terminal's bytes for F1/Home/Delete (Lab 7(c)), and the RFC2217 route (miniterm, `host.py --port`). Nothing in the AVR tree touched. |
| 2026-11-01 | ARM lesson 12 watched running in Wokwi (menu, start, stick, sound) after three Wokwi-only faults were diagnosed from serial output: a stale I2C TXIS after STOP left the OLED black (`_lib/i2c.c` and lesson 09's copy now toggle PE before each transaction); TIM3 counting at 1 MHz between notes slowed the simulated CPU ~800x (`_lib/beep.c` stops it); `WFI` with no timer running never woke (`SLEEP_WFI 0` spins instead). None changes behaviour on silicon. Lessons 09-19 rebuilt, zero warnings. The MPU6050 I2C read path is still unwatched. Nothing in the AVR tree touched. See `projects2026_arm/README.md`. |
| 2026-11-01 | ARM lesson 12 made to stand without lessons 09-11, which the class skipped: new slide 3b teaches I2C on one page (open drain, START/STOP, address byte, ACK/NACK, 9 clocks a byte) and slides 6, 7, 7b, 15 no longer point to lesson 09. The deck and Lab now record what Wokwi showed, including its ~100 kHz I2C (full repaint 106 ms). FLAP no longer claims B flaps (the arcade takes B as pause). Lessons 10-19 rebuilt, zero warnings; the i2c/beep fixes grew each by 40-76 B of flash, and every quoted size in the ARM READMEs, Labs and decks was updated. Lesson 12's host test not re-run (no host gcc here). Nothing in the AVR tree touched. |

## 9. The system pass (2026-09-05)

### VS Code tasks were broken for the 2026 tree
`tools/cli/cli-build-project.ps1` gates on `$ProjectDir -like "*\projects\*"`,
so **"Build Current Project" — the entry point the README documents — failed
outright on every 2026 lesson** with "Not a project folder". That script also
guesses the library set by branching on filenames, and one branch links
`_interrupt_manager.c`, which does not exist in `shared_libs/`.

Fixed with two dispatchers, `tools/cli/build-current.ps1` and
`simulate-current.ps1`. They identify the 2026 tree by the shared engine sitting
in a `_build` directory beside the lesson — **not** by the presence of a
`build.bat`, since legacy projects have one of those too — and fall through to
the legacy scripts for anything under `projects/`. `tasks.json` was rewritten
around them, legacy tasks relabelled `Legacy:`, and `$gcc` problem matchers
added so compiler errors become clickable.

### Added
- **`_build/verify-all.ps1`** — builds them all and validates each `Main.hex` as
  real Intel HEX. This exists because of the `echo Build OK -> Main.hex`
  redirect bug: every hex was 13 bytes of the word "Build OK" and every
  `Test-Path` check still passed.
- **`_build/build-slides.py`** — renders every `Slide.md` into a self-contained
  HTML deck (23 decks, 487 slides) with an index, keyboard navigation, a grid
  and print-to-PDF. No third-party packages, since `markdown` is not installed
  and the repo promises a clone needs no downloads. Two renderer bugs found and
  fixed while checking output: bold spanning a source line break, and `\|`
  escapes inside table cells.

### Documentation
- Root `README.md` rewritten. The old one **duplicated its entire first 75
  lines**, carried mojibake headings, described "35 Progressive Projects", and
  linked `FRAMEWORK_GUIDE.md` and `PROJECT_CATALOG.md`, neither of which exists
  at the repo root.
- `projects/ARCHIVED.md` added, saying plainly that the tree is not maintained,
  why, and listing the topics not yet carried across.

### SimulIDE SR2 is now committed
`tools/simulide110sr2/SimulIDE_1.1.0-SR2_Win64/` (2851 files) is in git, so a
fresh clone can simulate. The five `.zip` files beside it are ignored: one
duplicates the extracted folder, the other four are SimulIDE 2.x companion apps
nothing in the course uses. `tools/simulide200/` (159 MB) remains untracked.

## 9a. Lesson 00 (2026-09-06)

`projects2026_avr/00_Introduction/` — the first class. Three parts in one deck
(28 slides): what an embedded processor is, what the development environment
does to a `.c` file, and how the ATmega128 is built.

The deck quotes this repository's own build rather than generic figures: the
real compiler invocation, the real size output, the first record of the real
`Main.hex` decoded field by field, and the actual disassembly showing
`OUT`/`SBI` for low I/O registers against `STS` for USART1 in extended I/O.
Re-derive those numbers with `avr-objdump -d Main.elf` and `avr-size -A` if the
lesson source ever changes; they are quoted literally on slides 9, 11, 15, 16
and 21.

`Main.c` links nothing from `shared_libs` on purpose and prints a banner built
from linker symbols (`__data_start`, `__heap_start`, `RAMEND`), so the memory
figures on the slides are measured rather than asserted. It walks one LED along
PORT B and pauses while PD0 is held.

Two support changes went with it:

- **`gen-readme.py` grew a `NATIVE` table.** It took every lesson's title and
  focus line from `migrate.py`, which only knows lessons that had a legacy
  source folder. Lesson 00 has none, so it rendered as `# . 00_Introduction`
  with an empty focus line and picked up a note claiming its LEDs had been
  remapped off PORT C — a thing that never happened to it. Also fixed there:
  `"00".lstrip("0")` produced an empty lesson number, and ports reached through
  the `BTN_` macros were missed the way `LED_WRITE` already was.
- **Two commands in `projects2026_avr/README.md` were unusable.**
  `pwsh _build\verify-all.ps1` and `python _build\build-slides.py` held literal
  VT (0x0B) and BS (0x08) bytes where the backslash had been eaten — the same
  damage class as section 7's `
` findings, missed because the characters are
  invisible in a terminal and in rendered Markdown. A scan of every `.md`,
  `.bat`, `.ps1`, `.c`, `.h`, `.py` and `.json` under the maintained trees found
  no others.

## 9b. The datasheet link (2026-09-06)

Every deck's title slide already carried
`**Reference**: [ATmega128 Datasheet](https://ww1.microchip.com/downloads/en/DeviceDoc/doc2467.pdf)`,
but it scrolled away at slide 2 — on a 40-slide deck the reference is out of
reach for 39 of them.

`build-slides.py` now emits a fixed top bar carrying that link and the deck
name, so it is on screen for all 487 slides, and the index page links it too.
It is one constant, `DATASHEET`, at the top of the renderer; **change the URL
there and re-run, rather than editing 58 Markdown files**. The bar is hidden
when printing, since a `position: fixed` element does not repeat reliably
across printed pages.

Two source-side gaps closed at the same time:

- `22_RTOS_Scheduler/Slide.md` was the only deck with no reference line at all.
  It is CRLF where the rest of the tree is LF, so the line was spliced in with
  the file's own endings rather than rewriting it.
- `docs/Atmega128_Reference/Slide.md` pointed at a different mirror of the same
  document (`.../DataSheets/2467S.pdf`). Both of its links now use the course
  URL, so all 140 references across the 58 decks are identical.

## 9c. GitHub Pages (2026-09-06)

> **Superseded 2026-09-14 — see section 9g.** The site root now serves the
> **ARM edition** from `projects2026_arm/`, and these AVR decks are no longer
> published at all. The design notes below still describe how the workflow
> works; only which tree it renders has changed.

The decks are published at **https://gnoejh.github.io/soc3050code/** so a class
can be taught from a machine that does not have this repository, by
`.github/workflows/pages.yml`.

Two things drove the shape of it:

- **It renders rather than serves.** `_slides/` is committed and generated, so
  the two can disagree the moment someone edits a `Slide.md` without re-running
  the renderer. The workflow runs `build-slides.py` in CI and publishes its
  output, so the site is derived from the deck sources and the committed copy is
  only for offline use.
- **It checks out `projects2026_avr/` alone**, via `sparse-checkout` plus
  `filter: blob:none`. A full clone is ~1 GB, 856 MB of it the vendored
  toolchain and SimulIDE, none of which the renderer reads. The published
  artefact is the 820 KB `_slides/` directory.

Publishing from a branch folder was rejected: `/docs` is the only folder option
besides the repo root, and it already holds 28 unrelated framework Markdown
files with no index.

Per section 11, the workflow does not trust the renderer's exit code — it
asserts `index.html` is non-empty and has a `<title>`, and that at least 24 HTML
files came out, before the artefact is uploaded.

The decks needed no changes to become a website. They are self-contained HTML
with relative links; the only outbound reference in any of them is the datasheet
URL from section 9b.

**This publishes slides only.** Building and simulating still needs a clone,
because the compiler and the simulator are vendored. Closing that gap — a Colab
notebook that `apt-get`s `gcc-avr`/`avr-libc` to compile the real lesson sources,
and possibly `simavr` to run the UART-based lessons headless — is section 10
work and unproven; simavr's ATmega128 core is far less exercised than its
ATmega328. The LED, keypad and GLCD lessons have no browser story either way
(avr8js/Wokwi is ATmega328-family only).

## 9d. Pages is live (2026-09-07)

> **The URLs in this section no longer resolve.** They were accurate when
> written; since 2026-09-14 the root is the ARM index and the AVR decks are off
> the web. See section 9g.

The one step section 10 listed as un-automatable — flipping Settings -> Pages ->
Source to **GitHub Actions** — has been done, and the site is serving:

- `https://gnoejh.github.io/soc3050code/` returns the index, titled
  "SOC3050 Lecture Decks", listing all 23 decks.
- `.../00_Introduction.html` renders as a deck and carries the section 9b top
  bar with the datasheet link, so the relative links and the fixed chrome both
  survive the trip through the CI renderer and Pages.

That closes the deploy question: the workflow's build job was never in doubt,
only whether the deploy job had a Pages target to push to. It does.

A full `verify-all.ps1` run on the same day rebuilt all 23 lessons to valid
Intel HEX, unchanged from section 8 — 5 warnings in lesson code, 45 in
`shared_libs`, all of them the known `defined but not used` and
unused-parameter cases already accounted for there.

Only the index and one deck were fetched, not all 23. The remaining 22 come off
the same renderer in the same CI run, so a failure confined to one of them is
unlikely rather than excluded.

## 9e. The application track (2026-09-08)

The course was judged dull: every lesson ended in a blinking LED or a UART print,
with no artefact a student would show anyone. The agreed fix is an **application
track**, projects 23–33, in which the same C drives games, robots, a drone and
finally a joint of a Unitree G1 — the robot taught in the maintainer's other
course, SOC4180GH. The full plan, including the MuJoCo/SITL design for 25–33, is
in `~/.claude/plans/i-want-add-more-tidy-quill.md`.

**Delivered so far: the engine and the two board-only lessons.** These need no
new infrastructure and run on today's board.

### `shared_libs/_game.c` + `_game.h`

A 2D engine for the KS0108: RAM framebuffer, dirty-page flushing, fixed-timestep
loop, debounced edge-detected input, PROGMEM font, xorshift RNG, and
interrupt-driven sound. Roughly 600 lines. It is the first genuinely new shared
library since the import.

Three measured facts drove its design, and all three are quoted on the slides:

- **`_glcd.c` costs 14 µs per byte** — `KS0108_DELAY_SETUP 1 + ENABLE 1 + HOLD 2
  + COMMAND 10`. A full 1024-byte repaint is ~14.3 ms of a 20 ms frame at 50 Hz.
  The engine drops the 10 µs settling delay for *data* writes (it is only needed
  after commands) and streams pages using the KS0108's column auto-increment.
- **`_glcd.c`'s font sits in SRAM.** `static const uint8_t ks0108_font[95][5]`
  has no `PROGMEM`, so `avr-size -A` on `18_GLCD_Graphics` reports `.data 476` —
  11.6% of the 4 KB. The engine carries its own `PROGMEM` font and does not link
  `_glcd` at all, initialising the panel itself.
- **`gfx_clear()` skips already-blank pages.** Marking all eight dirty on every
  clear would have destroyed the optimisation for any game that clears and
  redraws. Scanning 1024 bytes of RAM is an order of magnitude cheaper than
  flushing one page that did not need it.

`_game.c` defines **`__vector_12` (TIMER1_COMPA)** and **`TIMER2_COMP`**, both of
which `_timer.c` also defines, so a lesson links `_game` or `_timer`, never both.
This is the same collision class as section 5's table.

### The lessons

| Lesson | `.data` | `.text` | `.bss` | SRAM |
|---|---|---|---|---|
| `18_GLCD_Graphics` (for comparison) | 476 | 1200 | 9 | 485 |
| `23_Game_Engine_GLCD` | 30 | 3088 | 1047 | 1077 |
| `24_Game_Arcade` | 66 | 5402 | 1306 | 1372 |

- **23** is a paddle-and-ball demo whose real subject is the profile readout:
  Timer3 free-runs at 2 MHz and the program prints how long its own flush took,
  with PD0 toggling dirty-page mode so students measure the difference
  themselves rather than being told it.
- **24** is Pong, Snake and Breakout behind a menu, with sound. 24 bricks live in
  3 bytes; the snake body is 240; three complete games add 259 bytes of `.bss`
  on top of the engine.

### Support changes

- **`gen-readme.py`** gained entries for both lessons in `NATIVE`, and a rule
  that a lesson linking `_game` touches PORTA/PORTD/PORTE, the KS0108 and
  Timer1 — none of which appear in its `Main.c`, because the engine owns them.
  Without it a GLCD lesson's README listed no display.
- Verified unchanged: **5 warnings in lesson code, 45 in `shared_libs`**, exactly
  the figures section 9d records. The new library and lessons add none.

### The blank screen (2026-09-09)

`23_Game_Engine_GLCD` built clean, produced valid hex, reported the right
section sizes — and displayed **nothing** in SR2. The cause was the floating
GLCD R/W line documented in section 6, and it is worth recording how it hid:

- Every automated check passed. `verify-all.ps1` validates Intel HEX, not
  pixels. Section 11's rule "never trust a build's exit code alone" now has a
  third member alongside batch redirects and missing artefacts: **firmware that
  is correct and still drives nothing, because a pin nobody configured is an
  input by default.**
- The header comment was the trap. `_glcd.h` asserted R/W was grounded, so
  `_game.c` was written against the comment rather than the netlist. Tracing
  `Simulator.simu` took two minutes and settled it immediately.
- Lessons 1-22 masked it. Every one that uses the GLCD also links `_port.c`,
  whose blanket `DDRG = 0xFF; PORTG = 0x00;` happens to leave R/W low. The bug
  only surfaced when a lesson linked a minimal library set.

**Rule for the rest of the application track: a lesson is not done until it has
been watched running in SR2.** Compilation, size and hex validity are necessary
and clearly not sufficient.

### The actual cause: the settling delay (2026-09-09)

The floating R/W line above was a real bug, but it was not what kept the screen
blank. `18_GLCD_Graphics` was confirmed rendering text in SR2, which proved the
panel, the wiring, the active-high chip select and the column auto-increment all
work — and therefore that the fault was in `_game.c`.

The one substantive difference left was timing. `ks0108_write_byte()` waits
`KS0108_DELAY_COMMAND` (10 us) after **every** byte, command and data alike.
The KS0108 datasheet says that settling time is only required after a command,
so `_game.c` dropped it for data writes and ran the bus at 4 us per byte instead
of 14. The datasheet argument was sound; the board disagreed, and the result was
a completely blank display.

The delay is restored as `PANEL_SETTLE_US` in `_game.h`, defaulting to 10 to
match the implementation that demonstrably works. **The engine's speed-up was
never supposed to come from a shorter bus transaction anyway** — it comes from
dirty-page flushing sending 128-256 bytes a frame instead of 1024. That part is
unaffected.

`23_Game_Engine_GLCD/Slide.md` slide 4 now tells this story instead of claiming
the 4 us figure, and lowering `PANEL_SETTLE_US` became an exercise with the
on-screen microsecond readout attached. The lesson is better for it: match the
implementation known to work, then optimise with a measurement in hand.

**The general trap, worth remembering:** every automated check in this
repository passed on firmware that drove nothing. Section 11's rule now has a
fourth member — *correct-by-datasheet timing that the hardware rejects.*

### What the false R/W premise had already cost (2026-09-09)

Chasing the blank screen turned up the damage the "R/W is tied to ground" belief
had done elsewhere. `ks0108_set_pixel()` contained:

```c
// Since we can't read (R/W tied to GND), we'll use a simple approach
uint8_t pixel_data = 0;
if (mode == KS0108_PIXEL_ON) pixel_data = (1 << bit);
ks0108_data(pixel_data, controller);
```

A page byte is eight vertically stacked pixels, so **writing `1 << bit` erased
the other seven.** Vertical lines kept only their last pixel, overlapping shapes
wiped each other, and `KS0108_PIXEL_XOR` fell through to writing zero.
`ks0108_get_pixel()` and `ks0108_read()` were abandoned stubs for the same
stated reason. The premise was false: R/W is PG1, so the read they wanted was
available the whole time.

**Fixed** with hardware read-modify-write: `ks0108_read()` now turns the data
bus around and reads, and a new `ks0108_read_at()` helper handles the KS0108's
pipelined read (after setting a column the *first* read returns the previously
latched byte; each read also auto-increments the column, so the address is
re-sent before every access).

A RAM shadow like `_game.c`'s was considered and rejected: nine lessons link
`_glcd`, and `22_RTOS_Scheduler` already uses 3267 bytes of SRAM, so another
1024 would put it at 4291 — over the 4096 the chip has. The read-modify-write
costs 18 bytes of flash and no RAM.

**Warning count fell from 45 to 9 in `shared_libs`.** The old figure was mostly
one library counted repeatedly: four `_glcd.c` warnings x nine lessons = 36.
Sections 8 and 9d record 45; that number is now stale rather than wrong.

### `ks0108_putchar` corrupted glyphs crossing column 64

The two KS0108 halves keep **separate column counters**. `ks0108_putchar()`
chose the controller per byte but addressed only the one owning `x_start`:

```c
ks0108_goto_xy(page, x_start);                  // addresses ONE controller
for (i = 0; i < KS0108_CHAR_WIDTH; i++) {
    controller = ks0108_get_controller_for_column(x_start + i);
    ks0108_data(font[char_index][i], controller);   // may be the OTHER one
}
```

A glyph straddling the boundary — `x_start = 60`, bytes at 60-63 left and 64
right — sent its last byte to a chip whose counter had never been set, so it
landed at an arbitrary address and corrupted an unrelated part of the display.
Character column 10 of every text line was affected. **Fixed** by re-addressing
whenever the controller changes mid-glyph; costs 64 bytes of flash.

### Still unexplained (2026-09-09)

`18_GLCD_Graphics` renders its header bar but the four following text lines
(`STR_TITLE`, `STR_SYMBOLS`, `STR_COURSE`, `STR_DEMO1`) do not appear on screen.
Source inspection of `lcd_string_P`, `ks0108_puts_at`, `ks0108_puts`,
`ks0108_set_cursor` and the cursor-advance logic found nothing that would drop
whole lines — the first `lcd_string_P` call works and the later ones differ only
in their line argument. Isolating it needs an observation from the simulator,
not more reading. `_diag_glcd/` is the bisect harness for this.

### `simulate.bat` was running stale firmware

`_build/simulate-lesson.bat` only built when `Main.hex` was **absent**:

```bat
if not exist "Main.hex" ( call build.bat )
```

So after a lesson had been built once, no later edit to `Main.c`, `config.h` or
anything in `shared_libs` ever reached the simulator — you fixed a bug, pressed
Play, and watched the old firmware fail identically. **It now always rebuilds.**
Anyone debugging a lesson that "did not change after the fix" was probably
looking at this.

### Still open

`projects/Port_Graphics_Display/build_elmp_free.bat` links
`shared_libs/_glcd_simulide.c`, **which does not exist**. Someone wrote a
SimulIDE-specific GLCD library and it is not in the repository. That legacy
project cannot build, and the fact that it was written at all suggests the
generic library's trouble with SimulIDE predates this work.

### SITL groundwork, proven end to end (2026-09-09)

The riskiest assumption in the plan for projects 25-33 was that a C control
function, a Python harness and a physics backend could actually talk to each
other on this machine. The load-bearing half is now proven:

- **MinGW-w64 gcc 15.2.0 builds a DLL and Python `ctypes` calls into it.**
  Verified with a throwaway `add2(19, 23) -> 42` before anything was designed
  around it.
- `_hal/control.h` defines the contract every application lesson implements:
  `void control_step(const sensors_t *in, actuators_t *out)`.
- `_hal/hal_sim.c` is the host backend, compiled into `control.dll`.

Two design decisions differ from the approved plan, both deliberate:

- **ctypes, not a socket.** Direct calls have no port to collide, no framing to
  get wrong, and cost microseconds rather than milliseconds - which matters when
  a scoring run is 20 000 steps and a class each want several. The risk moves to
  struct layout, so `hal_sim.c` exports `sim_sensors_size()`, `sim_actuators_size()`
  and two field offsets, and the harness must refuse to run if they disagree. A
  silent layout mismatch would look like subtly wrong physics, not an error.
- **Integer fixed-point, not float.** Every quantity is milli-units. The
  ATmega128 has no FPU, so a control loop written in float runs fine on the host
  and misses its deadline on the board - and nobody would find out until the end
  of term.

**Not yet written:** `_build/build-host.bat`, `_sim/world.py`, `_sim/harness.py`,
`_sim/score.py`, and `hal_avr.c`. MuJoCo and pygame are **not installed**; numpy
is, which is enough to build the 2D world and prove determinism before adding a
3D backend.

### `_diag_glcd/` is a diagnostic, not a lesson

`projects2026_avr/_diag_glcd/` fills the whole panel black and blank on a 1.5 s
cycle while alternating the LEDs, with no libraries, no timers and no
interrupts. It exists to split three failure modes that all look identical:
LEDs alternating proves the firmware runs, the screen changing proves the panel
accepts writes. The leading underscore keeps it out of `verify-all.ps1`,
`gen-readme.py` and `build-slides.py`, all of which match `^\d\d_`. Delete it
once the GLCD questions in this section are closed.

### Warning to future work

`printf` in Git Bash **ate the backslashes** while writing `24_Game_Arcade`'s
`build.bat`: `\_build\resolve-libs.py` became `\_buildesolve-libs.py` (`\r`) and
`\_build\build-lesson.bat` became `\_builduild-lesson.bat` (`\b`). This is
precisely the damage class section 9a describes, reproduced live. Write `.bat`
files with a quoted heredoc (`<<'EOF'`), never `printf`, and scan afterwards —
all 50 lesson `.bat` files were checked clean with an `od`-based detector.

## 9g. The site is the ARM edition now (2026-09-14)

`projects2026_arm/` (the live STM32 edition, started 2026-09-13) took over
**https://gnoejh.github.io/soc3050code/**. `.github/workflows/pages.yml` now
checks out, renders and publishes that tree alone.

**The AVR decks are no longer published anywhere.** They are still in the
repository, still committed under `projects2026_avr/_slides/`, and still open
from `_slides/index.html` in a clone — but every root URL they used to answer
on now 404s. An intermediate layout that kept them under `/avr/` with redirect
stubs at the old root URLs was written and then dropped on instruction; the
workflow header records how to bring them back if that is ever wanted.

One consequence worth holding on to: **the committed `projects2026_avr/_slides/`
is load-bearing again.** While CI re-rendered that tree on every push, a stale
committed deck could not reach a class. Now nothing re-renders it, so an
un-regenerated `_slides/` is exactly what someone will teach from. The AVR
README says so at the renderer's description.

The ARM decks also carry an **Open in Colab** link in the top bar, beside the
reference manual, pointing at `projects2026_arm/_notebooks/SOC3050_ARM.ipynb`
— one shared notebook that `apt-get`s `arm-none-eabi-gcc` on Colab's Ubuntu,
sparse-clones this tree plus the CMSIS headers, and runs the Part 0 material
against real build output. It is the "lab-less weeks" idea from the end of
section 9c, finally built — for ARM rather than AVR, where it is easier:
`gcc-arm-none-eabi` is an Ubuntu package, and Part 0 is about reading the
build, so nothing has to execute. The link is one constant, `COLAB`, at the top
of the ARM `build-slides.py`; change it there and re-render.

**Not verified:** nobody has run the notebook on Colab. The JSON, the rendered
links and the workflow's verify/assemble steps were checked locally; the
`apt-get` and the build inside Colab have not been. Section 11's rule applies
squarely — this is exactly the kind of artefact that passes every check here
and does nothing there.

Note that this tracker still documents the AVR edition only. `projects2026_arm/`
has its own `README.md` and is not described here.

**Site addendum (2026-09-22).** The published root now carries one page that is
not a deck: `board-pins.html`, every pin name Wokwi's `diagram.json` accepts
for the Nucleo-C031C6, generated by `projects2026_arm/_build/wokwi-pins.py`
from the committed `_targets/c031c6-pins.json` and linked from every slide's
top bar. It exists because those names are not the datasheet's (`PB0` is
rejected; `PB0.1` works) and are visible nowhere else. The workflow's verify
step now expects `lessons + 2` HTML files and asserts that page is present.

The root also serves one PDF, `UM2953_Nucleo64_MB1717.pdf` — ST's manual for
the real board — committed once at `projects2026_arm/_docs/` and copied
beside the decks by the renderer, which refuses to run without it. The top
bar links it relatively, so it opens from a clone too, and the verify step
asserts it is in the site. It is shipped rather than linked because st.com
did not answer from the machine that set it up. Both the page and the PDF
were confirmed serving on 2026-09-22.

Why and how are in the ARM README; the lesson change itself (the LED bar in
lesson 04) is documented in that lesson's README.

## 9h. The ARM toolchain is vendored (2026-09-19)

Recorded here rather than in section 9g because it changes `tools/`, which both
editions share. **Everything else about the ARM edition stays in
`projects2026_arm/README.md`**, per the boundary section 2 sets.

`tools/arm-toolchain/` — xPack `arm-none-eabi-gcc` 15.2.1-1.1, and
`tools/cmsis/` — CMSIS 6 core headers plus ST's STM32C0xx device headers. A
clone now compiles ARM with no installs, the same promise the AVR side has
always made.

No download was needed: the archive from the Phase 0 spike was still in a
previous session's scratchpad, and its sha256 matched xPack's published `.sha`
before unpacking. **The vendored compiler rebuilds `_spike/c031c6/` to
byte-identical figures** — FLASH 5260 B, RAM 2000 B, exactly what
`_spike/FINDINGS.md` recorded under A2. That is the check that mattered; a
toolchain that builds *something* proves nothing.

Pruned 1.4 GB → 227 MB, keeping three multilibs (`thumb/v6-m/nofp` for the
M0+ parts, `thumb/v7-m/nofp` for the F103C8, `thumb/v7e-m+fp/hard` for the
F446RE) so every candidate target still links. `arm-none-eabi-gdb.exe` kept
deliberately — the AVR tree never had a source-level debugger at all.

**Target A is decided: the STM32C031C6.** Open since Phase 0, and lesson 04
forced it because `startup.c` and `link.ld` are per-chip. Wokwi hosts it, the
browser-upload route is proven free, and all four Part 0 decks already quote
RM0490. Full reasoning in the ARM README.

### A new member for section 11's list

`_targets/c031c6.cfg` was written with a `.cfg` extension. **CMD's `call`
silently does nothing on an unknown extension** — no error, no exit code, no
output. Every variable it should have set stayed empty, and an empty `DEVINC`
made the include flag end `-I"...\cmsis\"`, where the trailing backslash
escaped the closing quote and swallowed the entire source list. gcc reported
*"no input files"*, naming nothing that pointed at the cause.

Target files are `.bat` now, and the engine refuses to build if `MCUFLAGS` or
`DEVINC` come back empty. This is the same family as the batch-redirect bug in
section 5 and belongs beside it: **a silent no-op that produces a plausible
error somewhere else.**

## 10. Next steps

- [ ] **Finish the application track, projects 25–33** (section 9e). The C-to-DLL-to-Python
      link is proven and `_hal/control.h` + `_hal/hal_sim.c` exist. Remaining:
      `_build/build-host.bat`, `_sim/world.py`, `_sim/harness.py` (must assert the
      exported struct sizes match), `_sim/score.py`, and `hal_avr.c`. Then 25
      `Robot_Line_Follower` to prove the chain end to end, 27 `Robot_Sumo_Arena`
      for the competition, then the MuJoCo lessons ending at a Unitree G1 joint.
      **Still unproven:** MuJoCo itself is not installed, so nothing has yet
      confirmed it runs the menagerie G1 model here.
- [ ] **Watch lessons 23 and 24 actually run in SR2.** Both build clean and the
      bus timing now matches the library that works, but neither has been seen
      rendering. Per section 9e that is not optional.
- [ ] **Explain lesson 18's missing text lines** (section 9e). Needs one
      observation from the simulator, not more source reading.
- [ ] **Wire the Servo and the SR04 to the MCU**, or retire `10_PWM_Servo`'s
      claim that something moves. Both components sit on the board with no MCU
      connection at all (section 6). Same GUI-only fix as the DS1307.
- [ ] **A Colab notebook for lab-less weeks**, per the end of section 9c.
      Verify `simavr` actually runs an ATmega128 hex before promising it.
- [ ] **Wire the DS1307 on the shared board** — the one item deliberately not
      automated. Both junction nodes on PD0 and PD1 are already at their
      three-connection limit, so adding the RTC means splicing new nodes into
      existing wires. That is safe by hand in the GUI and risky to do by editing
      the circuit file every lesson shares, with no way to confirm the
      electrical result from the CLI. Step-by-step instructions are in
      `16_I2C_RTC_DS1307/README.md`. The same applies to DHT22, DS18B20,
      DS1621, ESP01 and KY040 if those lessons are ever added.
- [ ] Decide whether to save the board in SR2's native `.sim1` format
      (rev 2208). It is currently the rev-2005 `.simu`, which SR2 reads but
      does not write.
- [ ] `shared_libs/` still has checked-in `.o` files (`_init.o`, `_port.o`) and
      unused variants (`_glcd_legacy`, `_init_safe`, `_init_simple`,
      `uart_enhanced`) that `resolve-libs.py` has to exclude by name.
- [ ] Carry across any wanted topics listed in `projects/ARCHIVED.md`.

## 11. Conventions for future work

- `projects2026_avr/` is the **source of truth**. The one-time importers in
  `_build/` (`migrate.py`, `remap-leds.py`, `fix-mojibake.py`) are a record of
  what changed, not a pipeline — re-running `migrate.py --force` would discard
  every fix made since.
- `verify-all.ps1`, `gen-readme.py`, `build-slides.py` and `resolve-libs.py`
  **are** meant to be re-run; all derive their output from the lesson sources.
- Run `verify-all.ps1` after touching anything shared — the engine,
  `shared_libs`, or a `config.h`.
- Re-run `build-slides.py` after editing a deck; `_slides/` is generated.
- Board-specific facts belong in a lesson's `config.h`, not scattered in `Main.c`.
- **Never trust a build's exit code alone** on this project. Batch redirects and
  missing artefacts have both produced green builds with no firmware.
- Work directly on `main`. The repository has one branch and no history to
  preserve alongside it - see section 12.

## 12. Git history

On 2026-09-05 the repository was deliberately flattened: every prior commit was
replaced by a single root commit and force-pushed over `origin/main`.

```
0cc10fa  SOC3050 ATmega128 embedded systems course   (no parent)
```

What went with it, permanently:

- the ten commits from the 2025-12-04 root onward,
- the `projects2026-avr-simulide-sr2` and `copilot/upgrade-copilot-plan`
  branches on the remote,
- the contribution history those commits represented - the green squares for
  that period are gone from the GitHub profile, since contributions are keyed
  to commits that no longer exist.

Local backup refs and the reflog were expired afterwards at the maintainer's
instruction, so **nothing survives to restore from**. The squashed tree was
verified byte-identical to the pre-squash tip before the push, and all 22
lessons were rebuilt from the new root, so no file content was lost - only
history.

`.git` went from 112 MB to 92 MB. It will not shrink much further: the 89 MB
pack is almost entirely the vendored AVR toolchain and SimulIDE install, which
are meant to be there.

Anyone holding a clone from before this needs a fresh one; `git pull` will
refuse to fast-forward.

**Before any future history rewrite**, weigh the same three costs: contribution
history disappears, existing clones break, and open pull requests lose their
base commits.
