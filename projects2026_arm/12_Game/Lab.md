# Lab 12 — Game

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Display**: [Wokwi SSD1306](https://docs.wokwi.com/parts/board-ssd1306) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A guided walkthrough, not a test — and an arcade.** You will play five
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

**The first three games were watched running in Wokwi (2026-11-01); DOOM and
PRINCE have not been.** Where this handout says what the *board* will show, it
is a prediction from the host model — say whether it came true.

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

Zero warnings, and about:

```
           FLASH:      ~27000 B        32 KB     ~82%
             RAM:        5400 B        12 KB     43.95%
```

FLASH depends on the compiler. The vendored gcc 15.2 built the three-game
arcade at 16 056 B, and DOOM and PRINCE added 10 832 B under gcc 13.2 — so
expect about 27 000 B; that sum is an estimate, not a measurement, so **write
down what yours prints**. RAM is the same on both compilers (4 248 B before the
two new games, 5 400 B after).

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

**Or play from the serial terminal** — the easier way once the OLED is zoomed
to fill the window. Click into the Wokwi serial terminal in VS Code and use:

| Key | Does |
|---|---|
| arrows | the stick |
| `a` | button A — play, serve, flap |
| `b` or `p` | button B — pause, back |
| `s` | stick press — `DIRTY` / `FULL` |
| `1` … `5` | the knob, as a SPEED level |
| `?` | print this list |
| `-` / `+` | slow motion: the pace, 20 / 30 / 40 / 60 / 80 / 100 % — **30 % at boot** |
| `quiet` + Enter | stop the `$PERF` line — `loud` + Enter brings it back |

The keys go into the same `pad_t` the pins fill (`_lib/kbd.c`), so no game can
tell. One difference you will feel: a terminal sends a key when it is
pressed, never when it is released, so an arrow holds the stick for 150 ms
and your PC's auto-repeat keeps it held. Hold Right in BREAKOUT and watch the
paddle pause once before auto-repeat starts.

**Best: play from the station.** With the simulation running, start

```
uv run tools\station\station.py
```

from the repository root. The OLED appears five times larger, `$PERF` is
charted beside it, and the same keys work once you click the picture — but
now the station sends each key's release too, so holding Right holds the
paddle with no pause. Type shell words (`quiet`, `loud`) into its command
line. Not yet tried against Wokwi: if the station keeps saying *waiting for
rfc2217://localhost:4000*, write that down and use the terminal keys.

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
| DOOM | **10 waves, 44 kills** in 5 min, hurt 5 times | breadth-first route to the exit, shoots whatever the ray walk says it sees |
| PRINCE | **11 laps** of the level in 2 min, never hurt | looks one tile ahead: jumps holes and spikes, climbs ledges, parries the blade |

**Guess first:** which one can a human beat?

> (DOOM and PRINCE: nobody has tried yet. Their autopilots see exact positions
> and never mistime a parry, so the bar to beat is the lap — about 29 s a wave in
> DOOM, 11 s a lap in PRINCE — not survival.)
>
> SNAKE, probably: its autopilot is greedy and boxes itself in. BREAKOUT's
> autopilot knows the ball's velocity exactly and never misses, so only its
> *score rate* is beatable — by aiming at the top rows, which pay 4× level per
> brick. FLAP's 75 is beatable by patient humans, and less so at higher speeds
> (Part 6).
>
> Every finished game printed a line like `$SCORE,SNAKE,42*1C`. Keep the best
> ones: Part 7 needs them.

---

## Part 2 — What do dirty pages buy? (20 min)

**First press `+` four times** (terminal or station), until the board says
`pace 100 %`: the numbers below are for full speed, and at the boot pace of
30 % `fps` and `ups` read about 15 instead of 50.

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
> | SNAKE `DIRTY` | 50 | 50 | ~47 | ~1 ms |
> | SNAKE `FULL` | ~40 | 50 | 1 032 | ~25 ms |
> | FLAP `DIRTY` | 50 | 50 | ~625 | ~15 ms |
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
> bus table moves SNAKE from **50 to 968 bytes per frame** (88 % of a full
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
uv run host\sprite.py apple.txt
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

`TICK_MS` in `engine.h` is 20: 50 updates a second at pace 100 % (press `+`
until it says so). Make it **10** (100 Hz).

**Guess first:** with a full flush costing ~25 ms, what happens to `fps` and
`ups` in `FULL` mode? And does the snake move faster?

> Run the host test first. Two checks fail, and *which* two is the lesson:
>
> - FLAP's *"hovers 3 s then falls on tick 193"* now says **343**. The hover is
>   written `3 * TICK_HZ` — three *seconds* — so it became 300 ticks. The fall
>   is in ticks, and the same 44 ticks now take half the time.
> - The arcade's high-score check fails because the 0.75 s hold after a game
>   over is `TICK_HZ * 3 / 4` = 75 ticks now, and the script only waits 50.
>
> Everything written in ticks — SNAKE's move period, every speed, gravity —
> now runs **twice as fast**; everything written in seconds stays put. The loop
> table shows `ups` 100 and, in `FULL`, `fps` still ~40: every frame now runs
> 2 or 3 updates. `MAX_CATCHUP = 4` also covers only 40 ms now.
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
    1   |     56.0     265.5 s  |      86.3        300.0 s  |   121.7     194.3 s
    2   |     61.0     235.8 s  |      85.7        300.0 s  |    65.3     108.8 s
    3   |     67.3     212.3 s  |      84.7        300.0 s  |    43.0      71.4 s
    4   |     67.7     188.2 s  |      89.0        300.0 s  |    31.3      51.2 s
    5   |     67.7     169.5 s  |     106.0        300.0 s  |     1.7       6.2 s
```

**Read it first.** FLAP's pipe count roughly halves from level 1 to 2, then
falls more gently to 31 at level 4 — and then off a **cliff**: 1.7 pipes at
level 5, 6.2 s survived. The gap shrinks 2 px per level
(`gap = 28 - 2 * lvl`: 20 px at level 4, 18 at level 5) while a flap still
climbs ~11 px. SNAKE's apple count hardly moves with level (56 at level 1,
67-68 from level 3 up) — its autopilot is never in a hurry; only the
survival time falls, from 266 s to 170 s.

**Do this:** change FLAP's curve so the survival time falls *smoothly* —
falling step by step all the way to level 5, not dropping by 95 % at the last
step. Ideas: shrink the gap
by 1 px per level and raise `speed` instead; or soften `GRAVITY`. Re-run
`--playtest` until the FLAP column looks like a curve, then run the full test
to be sure nothing else broke.

> The autopilot is not a human, but it is a *consistent* player, which a
> human is not. Game studios use bots exactly this way: not to find the right
> difficulty, but to see the shape of the curve and find the cliffs.

**The second table.** `--playtest` goes on to DOOM and PRINCE, whose autopilots
aim and parry perfectly and so *cannot tell the levels apart* (44 kills at every
level; never hurt). Each game therefore also gets a handicapped player:

```
  level | DOOM autopilot: kills waves | pacifist lasts | PRINCE autopilot: laps | never-parry: hearts lost per duel
    1   |            44.0   10.3 |        19.5 s  |             13.0       |                   1.76
    2   |            44.3   10.3 |        16.9 s  |             12.3       |                   2.31
    3   |            44.3   10.3 |        14.9 s  |             11.0       |                   3.36
    4   |            44.3   10.7 |        13.4 s  |             10.0       |                   3.71
    5   |            44.3   10.7 |        12.6 s  |              9.0       |                   4.20
```

DOOM's pacifist (walks the route, never fires) lives 19.5 s at level 1 and
12.6 s at level 5; PRINCE's never-parry prince loses more hearts per duel at
every level. Both are *monotone*, which is what a difficulty curve should be —
and PRINCE's was not until two bugs were fixed (Part 7b, task 3).

---

## Part 7 — The class high-score table (15 min)

Every finished game printed a checksummed line:

```
$SCORE,FLAP,23*52
```

**Do this:** paste your best lines into a file named after you, e.g.
`scores\kim.txt` (one folder for the class, one file per player), then

```
uv run host\leaderboard.py scores\*.txt
```

**Guess first:** change one digit of your score in the file. What happens?

> The line is **rejected**: the two hex digits are the XOR of every character
> between `$` and `*` (lesson 08's `proto_checksum()`), and a changed digit
> changes the XOR. `uv run host\leaderboard.py --check '$SCORE,FLAP,23*52'`
> shows the checksum it expects.
>
> Now compute the right checksum for your edited line by hand — XOR the ASCII
> codes — and it is **accepted**. The checksum catches copying mistakes, not
> cheating; that would need a secret key the board holds (slide 26). For a
> class leaderboard, honesty plus a checksum is the right amount of security.
>
> **Challenge for the class:** highest FLAP at SPEED 5. The autopilot manages
> 1.7 pipes there.

---

## Part 7b — The two big games (25 min, on the host)

DOOM and PRINCE are the same engine given harder problems. They are pure C like
the rest, so everything here runs with `gcc` and no board.

### 1. Where did the dirty pages go? (5 min)

**Do this:** run `host\run.bat` and find the bus table. SNAKE sends 129 B a
frame, PRINCE 432 B, DOOM 912 B.

**Guess first:** all three run at 50 updates a second. Which does `oled.c`'s
dirty-page trick help most, and which hardly at all?

> SNAKE most (12 % of a full repaint's bytes) and DOOM least (83 %). A first-person view changes
> nearly every pixel of every page the moment you move, so there is nothing
> unchanged to skip; and 71 % of DOOM's frames need more than the 20 ms tick
> on the bus alone. The loop still keeps 50 updates a second (slide 12) and the
> host's bus-only model draws 47.5 frames. **The CPU's share is not in that
> number**: DOOM writes 7 040 pixels a frame before the bus starts, so compare
> `$PERF`'s `busy %` for DOOM and SNAKE on the board.
>
> **In Wokwi, expect about 88 ms a DOOM frame** (a full repaint took 106 ms
> there; DOOM sends 912 B of 1 104): roughly 11 frames a second, and the game
> itself running a little slow once a frame passes 80 ms. A prediction — write
> down what you see.

### 2. Change the field of view (10 min)

**Do this:** the DOOM screenshot is taken at tick 766, with the imp dead ahead.
In `host/test_games.c` change that `766` (in the `run(&game_doom, ...)` line) to
`194` — an imp off to one side — run `host\run.bat`, and keep a copy of
`host\out\doom.txt`. Now in `doom.c` change `PLANE` from 169 to 256 (a 90° view
instead of 66°), run again, and compare the two pictures.

**Guess first:** how many of the 48 checks fail?

> **None.** The walls come out right, but a monster off to one side is drawn in
> the wrong column, because two numbers depend on the camera plane: `PLANE`,
> and the **97** in `draw_things()` (= 64 ÷ 0.66, columns of screen per unit of
> sideways-over-distance). With a 90° view the factor must be 64: a monster whose
> sideways offset is a third of its distance belongs **21 px** right of centre,
> and the stale 97 puts it 32 px right. Change 97 to 64 and the host test still
> passes — and now the picture is right too.
>
> The host test cannot see this: the pistol aims along the *centre* ray, which
> does not depend on the plane at all. A bug that the checks are blind to can
> only be found by looking, which is why the test writes pictures.

### 3. Parry, or don't (10 min)

**Do this:** `host\out\test_games.exe --playtest`, and read the last column of
the second table.

**Guess first:** why does a prince who never parries lose more hearts per duel
at every higher level, when the autopilot that parries loses none?

> Three numbers in `guard_think()` make a level: the pause between attacks
> (`56 - 8 * level`: 48 ticks at level 1, 20 at level 5), the guard's health
> (`2 + (level + 1) / 2`) and a flat 30 % chance to block. The curve climbs —
> **1.76, 2.31, 3.36, 3.71, 4.20** — only because the prince's grace after a
> blow, `hurt_t = 16` in `hurt()`, is *shorter than the shortest pause*.
>
> **Try it:** set `hurt_t = 30` and re-run. Level 4 falls from 3.71 to **2.38**,
> below level 3's 3.36: at levels 4 and 5 every second blade now lands inside
> the grace and is swallowed. (An earlier draft had a worse flaw: a landed blow
> cancelled the guard's raised blade, so tapping A every ten ticks locked him
> out of attacking and parrying was optional.) Put `hurt_t` back to 16.

### 4. Make the tiles page-aligned (optional, 15 min)

**Guess first:** PRINCE sends 432 B a frame. A figure 15 px tall in a world of
18 px rows always straddles three 8-row pages (3 × 129 = 387 B). If the rows
were 16 px and started on a page boundary, how many bytes would a moving figure
cost?

> Two pages — **258 B**, about 6 ms of bus instead of 9. Doing it means `TH =
> 16`, `SY0 = 16`, a one-row slab, and every literal `15` and `18` in
> `prince.c` that is really "the slab" or "a row" — a good half hour, and
> `host/test_games.c` will tell you whether you broke a rule. Measure bytes per
> frame before and after.

---

## Part 8 — Your turn (open-ended)

Pick one. Whatever you build, **add a check to `host/test_games.c`** that
fails if it breaks — the arcade's rule is that a game is not done until the
host test can play it.

- **A sixth game.** `game_t` is five function pointers; add yours to the
  `games[]` array in `arcade.c`. The menu is full — five rows of 9 px and the
  SPEED row fill the screen — so make it scroll. Flash is the other limit: the
  five games leave about 2.7 KB under gcc 13 (slide 28). Ideas that suit a 128 × 64 screen and a stick:
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
