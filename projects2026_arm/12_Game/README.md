# 12 — Game

**Part 3, lesson 1 — the first application.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz), on the **app
board**: SSD1306 OLED, joystick, buttons A/B, knob, buzzer.

An arcade — **SNAKE, BREAKOUT and FLAP** behind a menu, with sound, a speed
knob, a pause, game-over and high-score screens — on a 128×64 OLED over I²C.
The lesson teaches the display driver `_lib/oled.c` (the SSD1306's page memory,
the I²C control byte, the init sequence, the framebuffer and dirty pages, and
the bus budget: a full frame is ~25 ms of I²C, more than the 20 ms tick), and a
game engine (fixed-timestep loop, input edges, sprites, AABB collision,
xorshift RNG, fixed-point physics, a sound sequencer, a SysTick microsecond
stopwatch). Every finished game prints a checksummed `$SCORE` frame for a class
leaderboard; a `$PERF` frame each second reports fps, updates/s, flush time and
bytes per frame. All game logic is pure C, and `host/test_games.c` plays it on
a PC with scripted joysticks.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 35 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: nine parts, ~2 hours, **nothing handed in**; ends in a class high-score challenge and a game of your own |
| `Main.c` | **the platform** — the only file that includes `stm32c031xx.h`: SysTick ms + µs stopwatch, the fixed-timestep loop, the sound sequencer, `$SCORE` and `$PERF` frames, serial in and out by interrupt, the OLED mirror for the station |
| `engine.h`, `engine.c` | **subject.** The `game_t` contract, `sfx()` hook, xorshift32, `aabb()`, HUD and message box. Pure C |
| `arcade.h`, `arcade.c` | menu / play / pause / game-over state machine, high scores, `score_frame()`. Pure C |
| `snake.c`, `breakout.c`, `flappy.c` | the three games: ring buffer + bitmap; bricks as bitmasks; Euler physics and a sprite. Pure C |
| `_lib/kbd.h`, `_lib/kbd.c` | (shared, `LIBS=kbd`) every control from the keyboard — a terminal's bytes or the station's `$KEY` down/up frames — written into the same `pad_t` the pins fill; `-` / `+` for the pace, `quiet` / `loud` for `$PERF`. Pure C, on lesson 08's `keys.c` |
| `_lib/mirror.h`, `_lib/mirror.c` | (shared, `LIBS=mirror`) the OLED, copied to the station as `$FB` frames while it asks — 32-byte segments, only those that changed |
| `build.bat` | `LIBS=retarget i2c oled adc pad beep proto keys kbd mirror` — no `os` (slide 15) |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board, copied unchanged from `_targets/app-board-diagram.json`; `wokwi.toml` also forwards the serial port to the station (`rfc2217ServerPort = 4000`) |
| `host/test_games.c` | **the host test**: real game files + real `_lib/oled.c`/`proto.c`, a counting I²C stub; 45 checks, bus table, simulated loop, screenshots; `--playtest` for Lab Part 6 |
| `host/run.bat`, `host/run.sh` | build and run it with plain `gcc` |
| `host/embed_shots.py` | rewrites the deck's screenshot blocks from `host/out/*.svg` |
| `host/sprite.py` | ASCII art → the column bytes `oled_sprite()` wants (Lab Part 4) |
| `host/leaderboard.py` | the class table from pasted `$SCORE` lines, checksums verified (Lab Part 7) |
| `host/out/` | screenshots written by the test, `.txt` (below) and `.svg` (in the deck) |

## Build and run

```
build.bat        # FLASH 19304 B / 32 KB, RAM 4896 B / 12 KB, zero warnings
simulate.bat
host\run.bat     # or: bash host/run.sh  - plain gcc, no board needed
```

`arm-none-eabi-size -A`: `.text` 16 872, `.rodata` 2 112, `.data` 132,
`.bss` 3 224 (framebuffer 1 032, snake ring 806, serial rings 544). The arcade's `.data + .bss`
is 3 356 B of 12 KB; the AVR edition's lesson 24 arcade used 1 372 B of 4 KB.

**Controls:** stick steers / chooses, **A** plays, serves and flaps, **B**
pauses, **knob** = speed 1–5 (in the menu), **stick press** toggles `$PERF`
between `DIRTY` (send changed pages) and `FULL` (send all eight).

**The same controls from the serial terminal**, so the OLED can be zoomed to
fill the window with the rest of the board out of sight: arrows = stick,
`a` = A, `b` or `p` = B, `s` = stick press, `1`–`5` = knob, `?` lists them.
`-` and `+` set the **pace** — slow motion at 100, 80, 60, 40, 30 or 20 % of
real time, **30 % at boot** — by running each 20 ms game tick every 20, 25, 33,
50, 66 or 100 ms of real time (`Main.c`); no game file changes, physics
included. At full speed the games were too fast to follow in the simulator.
**Halved speeds (2026-11-03):** for the same reason every game object now
moves at half its old speed at every SPEED level - SNAKE 16 .. 8 ticks per
move (was 8 .. 4), BREAKOUT's ball 0.5 .. 0.8 px per tick (was 1.0 .. 1.6;
the paddle is unchanged), FLAP on half the time scale (gravity, flap, scroll
and terminal velocity) - and every host figure below was re-measured.
Type `quiet` + Enter to stop the `$PERF` line, `loud` + Enter to restart it.
Use the Wokwi VS Code extension's serial terminal, which sends each key as
it is pressed (lesson 08 saw arrows arrive through it). A terminal never says
when a key is *released*, so an arrow holds the stick for 150 ms and the
PC's auto-repeat renews it; before auto-repeat starts the stick lets go
once, and a BREAKOUT paddle stutters at first. `_lib/kbd.h` explains.

**Better: the station.** `uv run tools/station/station.py` (with the
simulation running in VS Code) draws the OLED five times larger on the PC,
charts `$PERF`, lists every `$SCORE`, and sends each key as a real key-down
and key-up (`$KEY` frames), so a held arrow is simply held — no auto-repeat
stutter. The screen reaches the PC as `$FB` frames from `_lib/mirror.c`,
only while the station asks for them; serial output now goes through an
interrupt-driven ring so the mirror never stalls the game. See
`tools/station/README.md`.

## Class tasks - one change, predict, run, explain

Your instructor gives you **one task number**. For that task:

1. **Predict.** Before you build, write down what you expect to see - on the
   OLED (or the station), in `$SCORE` / `$PERF`, and in the host test.
2. **Change.** Make only the change the task asks for.
3. **Run.** `build.bat`, restart the simulation, play it. Then `host\run.bat`:
   does every check still pass? If one fails, is the test wrong, or the game?
4. **Explain.** In one sentence: why did it behave that way? If your
   prediction was wrong, say which line you misread.
5. **Show.** `git diff` - the instructor reads your change there.
   Afterwards `git checkout -- .` puts the lesson back.

| Task | Change | Ask yourself |
|---|---|---|
| **T1** Wider paddle | `breakout.c`: `pw = 22 - 2 * lvl` -> `pw = 40 - 2 * lvl` | Easier at every SPEED? Look at how the bounce angle is computed from `pw` - does a wide paddle change where the ball goes? |
| **T2** More bricks | `breakout.c`: `COLS 12` -> `14` | Where is brick 14 drawn? Can a wave ever be cleared? (Then try `13`.) |
| **T3** More rows | `breakout.c`: `ROWS 4` -> `8` | How close do the bricks come to the paddle? What happens to the score? |
| **T4** More lives | `breakout.c`: `lives = 3` -> `lives = 5` | The game is fine - what does the host test say, and who is right? |
| **T5** Wider gaps | `flappy.c`: `gap = 28 - 2 * lvl` -> `gap = 36 - 2 * lvl` | Run `host\out\test_games.exe --playtest`: where is FLAP's difficulty cliff now? |
| **T6** Moon gravity | `flappy.c`: `GRAVITY 7` -> `3` | Floatier - but how high does one flap now lift the bird? (Peak height = v^2 / 2g.) |
| **T7** Bigger snake | `snake.c`: `CELL 4` -> `5` | Watch the right and bottom edges. Does the host test notice? What second constant should have changed? |
| **T8** Your own sound | `Main.c`: the notes in `T_EAT[]`, `{Hz, ms}` pairs | Make eating an apple play a three-note tune. What does `{0, 0}` do? |
| **T9** Full speed | `Main.c`: `PACE_START 4` -> `0` | What do `$PERF`'s `fps` and `ups` read now, and why did nothing in the games change? |
| **T10** Rich bricks | `breakout.c`: `(ROWS - r) * level` -> `(ROWS - r) * level * 10` | What does `$SCORE` print at game over? Does lesson 08's checksum still accept it? |

Each task was built and run on the host on 2026-11-03: every one builds with
zero warnings. Some make a host check fail and some make the screen wrong
while every check passes - which is which is part of the task.

### Answers - read them after you have written your prediction

Measured on the host (`host/run.sh`, the real game code), each change applied
alone and then undone. Without any change: SNAKE 42 apples in 152.8 s;
BREAKOUT 87 bricks, 1 wave, score 648 in 5 minutes; FLAP 75 pipes in 115.9 s;
45 of 45 checks pass.

| Task | What happens | Why, in one sentence | Host test |
|---|---|---|---|
| **T1** | Easier: the autopilot breaks 92 bricks (was 87), score 684. The ball still leaves the paddle within the same range of angles, but a hit at a given distance from the centre now sends it out at a gentler angle. | `vx = off * speed * 2 / pw` in `breakout_step()`: the angle depends on where the ball hits *relative to* the paddle's width. | passes |
| **T2** | With 14 columns, brick 14 sits at x = 4 + 13 x 10 = 134 px, entirely off the 128-px screen. The ball can never reach it, so **no wave is ever cleared** (0 waves in 5 minutes). With 13, the last brick is at x = 124..132: half visible, still hittable, and waves clear. | Nothing checks that the layout fits the screen; `rows[]` is `uint16_t`, so up to 16 columns compile without a word. | passes - the test cannot see it |
| **T3** | The bricks reach y = 13 + 8 x 5 = 53, only 7 px above the paddle at y = 60. The autopilot breaks 228 bricks in 2 waves, score 3 000. With no input the game ends sooner (tick 610, was 746) - probably because the served ball meets a brick almost at once and comes straight back (not traced). | Each row pays `(ROWS - r) * level`, so the top row is now worth 8 x level. | passes |
| **T4** | The game works: five balls. | The host check *"3 balls lost, game over on tick 746"* fails (now 1364). **The test wrote down a design decision**, so it must change with the design. Is a test that pins a constant a good test? | **1 check fails** |
| **T5** | Gaps of 34..26 px instead of 26..18. The autopilot is still flying when its 2-minute run ends (76 pipes). Run `--playtest` yourself for where the cliff moves. | A wider gap gives every level 8 px more room for error. | passes |
| **T6** | Floaty - and the autopilot cannot play it: 0 pipes, crashed after 4.2 s and 3 flaps. With no input the bird lands on tick 217 (was 193). | A flap's peak is v^2 / 2g = 200^2 / (2 x 3) / 256 = about 26 px, against about 11 px at gravity 7: one flap now lifts the bird 2.3 times higher, into the ceiling or over the gap. | **2 checks fail** |
| **T7** | The grid is still 31 x 13 cells, but now 155 x 65 px: the snake runs off the right and bottom of the screen, the right border is drawn in the wrong place, and apples can appear where nobody can see them. | `GW` and `GH` are cell counts chosen for 4-px cells; nothing derives them from the screen. To try: compute them from `OLED_W`, `OLED_H` and `CELL`. | **passes** - the test plays in cells and never looks at the screen |
| **T8** | Each apple plays your tune, e.g. `{523, 80}, {659, 80}, {784, 160}` = C, E, G. `{0, 0}` ends the effect; `{0, ms}` is a rest. Flash grows by 4 bytes. | `sound_poll()` in `Main.c` starts the next note when the last one has ended; `beep()` and TIM3 do the rest. | passes |
| **T9** | Full speed: `$PERF` reads about 50 fps and 50 updates/s (was about 15 at 30 %). Much harder. | The games count ticks; the pace only decides how much real time a tick gets - game time is not wall time. | passes - the host test counts ticks |
| **T10** | `$SCORE,BREAKOUT,6480*CS` - ten times larger - and lesson 08's `proto_check()` still accepts it. | The board computes the checksum over whatever it sends: a checksum proves a line arrived intact, not that the score is honest. | passes |

Worth comparing side by side: **T4 and T7** - a test that fails on a game that
works, and a test that passes on a game that is broken. And **T2 with 13 and
then 14 columns**.

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| SSD1306 init values `AE, D5 80, A8 3F, D3 00, 40, 8D 14, 20 00, A1, C8, DA 12, 81 CF, D9 F1, DB 40, A4, A6, AF` | `_lib/oled.c`, compared with Adafruit's `Adafruit_SSD1306.cpp` `begin()` (128×64, `SWITCHCAPVCC`), which also sends `2E` (stop scroll) |
| `0x20` memory mode, `0x21` column address, `0x22` page address, `0xA6`/`0xA7` normal/invert, `0x8D` charge pump | `Adafruit_SSD1306.h` `#define`s |
| control byte `0x00` = commands, `0x40` = data (bit 6 D/C#, bit 7 Co) | SSD1306 datasheet's I²C write format, as `oled.c` uses it |
| `board-ssd1306` pins GND/VCC/SCL/SDA, `i2cAddress` default `0x3c` | Wokwi `board-ssd1306` page |
| joystick HORZ 0 V = right, VERT 0 V = bottom; SEL; arrow keys after focusing it | Wokwi `wokwi-analog-joystick` page |
| pushbutton `key`: "only active when the simulation is running and the diagram has focus" | Wokwi `wokwi-pushbutton` page |
| buzzer pins 1 (−) / 2 (+); a PWM square wave plays its frequency | Wokwi `wokwi-buzzer` page |
| I²C1 `CR2`: `SADD` 7:1, `RD_WRN` 10, `START` 13, `NBYTES` 23:16 (8 bits: ≤ 255 per transfer), `AUTOEND` 25 | `stm32c031xx.h` (`I2C_CR2_*`), as in lesson 09 |
| `SysTick_Config()` sets `LOAD = ticks − 1`, `VAL = 0`, `CTRL = CLKSOURCE | TICKINT | ENABLE`; `RELOAD` is 24 bits | `core_cm0plus.h` |
| `__WFI()` | `cmsis_gcc.h` |
| every register and bit name used in code and slides (`SysTick->LOAD/VAL/CTRL`, `TIM3` / `TIM_CCMR1_OC1M`, `OC1PE`, `TIM_CCER_CC1E`, `I2C_CR2_*`, `I2C_ISR_*`) | grepped present in `stm32c031xx.h` / `core_cm0plus.h` |
| AVR comparison: KS0108 14 µs/byte, 14.3 ms full frame; lesson 24 `.bss` 1 306, SRAM 1 372; 16-bit xorshift shifts 7, 9, 8; `PROGMEM` font | `CLAUDE.md` §9e and `shared_libs/_game.c` |
| xorshift32 shifts 13, 17, 5; period 2³² − 1 | Marsaglia, "Xorshift RNGs" (2003) — from memory, not re-fetched |

## Verified by execution — on the host

`host/run.bat` compiles `engine.c`, `arcade.c`, `snake.c`, `breakout.c`,
`flappy.c`, `_lib/oled.c`, `_lib/proto.c`, `_lib/keys.c`, `_lib/kbd.c` and `_lib/mirror.c` with MinGW gcc 15.2 (`-Wall
-Wextra`, zero warnings) and replaces only `i2c_write_read()` (counts bytes,
models 9 bits per byte at 400 kHz), `sfx()` and `on_score()`. **45 checks, all
pass; exits non-zero on any failure.**

| Test | Measured on the host |
|---|---|
| SNAKE autopilot, level 3 | 42 apples in 152.8 s, length 87 = 3 + 2 × 42, score 126 = 3 × 42 |
| SNAKE, no input | hits the wall on tick **252** = predicted 21 moves × 12 ticks |
| determinism | same seed + inputs → same 7 642 ticks, score, framebuffer hash; other seed → different |
| BREAKOUT autopilot, level 3 | 5 min: 87 bricks, 1 wave, score 648, no ball lost |
| BREAKOUT, no input | auto-serves after 3 s, three misses, game over on tick 746 |
| FLAP autopilot, level 3 | 75 pipes in 115.9 s |
| FLAP, no input | hovers to tick 150, lands on tick **193** = predicted 149 + 44 |
| `oled_init()` | 32 transfers, 1 161 bytes |
| bus per frame, dirty pages | SNAKE 50 B / 1.12 ms (worst 18.7 ms), BREAKOUT 150 B / 3.38 ms, FLAP 669 B / 15.10 ms (51 % of frames over 20 ms) |
| bus per frame, full repaint | 1 104 B / **24.92 ms** — over the 20 ms tick |
| SNAKE drawn clear-and-redraw (Lab Part 3) | 968 B / frame — the dirty-page check fails, as it should |
| loop vs a simulated clock | 50 updates/s in every case; fps 50 (every game, dirty pages), 40.1 (any full repaint) |
| arcade | menu navigation, A to play, game over reported once, `$SCORE,SNAKE,172*2E` passes `proto_check()`, high score kept, an edited score (`*2A`) rejected |
| keyboard (`kbd.c`) | `ESC [ A` = stick up for 150 ms, held by 30 Hz auto-repeat; the last arrow wins; `a` is one press; `quiet`/`loud` + Enter press nothing; Backspace edits; `4` = knob 700 until the real knob turns; `-` slower, `+`/`=` faster; one Up tap + `a` starts FLAP |
| station keys (`$KEY`) | down holds until up (no 150 ms guess); a key never renewed lets go after 600 ms; a repeat renews without re-pressing; bad checksum ignored; a `$KEY` line also works arriving byte by byte |
| OLED mirror (`mirror.c`) | silent until `$MIRROR,1`; then the whole screen once, 32 frames of ≤ 77 B, all checksummed; then only what changes (one pixel = one segment); one unchanged segment re-sent every 100 ms; stops 3 s after the last `$MIRROR,1`. A full screen costs 2 464 B = 214 ms at 115 200 baud |
| `--playtest`, FLAP pipes at levels 1–5 | 121.7, 65.3, 43.0, 31.3, 1.7 — a cliff between 4 and 5 (Lab Part 6) |

**Two bugs the host test found in this lesson's own first draft**, both fixed
and both now taught:

1. **Integer truncation in the paddle.** `px += in->x * 4 / 100` in whole
   pixels is 0 for any stick under 25 %, so a gentle push never moved the
   paddle; the autopilot kept missing by 3 px. Now the paddle is in 1/256 px
   (slide 18).
2. **A bite test after the tail moved.** SNAKE popped its tail before checking
   for a bite, so a fatal move had already shortened the snake: the
   length-equals-3-plus-twice-apples check was off by one. Now the bite test
   comes first, with the tail cell allowed (slide 22).

### Screenshots — rendered on the host from the real game code

The framebuffer after the real code ran, two pixel rows per character (`'` top,
`.` bottom, `:` both). The deck shows the same frames as SVG.

SNAKE, tick 2000 of the autopilot:

```
+--------------------------------------------------------------------------------------------------------------------------------+
|.'''' :   : .'''. :  .' :''''                                                                                       .'''. ''':' |
|'...  :'. : :   : :.'   :...                                                                                        '...:   '.  |
|    : :  ': :''': : '.  :                                                                                              .' .   : |
|''''  '   ' '   ' '   ' '''''                                                                                        ''    '''  |
|::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                         ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: :::                              :|
|:                                         ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' '''                              :|
|:         ::: ::: ::: ::: ::: ::: ::: ::: :::         ::: :::                                                                  :|
|:         ''' ''' ''' ''' ''' ''' ''' ''' '''         ''' '''                                                                  :|
|:         :::                                         ::: :::                                                                  :|
|:         '''                                         ''' '''                                                                  :|
|:         :::                                         ::: :::                                                                  :|
|:         '''                                         ''' '''                                                                  :|
|:         :::                                                                                             .:.         ::: :::  :|
|:         '''                                                                                              '          ''' '''  :|
|:         :::                                                                                                             :::  :|
|:         '''                                                                                                             '''  :|
|:         ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: ::: :::  :|
|:         ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' ''' '''  :|
|:..............................................................................................................................:|
+--------------------------------------------------------------------------------------------------------------------------------+
```

BREAKOUT, tick 900:

```
+--------------------------------------------------------------------------------------------------------------------------------+
|:'''. :'''. :'''' .'''. :  .' .'''. :   : '':''                                                                     ''':' ''':' |
|:...' :...' :...  :   : :.'   :   : :   :   :           ::::  ::::  ::::                                              '.    '.  |
|:   : : '.  :     :''': : '.  :   : :   :   :           ::::  ::::  ::::                                            .   : .   : |
|''''  '   ' ''''' '   ' '   '  '''   '''    '                                                                        '''   '''  |
|:'''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''':|
|:                                                                                                                              :|
|:   ......... ......... ......... ......... ......... ......... ......... ......... ......... ......... .........              :|
|:   ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: :::::::::              :|
|:                                                                                                                              :|
|:   ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: :::::::::              :|
|:   ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' '''''''''              :|
|:   ......... ......... ......... ......... ......... ......... ......... ......... ......... ......... .........     ::       :|
|:   ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: :::::::::              :|
|:                                                                                                                              :|
|:   ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: ::::::::: :::::::::                        :|
|:   ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' ''''''''' '''''''''                        :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                                              :|
|:                                                                                                             :::::::::::::::: :|
|:                                                                                                                              :|
+--------------------------------------------------------------------------------------------------------------------------------+
```

FLAP, tick 700:

```
+--------------------------------------------------------------------------------------------------------------------------------+
|:'''' :     .'''. :'''.                                                                                              .:     .:  |
|:...  :     :   : :...'                                                                                               :   .' :  |
|:     :     :''': :                                                                                                   :   ''':' |
|'     ''''' '   ' '                                                                                                  '''     '  |
|'''''''''''''''''''''''''''''''::::::::''''''''''''''''''''''''''''''''''''''''''''::::::::'''''''''''''''''''''''''''''''''''''|
|                               :      :                                            :      :                                     |
|                               :      :                                            :      :                                     |
|                               :      :                                            :      :                                     |
|                               :      :                                            :      :                                     |
|                               :      :                                            :      :                                     |
|                               :      :                                            :      :                                     |
|                               :      :                                            :      :                                     |
|                               :      :                                            :......:                                     |
|                               :      :                                           ::::::::::                                    |
|                               :      :                                           ''''''''''                                    |
|                               :      :                                                                                         |
|                               :......:                                                                                         |
|                              ::::::::::                                                                                        |
|                              ''''''''''                                                                                        |
|                                                                                                                                |
|                                                                                                                                |
|                                                                                                                                |
|                                                                                                                                |
|                      ...                                                                                                       |
|                    ::. .'.                                                                                                     |
|                     :    :'                                                      ..........                                    |
|                      ''''                                                        ::::::::::                                    |
|                                                                                   :'''''':                                     |
|                                                                                   :      :                                     |
|                              ..........                                           :      :                                     |
|                              ::::::::::                                           :      :                                     |
|                               ::::::::                                            :......:                                     |
+--------------------------------------------------------------------------------------------------------------------------------+
```

The menu:

```
+--------------------------------------------------------------------------------------------------------------------------------+
|                      .'''' .'''. .'''. ''':' .'''. :'''' .'''.       .'''. :'''. .'''. .'''. :''.  :''''                       |
|                      '...  :   : :       '.  : .': ''''. : .':       :   : :...' :     :   : :   : :...                        |
|                          : :   : :   . .   : :'  : .   : :'  :       :''': : '.  :   . :''': :  .' :                           |
|                      ''''   '''   '''   '''   '''   '''   '''        '   ' '   '  '''  '   ' '''   '''''                       |
|''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''|
|  ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::  |
|  ::::::'....: ::: :'...': ::'.: ....::::::::::::::::::::::::::::::::::::::::::::::::::::::::::: ::: ::. .::::::::'...':::::::  |
|  ::::::.''':: .': : ::: : '.::: ''':::::::::::::::::::::::::::::::::::::::::::::::::::::::::::: ''' ::: ::::::::: :'. :::::::  |
|  :::::::::: : ::. : ... : :.':: ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::: ::: ::: ::::::::: .:: :::::::  |
|  ::::::....::.:::.:.:::.:.:::.:.....:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::.:::.::...:::::::::...::::::::  |
|  ''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''  |
|        ....  ....  .....  ...  .   .  ...  .   . .....                                         .   .  ...         ...          |
|        :   : :   : :     :   : : .'  :   : :   :   :                                           :   :   :         :  .:         |
|        :'''. :':'  :'''  :...: :'.   :   : :   :   :                                           :''':   :         :.' :         |
|        :...' :  '. :.... :   : :  '. '...' '...'   :                                           :   :  .:.        '...'         |
|                                                                                                                                |
|                                                                                                                                |
|        :'''' :     .'''. :'''.                                                                 :   :  ':'        .'''.         |
|        :...  :     :   : :...'                                                                 :...:   :         : .':         |
|        :     :     :''': :                                                                     :   :   :         :'  :         |
|        '     ''''' '   ' '                                                                     '   '  '''         '''          |
|                                                                                                                                |
|                                                                                                                                |
|        .'''' :'''. :'''' :'''' :''.        :::::::  :::::::  :::::::  :::::::  :''''':                                         |
|        '...  :...' :...  :...  :   :       :::::::  :::::::  :::::::  :::::::  :     :                                         |
|            : :     :     :     :  .'       :::::::  :::::::  :::::::  :::::::  :     :                                         |
|        ''''  '     ''''' ''''' '''         '''''''  '''''''  '''''''  '''''''  '''''''                                         |
|                                                                                                                                |
|          .'''.  ..   :'''. :     .'''. :   :             .'''' '':''  ':'  .'''. :  .'  ..   :'''.  ':'  .'''. :  .'           |
|          :   :  ''   :...' :     :   : '. .'             '...    :     :   :     :.'    ''   :...'   :   :     :.'             |
|          :''':  ::   :     :     :''':   :                   :   :     :   :   . : '.   ::   :       :   :   . : '.            |
|          '   '       '     ''''' '   '   '               ''''    '    '''   '''  '   '       '      '''   '''  '   '           |
+--------------------------------------------------------------------------------------------------------------------------------+
```

## Status

**Builds with zero warnings** (FLASH 19 304 B, RAM 4 896 B). **The host test
passes all 45 checks** with the numbers above. Not run on silicon or in Renode.

**Watched running in Wokwi on 2026-11-01**: the menu shows, A starts a game,
the stick moves the selection, sound plays. Getting there took three fixes,
each found from serial output (details in the ARM README, "Wokwi does not
implement"):

1. **Black screen.** Wokwi's I2C leaves TXIS set after a STOP, so the second
   transfer of `oled_init()` hung. `_lib/i2c.c` now resets the block before
   every transaction.
2. **2 fps, buttons and stick ignored, no sound.** `beep_init()` left TIM3
   counting at 1 MHz between notes, which starved Wokwi's CPU (~800x).
   `_lib/beep.c` now stops the timer between notes.
3. **Frozen after the banner** once TIM3 stopped: Wokwi's `WFI` never woke.
   `SLEEP_WFI 0` at the top of `Main.c` spins to the next tick instead; set it
   to 1 on a real board. Slide 12's listing shows the `WFI` form.

**Still to measure in Wokwi**: the `$PERF` figures against the host's
prediction (~50 fps dirty, full-frame `flush_us`), at pace 100 % (`+` four times;
the default 30 % shows ~15). A full repaint measured
106 ms there - I2C runs at ~100 kHz in Wokwi whatever `TIMINGR` says - so
`FULL` mode will not reach 50 fps in the simulator. Also whether the stick's
HORZ runs the right way in BREAKOUT. **The terminal keyboard (`kbd.c`, added
2026-11-03) is not yet watched in Wokwi** - only on the host - including how
the paddle feels under auto-repeat. **The station route is not yet watched
either**: it has run against `tools/station/fakeboard.py` only, and Wokwi's
RFC2217 forwarding has never been exercised in this repository.

**Taught without lessons 09-11 (2026-11-01).** The class skipped them, so this
deck is its first contact with I2C. Slide 3b now teaches the bus on one page
(open drain, START/STOP, the address byte, ACK/NACK, 9 clocks a byte), and
slides 6, 7, 7b and 15 no longer send the reader back to lesson 09. Lessons
10 and 11 appear only as forward pointers (DMA, sleep current).

**FLAP flaps on A or stick up, not B** (2026-11-01). `flappy.c` also tested B,
but the arcade takes B as pause before a game sees it. The host test was not
re-run after that change - no host gcc on the machine that made it - but it
never presses B.
