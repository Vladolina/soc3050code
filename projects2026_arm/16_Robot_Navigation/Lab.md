# Lab 16 — Robot Navigation

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A guided walkthrough, not a test.** A robot that has never seen the room
maps it, plans across it and dodges what it did not expect. You will watch it
learn, then break each of its five layers one at a time — wheels for heading,
no keep-away margin, no brake — and see exactly what each one was buying. Then
you race it, draw your own maze for it, and finally take its goal away.

Each part says **Do this**, asks you to **guess**, then explains **what you
see and why**. Nothing is marked or collected.

Two places to run everything:

- **Wokwi** — the board, the OLED, the buttons. `simulate.bat`.
- **The host** — the same `world.c`, `nav.c`, `astar.c` compiled for your PC,
  every level in a second: `host\run.bat` (or `bash host/run.sh`).

Predictions marked *host* were measured by `host\run.bat`. Predictions about
what the OLED, the buzzer or the buttons do **have not been watched in Wokwi
yet** — if you see something different, write it down: you have found
something nobody knew.

**Put every change back before the next part** unless the part says otherwise.

---

## Part 0 — Build it, run it, read the banner (15 min)

### Do this

```
build.bat
simulate.bat
```

Zero warnings, and close to:

```
           FLASH:       29216 B        32 KB     89.16%
             RAM:       11328 B        12 KB     92.19%
```

Paste `diagram.json` into Wokwi's `diagram.json` tab (the OLED, joystick,
buttons, knob and buzzer live there), `F1` → **Upload Firmware and Start
Simulation…** → `Main.elf`. The banner:

```
=== SOC3050 lesson 16 - Robot Navigation ===
  pad      : joystick, knob, A/B/SEL
  OLED     : SSD1306 at 0x3C
  arena    : 32 x 14 cells of 100 mm, 5 levels; robot r=35 mm, 5 rangefinders
  RAM      : navigation 3288 B, of which A* 2112 B
```

### Guess first

89% of flash and 92% of RAM. Where did it all go — the robot, the world, or
the C library?

> **What you see and why.** Slide 23 has the answer, measured: the first
> build did not fit at all (34.9 KB / 15.1 KB). Half of the rescue was the
> C library — one `strtok()` call linked malloc, assert and fprintf, 3 KB —
> and half was measuring the stacks instead of guessing them. The robot's
> whole brain is 3288 B of RAM. Keep these two numbers in mind for Part 7.

---

## Part 1 — Watch the map fill in (15 min)

### Do this

Press **A** (key `a`). For two seconds nothing moves — `CAL` on the status
line. Then watch the OLED. Press **SEL** (space, with the joystick focused)
while it drives.

### Guess first

Why does it stand still for two seconds? What will the dots do?

> **What you see and why** (*not yet watched in Wokwi*). The dots are cells
> the robot knows nothing about; they vanish in fans in front of it as the
> five beams sweep. Walls appear as solid blocks one echo at a time. SEL
> outlines the walls it has *not* found yet — the truth it does not have.
> The two seconds are slide 7: the gyro's bias, measured while standing still.
> The line is the plan; on level 1 it should arrive in about **14 s** (*host*:
> 13.9 s over five seeds; Renode, running this very firmware: 14.6 s).

Press **B** to cycle the levels. On level 3, notice the plan drive straight
into the U — unknown space looks free — and then bend out of it.

---

## Part 2 — Where am I? Break the heading (20 min)

### Do this

Type into the serial monitor, then press A (twice if a run is under way: the
first press resets the level, the second starts it — settings survive both):

```
wheels 1
```

Try level 1, then level 4. Then `wheels 0` and `cal 0`.

### Guess first

The wheels are only 1.0% and 0.5% off their datasheet size. How far off will
the ring (the estimate) be from the disc (the truth) after a minute?

> **What you see and why.** *Host*, all five levels: heading from the wheels
> reaches the goal in **0 of 25** runs; the estimate ends 692 mm (worst
> 1172 mm) from the truth. A 1.5% wheel difference is 0.25 rad of heading per
> metre (slide 6) — the robot draws every wall at the wrong angle, its map
> seals its own corridors, and it reports `NOPATH` for goals that are plainly
> reachable. Without calibration (`cal 0`) the gyro's 4 mrad/s bias does the
> same more slowly: **16 of 25** arrive. The ring and the disc separating on
> the OLED *is* that number.

**Challenge.** In `nav.c`, change `CAL_SAMPLES` from 50 to 10. Run
`host\run.bat` and look at table [4]'s defaults row. How short can calibration
be before the arrival count drops?

---

## Part 3 — Keep away from the walls (15 min)

### Do this

```
inflate 0
```

Run level 4 (the maze) and level 1. Then `inflate 2`.

### Guess first

With no keep-away cost the plan hugs every wall. Will the robot crash?

> **What you see and why.** Barely, *with the brake on*: *host* says
> 0.04 collisions per run at `inflate 0` vs 0.00 at 1 — the reactive layer
> catches what the planner risks. Now try `python host\planner.py --level 4
> --inflate 0` and `--inflate 1`: the same path, but cost 532 vs 1100 —
> every cell of a 2-wide corridor touches a wall, so inflation cannot choose
> a "middle" that does not exist. The margin only matters when the brake is
> gone — Part 4.

---

## Part 4 — Take away the brake (15 min)

### Do this

```
react 0
inflate 0
```

Run level 2 and level 4. Listen (the buzzer clicks on every collision) and
watch `C` on the status line.

### Guess first

The planner still knows where the walls are. How many collisions per run?

> **What you see and why.** *Host*: reactive off → **10.6 collisions per run**;
> off with inflation 0 → **18.5**; on → 0. And look at the ring: when the body
> stops against a wall the *wheels keep turning*, the encoders keep counting,
> and the estimate runs away — the worst error measured was 8.4 m on a 3.2 m
> arena. A collision costs you twice. That is why the reactive layer exists,
> and why it must not depend on the map (slide 18).

---

## Part 5 — The door, and the replans (15 min)

### Do this

`react 1`, `inflate 1`, level 5. Copy the serial monitor's output into
`capture.txt` when it arrives, then:

```
python ..\08_UART_And_Python_Host\host.py --file capture.txt --chart NAV,11,0,30
python host\planner.py --file capture.txt
```

### Guess first

The short way is through the door, three cells above the start. When does the
robot find out it is shut?

> **What you see and why.** The door shuts when the robot comes within
> 250 mm of it — after it has planned through it. One or two echoes mark it
> (slide 8), `plan_invalid()` notices the path is now wall (slide 16), and the
> new plan goes the long way round: *host* 32.9 s and 17.2 replans per run;
> Renode 29.0 s and 14. Field 11 of `$NAV` is the replan count — the chart
> steps up exactly when the map changes. `planner.py --file` also prints the
> worst gap between estimate and truth over the whole run.

---

## Part 6 — Python plans, the robot drives (20 min)

### Do this

1. `python host\planner.py --level 3` — the same A*, in Python, with the
   path drawn.
2. Make a map: copy the level-3 picture into `mine.txt` (14 lines of 32
   characters, `#` wall, `.` floor, one `S`, one `G`), change it, then
   `python host\planner.py --map mine.txt --frames`.
3. Paste the frames into the serial monitor one at a time. Press A.
4. While it drives, type `dump`, copy everything into `capture.txt`, and run
   `python host\planner.py --file capture.txt`.

### Guess first

Will Python's path be the *same* as the robot's, or just as good?

> **What you see and why.** The same, cell for cell — same costs, same
> neighbour order, same `(f, h, cell)` tie-break. Measured: 15 of 15 on the
> full maps (*host*), and **5 of 5 on the robot's own half-built maps** dumped
> from the firmware running in Renode — unknown cells, vetoes and all. If
> yours says DIFFERENT, one of the two programs has changed: that is exactly
> what the check is for.

**Challenge.** Draw a map with **no** path to the goal. How long does the
robot look before it says `NOPATH`? (*Host*, a goal sealed in a box: 23.9 s.)

---

## Part 7 — The 40 ms budget (15 min)

### Do this

After any run, type `stats`.

```
A* time : last ... cycles = ... us, peak ... cycles = ... us
budget  : control_step peak ... us of 40 ms, world_step peak ... us of 20 ms
  task robot  stack 174 of 224 words
```

### Guess first

A* expands about 300 cells on the busiest plans. The PC does that in 60 µs.
The M0+?

> **What you see and why.** Renode measured **10.4–11.2 ms** per worst plan —
> about 1600 instructions per cell, 170× the PC — and `control_step()` at
> 16 ms of its 40 ms. Real silicon spends more than one cycle on loads and
> branches, so expect Wokwi to report somewhat *more*. Write down your number;
> nobody has seen it yet.

**Challenge — beat 1600.** `astar.c`'s `key()` recomputes `h()` on every heap
comparison. Cache it, or skip the bounds checks for interior cells. Check that
`host\run.bat` still says 15/15 identical, then compare `stats`. What did it
cost in RAM? (Slide 12 has the budget.)

---

## Part 8 — Speed run: the maze leaderboard (15 min)

### Do this

Level 4. Set the knob (it is the robot's speed, 100–400 mm/s) and race.
The serial monitor prints

```
ARRIVED  level 4 Maze: 40.0 s, 0 collision(s), 43 replans -> score 40.0
```

Score = time + **5 s per collision**. Lowest wins. You may change anything in
`nav.c` — `LOOKAHEAD_MM`, `TURN_MAX`, `BRAKE_DECEL`, the costs — but not
`world.c`.

### Guess first

Is flat-out (400 mm/s) the fastest?

> **What you see and why.** *Host*, all levels: 400 mm/s averages 17.2 s
> against 26.5 s at 250 with 0.04 collisions a run — fast pays, here. But
> the bar to beat is *your* maze run, and a tuning that wins on one seed can
> crash on the next: before you claim a record, run `host\run.bat` and check
> that table [3] still says 5/5 arrived and 0 collisions on every level.

---

## Part 9 — Open-ended: no goal at all (the rest of the time)

Take the goal away and make the robot **map the whole arena**.

A *frontier* is a known-free cell with an unknown neighbour. Exploration is:
plan to the nearest frontier; when you get there (or it stops being a
frontier), pick the next; stop when there are none.

Hints:

- `nav_cell()` tells you free / unknown / wall. Scan the grid for frontiers
  in `control_step()` when the robot arrives — or every second.
- "Nearest" can be straight-line distance; better is A*'s cost — what does
  that cost you in time (Part 7)?
- Score it: cells mapped per second, and the final map compared with the truth
  (`dump`, then `planner.py --file` draws it).
- Make it a race: two of you, same level, who maps 95% first?

There is no reference answer in this folder. When it works, the OLED should
end with no dots left — tell the maintainer, because nobody has seen that yet.
