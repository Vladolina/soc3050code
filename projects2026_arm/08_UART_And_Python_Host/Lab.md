# Lab 08 — UART and a Python Host

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2½ hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A guided walkthrough, not a test.** You will talk to the board by hand, then
through `host.py`, check frames in both directions, and break the UART driver
in the two ways that matter most. Part 6 is a protocol change made on both
sides at once. Part 7 decodes keys from a terminal: an arrow key arrives as
three bytes, and the board has to put them back together.

Answers marked **measured** come from running the firmware in Renode
(lesson 07, slide 20) and feeding its output to `host.py`. Where Wokwi may
differ, the part says so.

Run `uv sync` once at the repository root: it brings Python, `pyserial`
(Part 3) and `matplotlib` (a real plot). Then `uv run host.py ...`.

---

## Part 0 — Build it, run it, talk to it (20 min)

```
build.bat
simulate.bat
```

Zero warnings:

```
           FLASH:       11868 B        32 KB     36.22%
             RAM:        6520 B        12 KB     53.06%
```

FLASH has grown by about 4 KB since lesson 07. **Guess first:** what for?

> Mostly the C library. Comparing the two builds' `Main.map` files, this lesson
> newly pulls in about 2 KB of it: the string-formatting side of `printf`
> (`svfprintf`, 688 B, and `vsnprintf`), 32-bit signed division (`_divsi3`,
> 468 B — the M0+ has no divide instruction), `strtol` with its character
> tables (545 B) and `puts`. The rest is the shell's text and its parsing,
> and about 1 KB for Part 7's key decoder and line editor.
> Every `%` and every `strtol` has a price on a 32 KB part, and the map file is
> where it is written down.

Paste `diagram.json`, upload `Main.elf`. The banner reads back
`BRR = 417` (slide 1b), then a telemetry frame arrives every second:

```
$TEL,1000,0,500,0,0*75
```

Click in the serial monitor's input box and type, one at a time:

```
help
led 170
rate 250
stats
```

> **Measured** replies: the help text; `bar = 0xAA` (and LEDs 1, 3, 5, 7 light
> — 170 is `10101010`); `telemetry every 250 ms`; then the counters.
> `rx overruns 0` — the interrupt keeps up with your typing with room to spare.
> Each reply follows a copy of your command. That copy is sent by the board,
> not drawn by the serial monitor (Part 7). The measured runs were made before
> the board echoed, so they do not show it.

Press button A a few times and watch the fifth field of `$TEL` count.

---

## Part 1 — Frames by hand (20 min)

On your PC, in this folder:

```
python host.py --selftest
```

> ```
> real GPS sentence   : accepted
> $LED,170 -> $LED,170*57   as expected
> one bit flipped     : rejected (checksum 56 != 57)
> ```
>
> The first line matters most: our checksum agrees with a real GPS receiver's,
> so it is **the** NMEA checksum, not just one consistent with itself.

Now make a frame and give it to the board:

```
python host.py --frame "LED,85"
```

It prints `$LED,85*6C`. Paste that into the serial monitor's input box.

> **Measured:** `$ACK,LED,85*09`, and the bar shows `01010101`.

Change one character — send `$LED,85*00`, and then `$LED,85` with no checksum.

> **Measured:** `$NAK,CHECKSUM*65` and `$NAK,NOSTAR*7D`. The board refused both
> and changed nothing. Compare Part 4, where a command *without* this
> protection does something nobody asked for.

Work out one checksum by hand, the way slide 10 does, for `PING`. Check it with
`--frame`.

---

## Part 2 — `host.py` reads the board (15 min)

Let the board run for ten seconds with `rate 250`. Select all the text in the
serial monitor, copy it, and save it as `capture.txt` in this folder. Then:

```
python host.py --file capture.txt
```

> **Measured** on a Renode session:
>
> ```
> t=   2.00s seq=1     tri=1000  A=0 B=0  |########################################
> t=   2.25s seq=2     tri= 875  A=0 B=0  |###################################
> FRAME STAT,71,3,2,0,0,0
> ...
> frames good 9, bad 0, lost (sequence gaps) 0, other lines 11
> ```
>
> "Other lines" are the banner and the shell replies — not frames, so not
> checked. Every `$` line that came out of the C code passed the Python check.

Now edit `capture.txt`: change one digit inside one `$TEL` line, and delete
another `$TEL` line entirely. Run it again.

> The changed line is counted **bad**. The deleted one shows up as a
> **sequence gap** — and so does the changed one, because its sequence number
> never arrived intact. `python host.py --demo` does the same to made-up data if
> you want to see it without a board.

If you have matplotlib, add `--plot`.

---

## Part 3 — Live, if you can (optional)

The browser's serial port lives inside the browser; your PC cannot open it.
Two ways round that:

- **Wokwi VS Code extension:** this folder's `wokwi.toml` contains
  `rfc2217ServerPort = 4000`. Start the simulator in VS Code, then
  `python host.py --port rfc2217://localhost:4000 --send "RATE,100" --seconds 10`
- **A real Nucleo-C031C6** on USB: `--port COM5` (or whatever Device Manager
  says).

> This route has not been exercised on this course yet — the file route has.
> If you try it, note what happened.

---

## Part 4 — A queue that is too short (15 min)

In `uart.c`, shrink the receive queue from 64 bytes to 4:

```c
#define RX_DEPTH    4u
```

Rebuild, upload, then **paste** — do not type — a long line all at once:

```
led 255 and a lot of extra words
```

then type `stats`.

**Guess first:** what does the bar show?

> **Measured** in Renode, where a pasted line arrives in one burst:
>
> ```
> bar = 0x00
> rx 40 bytes, 2 lines; frames 0 ok, 0 bad
> rx overruns 0, rx dropped 29; tx 209 bytes, tx waits 0
> ```
>
> The interrupt caught **every** byte — `overruns 0` — but the queue held 4, so
> 29 were thrown away. What was left of the line reached the shell as `led`
> plus fragments, `strtol` found no number, and **the board set the bar to
> 0** — acting on a command nobody sent.
>
> That is why programs speak in checksummed frames and people get the shell.
> A frame damaged the same way fails its checksum and is refused (Part 1).
>
> On silicon, bytes arrive 87 µs apart and the protocol task empties the queue
> between them, so a pasted line might survive even with 4. Wokwi's paste
> timing will decide — worth trying.

Put `RX_DEPTH` back to 64.

---

## Part 5 — Two ways to wedge a UART (15 min)

**(a) Leave the transmit interrupt on.** In `USART2_IRQHandler()`, comment out
the line that turns `TXEIE` off when the ring is empty.

> **Measured: nothing is printed — not even the banner.** `TXE` is true
> whenever the transmitter is idle, so with `TXEIE` left on the handler runs
> again the instant it returns, at priority 1, forever. `main()` never gets far
> enough to print a second character. Slide 6.

**(b) Remove the print lock** — reason about it, since it is timing-dependent.
In `say()`, delete the `os_mutex_lock` and `os_mutex_unlock` lines.

> The build now gives one warning: `'print_lock' defined but not used`. The
> compiler noticed the lock had gone, even if you did not.
>
> In Renode nothing visibly broke: its UART sends instantly, so a task is never
> caught half-way through a line. **Work out when it would break on silicon:**
> which task is printing, which one preempts it, and what wakes the preempting
> task? (Hint: the protocol task has priority 3, telemetry 2, and a byte
> arriving on the UART makes the protocol task ready.) What would `host.py`
> report for the damaged frame?

Put both back.

---

## Part 6 — Change the protocol, on both sides (open-ended)

Add the **LED bar's value** to every `$TEL` frame, as a seventh field, and
teach `host.py` to print it.

> Change the `send_frame("TEL,...")` call in `task_tel()` to append
> `,%u` with `(unsigned)bar`.
>
> **Measured, before touching `host.py`:** the board sends
> `$TEL,1000,0,500,0,0,1*68`, and the *unchanged* `host.py` still reads it —
> `frames good 2, bad 0`. It only looks at the fields it knows and ignores the
> rest. That is **forward compatibility**, and it is not an accident: it is
> why `Telemetry.feed()` tests `len(f) >= 6`, not `== 6`.
>
> Now add the new field to `host.py`'s output. Then consider the reverse: an
> *old board* and a *new* `host.py` that expects seven fields. What should the
> new `host.py` do with a six-field frame?

---

## Part 7 — A key is a frame too (25 min)

So far every byte you sent was a letter. Now press an **arrow key**.

You need a terminal that sends each key **as you press it**. The browser's
serial monitor does not: its input box sends a whole line when you press
Enter, and an arrow key only moves the cursor inside the box, so the board
never sees it. Two routes that should work:

- **The Wokwi VS Code extension's serial terminal** (use this one). It runs in
  VS Code's Terminal panel once you press Play on `diagram.json`, and it sends
  each key as you press it. **Seen working on 2026-10-19:** the arrow keys
  reached the board and were decoded.
- **miniterm**, pyserial's terminal, over Part 3's live route:
  `python -m serial.tools.miniterm rfc2217://localhost:4000 115200`.
  On Windows it turns the arrow keys into the same sequences a Linux terminal
  sends. This route has not been tried yet.

> If your terminal sends a key differently from the table in (c), the board
> spells out what it actually received, so (c) is where you will see the
> difference.

**(a) Who draws the letters?** Type `lex`, press Backspace, then finish with
`d 3` and press Enter.

> The `x` goes away and the bar shows `0x03`. Your terminal does **not** draw
> what you type. The board sends every character back (`say("%c", c)` in
> `task_proto()`), and for Backspace it sends `\b \b`: move back, write a
> space over the letter, move back again. Comment out the echo, rebuild, and
> you are typing blind. Put it back.
>
> Backspace is handled for two codes, `0x7F` and `0x08`, because terminals
> disagree about which one the key sends.

**(b) Arrows.** Press Left four times, then Right once. Then press Up and
Down.

> Each press prints its name (`Left   bar = 0x02`) and the lit LED moves along
> the bar. Up halves the telemetry period and Down doubles it, staying within
> 20–5000 ms. Every press was **three bytes**, `ESC [ D` for Left, and
> `keys.c` put them back together. Press an arrow half-way through typing a
> word: its report goes on a line of its own, then your half-typed word is
> printed again so you can carry on.

**(c) Keys with no name.** Press F1, Home, Delete and Ctrl-Right. The board
spells out each sequence. Fill in the table:

| Key | Sequence the board printed | Expected (xterm) |
|---|---|---|
| F1 | | `ESC O P` |
| Home | | `ESC [ H` |
| Delete | | `ESC [ 3 ~` |
| Ctrl-Right | | `ESC [ 1 ; 5 C` |

> The expected column is what xterm-style terminals send. VS Code's terminal
> is one. A terminal that sends something else is not wrong; it follows a
> different convention. Look at `ESC [ 1 ; 5 C`: Right is still `C`, with
> extra bytes in front of it. How does `key_feed()` know the sequence has
> ended after the `C` and not after the `1`? (Slide 9b.)

**(d) The Esc key on its own.** Type `rat`, press Esc, and wait. **Guess
first:** when does `Esc    line cancelled` appear?

> **Not until you press another key.** Esc sends one byte, `0x1B`, and it is
> the same byte that starts every arrow key. The decoder can only tell them
> apart by the silence afterwards (`KEY_ESC_WAIT`, 50 ms). But
> `uart_getc()` blocks until the *next* byte arrives, so the silence is only
> measured once it is over. Fixing that would need a `uart_getc()` with a
> timeout, and lesson 07's queue has no timed `get` yet. How would you add
> one?

**(e) Typing stops the telemetry.** Type half a word, say `sta`, and wait
two seconds.

> No `$TEL` frames arrive until you finish the line or press Esc. The echo has
> left `sta` on the screen with no newline after it. A frame sent now would
> come out as `sta$TEL,...`. `host.py` looks for lines that **start** with
> `$`, so it would skip that frame. So `task_tel()` waits while `typing` is
> set. It does not advance `seq` for a frame it skips, so `host.py` reports no
> gap.
>
> For the same reason, a line that **starts** with `$` is never echoed. If the
> board sent `host.py`'s `$RATE,100*..` back to it, `host.py` would take its
> own command for a frame from the board.

**(f) A timeout that is too short.** Set `KEY_ESC_WAIT` in `keys.h` to `0`,
rebuild, and press Left many times. Reason about it before you try.

> At 115200 baud the three bytes of an arrow key arrive about 87 µs apart. The
> clock is `os_ticks()`, which counts whole milliseconds. Most of the time all
> three bytes arrive within the same millisecond and Left still works. When a
> tick falls between the `ESC` and the `[`, the decoder decides the ESC was
> the Esc key. You should see an occasional `Esc    line cancelled` with `[D`
> typed after it. This is a reasoned prediction and has not been measured.
> It is the same class of bug as a frame cut in half: the decoder gave up on a
> sequence before it had finished arriving.

Put `KEY_ESC_WAIT` back to 50.

---

## Where to go next

- Send `$PING*..` from `host.py --send PING` on a live port and time the reply
  on the PC. How much of the round trip is the UART (count the bytes), and how
  much is the board?
- `TX_SIZE` is 256. Type `help` with `rate 20`. Does `tx waits` ever move? At
  what telemetry rate would the ring fill? (Bytes per frame × frames per second
  against 11 520 bytes per second.)
- Read `USART2_IRQHandler()` and find the case where `ORE` and `RXNE` are both
  set at once. Is the byte that caused the overrun lost, or the one before it?
