# SOC3050 — Embedded Systems

A self-contained teaching framework. Every lesson ships with source, a lecture
deck and a simulator circuit, and the whole toolchain is vendored — clone the
repository and it works, with nothing to install.

Two editions live here. **`projects2026_avr/` (ATmega128) is finished and
frozen; `projects2026_arm/` (STM32) is the live one**, and it is what
<https://gnoejh.github.io/soc3050code/> publishes. All 20 ARM lessons are
written: four theory lessons; lessons 04–09 on startup code, GPIO and
interrupts, timers, a from-scratch RTOS, an interrupt UART with a Python host,
and the ADC, I²C and SPI buses; lessons 10–11 on fixed point, DMA, faults and
watchdogs; and lessons 12–19, which turn the same C into an arcade game, two
simulated drones, four simulated robots and a final project. They build with
the vendored `tools/arm-toolchain/` and run in the browser on Wokwi's
Nucleo-C031C6. Lessons 10–19 are verified by host tests of their pure-C logic;
of 05–19, only 06 and 12 have yet been watched running in Wokwi. The AVR tree
is still the larger body of lessons watched running in a simulator.

---

## Quick start

```bash
git clone https://github.com/gnoejh/soc3050code.git
cd soc3050code
code .
```

**Clone into a short folder** such as `C:\work\` or `Documents\`. The
vendored SimulIDE carries paths up to 172 characters, and Windows' default
260-character limit leaves about 85 for the folder you clone into. Past that,
git prints `Filename too long`, says *"Clone succeeded, but checkout failed"*,
and the clone is unusable. If you must clone deep, enable long paths first:

```bash
git config --global core.longpaths true
```

Nothing else needs installing to build: both compilers and SimulIDE are in
`tools/`. Two things are not in the repository, for the ARM lessons only:

- the **Wokwi** VS Code extension, which simulates them - VS Code offers it
  when you open the folder, and it asks you to activate a licence once;
- **uv**, which brings Python and every package the course's PC-side tools
  use (the station, the lessons' host scripts). Install it once:
  `powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"`
  (or `winget install astral-sh.uv`), then at the repository root:

  ```
  uv sync
  ```

  `pyproject.toml`, `uv.lock` and `.python-version` pin it all: Python 3.12
  (with the tkinter the station needs), pyserial, matplotlib, capstone.
  Run a tool with `uv run`, e.g. `uv run tools/station/station.py`, or use
  **Terminal → Run Task → ARM: Station**. The previous edition's Python
  track has its own group: `uv sync --group legacy`.

Then open any file under `projects2026_avr/01_Port_Basic/` and press
**Ctrl+Shift+B** to build, or run **Terminal → Run Task → Simulate Current
Project** to open it in the simulator.

From a terminal instead:

```
cd projects2026_avr\01_Port_Basic
build.bat        compiles Main.c to Main.hex
simulate.bat     opens the shared board in SimulIDE 1.1.0-SR2
```

In SimulIDE press **Play**. Where a lesson prints to the serial port, open the
**Serial Monitor** at **9600 baud, 8N1**.

To check the whole course still builds:

```
pwsh projects2026_avr\_build\verify-all.ps1
```

---

## Which lesson tree to use

There are three. They are independent; none imports from another.

| | `projects2026_arm/` | `projects2026_avr/` | `projects/` |
|---|---|---|---|
| Chip | STM32C031 (Cortex-M0+) | ATmega128 | ATmega128 |
| Status | **live, all lessons written** | finished, frozen | archived |
| Lessons | 20: Part 0 theory, Part 1 peripherals (04–09), Part 2 systems (10–11), Part 3 applications (12–19) | an introduction plus 24 | 53, with overlaps |
| Build | one shared engine, vendored gcc | one shared engine | per-project scripts |
| Run it | Wokwi in the browser | SimulIDE **1.1.0-SR2** | SimulIDE 1.1.0-SR1 |
| Slides | all 20, **published online** | all 25, offline only | 34 of 53 |

**To teach or study a topic today, start in `projects2026_avr/`** — see
[its README](projects2026_avr/README.md) for the lesson list and the board map.
It is frozen, not abandoned: everything in it builds, simulates and is verified.

**`projects2026_arm/`** is where new work goes. Part 0 is four theory decks on
the programmer's model, the instruction set, the toolchain and concurrency;
Part 1 (04–09) covers the peripherals and Part 2 (10–11) the system concerns.
Part 3 (12–19) is the application track: an arcade on an OLED, a quadcopter
and its missions, a line follower, a mapping robot, a balancing robot, a sumo
tournament and the final project, all on one shared app board, with the
drones and robots simulated inside the firmware. Every lesson from 04 has a
`Slide.md` and a `Lab.md`. The syllabus and each lesson's verification status
are in [its README](projects2026_arm/README.md).

`projects/` is left in place because a lot of supporting material still points
at it, but it is not being maintained — see
[projects/ARCHIVED.md](projects/ARCHIVED.md).

---

## The lessons

**Lesson 00** is the first class: what an embedded processor is, how the
toolchain turns C into firmware, and how the ATmega128 is built. It is the one
lesson to read before touching anything else.

Then twenty-two, in teaching order:

| | | | |
|---|---|---|---|
| 01 Port Basic | 07 Timer1 CTC | 13 UART Basic | 19 EEPROM |
| 02 Button Debounce | 08 Input Capture | 14 UART Ring Buffer | 20 Sleep Modes |
| 03 Keypad Matrix | 09 PWM DC Motor | 15 SPI Master | 21 Watchdog |
| 04 Inline Assembly | 10 PWM Servo | 16 I2C RTC | 22 RTOS Scheduler |
| 05 External Interrupts | 11 ADC Basic | 17 Character LCD | |
| 06 Timer0 Overflow | 12 ADC Light Sensor | 18 GLCD Graphics | |

Each lesson folder holds `Main.c`, `config.h`, `Slide.md`, `README.md`,
`build.bat` and `simulate.bat`.

---

## Lecture slides

Every lesson has a `Slide.md`. To turn all 25 into presentable HTML decks:

```
python projects2026_avr\_build\build-slides.py
```

Open `projects2026_avr/_slides/index.html`. Arrow keys or space to move, `o`
for an overview grid, `p` to print or save as PDF. There is also a
**Build Slides (2026)** task in VS Code.

### On the web

**<https://gnoejh.github.io/soc3050code/>** publishes the **ARM edition**
(`projects2026_arm/`), for teaching from a machine that does not have this
repository on it.

The AVR decks above are **not** published. They were the site until
2026-09-14; they are still here and still open from `_slides/index.html` in a
clone, but nothing serves them on the web now, so their old root URLs 404.

`.github/workflows/pages.yml` re-renders the ARM decks from their `Slide.md`
sources and deploys on every push that touches a deck or the renderer, so the
published site never lags the lesson text. Nothing else in the repository is
published — the workflow checks out only `projects2026_arm/` and uploads only
the rendered `_slides/` directory.

The site carries the slides only. Building and simulating a lesson still needs
a clone, because the compiler and SimulIDE are vendored under `tools/`.

---

## What is included

```
projects2026_arm/     the live STM32 edition - lessons 00-19, owns the website
projects2026_avr/     the finished AVR edition: 25 lessons + the shared board
projects/             the archived 53-project edition
shared_libs/          _port, _adc, _uart, _timer, _glcd, _eeprom, _pwm, _interrupt
tools/avr-toolchain/  avr-gcc 15.1.0, avr-objcopy, avr-size, avrdude
tools/arm-toolchain/  arm-none-eabi-gcc 15.2.1 + gdb, pruned to 227 MB
tools/cmsis/          CMSIS 6 core headers + ST's STM32C0xx device headers
tools/simulide110sr2/ SimulIDE 1.1.0-SR2 — the simulator the AVR edition targets
tools/simulide/       SimulIDE 1.1.0-SR1 and 0.4.15, for the archived tree
tools/cli/            build, program and analysis scripts
docs/                 framework, hardware and SimulIDE documentation
python_projects/      a separate Python track — see PYTHON_PROJECTS_GUIDE.md
```

**Nothing needs installing to build, on either edition.** Both compilers are
vendored, and so is SimulIDE.

The one difference: the **AVR** edition also vendors its simulator, so it needs
no internet at all. The **ARM** edition's simulator is
[wokwi.com](https://wokwi.com) in a browser — free, no account, no licence, but
it is a web page rather than a file in this repository. Students build locally
and upload `Main.elf` to it.

---

## VS Code tasks

| Task | What it does |
|------|--------------|
| **Build Current Project** (Ctrl+Shift+B) | Builds the lesson containing the open file |
| **Simulate Current Project** | Builds if needed, then opens it in SimulIDE |
| **Verify All Lessons (2026)** | Builds all 25 and validates every `Main.hex` |
| **Build Slides (2026)** | Renders all `Slide.md` files into HTML decks |
| **Show Memory Usage** | `avr-size` for the lesson's `Main.elf` |
| **Clean Project** | Removes build output from the lesson folder |
| **Program Hardware** | Flashes the built hex via avrdude |

Build and Simulate work in **both** trees — the task looks at which folder the
open file is in and picks the matching build system.

---

## Hardware

The lessons are written against the shared simulator board, and the same pin
map applies to a physical ATmega128 lab board.

| Port | Used for |
|------|----------|
| **PORTA0–7** | KS0108 graphic LCD data bus |
| **PORTB0–7** | 8 LEDs, **active low**; also stepper coils and the SPI bus |
| **PORTD0, D1, D4–D7** | push buttons, pulled up |
| **PORTD2, D3** | RXD1 / TXD1 — serial, 9600 8N1 |
| **PORTE4–E7** | GLCD control lines, also INT4–INT7 |
| **PORTF0–F2** | ADC inputs — potentiometer and joystick axes |
| **PORTG1, G4** | GLCD control, slide switch |

**LEDs are active low**: the anode is on the 5 V rail and the cathode on the
pin, so driving a pin LOW lights its LED. **PORTC is not connected** on this
board.

For real hardware you also need an ATmega128 board, a programmer (Arduino as
ISP, USBasp or similar) and a USB-serial adapter for the console.

---

## Troubleshooting

**PowerShell refuses to run a script**

```
setup.bat                                              # easiest
powershell -ExecutionPolicy Bypass -File fix-powershell.ps1
```

**Check the environment**

```
powershell -ExecutionPolicy Bypass -File verify-environment.ps1 -Verbose
```

**A lesson will not build** — run `pwsh projects2026_avr\_build\verify-all.ps1
-ShowWarnings` to see whether it is just that lesson or something shared.

**Nothing appears in the serial monitor** — check it is on 9600 baud, and that
the board's serial component is the one wired to PD2/PD3.

**The simulator will not start** — check that
`tools/simulide110sr2/SimulIDE_1.1.0-SR2_Win64/simulide.exe` is present. It is
committed to the repository, so a complete clone has it.

---

## Documentation

- [projects2026_avr/README.md](projects2026_avr/README.md) — the current edition, board map, build system
- [CLAUDE.md](CLAUDE.md) — development tracker: decisions, known issues, next steps
- [docs/LAB_GUIDE.md](docs/LAB_GUIDE.md) — lab manual
- [docs/LIBRARY_REFERENCE.md](docs/LIBRARY_REFERENCE.md) — shared library functions
- [docs/FRAMEWORK_GUIDE.md](docs/FRAMEWORK_GUIDE.md) — architecture and design
- [docs/HARDWARE_REFERENCE.md](docs/HARDWARE_REFERENCE.md) — board and pin details
- [PYTHON_PROJECTS_GUIDE.md](PYTHON_PROJECTS_GUIDE.md) — the Python track

---

## License

Educational use. See individual source files for specific terms.
