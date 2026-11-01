# Lab 12 — Game

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Display**: [Wokwi SSD1306](https://docs.wokwi.com/parts/board-ssd1306) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A guided walkthrough, not a test — and an arcade.** You will play three
games, then take the engine under them apart: measure what the display costs,
break the trick that makes it cheap, draw a sprite, tune a difficulty curve,
and finish with a class high-score table and a game of your own.

Two tools do the measuring:

- **On the board**, the serial monitor prints a `$PERF` line every second:
  frames and updates per second, flush time, bytes per frame, CPU busy.
  Pressing the **joystick** (SEL, space bar when the stick has focus) switches
  the display between `DIRTY` (send changed pages) and `FULL` (send all eight).
- **On your PC**, `host\run.bat` (or `bash host/run.sh`) runs the *same* game
  files with scripted joysticks and prints the bus cost of every game. It
  needs only `gcc`. Everything it prints was measured; the numbers quoted
  below as "the host says" come from it.

**Nobody has yet watched this firmware run in Wokwi.** Where this handout says
what the *board* will show, it is a prediction from the host model — say
whether it came true.

Each part says **Do this**, asks you to **guess**, then explains **what you see
and why**. Nothing is marked or collected. **Put every change back before the
next part** unless the part says otherwise.

---

## Part 0 — Build it, run it, read the banner (15 min)

### Step 1. Build and simulate

```
build.bat
simulate.bat
```

Zero warnings, and:

```
           FLASH:       16056 B        32 KB     49.00%
             RAM:        4248 B        12 KB     34.57%
```

Paste `diagram.json` into Wokwi (**required** — the OLED, joystick, buttons,
knob and buzzer all live there), then `F1` → **Upload Firmware and Start
Simulation…** → `Main.elf`.

### Step 2. Read the banner

```
=== SOC3050 lesson 12 - Game ===
  OLED     : SSD1306 at 0x3C, initialised (16 commands)
  pad      : ADC ready: stick PA0/PA1, knob PA4; A PB4, B PB5, SEL PB3
  buzzer   : PA6, TIM3_CH1
```

If the OLED line says **no ACK at 0x3C**, check the two I²C wires in
`diagram.json` before anything else. The menu should be on the screen.

### Step 3. The controls

Click the joystick, then use the **arrow keys**. Click the diagram and hold
**a** or **b** for the buttons (Wokwi's buttons answer their `key` "when the
diagram has focus"). Turn the **knob**: the five SPEED boxes follow it. Press
**A** to play.

> Not yet watched: whether the `a` key reaches button A while the joystick
> has keyboard focus. If it does not, click the button with the mouse — and
> note it down; it decides how playable FLAP is.

---

## Part 1 — Play, and beat the machine (15 min)

**Do this:** play each game at **SPEED 3** (knob in the middle). Note your
best score of three tries.

The host test's autopilots set the bar, at level 3:

| Game | Autopilot (level 3) | How |
|---|---|---|
| SNAKE | **126** (42 apples) | flood fill + shortest path |
| BREAKOUT | **1 395** in 5 min, never misses | predicts where the ball lands |
| FLAP | **97** pipes | flaps when its feet near the gap's bottom |

**Guess first:** which one can a human beat?

> SNAKE, probably: its autopilot is greedy and boxes itself in. BREAKOUT's
> autopilot knows the ball's velocity exactly and never misses, so only its
> *score rate* is beatable — by aiming at the top rows, which pay 4× level per
> brick. FLAP's 97 is beatable by patient humans, and less so at higher speeds
> (Part 6).
>
> Every finished game printed a line like `$SCORE,SNAKE,42*1C`. Keep the best
> ones: Part 7 needs them.

---

## Part 2 — What do dirty pages buy? (20 min)

**Do this:** play SNAKE for 10 seconds and copy three `$PERF` lines. Press the
joystick (SEL) once — the last field changes from `DIRTY` to `FULL` — and copy
three more. Repeat in FLAP.

```
$PERF,<fps>,<ups>,<flush_us>,<max_us>,<bytes/frame>,<busy %>,<mode>*CS
```

**Guess first:** in `FULL` mode, does SNAKE get slower, or just choppier?

> The host predicts (slides 11 and 13), converted to what `$PERF` counts —
> pixel bytes only, 129 per page, where the host table also counts the window
> command and address bytes (138 per page):
>
> | | fps | ups | bytes/frame | flush |
> |---|---|---|---|---|
> | SNAKE `DIRTY` | 50 | 50 | ~120 | ~3 ms |
> | SNAKE `FULL` | ~40 | 50 | 1 032 | ~25 ms |
> | FLAP `DIRTY` | ~47 | 50 | ~870 | ~21 ms |
>
> **`ups` stays 50**: the snake moves at exactly the same speed. Only the
> picture rate falls, because the fixed-step loop runs two updates before
> some frames. That is the whole point of slide 12.
>
> `flush_us` is the first *real* measurement of the bus: the host assumes
> 400 kHz and 9 bits per byte, but `i2c.c` polls each byte and ST's `TIMINGR`
> is not exactly 400 kHz. **Write down the real full-frame time** — it is the
> number every later lesson's display budget starts from.
>
> **In Wokwi, expect every flush to take about 4× the table.** Its I²C runs at
> about 100 kHz whatever `TIMINGR` says; a full repaint measured **106 ms**
> there (2026-11-01). So `FULL` mode drops well below 40 fps. `ups` holds 50
> only while a frame takes under 80 ms (`MAX_CATCHUP` = 4 updates); past that
> the loop gives the time up and `ups` falls too — `FULL` mode in Wokwi should
> show it. Not yet measured: write down which you ran on, and both numbers.

---

## Part 3 — Break the trick (15 min, on the host)

Dirty pages only help if the game does not *re-dirty* everything. Make SNAKE
draw the obvious way. In `snake.c`, `snake_draw()`, change

```c
    if (full || need_full) {
```

to

```c
    if (1) {
```

so every frame clears the screen and redraws the wall and the whole snake.

**Guess first:** the picture is identical. How many bytes per frame now?

**Do this:** run `host\run.bat`.

> The test still passes every *game* check — the game is unchanged — but the
> bus table moves SNAKE from **129 to 970 bytes per frame** (88 % of a full
> repaint, every frame over the 20 ms tick), and the check *"dirty pages cut
> SNAKE's bus traffic by more than 4x"* **fails** — measured on the host. Clearing changes every lit byte to 0 and redrawing changes it
> back: two real changes per byte, so every page with anything on it is dirty
> (slide 10). Rule 2 cannot see that the net effect was nothing.
>
> This is the most common way a framebuffer gets slow, and nothing in the
> picture tells you. Only a measurement does.

Put it back.

---

## Part 4 — Draw a sprite (15 min)

The apple in SNAKE is a plus sign drawn with three calls. Replace it with a
sprite.

**Do this:** make `apple.txt`:

```
.#.
###
.#.
```

then

```
python host\sprite.py apple.txt
```

It prints the bytes `oled_sprite()` wants — **columns, bit 0 at the top**.

**Guess first:** what are the three bytes, before you run it?

> `0x02, 0x07, 0x02`. Column 0 has only the middle row lit: bit 1 → `0x02`.
> Column 1 is all three: `0x07`.

Now draw something better — a 3 × 3 cell has room for very little, so try a
new FLAP bird instead (8 × 6, two frames, blank line between them), paste the
array over `bird[2][BIRD_W]` in `flappy.c`, and run `host\run.bat`: the
screenshot in `host\out\flap.txt` shows your bird, drawn by the real code.

> The sprite format is the panel's format, so `oled_sprite()` is a loop over
> bits with no conversion. Draw sideways once, at design time, and the
> hardware never has to.

---

## Part 5 — Spend the frame budget (15 min)

`TICK_MS` in `engine.h` is 20: 50 updates a second. Make it **10** (100 Hz).

**Guess first:** with a full flush costing ~25 ms, what happens to `fps` and
`ups` in `FULL` mode? And does the snake move faster?

> Run the host test first. Two checks fail, and *which* two is the lesson:
>
> - FLAP's *"hovers 3 s then falls on tick 171"* now says **321**. The hover is
>   written `3 * TICK_HZ` — three *seconds* — so it became 300 ticks. The fall
>   is in ticks, and the same 22 ticks now take half the time.
> - The arcade's high-score check fails because the 0.75 s hold after a game
>   over is `TICK_HZ * 3 / 4` = 75 ticks now, and the script only waits 50.
>
> Everything written in ticks — SNAKE's move period, every speed, gravity —
> now runs **twice as fast**; everything written in seconds stays put. The loop
> table shows `ups` 100 and, in `FULL`, `fps` still ~40: nearly every frame now
> runs 2 or 3 updates. `MAX_CATCHUP = 4` also covers only 40 ms now.
>
> A commercial engine would express every speed per *second* and scale by
> `TICK_MS`; this one keeps ticks as the unit so the integer physics is exact
> and the host test can predict ticks to the one. Which would you choose for
> lesson 13's drone, where the control period is a design parameter?

Put it back.

---

## Part 6 — Tune the difficulty curve (15 min, on the host)

```
host\out\test_games.exe --playtest
```

runs every autopilot at levels 1–5 with three seeds:

```
  level | SNAKE apples  survived | BREAKOUT bricks  survived | FLAP pipes  survived
    1   |     67.7     111.3 s  |     117.3        300.0 s  |   135.3     104.8 s
    2   |     67.7     100.9 s  |     154.0        300.0 s  |    92.7      73.3 s
    3   |     67.7      92.6 s  |     129.7        300.0 s  |    63.3      48.9 s
    4   |     67.7      85.8 s  |     154.7        300.0 s  |    15.3      13.9 s
    5   |     67.7      81.0 s  |     230.0        299.1 s  |     3.7       4.6 s
```

**Read it first.** FLAP falls off a **cliff** between levels 3 and 4: the gap
shrinks 2 px per level (`gap = 28 - 2 * lvl`), and a flap climbs ~11 px, so
below a 22 px gap there is almost no room to recover. SNAKE's apple count does
not change with level at all — its autopilot is never in a hurry; only the
time does.

**Do this:** change FLAP's curve so the survival time falls *smoothly* —
roughly halving per level, not dropping by 70 % at once. Ideas: shrink the gap
by 1 px per level and raise `speed` instead; or soften `GRAVITY`. Re-run
`--playtest` until the FLAP column looks like a curve, then run the full test
to be sure nothing else broke.

> The autopilot is not a human, but it is a *consistent* player, which a
> human is not. Game studios use bots exactly this way: not to find the right
> difficulty, but to see the shape of the curve and find the cliffs.

---

## Part 7 — The class high-score table (15 min)

Every finished game printed a checksummed line:

```
$SCORE,FLAP,23*52
```

**Do this:** paste your best lines into a file named after you, e.g.
`scores\kim.txt` (one folder for the class, one file per player), then

```
python host\leaderboard.py scores\*.txt
```

**Guess first:** change one digit of your score in the file. What happens?

> The line is **rejected**: the two hex digits are the XOR of every character
> between `$` and `*` (lesson 08's `proto_checksum()`), and a changed digit
> changes the XOR. `python host\leaderboard.py --check '$SCORE,FLAP,23*52'`
> shows the checksum it expects.
>
> Now compute the right checksum for your edited line by hand — XOR the ASCII
> codes — and it is **accepted**. The checksum catches copying mistakes, not
> cheating; that would need a secret key the board holds (slide 26). For a
> class leaderboard, honesty plus a checksum is the right amount of security.
>
> **Challenge for the class:** highest FLAP at SPEED 5. The autopilot manages
> 3.7 pipes there.

---

## Part 8 — Your turn (open-ended)

Pick one. Whatever you build, **add a check to `host/test_games.c`** that
fails if it breaks — the arcade's rule is that a game is not done until the
host test can play it.

- **A fourth game.** `game_t` is five function pointers; add yours to the
  `games[]` array in `arcade.c` and the menu shows it (move the SPEED row down,
  or make the menu scroll). Ideas that suit a 128 × 64 screen and a stick:
  **PONG** against a CPU paddle that tracks the ball with a speed limit (the
  speed limit *is* the difficulty); a **space dodger** — stick up and down,
  rocks scroll in from the right, AABB against each; **TRON** light-cycles
  against a bot. Write its autopilot too.
- **A power-up.** BREAKOUT: a falling capsule that widens the paddle for
  10 seconds. SNAKE: a golden apple worth 5 that disappears after 3 s. Use
  `rng_below()` to decide when one appears.
- **A smarter flush.** `oled.c` sends a whole page if any byte in it changed.
  Track the leftmost and rightmost changed column per page and send only that
  window (the `0x21` command takes any range). Copy `oled.c` into this folder,
  drop `oled` from `LIBS`, and measure bytes per frame for BREAKOUT before
  and after with the host test. How close does it get to the bytes that
  really changed?

---

## Where to go next

- `$PERF`'s `busy %` includes the flush. Add a field for update + draw alone
  (`t1 - t0` in `Main.c`). How many microseconds does a SNAKE frame really
  need without the bus?
- The flush is polled: the CPU watches `TXIS` for the whole 25 ms. Lesson 10
  moves transfers like this to DMA. Sketch which of the loop's steps could
  overlap with a DMA flush — and which one must not (hint: slide 15).
- Read `pad.c`. `pressed` is computed from two agreeing samples. At 50 Hz,
  how long must a button be held to register — and what is the shortest tap
  FLAP can miss?
