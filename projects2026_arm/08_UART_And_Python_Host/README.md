# 08 — UART and a Python Host

**Part 1, lesson 5.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz).

USART2 driven by interrupts in both directions — receive through the ISR into
a kernel queue that a task blocks on, transmit through a lock-free ring drained
by the TXE interrupt — and a line protocol with two audiences: a shell for
people (`led 170`) and NMEA-style checksummed frames for programs
(`$LED,170*57`). `host.py`, the course's first Python, checks, charts, builds
and sends those frames.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 20 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: eight parts, ~2.5 hours, **nothing handed in** |
| `uart.c`, `uart.h` | **the subject of the lesson.** ISR with ORE/RXNE/TXE, RX kernel queue, TX SPSC ring, strong `_write()` |
| `proto.c`, `proto.h` | the frame format: XOR checksum and frame check. Pure C — no hardware |
| `keys.c`, `keys.h` | terminal escape sequences back into keys (`ESC [ D` is Left), including the timed lone-Esc case. Pure C — no hardware |
| `Main.c` | three tasks: protocol (shell + frames + keys, with echo and Backspace), telemetry, buttons; a mutex so whole lines never interleave |
| `host.py` | the PC side. `--selftest`, `--frame`, `--file`, `--port`, `--send`, `--demo`, `--plot` |
| `build.bat` | `LIBS=retarget os` |
| `simulate.bat` | always rebuilds, then opens the Wokwi board and explains both Python routes |
| `diagram.json` | lesson 05's board: LED bar and buttons A/B |
| `wokwi.toml` | also sets `rfc2217ServerPort = 4000` — the live route for `host.py` via the VS Code extension |

**Shared-code changes that came with this lesson:**

- `_lib/os.c`, `_lib/os.h` — lesson 07's kernel, copied once it had a second
  user, exactly as `_startup/` was copied from lesson 04. `07_RTOS/` keeps its
  own copy because there it is the subject.
- `_lib/retarget.c` — `uart2_init()` and `_write()` are now **weak**, so a
  lesson can replace them by defining its own. This one does. Lessons 05–07
  rebuild to identical sizes.

## Build and run

```
build.bat        # FLASH 11868 B / 32 KB, RAM 6520 B / 12 KB, zero warnings
simulate.bat
uv run host.py --selftest
```

## Python and Wokwi — the three routes

The browser's serial port cannot be opened from the PC. So:

| Route | How |
|---|---|
| **browser** (every student) | copy the serial monitor's text to `capture.txt`; `host.py --file capture.txt`. Frames to send: `host.py --frame "LED,170"` and paste |
| **Wokwi VS Code extension** | `wokwi.toml` forwards USART2 as RFC2217 on port 4000; `host.py --port rfc2217://localhost:4000` (documented by Wokwi; not yet exercised here) |
| **real Nucleo** | `host.py --port COM5` |

The protocol is printable ASCII **because** of the first route: a checksummed
text line survives a copy and paste; a binary protocol would not.

## Verified by execution — in Renode, not yet Wokwi

Run with the lesson 07 harness (Renode 1.17.0, STM32F072 Cortex-M0; `Main.c`,
`uart.c`, `proto.c` and `_lib/os.c` unchanged; test-only `SystemInit()`). Input
was typed into USART2 with Renode's `WriteChar`; output was captured and given
to `host.py --file`.

| Test | Result |
|---|---|
| session: `help`, `led 170`, `$LED,85*6C`, `$LED,85*00`, `$LED,85`, `bogus`, `$RATE,250*19`, `$STAT*12`, `stats` | help text; `bar = 0xAA`; `$ACK,LED,85*09`; `$NAK,CHECKSUM*65`; `$NAK,NOSTAR*7D`; `unknown command`; `$ACK,RATE,250*7C` and telemetry moves to 250 ms; `$STAT,71,3,2,0,0,0*25`; 0 overruns, 0 drops |
| `host.py --file` on that session | **frames good 9, bad 0, lost 0** — every C-built frame passed the Python check |
| `TXEIE` left on (Lab 5a) | **nothing printed, not even the banner** |
| print lock removed (Lab 5b) | one warning (`print_lock` unused); no visible interleaving — Renode's UART is instantaneous, so the lab makes this a reasoning question |
| `RX_DEPTH` 4, a pasted line (Lab 4) | `rx dropped 29`, `overruns 0`, and the shell executed the remains as `bar = 0x00` |
| seventh `$TEL` field (Lab 6) | `$TEL,1000,0,500,0,0,1*68`; unchanged `host.py` reads it: good 2, bad 0 |

`host.py --selftest` passes: it accepts the reference GPS sentence
`$GPGGA,...*47`, produces `$LED,170*57`, and rejects the one-bit change
`$LED,171*57`. (Writing this README caught an error: the example frame had
first been written as `*62`; the self-test said `57`, and it was right.)

**The Renode runs above came before keys and echo were added (Lab Part 7).**
The firmware now sends back each shell command as it is typed, so the same
session today would show each command before its reply, and `host.py --file`
would count more "other lines". The frames are unchanged.

### Keys and echo (Lab Part 7): working in Wokwi, tested on a PC

**Seen working in Wokwi on 2026-10-19,** through the VS Code extension's serial
terminal. That terminal sends each key as it is pressed, the arrow keys
reached USART2 as escape sequences, and `keys.c` decoded them. The bytes it
sends for F1, Home, Delete and Ctrl-Right have not been written down yet
(Lab Part 7(c)).

Before that, everything was tested on a PC:

`keys.c` was compiled with the host gcc and fed 15 byte strings: the four
arrows in both CSI and SS3 form, F1, Home, Delete, Ctrl-Right, Alt-x, a lone
Esc followed by silence, Esc followed quickly by `a`, an unfinished `ESC [`, and
a `$` frame passing through untouched. All 15 decoded as expected.
`task_proto()` was then compiled on the PC against a stub kernel and fed whole
sessions. That covered the browser's line mode (`led 170\r\n`), a typo fixed
with Backspace, arrows, an arrow pressed mid-line (its report, then the
half-typed text printed again), `rat` + Esc + `led 2` with a 290 ms gap, a
frame (no echo, correct ACK), and an empty Enter. All gave the expected output.

**Not yet seen:** miniterm over RFC2217 talking to this firmware.

**Still to do:** how fast a paste arrives in Wokwi (Lab Part 4); the
VS Code terminal's bytes for the unnamed keys (Lab Part 7(c)); and the RFC2217
live route, for both `host.py` and miniterm.

## Registers and peripherals touched

| | |
|---|---|
| `RCC->APBENR1` | USART2 clock gate |
| `GPIOA` | PA2/PA3 AF1 (USART2), PA0/PA1 buttons |
| `USART2` | `BRR` 417, `CR1` UE/TE/RE/RXNEIE (+TXEIE on demand), `ISR`, `ICR.ORECF`, `RDR`, `TDR` |
| NVIC | `USART2_IRQn` 28, priority 1 — above the kernel's 3 |
| `GPIOB` | the LED bar, set by `led` / `$LED` |
