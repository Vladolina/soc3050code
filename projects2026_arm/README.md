# SOC3050 — 2026 ARM edition (STM32)

The live edition. `projects2026_avr/` is the finished 2026 AVR edition and is
**frozen**; the two trees are independent and neither imports from the other.

## What is here now

```
_build/build-lesson.bat   the build engine - lessons call it, nobody runs it
_build/build-slides.py    the renderer (this tree's own copy)
_build/regdiag.py         draws ```regs register bit maps for it (see "Writing diagrams")
_build/disasm.py          disassemble an ELF without the toolchain
_build/wokwi-pins.py      renders the board pin map (below) from the facts file
_targets/c031c6.bat       per-chip flags, include paths and memory sizes
_targets/c031c6-pins.json every pin name Wokwi accepts for the board -> MCU pin
_startup/c031c6/          shared startup.c + link.ld - linked by any lesson without its own
_lib/                     shared modules a lesson names in LIBS: retarget (printf -> USART2,
                          weak _write), os (lesson 07's kernel), uart + proto (lesson 08's
                          interrupt UART and frame format), i2c + adc (lesson 09's drivers),
                          oled (SSD1306 framebuffer), pad (joystick, buttons, knob), beep
                          (buzzer on TIM3); gpio.h and fmath.h are header-only
_targets/app-board-diagram.json  the app board every lesson from 10 on starts from
_slides/                  generated decks + board-pins.html - do not edit, re-render instead
_docs/                    the board manual (ST UM2953), copied beside the decks on render
_notebooks/               the Colab workbench, one notebook for the course
00_Architecture/          Part 0 - the programmer's model
01_Instruction_Set/       Part 0 - Thumb vs ARM, what the CPU does
02_Development/           Part 0 - toolchain, sections, linking, boot
03_Execution_Concurrency/ Part 0 - interrupts, volatile, atomicity
04_Startup_And_LinkerScript/  Part 1 - the first lesson with code
05_GPIO_And_Interrupts/   Part 1 - GPIO, EXTI, NVIC; polled vs interrupt buttons
06_Time_And_Timers/       Part 1 - SysTick IRQ, TIM3 PWM servo, TIM14 input capture
07_RTOS/                  Part 1 - a preemptive kernel from scratch; priority inversion
08_UART_And_Python_Host/  Part 1 - interrupt UART, checksummed frames, host.py
09_Sensors_And_Buses/     Part 1 - ADC, I2C (MPU6050), SPI (MAX7219)
10_Fixed_Point_And_DMA/   Part 2 - a benchmark arena: float vs Q15/Q16 vs int; DMA ADC scan
11_Faults_Watchdog_Power/ Part 2 - the crash lab: HardFault decoding, watchdogs, WFI
12_Game/                  Part 3 - an arcade on the OLED: Snake, Breakout, Flap
13_Drone_Attitude/        Part 3 - a simulated quadcopter: IMU fusion, cascaded PID, mixer
14_Drone_Missions/        Part 3 - position hold, waypoint missions from Python, failsafes
15_Robot_Line_Follower/   Part 3 - PID line following on four tracks, lap leaderboard
16_Robot_Navigation/      Part 3 - occupancy mapping, A* replanning, a reactive layer
17_Robot_Balancing/       Part 3 - a wheeled inverted pendulum: PID vs LQR (lqr.py)
18_Robot_Competition/     Part 3 - a sumo tournament: your strategy vs five bots
19_Final_Project/         Part 3 - the project brief, rubric and a template firmware
_spike/                   Phase 0 proof of concept - throwaway, see below
```

**Every lesson folder carries both a `Slide.md` (the lecture) and, from
lesson 04 onward, a `Lab.md` (the lab handout), so one week's teaching is one
directory.** Part 0's four lessons are theory and have no `Lab.md`.

## The slides

Read them at `_slides/index.html`, or online once pushed:
**https://gnoejh.github.io/soc3050code/**

That is the site root. This edition took it over from the AVR edition on
2026-09-14; `.github/workflows/pages.yml` now renders and publishes this tree
alone, and the AVR decks are no longer online anywhere.

Arrow keys move, `o` gives an overview grid, `p` prints or saves as PDF.

Re-render after editing any `Slide.md`:

```
uv run projects2026_arm/_build/build-slides.py
```

`_slides/` is generated. Edit the `Slide.md`, never the HTML.

## The board pin map

Every deck's top bar carries a **Board pins** link to `_slides/board-pins.html`:
one table of every string Wokwi accepts on the board end of a `diagram.json`
wire, and the MCU pin or rail each one reaches.

It exists because those strings are visible nowhere a student would look. The
board image does not print them, the editor does not list them, and they are
not the datasheet names: plain `PB0` is rejected, `PB0.1`, `PB0.2` and `D10`
all work and all reach PB0, and `GND` is really `GND.1` to `GND.9`. Lesson 04's
`diagram.json` hit exactly this, and a student adding one LED would have had
no way to find out why.

Two files, one derived from the other:

- `_targets/c031c6-pins.json` — the facts, committed. Derived from Wokwi's
  board definition (`wokwi/wokwi-boards`, `boards/st-nucleo-c031c6/board.json`)
  by `uv run _build/wokwi-pins.py --refresh`. That repository declares no
  licence, so the board file itself is **not** vendored; only the name-to-pin
  facts are, in our own format.
- `_slides/board-pins.html` — the page. `build-slides.py` renders it on every
  run, so it cannot be stale relative to the facts, and the Pages workflow
  verifies it is present. The notes column (pins the course reserves, board-file
  quirks such as `PD3` mapping to the 3.3 V rail) is the `NOTES` table in the
  script, maintained by hand.

If Wokwi renames a pin, run `--refresh` and re-render; do not edit the page.

### Each lesson's pins, drawn

From lesson 04 on, every deck opens with two generated slides, so students see
*where* a pin is before they meet its registers:

- **The Board** — the Nucleo's headers to scale, as Wokwi lays them out, with
  the exact header pins the lesson's `diagram.json` wires to highlighted and
  labelled. Positions come from `_targets/c031c6-layout.json` (derived from
  Wokwi's board file, like the pin names); *which* pins come from the lesson's
  own `diagram.json`, so the drawing cannot disagree with the circuit.
- **The Chip** — ports A and B as sixteen bit-cells each, in register order,
  marked `in` / `out` / `afN` / `an` as the lesson configures them. That part
  is course knowledge, in the `LESSONS` table of `_build/pinmap.py`.

```
uv run _build/pinmap.py              # after changing any diagram.json or LESSONS
uv run _build/pinmap.py --refresh    # re-derive the layout facts from Wokwi
uv run _build/build-slides.py        # then re-render
```

A slide opts in with a ```` ```svg ```` block whose first line is the
generator's marker comment; only those blocks are rewritten. A new lesson needs
an entry in `LESSONS` and the two marked blocks — copy them from lesson 09.

The top bar also carries the real board's manual, **ST UM2953, STM32 Nucleo-64
boards (MB1717)**: header pinouts, solder bridges, schematics. The pin-name
page says what Wokwi calls a pin; the manual says what it is.

The PDF lives in the tree, at `_docs/UM2953_Nucleo64_MB1717.pdf`, and
`build-slides.py` copies it beside the decks on every render, so the link is
relative and works from a clone and from the site without st.com having to
answer (it did not, from the machine that set this up). The renderer refuses
to run if the file is missing. The copy in `_slides/` is git-ignored — one
committed copy, in `_docs/`, is enough — and ST's own URL is offered on the
index page as a fallback. The constants are at the top of `build-slides.py`.

## The Colab notebook

Every deck's top bar carries an **Open in Colab** link, next to the reference
manual. It opens one shared notebook, `_notebooks/SOC3050_ARM.ipynb`, straight
from GitHub - Colab renders any notebook GitHub can serve, so the URL is just
this repository's path with `colab.research.google.com/github/` in front.

The notebook gives a student with no toolchain the **build half** of the course:
it `apt-get`s a real `arm-none-eabi-gcc`, sparse-clones this tree and the CMSIS
headers, builds the C031C6 target, and then walks the Part 0 material against
real output - sections and the memory map, the vector table read out of
`Main.bin`, Thumb-2 disassembly, and the `volatile` and read-modify-write
experiments from lesson 03.

It cannot flash a board or run the firmware; Colab has no USB, and Renode and
Wokwi stay local steps. That boundary is stated on the notebook's first screen
so nobody goes looking for a blinking LED.

The link is one constant, `COLAB`, at the top of `build-slides.py` - **change it
there and re-render**, rather than editing the decks. The notebook itself is
maintained by hand; it is not generated from anything.

### Writing diagrams

This renderer passes a ` ```svg ` fenced block through verbatim into a
`<figure class="diagram">`. Ordinary code fences still escape as usual.

Diagrams must work in both the light and dark themes, so **use `currentColor`
and the CSS variables, never hardcoded colours**. Helper classes:

| class | use |
|---|---|
| `.box` | filled panel with border |
| `.reg` | outlined cell — a register, an address |
| `.wire` / `.dash` | connector / muted dashed line |
| `.hi` `.hifill` | accent outline / tinted fill — *the thing being taught* |
| `.ok` | correct-path highlight |
| `.mono` `.lbl` | monospace / small muted caption |

Give every `<svg>` a `viewBox` (it scales; fixed width/height does not) and a
`role="img"` with an `aria-label`.

### Register diagrams: ` ```regs `

A register's bit layout is not hand-drawn. A ` ```regs ` block describes it in
one line per register, and `_build/regdiag.py` draws it as an SVG to a fixed
scale: 20 px per bit in every deck, or 10 for a 64-bit `AFR` pair. All rows
line up on bit 0, so a 2-bit field is visibly twice as wide as a 1-bit one:

````
```regs
# SysTick->CTRL = 7: core clock, interrupt, counter on
CTRL ; SYST_CSR | 32 = 0x00000007 | 16 COUNTFLAG, !2 CLKSOURCE, !1 TICKINT, !0 ENABLE
MODER ; 2 bits per pin | 32 | each 2 pins mark 5
```
````

`NAME ; note | WIDTH [= value] | fields`. A field is `hi:lo NAME` or
`bit NAME`, and `!` shades it. Bits that no field covers are drawn dashed as
reserved. A value writes every bit's digit into its cell. `each N` draws
uniform per-pin fields. The full format is in the module's docstring.

It started from a hand sketch of GPIOA's registers at one bit scale, which is
still in lesson 05 as a raw SVG (slide 4). There are now 62 of these diagrams
across the ten decks.

**Every bit position in them was checked against the CMSIS headers**
(`tools/cmsis/.../stm32c031xx.h`, `core_cm0plus.h`), with one deliberate
exception. The header masks IPSR's exception number as 9 bits (`0x1FF`), but
the Cortex-M0+ implements only bits 5:0 (ARMv6-M), so the diagrams draw 5:0.
ICSR's `VECTACTIVE` (5:0) and `VECTPENDING` (17:12) follow the same rule. Keep
new diagrams consistent with that.

## Curriculum shape: models before code

**Part 0 (lessons 00–03) is theory. No `Main.c`, no build, no simulator.**
It teaches one model — the programmer's model, the instruction set, the
toolchain, and concurrency — so that the peripheral lessons that follow are
*instances* of it rather than twenty-five separate things to memorise.

Part 0 slides quote **real output from this repository's own build** (real
disassembly, real section sizes, a real Intel HEX record decoded field by
field). That is evidence, not student coding.

**Everything a Part 0 deck quotes must be reproducible from the image**, by
reading bytes and symbols out of `_spike/c031c6/Main.elf`, `Main.bin`,
`Main.hex` and `Main.map`. Lesson 01 had one row that was not: a 32-bit
encoding `f000 f812` that appears nowhere in the image. It is now `f7ff ffcf`,
the `bl SystemInit` at `0x0800028a`, checked against the symbol addresses in
`Main.map`. **Check a quoted encoding against the image before adding it**, the
same way the AVR edition checks a hex file rather than trusting that a build
ran.

> **Those artefacts are committed** (2026-09-19). They were described as
> committed long before they were: `.gitignore` excluded
> `projects2026_arm/**/Main.elf` and friends, so a fresh clone had none of
> them, every encoding above was uncheckable there, and the documented
> `disasm.py _spike/c031c6/Main.elf` example could not run. The ignore rule now
> carries an explicit exception for `_spike/`, which is a record rather than a
> lesson. ~2 MB.

With the toolchain vendored you can also just regenerate them — `_spike/README.md`
has the exact command, and it reproduces the committed figures (FLASH 5260 B,
RAM 2000 B) section for section.

From Part 1 onward every lesson has code and follows three beats:

> **Model → Map → Measure**
> — the abstract idea, drawn; mapped onto this chip's registers by reading the
> reference manual; then written, run and *observed*.

## The syllabus (agreed 2026-09-26)

**One course, 20 lessons (00–19), no semester split.** One lesson per *model*,
not per peripheral: topics that are instances of the same model share a lesson,
and the smaller ones become Lab parts. Dense lessons may take two weeks.
Applications run **Game → Drone → Robot**, and drones and robots are
**simulation only**. RTOS and Python arrive early so the applications can lean
on both. The course stands alone; it does not depend on SOC4180GH.

| Part | # | Lesson | Target / simulator |
|---|---|---|---|
| 0 Models | 00–03 | Architecture, Instruction Set, Development, Execution & Concurrency ✅ | none |
| 1 Instances | 04 | Startup_And_LinkerScript ✅ | C031C6 / Wokwi |
| | 05 | GPIO_And_Interrupts — GPIO, EXTI, NVIC, handler names ◐ | |
| | 06 | Time_And_Timers — SysTick IRQ, TIM3, PWM (servo), input capture ✅ watched in Wokwi 2026-10-04; echo measured via EXTI, since Wokwi has no input capture | |
| | 07 | RTOS — PendSV switch, MSP/PSP, mutex, queue, HardFault ◐ | |
| | 08 | UART_And_Python_Host — ring buffer, framed protocol, `host.py` ◐ | |
| | 09 | Sensors_And_Buses — ADC, I²C (MPU6050), SPI (MAX7219) ◐ | |
| 2 Systems | 10 | Fixed_Point_And_DMA — float vs fixed point on a core with no FPU, the M4F's FPU read from a real disassembly, DMA ◐ | C031C6 / Wokwi (app board) |
| | 11 | Faults_Watchdog_Power — HardFault decoding on ARMv6-M, CFSR as the M4 has it, watchdogs, WFI; HAL vs CMSIS ◐ | |
| 3 Applications | 12 | Game — SSD1306 engine + arcade ✅ watched in Wokwi 2026-11-01 after three Wokwi-only fixes; slide 3b teaches I²C, so it stands without 09–11 | C031C6 / Wokwi (app board) |
| | 13 | Drone_Attitude — IMU fusion, attitude loops, motor mixing ✅ watched in Wokwi 2026-11-08 after a scheduling fix (lock-step + overrun guard); the station flies it as a 3D race through rings; slides 0a–0j teach how a drone flies from zero | simulated inside the firmware |
| | 14 | Drone_Missions — position hold, missions from Python, failsafes ◐ | |
| | 15 | Robot_Line_Follower ◐ | |
| | 16 | Robot_Navigation — mapping, A*, Python path planner ◐ | |
| | 17 | Robot_Balancing — PID vs LQR ◐ | |
| | 18 | Robot_Competition — sumo tournament ◐ | |
| | 19 | Final_Project ◐ | |

**Two tiers in Part 3, as in real systems:** C on the MCU (RTOS tasks) owns
sensors, filters, control loops and failsafes; Python on the host owns
missions, planning, telemetry and scoring. The low-level contract is
`control_step(const sensors_t *, actuators_t *)`, declared in each
application lesson's own `control.h` (16 calls its header `sim.h`).

✅ taught and watched running · ◐ written, zero warnings, **not yet watched in
Wokwi** — 06–09 were partly *run* in Renode instead (below); 10–19 were
verified by host tests instead (see "Parts 2 and 3").

**Re-scoped 2026-10-03, when Parts 2 and 3 were written.** Two rows above
changed from the agreed plan, both for the same reason: the planned tool was
not on this machine, so nothing written for it could have been checked.

- *10 and 11 moved from the F446RE in Renode to the C031C6 in Wokwi.* Renode
  is not vendored (the spike below said it would be; it was not) and no
  STM32F4 device headers are in `tools/cmsis/`. The Cortex-M4F survives where
  it teaches most: lesson 10 compiles the same C for it with the vendored
  `thumb/v7e-m+fp/hard` multilib and quotes the real `vmul.f32` against the
  M0+'s `bl __aeabi_fmul`; lesson 11 draws the M4's `CFSR`, labelled as not
  on this chip.
- *13–18 simulate the drone and the robots inside the firmware, not in
  Webots.* Webots, MuJoCo and PyBullet are not installed, and the repository
  promises a clone needs no downloads. A world task steps the physics and
  synthesises the sensors; the student's controller sees only the sensors.
  That is the Software-In-The-Loop architecture real drone stacks use, and it
  runs in the same free Wokwi tab as every other lesson.

**Spikes, and what became of them:**

- *Before 07, RTOS in Wokwi* — **half done.** The kernel was run, unchanged,
  on Renode's STM32F072 (a Cortex-M0, the same ARMv6-M): context switching,
  priority inversion with and without inheritance, stack overflow and the
  HardFault reporter all behaved as the slides say. Wokwi itself not yet.
- *Before 08, Python to a simulated UART* — **resolved by design.** The frame
  format is printable ASCII with an NMEA checksum, so the browser route is
  copy-and-paste into `host.py --file`; the VS Code extension's documented
  `rfc2217ServerPort` is the live route; a real board is a COM port. No
  lesson had to move.
- *Before 10* — a `nucleo_f446re.repl` was written (`_spike/f446re/`), but
  Renode itself was **never vendored**; this line used to say it was. Lessons
  10 and 11 moved to the C031C6 instead (above).
- *Before 13* — **not done.** Webots was proposed and never installed or
  tested. Lessons 13–18 simulate inside the firmware instead (above); the
  Webots route stays open for a future edition.

### How lessons 06–09 were run without Wokwi

Renode 1.17.0 (portable, not vendored — downloaded to a scratch directory) on
its STM32F072 platform. The F0 shares the C0's USART2, TIM3, TIM14, I²C1, SPI1
and ADC addresses and IRQ numbers, so lesson code ran unchanged, with three
harness-only adjustments: a test `SystemInit()` that skips the C0's clock and
flash-latency polls; the C0's GPIO block mapped as plain RAM, with `IDR`
written to press buttons; and the F0's timers clocked at 48 MHz instead of
Renode's default 10 MHz. Each lesson README says exactly what was run, what
it showed, and what Renode could not model. Where Renode and silicon
disagree — it does not fault on unaligned access, does not NACK an absent I²C
device, and its F0 ADC has no `CCRDY` — the lessons say so rather than
papering over it.

The numbering in `_spike/FINDINGS.md` (EXTI as "lesson 06") predates this
syllabus; EXTI is lesson 05.

## Parts 2 and 3: the app board, and a chip that simulates its own robot

**One circuit for lessons 10–19**, as the AVR edition had one shared board:
`_targets/app-board-diagram.json`. Each lesson's `diagram.json` is a copy (13
adds an MPU6050 beside the OLED). `_lib/pad.h` is its pin table:

| Pin | Part | Shared module |
|---|---|---|
| PA0, PA1 | analog joystick HORZ, VERT (Wokwi: HORZ reads 0 V at the *right*; `pad.c` flips it) | `pad` + `adc` |
| PA4 | potentiometer, "the knob" | `pad` + `adc` |
| PB3, PB4, PB5 | joystick SEL, buttons A and B (keys space, a, b) | `pad` |
| PA6 | buzzer, TIM3_CH1 | `beep` |
| PB8, PB9 | I²C1: SSD1306 OLED at 0x3C (and the MPU6050 at 0x68) | `i2c` + `oled` |
| PA2, PA3 | USART2 to the serial monitor | `retarget` / `uart` |

`_lib/oled.c` keeps a 1 KB framebuffer and sends only the 8-row pages that
changed: measured on the host, one moved pixel costs 136 bytes on the bus
instead of 1088, and an unchanged frame costs nothing. Its drawing half is
pure C, so every application lesson's host test can print a real frame as
ASCII art. `_lib/fmath.h` replaces libm's `sinf`/`cosf` (about 4 KB of flash)
with polynomials good to 4e-6.

**Lessons 13–18 put the physics on the chip.** A world task integrates the
drone or robot at 500 Hz–1 kHz and synthesises noisy sensors; a controller
task — the student's code — sees only `sensors_t` and writes only
`actuators_t`. The same pure-C world and controller compile on a PC, so each
lesson ships a `host/` test (`host/run.bat`, or `run.sh`) that flies, drives
or races them and fails on a bad result.

**What was verified, 2026-10-03.** Every lesson builds with zero warnings and
every host test passes:

| Lesson | FLASH / RAM | Host test, headline |
|---|---|---|
| 10 | 27 368 / 8 016 | Q15/Q16 maths 15/15; a Cortex-M0+ instruction model (`host/m0sim.py`) runs `Main.elf` and agrees with the PC on 21 of 22 workloads |
| 11 | 25 340 / 5 256 | 107/107; the fault decoder agrees with `objdump` on all 2327 load, store, branch and trap instructions in the image |
| 12 | 19 304 / 4 896 | 45/45; each game played by script, by terminal keys and by `$KEY` frames; the OLED mirror; Snake costs 50 bus bytes a frame against 1104 for a full repaint (games at half speed since 2026-11-03) |
| 13 | 28 768 / 11 144 | 24/24; a 20° roll step settles in 0.34 s with 0.3 % overshoot |
| 14 | 29 600 / 10 776 | default mission 57.7 s in still air, 61.0 s in 8 m/s wind; all failsafes fire |
| 15 | 29 484 / 9 744 | all four tracks, five laps each, no DNF; PD beats P-only 3.80 s to 12.56 s a lap |
| 16 | 31 100 / 11 584 | 25/25 runs reach the goal, 0 collisions; `planner.py` and the C A* agree 15/15 |
| 17 | 26 280 / 10 408 | LQR survives the biggest push (0.78 N s); the cascaded PID settles faster and carries more payload |
| 18 | 29 244 / 9 872 | a 1500-match league in 1.9 s, identical hash on two runs and at -O0/-O2/-Os |
| 19 | 18 780 / 10 552 | 15/15; the template's health and watchdog logic |

FLASH / RAM for 12-19 are the 2026-11-03 builds, after the station wiring
(`kbd`, `mirror`, `keys`): 1.9-2.5 KB more flash and 250-600 B more RAM each.
Lesson 16 now has 1.7 KB of flash and 704 B of RAM left; lesson 18's room for
a student's `student.c` under its 29 KB line fell from ~2 KB to ~0.4 KB.

**Wokwi does not implement DMA, IWDG, PWR or RTC** on this board, and WWDG is
"implemented, not tested" ([Wokwi's board page](https://docs.wokwi.com/parts/board-st-nucleo-c031c6)).
**Nor does it implement timer input capture**, which its page does not say:
lesson 06, watched on 2026-10-04, counted 0 capture edges while EXTI on the
same pin caught every echo. Its timers are "used by analogWrite()" - PWM out.
Lesson 06 now measures the echo through EXTI as well, and says which path ran.
Lesson 10 detects a DMA channel that never moves and falls back to polled
reads, saying which path ran. Lessons 11 and 19 keep the real IWDG code for
silicon and add a software watchdog the simulator can show.

**Three more Wokwi behaviours, found getting lesson 12 to run (2026-11-01),**
each measured on serial output, none on Wokwi's page:

- **I2C leaves TXIS set after a STOP** (`ISR = 0x23` once a write ends). The
  next transfer's first wait for TXIS passed at once, byte 1 went out before
  the address, the byte count was off by one, and the transfer hung - the
  OLED stayed black and every flush timed out. `i2c.c` (shared and lesson
  09's copy) now resets the block with PE before every transaction. It also
  ignores TIMINGR's PRESC/SCLDEL/SDADEL (reads back `0x273D`): a 129-byte page
  takes 12.5 ms, about 100 kHz, not 400.
- **A timer left counting starves the CPU.** `beep.c` kept TIM3 running at
  1 MHz with 0% duty between notes; lesson 12's loop then ran ~800x slower
  than the same code before `beep_init()` (96 ms per `pad_read()`), and every
  button press fell between two reads. TIM3 now stops between notes.
- **`WFI` with no timer running never wakes** - the board froze right after
  the banner once TIM3 stopped. Lesson 12's idle loop spins to the next tick
  (`SLEEP_WFI 0`); lesson 11 has the same kind of switch.

Also: Wokwi's CPU is several times slower than 48 MHz against its own
SysTick (100 000 empty loop passes: 200 ms), so per-pixel code costs more
there than on silicon - a full menu redraw takes 40 ms.

**Watched in Wokwi: 12** (2026-11-01, menu, start, stick, sound) **and 13**
(2026-11-08, after a scheduling fix; OLED, sound, controls and the station's
3D race). **Not yet watched — 10, 11, 14–19.** Lessons 14–17 schedule their
physics the way 13 did before its fix, so expect the same freeze. Each README's Status section
lists the first three things to look at. The largest open questions are
on-chip ones a PC cannot answer: real cycles per control step, task stack
high-water marks (every lesson prints them on `stats`), and whether lesson
18's `$RESULT` hash from the chip matches the host league's.

## The `_spike/` directory

Phase 0 proof of concept. **Not lessons** — delete or promote when the build
engine lands. It exists because the plan's riskiest assumptions needed testing
before any lesson was written. All six passed; see `_spike/FINDINGS.md`.

```
_spike/c031c6/   Nucleo C031C6 (Cortex-M0+)  - builds, runs in Wokwi
_spike/l031k6/   Nucleo L031K6 (Cortex-M0+)  - builds, runs in Renode
_spike/f103c8/   Blue Pill F103C8 (Cortex-M3)- builds, runs in Renode
_spike/f446re/   Nucleo F446RE (Cortex-M4F)  - builds, runs in Renode
```

Each carries the three files that replace what avr-libc did for free:
`startup.c` (vector table, reset handler, `.data`/`.bss` init, clock),
`link.ld` (memory map), and a `main.c` that blinks and prints over USART2.

## Setting up a PC (once)

1. **Clone into a short folder** (`C:\work\`): the vendored SimulIDE has long
   paths, and a deep folder makes the checkout fail (root README).
2. **VS Code**, opened at the repository root. Accept the recommended
   extensions - **Wokwi** simulates the lessons (activate its licence once).
3. **uv**, once: `powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"`
   (or `winget install astral-sh.uv`), then in a new terminal at the root:
   `uv sync`. That fetches Python 3.12 and every package the PC-side tools use.
4. **Build before simulating**: the firmware is not in git. Open a lesson's
   `Main.c` and press **Ctrl+Shift+B** (or run `build.bat` in its folder).
5. **Simulate**: F1 → *Wokwi: Start Simulator* with the lesson's folder.
6. **Station** (lessons 12-19): **Terminal → Run Task → ARM: Station**, or
   `uv run tools/station/station.py`. Click the OLED; the keyboard is the board.

Every Python command in these lessons is written `uv run X.py`; it works from
any folder inside the repository.

## Building

**The toolchain is vendored** (2026-09-19). A clone compiles with no installs:

```
tools/arm-toolchain/   xPack arm-none-eabi-gcc 15.2.1-1.1, pruned to 227 MB
tools/cmsis/           CMSIS 6 Core headers + ST's STM32C0xx device headers
                       (CMSIS = Common Microcontroller Software Interface
                       Standard, ARM's vendor-neutral Cortex-M header set)
```

The prune keeps three multilibs — `thumb/v6-m/nofp` (M0+, the C031C6 and
L031K6), `thumb/v7-m/nofp` (M3, the F103C8) and `thumb/v7e-m+fp/hard`
(M4F, the F446RE) — so all four candidate targets still link. It drops the
other 36, C++, Fortran, LTO and the bundled Python. `arm-none-eabi-gdb.exe` is
kept deliberately: both Wokwi and Renode expose a GDB server, and the AVR tree
never had a source-level debugger at all.

The archive's sha256 was verified against xPack's published `.sha` before
unpacking, and the vendored compiler was confirmed to rebuild
`_spike/c031c6/` to **byte-identical** figures (FLASH 5260 B, RAM 2000 B) —
the same numbers `_spike/FINDINGS.md` recorded under A2.

To build a lesson, run `build.bat` from inside its folder. The engine compiles
**every `.c` in that folder**, so a lesson is self-sufficient by construction
with no source list to maintain:

```bat
set TARGET=c031c6
set LIBS=
call "%~dp0..\_build\build-lesson.bat"
```

A lesson that carries its own `startup.c` and `link.ld` uses them; otherwise
the engine falls back to the target's defaults.

> **The simulator is still hosted.** The compiler ships in the repository, but
> Wokwi is a web page. That is the one part of the AVR edition's "a clone needs
> nothing" promise this tree cannot repeat — and students still pay nothing.

### The `.cfg` trap, recorded so it is not repeated

`_targets/c031c6.bat` was first written as `c031c6.cfg`. **CMD's `call`
silently does nothing on an unknown extension** — no error, no exit code. Every
variable it should have set stayed empty, and an empty `DEVINC` made the
include flag end `-I"...\cmsis\"`, where the trailing backslash escaped the
closing quote and swallowed the entire source list. gcc reported *"no input
files"*, naming nothing that pointed at the real cause.

Target files are therefore `.bat`, and the engine now refuses to build if
`MCUFLAGS` or `DEVINC` came back empty.

## Seeing the assembly

Every Part 0 deck quotes disassembly, and until the toolchain is vendored
nobody with a fresh clone can reproduce it. `_build/disasm.py` closes that gap
by reading the ELF itself:

```
uv sync                                                    # once: capstone comes with it
uv run _build/disasm.py _spike/c031c6/Main.elf -f _write
uv run _build/disasm.py _spike/c031c6/Main.elf            # the whole image
uv run _build/disasm.py _spike/c031c6/Main.hex            # no symbols - see below
```

capstone is a 3 MB wheel rather than a 335 MB toolchain download. The script
follows the ELF's `$t` / `$d` **mapping symbols**, so literal pools print as
`.word` instead of being rendered as invented instructions, and it labels
branch targets with the function they land in. Its output for `_write` was
compared line by line with lesson 01 slide 14 - 23 instructions, every encoding
and every mnemonic identical.

A `.hex` or `.bin` carries neither symbols nor mapping symbols, so it is
disassembled as one long Thumb run and the vector table at the front comes out
as nonsense. That is worth showing a class exactly once: it is what "the ELF is
not just the bytes" means.

**`arm-none-eabi-objdump -d` remains the real answer** - richer output, source
interleaving with `-S`, and no second implementation to trust. The Colab
notebook already runs it. This is the offline stopgap, not a replacement.

## Simulation, and what it costs

| Route | Licence | Cost | Renewal |
|---|---|---|---|
| wokwi.com in a browser, loading a locally built ELF | **none** | **free** | never |
| Wokwi VS Code extension | personal key | free | every 30 days |
| Renode, offline | none | free | never |

**Wokwi is a simulator only — it never compiles anything for this course.**
Students build locally with `arm-none-eabi-gcc`, then load `Main.elf` in the
browser via `F1 → Upload Firmware and Start Simulation…`. That works for STM32
and is documented nowhere except `_spike/FINDINGS.md`; Wokwi's own docs
describe the feature only under ESP32.

The extension buys two things: the simulator inside VS Code instead of a
browser tab, and **source-level debugging**. Running a lesson still needs
nothing; debugging one needs the extension and its free key.

### Debugging a lesson in VS Code (F5)

Set up (2026-09-30) so the Run menu works on every lesson from 04 on:
breakpoints, Step Over / Into / Out, Continue, variables, the call stack.

**One-time setup:**
- Install the extensions VS Code offers from `.vscode/extensions.json`. The two
  that matter are **Wokwi Simulator** and **C/C++** (`ms-vscode.cpptools`),
  which provides the `cppdbg` debugger.
- `F1 → Wokwi: Request a New License`. The key is free; it expires after
  30 days and is renewed the same way.

**Each session:**
1. Open the lesson's `Main.c` and build it: `Ctrl+Shift+B`.
2. Open the lesson's `diagram.json`. It opens in Wokwi's diagram editor; press
   its green **Play** button. The board starts running.
3. Back in `Main.c`, set breakpoints with `F9` and press **`F5`**.

**How it works.** Each lesson's `wokwi.toml` sets `gdbServerPort = 3333`, so
the simulated chip is also a GDB server. `.vscode/launch.json` rebuilds the
lesson through the "Build Current Project" task; Wokwi notices the changed
firmware and reloads it by itself. The vendored
`tools/arm-toolchain/bin/arm-none-eabi-gdb.exe` then attaches to
`localhost:3333` with the lesson's `Main.elf`. Step 2 goes through the diagram
on purpose: Wokwi then reads the `wokwi.toml` beside that diagram, so it always
simulates the lesson being debugged. Starting from the command palette uses
whichever config was selected last, and that may be a different lesson.

**To stop at the first instruction**, for example to step through
`Reset_Handler` in lesson 04: replace step 2 with `F1 → Wokwi: Select Config
File` (this lesson's `wokwi.toml`), then `F1 → Wokwi: Start Simulator and Wait
for Debugger`. The chip then holds at reset until `F5` attaches.

**The one limit:** `F5` cannot start the simulator itself. That is an editor
command, and a `preLaunchTask` can only run programs. If `F5` reports
*connection refused* on `localhost:3333`, the simulator is not running: do
step 2.

Lessons build with `-Os`, so stepping can jump between lines and some locals
read *optimized out*. That is the code the chip really runs, and the slides
quote its addresses, so the build is not switched to `-Og` for debugging.

### Reaching code that only an event reaches

Lesson 05's two interesting paths — the debouncer accepting a press, the EXTI
handler running — are entered by a button, and clicking a Wokwi button under a
debugger mostly fails: a click is a press *and* a release, and while the chip
is halted simulated time is frozen, so the press is over before the program
samples the pin. Three rules, worked step by step in Lab 05 Part 8 and on the
lesson's slide 33b:

1. **Latch the input.** Ctrl-click a Wokwi pushbutton and it stays pressed
   until the next click; holding its `key` does the same. Then Continue and let
   the program reach the breakpoint in its own time.
2. **Break where the event is decided, not where the signal arrives.** The
   handler stops on every bounce edge; the debouncer's first line stops a
   thousand times a second. The accept line and `main()`'s decision run once.
3. **Inject the event from the register.** The STM32C0's `EXTI->SWIER1`
   (`0x40021808`) raises a line as a rising edge would, and the NVIC's `ISPR`
   (`0xE000E200`) pends a vector outright — both from the Debug Console with
   `-exec set *(unsigned int *)ADDR = VALUE`. No mouse, one clean edge, and
   the same trick tests any later interrupt before its source exists.

`debounce_step()` is inlined under `-Os`, but line breakpoints inside it still
work: `-g3` keeps the line table. Whether Wokwi's GDB server accepts writes to
peripheral registers has not been confirmed; Lab 05 Part 8 says so.

## The station: the board's screen and keyboard on the PC (2026-11-03)

The app board's OLED is 128 x 64 - too small to watch in a simulator window,
and too small for a drone or a robot. So lessons 12-19 are watched and driven
from **`tools/station/station.py`**, a ground station in one window: the OLED
drawn large, the lesson's telemetry drawn at the PC's resolution (an artificial
horizon, a map, a pendulum), and a console. Its keyboard sends real key-down
and key-up as `$KEY` frames; `_lib/kbd.c` folds them into the same `pad_t`
the pins fill. The OLED reaches the PC as `$FB` frames from `_lib/mirror.c`,
sent only while the station asks. The window opens maximised; a lesson whose
own picture matters more than the OLED (lesson 13's horizon) takes the whole
left side, and the OLED moves to a small panel above the console. **The board computes; the PC draws** - the
serial link carries state at a few KB/s, never pixels at PC resolution.

Python and pyserial come from the course's uv environment (`uv sync` at the
root). Each lesson's `wokwi.toml` forwards the UART as
`rfc2217ServerPort = 4000`. `tools/station/fakeboard.py` stands in for a board.
Tested against `fakeboard.py` and by drawing each view headless; **not yet
confirmed here against Wokwi**. Details
and the protocol: `tools/station/README.md`.

## Decisions

- **Target A is the STM32C031C6** (decided 2026-09-19, at lesson 04 — the
  first lesson with code, which is where the choice finally bites). Three
  things settled it: Wokwi hosts it and the browser-upload route is proven
  free (`_spike/FINDINGS.md`, A1b); its `MODER` GPIO and one-write 48 MHz clock
  match every modern STM32 family, where the F103C8's `CRL`/`CRH` matches none;
  and all four Part 0 decks already quote RM0490, the STM32C0x1 reference
  manual, so a different part would contradict four weeks of taught slides.

  What was given up: the F103C8 would have run offline in Renode with no
  dependence on a hosted service staying free. That risk is accepted, and
  `_startup/` is structured per-target so a port is a new `_targets/*.bat`
  plus a startup file, not a rewrite.
- **Target B** was to be the F446RE under Renode for the application track.
  **Superseded 2026-10-03:** the application track runs on Target A, with the
  physics simulated in the firmware (see "Parts 2 and 3"). The F446RE's
  multilib (`thumb/v7e-m+fp/hard`) is still kept, and lesson 10 compiles for
  it to read FPU instructions.

## Conventions

- `_slides/` is generated. Re-run the renderer; never edit the HTML.
- **Every register block gets its whole map on one page first** (2026-10-03,
  the maintainer's rule). The first slide that introduces any register of a
  peripheral or core block shows the block's complete register list, offsets
  from the header, how the lesson uses each one and which slide explains it,
  plus one `regs` drawing of every register the lesson touches. Slides that
  explain registers one at a time come after it. Lesson 06's slides 1 and 5
  are the model. A new map slide takes a letter suffix (5b, 20b) so that no
  existing slide number, or any Lab reference to it, moves.
- **A `---` line is what starts a slide.** A heading alone does not; two
  sections merge silently and the deck still renders, so check the slide count
  the renderer prints after editing. Lesson 01 shipped with slides 9 and 10
  fused this way.
- **Never touch `projects2026_avr/`.** `git status projects2026_avr` should
  always be empty.
- **Never rely on a backslash escape surviving a heredoc** when generating
  files — use `chr(10)` rather than a newline escape. This has bitten this
  repository three times; see `CLAUDE.md` §9a, §9e and `_spike/FINDINGS.md`.
- Per `CLAUDE.md` §11: never trust a build's exit code alone. A lesson is not
  done until its output has been *watched*.
