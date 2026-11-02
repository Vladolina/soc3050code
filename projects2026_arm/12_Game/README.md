# 12 — Game

**Part 3, lesson 1 — the first application.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz), on the **app
board**: SSD1306 OLED, joystick, buttons A/B, knob, buzzer.

An arcade — **SNAKE, BREAKOUT, FLAP, DOOM and PRINCE** behind a menu, with
sound, a speed knob, a pause, game-over and high-score screens — on a 128×64
OLED over I²C.
The lesson teaches the display driver `_lib/oled.c` (the SSD1306's page memory,
the I²C control byte, the init sequence, the framebuffer and dirty pages, and
the bus budget: a full frame is ~25 ms of I²C, more than the 20 ms tick), and a
game engine (fixed-timestep loop, input edges, sprites, AABB collision,
xorshift RNG, fixed-point physics, a sound sequencer, a SysTick microsecond
stopwatch). Every finished game prints a checksummed `$SCORE` frame for a class
leaderboard; a `$PERF` frame each second reports fps, updates/s, flush time and
bytes per frame. All game logic is pure C, and `host/test_games.c` plays it on
a PC with scripted joysticks.

**DOOM and PRINCE** were added after the first three games, to give the same
engine harder problems. **DOOM is a DOOM-*style* raycaster, not Doom**: the real
engine cannot run here (its data file alone is about 4 MB; this chip has 32 KB
of flash and 12 KB of RAM), so this is the technique that came before it —
Wolfenstein 3D's — in integer C, with an original 24 × 16 map, original sprites
and two monster types. **PRINCE is a *Prince of Persia*-style platformer**:
tiles, running and standing jumps, ledge climbing, falls counted in floors,
spikes, a loose floor, a pressure plate and gate, and a sword duel. No code,
art or data from either original is used. **Neither has been watched running in
Wokwi** — see Status.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 38 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: nine parts plus Part 7b (the two big games), ~2.5 hours, **nothing handed in**; ends in a class high-score challenge and a game of your own |
| `Main.c` | **the platform** — the only file that includes `stm32c031xx.h`: SysTick ms + µs stopwatch, the fixed-timestep loop, the sound sequencer, `$SCORE` and `$PERF` frames |
| `engine.h`, `engine.c` | **subject.** The `game_t` contract, `sfx()` hook, xorshift32, `aabb()`, HUD and message box. Pure C |
| `arcade.h`, `arcade.c` | menu / play / pause / game-over state machine, high scores, `score_frame()`. Pure C |
| `snake.c`, `breakout.c`, `flappy.c` | the first three games: ring buffer + bitmap; bricks as bitmasks; Euler physics and a sprite. Pure C |
| `doom.c` | **DOOM**: a raycaster (one DDA ray per screen column), a 24 × 16 map, billboard monsters with a per-column depth buffer, a pistol, doors, an exit, waves. Integer only. Pure C |
| `prince.c` | **PRINCE**: a 30 × 6 tile world in three rooms across and two down, run / jump / grab / climb, falls counted in rows, spikes, a loose slab, a plate and gate, a guard and a sword duel; the room redrawn by tile difference. Pure C |
| `build.bat` | `LIBS=retarget i2c oled adc pad beep proto` — no `os` (slide 15) |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board, copied unchanged from `_targets/app-board-diagram.json` |
| `host/test_games.c` | **the host test**: real game files + real `_lib/oled.c`/`proto.c`, a counting I²C stub; 48 checks, bus table, simulated loop, screenshots, an autopilot per game; `--playtest` for Lab Part 6 |
| `host/run.bat`, `host/run.sh` | build and run it with plain `gcc` |
| `host/embed_shots.py` | rewrites the deck's screenshot blocks from `host/out/*.svg` |
| `host/sprite.py` | ASCII art → the column bytes `oled_sprite()` wants (Lab Part 4); `--rows` makes DOOM's 16-row ink-and-mask words |
| `host/doom_art.txt`, `host/prince_art.txt` | the sprites of the two new games as ASCII art; `sprite.py` turns them into the tables in `doom.c` and `prince.c` |
| `host/leaderboard.py` | the class table from pasted `$SCORE` lines, checksums verified (Lab Part 7) |
| `host/out/` | screenshots written by the test, `.txt` (below) and `.svg` (in the deck) |

## Build and run

```
build.bat        # zero warnings; FLASH about 27 KB / 32 KB (estimated, see below), RAM 5 400 B / 12 KB
simulate.bat
host\run.bat     # or: bash host/run.sh  - plain gcc, no board needed
```

**Sizes, and which compiler measured them.** The vendored gcc 15.2.1 is a
Windows program and was not run when DOOM and PRINCE were added; the numbers
below are from Ubuntu's `arm-none-eabi-gcc` 13.2.1 with the same flags as
`_build/build-lesson.bat`, which builds the *same three-game arcade* 3 164 B
larger than the vendored compiler did.

| | FLASH | RAM |
|---|---:|---:|
| 3 games, gcc 15.2 (vendored, 2026-11-01) | 16 056 B | 4 248 B |
| 3 games, gcc 13.2 (same sources) | 19 220 B | 4 248 B |
| **5 games, gcc 13.2** | **30 052 B** (91.7 %) | **5 400 B** (43.9 %) |
| 5 games, gcc 15.2 — *estimate* | ~26 900 B (16 056 + 10 832) | 5 400 B |

`arm-none-eabi-size -A` (gcc 13.2, five games): `.text` 25 908, `.rodata` 3 816,
`.data` 140, `.bss` 3 720. From their own object files, DOOM is **5 448 B** of
flash and **741 B** of RAM and PRINCE **5 663 B** and **400 B** — together
bigger than the engine, the menu and the first three games (5 739 B, 1 193 B).
Run `build.bat` and trust its figure over this one. The AVR edition's lesson 24
arcade used 1 372 B of 4 KB.

**Controls:** stick steers / chooses, **A** plays, serves and flaps, **B**
pauses, **knob** = speed 1–5 (in the menu), **stick press** toggles `$PERF`
between `DIRTY` (send changed pages) and `FULL` (send all eight).

| | stick | A | ends when |
|---|---|---|---|
| DOOM | up/down walk, left/right turn; a door opens when you bump it | hold to fire (pistol, 10 ticks between shots) | health 0; the framed switch at the far end starts the next, harder wave |
| PRINCE | left/right run, **up jumps** (a running jump if you are running) | in the air: grab a ledge, then **up** to climb; with a guard near: strike (**up** then parries) | hearts 0 — a fall of 3 floors, spikes, or a blade; the exit door starts the next wave |

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
| the raycaster: a grid walk from line to line (DDA), a flat camera plane so distances are perpendicular and walls do not bulge | the standard Wolfenstein 3D-style method (Lode Vandevenne's raycasting tutorial) — **from memory, not re-fetched**; the walk was checked against `doom_los()` and the autopilot, not against a reference |
| the real DOOM's data file is about 4 MB, the engine needs megabytes of RAM | **from memory**, not re-fetched; the point is the order of magnitude against 32 KB + 12 KB |
| *Prince of Persia* (Jordan Mechner, 1989, Apple II): 10 × 3 tile screens, hand-drawn animation | **from memory**, not re-fetched; only the idea is used, no code, art or data |
| the Cortex-M0+ has no divide instruction, so `/` calls libgcc | `engine.c`'s existing comment; `__divsi3` is **460 B** in the five-game build (`arm-none-eabi-nm -S`, gcc 13.2) |
| the sizes of DOOM and PRINCE | measured: separate object files, then the linked image, `arm-none-eabi-gcc` 13.2.1 (Ubuntu), `-Os`, the flags of `_build/build-lesson.bat`; **not** the vendored 15.2.1 |
| Wokwi's I²C is ~100 kHz (a full repaint took 106 ms) | this lesson's Wokwi session, 2026-11-01; the 88 ms DOOM-frame figure is that number scaled by 912 / 1 104 bytes — arithmetic, not a measurement |

## Verified by execution — on the host

`host/run.bat` compiles `engine.c`, `arcade.c`, `snake.c`, `breakout.c`,
`flappy.c`, `doom.c`, `prince.c`, `_lib/oled.c` and `_lib/proto.c` (`-Wall
-Wextra`, zero warnings) and replaces only `i2c_write_read()` (counts bytes,
models 9 bits per byte at 400 kHz), `sfx()` and `on_score()`. The first three
games were run with MinGW gcc 15.2; **the 48 checks below were run with
Ubuntu gcc 13.3** (`bash host/run.sh`) when DOOM and PRINCE were added. **All 48
pass; exits non-zero on any failure.**

| Test | Measured on the host |
|---|---|
| SNAKE autopilot, level 3 | 42 apples in 58.7 s, length 87 = 3 + 2 × 42, score 126 = 3 × 42 |
| SNAKE, no input | hits the wall on tick **126** = predicted 21 moves × 6 ticks |
| determinism | same seed + inputs → same 2 934 ticks, score, framebuffer hash; other seed → different |
| BREAKOUT autopilot, level 3 | 5 min: 188 bricks, 3 waves, score 1 395, no ball lost |
| BREAKOUT, no input | auto-serves after 3 s, three misses, game over on tick 599 |
| FLAP autopilot, level 3 | 97 pipes in 71.3 s |
| FLAP, no input | hovers to tick 150, lands on tick **171** = predicted 149 + 22 |
| `oled_init()` | 32 transfers, 1 161 bytes |
| bus per frame, dirty pages | SNAKE 129 B / 2.92 ms (worst 18.7 ms), BREAKOUT 220 B / 4.96 ms, FLAP 935 B / 21.11 ms |
| bus per frame, full repaint | 1 104 B / **24.92 ms** — over the 20 ms tick |
| SNAKE drawn clear-and-redraw (Lab Part 3) | 970 B / frame — the dirty-page check fails, as it should |
| loop vs a simulated clock | 50 updates/s in every case; fps 50 (SNAKE, BREAKOUT dirty), 46.8 (FLAP dirty), 40.1 (any full repaint) |
| arcade | menu navigation, A to play, game over reported once, `$SCORE,SNAKE,172*2E` passes `proto_check()`, high score kept, an edited score (`*2A`) rejected |
| `--playtest`, FLAP pipes at levels 1–5 | 135, 93, 63, 15, 4 — a cliff between 3 and 4 (Lab Part 6) |
| DOOM autopilot, level 3 | 5 min: **10 waves**, 44 kills for 222 shots, hurt 5 times, health 92, score 5 010 |
| DOOM, no input | 1 min: all 8 monsters still asleep, health 100, ammo 40 |
| DOOM pacifist (walks the route, never fires) | killed on tick 770 (15.4 s); at levels 1–5: 19.5, 16.9, 14.9, 13.4, 12.6 s |
| DOOM line of sight | clear along a room; blocked by a closed door; blocked by a pillar's corner (the bug, below) |
| PRINCE autopilot, level 3 | 2 min: **11 laps** through the exit, 60 jumps, 69 sword hits, 38 parries or blocks, never hurt, score 11 150 |
| PRINCE, no input | 1 min: stands at x = 18, 3 hearts |
| PRINCE running right | into the first pit; game over on tick 117 (31 to the edge + 25 falling + 61 dying — traced, not derived) |
| PRINCE skipping the spikes / not holding A / never parrying | impaled on tile 7, game over on tick 112 / stuck below the first ledge at x = 164 / hurt 7 times, dead on tick 871 |
| PRINCE falls | 1 row free, 2 rows 1 heart, 3 rows dead (`prince_fall_cost()`) |
| bus per frame, dirty pages vs full | DOOM **912 B** (83 %; 71 % of frames over 20 ms), PRINCE **432 B** (39 %) against 1 104 B |
| loop vs a simulated clock | DOOM 47.5 fps dirty (bus time only — CPU time not modelled), PRINCE 50 |
| arcade | stick up from SNAKE wraps to PRINCE; DOOM (`$SCORE,DOOM,0*71`) and PRINCE (`$SCORE,PRINCE,0*7B`) are reported by name with valid checksums |
| `--playtest`, second table | DOOM pacifist lasts 19.5 → 12.6 s; PRINCE never-parry loses 1.76, 2.31, 3.36, 3.71, 4.20 hearts per duel at levels 1–5 |

**Bugs the host test found in this lesson's own first draft**, all fixed and
all taught:

1. **Integer truncation in the paddle.** `px += in->x * 4 / 100` in whole
   pixels is 0 for any stick under 25 %, so a gentle push never moved the
   paddle; the autopilot kept missing by 3 px. Now the paddle is in 1/256 px
   (slide 18).
2. **A bite test after the tail moved.** SNAKE popped its tail before checking
   for a bite, so a fatal move had already shortened the snake: the
   length-equals-3-plus-twice-apples check was off by one. Now the bite test
   comes first, with the tail cell allowed (slide 22).
3. **DOOM: a line-of-sight probe that missed a pillar's corner.** The first
   autopilot fired all 40 rounds and killed one monster. `doom_los()` probed
   every quarter cell; a corner thinner than that slipped between two probes, so
   a zombie "saw" the player through it while every bullet struck the pillar.
   Now it is the same exact ray walk the renderer and the pistol use, and
   a check pins the case (slide 24b).
4. **PRINCE: a landed blow cancelled the guard's raised blade.** Tapping A every
   ten ticks then locked the guard out of attacking, so parrying was optional and
   the levels were noise. A hit now cancels nothing (slide 24c).
5. **PRINCE: grace longer than the guard's pause.** After a hit the prince was
   untouchable for 30 ticks; the guard's pause at levels 4 and 5 is 20-24, so every
   second blade was swallowed and level 4 hurt *less* than level 3 (2.38 vs 3.36
   hearts per duel). With 16 ticks the curve climbs. Lab Part 7b, task 3, makes
   students reproduce it.
6. **PRINCE: a dead prince ran physics again.** Falling into the abyss re-called
   `die()` every tick, restarting the death timer and the hurt sound. A dead
   prince now does nothing but count (found by tracing the run-right case).

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

DOOM, tick 766 of the autopilot (an imp straight ahead in a brick corridor):

```
+--------------------------------------------------------------------------------------------------------------------------------+
|:''.  .'''. .'''. :. .:                                                                                        .:   .'''. .'''. |
|:   : :   : :   : : : :                                 ::::  ::::  ::::  ::::  ::::                            :      .' : .': |
|:  .' :   : :   : :   :                                 ::::  ::::  ::::  ::::  ::::                            :    .'   :'  : |
|'''    '''   '''  '   '                                                                                        '''  '''''  '''  |
|::::::::::::::::::::''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''::::::::::::::::::::::::|
|::::::::::::::::::::                                                                                    ::::::::::::::::::::::::|
|::::::::::::::::::::                                                                                    ::::::::::::::::::::::::|
|::::::::::::::::::::                                                                                    ::::::::::::::::::::::::|
|::::::::::::::::::::                                                                                    ::::::::::::::::::::::::|
|::::::::::::::::::::                                                                                    ::::::::::::::::::::::::|
|::::::::::::::::::::                                                                                    ::::::::::::::::::::::::|
|::::::::::::::::::::                                                                                    ::::::::::::::::::::::::|
|::::::::::::::::::::                                                                                    ::::::::::::::::::::::::|
|::::::::::::::::::::                                                                                    ::::::::::::::::::::::::|
|::::::::::::::::::::                                   ..          .                                    ::::::::::::::::::::::::|
|::::::::::::::::::::  ' ' ' ' '   ' ' ' ' ' ' ' ' ' '  ::.  ' '  ..:  ' ' ' ' ' ' ' ' '   ' ' ' ' ' '   ::::::::::::::::::::::::|
|::::::::::::::::::::  ' ' ' ' '   ' ' ' ' ' ' ' ' ' '  ..:::::::'::.  ' ' ' ' ' ' ' ' '   ' ' ' ' ' '   ::::::::::::::::::::::::|
|::::::::::::::::::::  ' ' ' ' '   ' ' ' ' ' ' ' ' ' '  ::'::''':.'':  ' ' ' ' ' ' ' ' '   ' ' ' ' ' '   ::::::::::::::::::::::::|
|'' '''''''''''''''''  ' ' ' ' '   ' ' ' ' ' ' ' ' ' '  ''::':..'::.   ' ' ' ' ' ' ' ' '   ' ' ' ' ' '   '''''''''''''''''''''' '|
|                        ' ' ' ' ' ' ' ' ' ' ' ' ' ' '     ::::::      ' ' ' '   ' ' ' ' ' ' ' ' ' ' '                           |
|                        ' ' ' ' ' ' ' ' ' ' ' ' ' ' ' .''::::::::::'. ' ' ' '   ' ' ' ' ' ' ' ' ' ' '                           |
|          .. ....::.::  ' ' ' ' ' ' ' ' ' ' ' ' ' '  .'  :::::::::: '.  ' ' '   ' ' ' ' ' ' ' ' ' ' '::::::.......  .           |
|..:::..:::::::::::::::  ' ' ' ' ' ' ' ' ' ' ' ' ' '  '   ::''''':::  '  ' ' '   ' ' ' ' ' ' ' ' ' ' '::::::::::::::::::::::::::.|
|''''''''''''''''''''''                                 ..::     :::.                                 :::::::::::::::::::::::::::|
|                                        '       '     ''''    .  . ''   '       '       '       '    :::::::::::::::::::::::::::|
|   .::::::::.                                                 ::::                                   :::::::::::::::::::::::::::|
|  ::'::::::'::                          '       '       '    .::::.     '       '       '       '    :::::::::::::::::::::::::::|
|  :..::::::..:                                               :.::.:                                  :::::::''''''''''''''''''''|
|  ::::.''.::::   .'''. ''':'        '   '   '   '   '   '   .::::::.    '   '   '   '   '   '   '   '::::::: .'''. ''':' .'''.  |
|  :::'''''':::   '...:   '.         '   '   '   '   '   '   :.::::.:    '   '   '   '   '   '   '   '::::::: :   :   '.  : .':  |
|   '::::::::'       .' .   :        '   '   '   '   '   '   :.::::.:    '   '   '   '   '   '   '   '::::::: :''': .   : :'  :  |
|                  ''    '''         '   '   '   '   '   '  ::::::::::   '   '   '   '   '   '   '   '::::::: '   '  '''   '''   |
+--------------------------------------------------------------------------------------------------------------------------------+
```

PRINCE, tick 245 (the prince strikes; the guard stands; the plate and the gate are beyond):

```
+--------------------------------------------------------------------------------------------------------------------------------+
|:'''. :'''.  ':'  :   : .'''. :''''                                                                                 '''': .'''. |
|:...' :...'   :   :'. : :     :...                      ::::  ::::  ::::  ::::  ::::                                  .'  : .': |
|:     : '.    :   :  ': :   . :                         ::::  ::::  ::::  ::::  ::::                                 :    :'  : |
|'     '   '  '''  '   '  '''  '''''                                                                                  '     '''  |
|''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''|
|  .:             .:::.                    .:::.                                         ':'':'':'':'            :''''':'''''    |
|  ':             ':::'                   ':::::'                                         :  :  :  :             :     :         |
|''::            ..:::....             .  ..:::..                                         :  :  :  :             :     :         |
|  ::           : ::::: :''            : :'.:::.':                                        :  :  :  :             ''':''''':''    |
|  ::           ' :::::                 '  ::::: '                                        :  :  :  :                :     :      |
|  ::            .:' ':.                   ::'::                                          :  :  :  :                :     :      |
| .::           ::     ::                  :: ::                    ::::::                :  :  :  :             :''''':'''''    |
|    ...........::.....:::................:::.:::...................::::::................:..:..:..:.............:     :         |
|    :.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....    |
|    :''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':'''''    |
|    :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :         |
|    :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :         |
|    ''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''    |
|       :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :      |
|       :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :      |
|    :''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':'''''    |
|    :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :         |
|    :.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....    |
|    :''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':'''''    |
|    :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :         |
|    :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :         |
|    ''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''    |
|       :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :      |
|       :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :      |
|    :''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':''''':'''''    |
|    :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :     :         |
|    :.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....:.....    |
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
|  :::::::'''':':::'::'''::':::':''''':::::::::::::::::::::::::::::::::::::::::::::::::::::::::::':::'::''':::::::::'''::::::::  |
|  :::::: ::::: ':: : ::: : :'.:: ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::: ::: ::: ::::::::: ::' :::::::  |
|  :::::::...': :.' : ''' : .'::: ...:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::: ... ::: ::::::::: '.: :::::::  |
|  ::::::''''.: ::: : ::: : ::.': ''''::::::::::::::::::::::::::::::::::::::::::::::::::::::::::: ::: ::' '::::::::.'''.:::::::  |
|  ''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''''  |
|        :'''. :'''. :'''' .'''. :  .' .'''. :   : '':''                                         :   :  ':'        .'''.         |
|        :...' :...' :...  :   : :.'   :   : :   :   :                                           :...:   :         : .':         |
|        :   : : '.  :     :''': : '.  :   : :   :   :                                           :   :   :         :'  :         |
|        ''''  '   ' ''''' '   ' '   '  '''   '''    '                                           '   '  '''         '''          |
|        ..... .      ...  ....                                                                  .   .  ...         ...          |
|        :     :     :   : :   :                                                                 :   :   :         :  .:         |
|        :'''  :     :...: :'''                                                                  :''':   :         :.' :         |
|        :     :.... :   : :                                                                     :   :  .:.        '...'         |
|                                                                                                                                |
|        :''.  .'''. .'''. :. .:                                                                 :   :  ':'        .'''.         |
|        :   : :   : :   : : : :                                                                 :...:   :         : .':         |
|        :  .' :   : :   : :   :                                                                 :   :   :         :'  :         |
|        '''    '''   '''  '   '                                                                 '   '  '''         '''          |
|        ....  ....   ...  .   .  ...  .....                                                     .   .  ...         ...          |
|        :   : :   :   :   :.  : :   ' :                                                         :   :   :         :  .:         |
|        :'''  :':'    :   : '.: :     :'''                                                      :''':   :         :.' :         |
|        :     :  '.  .:.  :   : '...' :....                                                     :   :  .:.        '...'         |
|                                                                                                                                |
|  .'''' :'''. :'''' :'''' :''.      ::::::  ::::::  ::::::  ::::::  :'''':                 .'''.  ..   :'''. :     .'''. :   :  |
|  '...  :...' :...  :...  :   :     ::::::  ::::::  ::::::  ::::::  :    :                 :   :  ''   :...' :     :   : '. .'  |
|      : :     :     :     :  .'     ::::::  ::::::  ::::::  ::::::  :    :                 :''':  ::   :     :     :''':   :    |
|  ''''  '     ''''' ''''' '''       ''''''  ''''''  ''''''  ''''''  ''''''                 '   '       '     ''''' '   '   '    |
+--------------------------------------------------------------------------------------------------------------------------------+
```

## Status

**Builds with zero warnings** (five games, gcc 13.2: FLASH 30 052 B, RAM
5 400 B — the vendored gcc 15.2 was not run; see Build and run). **The host test
passes all 48 checks** with the numbers above. Not run on silicon or in Renode.

**DOOM and PRINCE were written after the Wokwi session below and have never
been run in Wokwi or on silicon.** What is checked is what the host can check:
the games' rules, their autopilots, their bus traffic and their pictures. What
is *not*:

- **frame rate on the board.** The host's 47.5 fps for DOOM is bus time only. The
  CPU's share (7 040 pixel writes a frame, three divisions a column) is
  unmeasured; `$PERF`'s `busy %` will say. In Wokwi, where a full repaint took
  106 ms, a 912 B DOOM frame should take about 88 ms — roughly 11 frames a second,
  and the game itself a little slow (a frame past 80 ms exceeds `MAX_CATCHUP`). A
  prediction, not a result.
- **whether the games are fun or fair with a real stick**, in particular DOOM's
  aim (a 0.31-cell-wide target) and PRINCE's ledge-grab window.
- **the six new sound effects** (`SFX_SHOT` ... `SFX_THUD`) and the five-row menu,
  host-tested for logic only.
- **the vendored compiler's sizes**, estimated above, not measured.

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
prediction (~50 fps dirty, full-frame `flush_us`). A full repaint measured
106 ms there - I2C runs at ~100 kHz in Wokwi whatever `TIMINGR` says - so
`FULL` mode will not reach 50 fps in the simulator. Also whether the stick's
HORZ runs the right way in BREAKOUT.

**Taught without lessons 09-11 (2026-11-01).** The class skipped them, so this
deck is its first contact with I2C. Slide 3b now teaches the bus on one page
(open drain, START/STOP, the address byte, ACK/NACK, 9 clocks a byte), and
slides 6, 7, 7b and 15 no longer send the reader back to lesson 09. Lessons
10 and 11 appear only as forward pointers (DMA, sleep current).

**FLAP flaps on A or stick up, not B** (2026-11-01). `flappy.c` also tested B,
but the arcade takes B as pause before a game sees it. The host test was not
re-run after that change - no host gcc on the machine that made it - but it
never presses B.
