# Lab 15 — Robot Line Follower

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A race, then a tuning contest.** You drive the robot yourself, then let
`control.c` drive it, then break it in every way a real line follower breaks
— too fast, no D term, noisy sensors, a dead sensor, a bright room — and fix
each one. The last part is yours: a new track, or a smarter controller.

Each part says **Do this**, asks you to **guess**, then explains **what you
see and why**. Nothing is marked or collected — but Part 4 has a leaderboard.

Two kinds of predicted numbers appear below, and they are labelled:

- **measured on the host** — `host\run.bat` raced the same `world.c` and
  `control.c` on a PC. Those are real measurements of this code.
- **not yet watched in Wokwi** — nobody has seen this firmware run in the
  simulator yet. If what you see differs, write it down: you have found
  something.

**Put every change back before the next part** unless the part says otherwise.
The shell commands (`kp`, `noise`, …) last until reset; pressing the Nucleo's
reset or re-uploading restores the defaults.

---

## Part 0 — Build it, race it once (15 min)

### Step 1. Build and run the host test first

```
host\run.bat
```

This needs no simulator. It ends `PASS: 0 failure(s)` and prints nine tables
— the numbers the rest of this lab compares against. Find table 2: with the
default gains at knob 500, every track finishes five laps.

### Step 2. Build and simulate

```
build.bat
simulate.bat
```

Zero warnings, and about **FLASH 26 988 B, RAM 9 352 B**. Paste
`diagram.json` into the Wokwi tab (**required** — the OLED, joystick,
buttons, knob and buzzer all live there), then `F1` → **Upload Firmware and
Start Simulation…** → `Main.elf`. The serial monitor shows:

```
=== SOC3050 lesson 15 - Robot Line Follower ===
  pad      : joystick, knob, A, B, SEL
  OLED     : SSD1306 at 0x3C
  world    : 200 Hz physics, 100 Hz control, 7 sensors at 13 mm
  shell    : kp ki kd df slow thr N | noise ambient fail N | gains | stats
```

### Step 3. Race

On the OLED's menu, push the joystick **down/up** to pick `1 OVAL`, press
**A**. The track appears with the robot on the start line. Leave the knob at
its default (half way) and press **A** again.

**Guess first:** how long is one lap of OVAL at this speed?

> **Measured on the host: 5.20 s for lap 1, then 5.12 s every lap.** Lap 1 is
> slower because it starts from a standstill and the motors lag 40 ms (slide
> 6). The top row of the OLED shows the lap clock and best lap; every lap
> beeps and prints `$LAP,1,5120*..`.
>
> **Not yet watched in Wokwi.** The firmware's lap time equals the host's only
> if the world task keeps up with its 5 ms period — world time is counted in
> steps, not read from a clock. Type `stats` now: if `world step` max is well
> under 240 000 cycles, it keeps up.

---

## Part 1 — Beat the robot (15 min)

**Do this:** press **SEL** (space). The bottom row's `A` becomes `M`: the
joystick drives. Press **A** to stop and go back to the line, **A** to start,
and drive a lap of OVAL with the arrow keys.

**Guess first:** can you beat 5.12 s?

> Up on the joystick is 1200 mm/s, faster than the robot's default 1000 — so
> you have the speed. What you do not have is a 100 Hz control loop and a
> view of the tape 70 mm ahead. Manual laps print `manual lap 7.43 s` but
> **never** a `$LAP` frame: the leaderboard ranks controllers, not thumbs.
>
> Now try HAIRPINS (**B** twice). The tightest U-turn is 120 mm; above
> √(0.7 g × 0.12 m) ≈ 0.9 m/s the tyres let go (slide 7). Watch the bottom
> row say `SLIP`.

Press **SEL** again to hand back to `control.c`.

---

## Part 2 — What the controller believes (15 min)

**Do this:** on S-BENDS, race a few laps, then copy the serial monitor into
`capture.txt` and chart two fields of `$LINE`:

```
python ..\08_UART_And_Python_Host\host.py --file capture.txt --chart LINE,2,-400,400
python ..\08_UART_And_Python_Host\host.py --file capture.txt --chart LINE,3,-400,400
```

Field 2 is the controller's estimate of the line position, field 3 the world's
**true** error, both in tenths of a millimetre.

**Guess first:** will they agree?

> They agree in sign and roughly in size, but not exactly. The estimate is
> made at the sensor bar, 70 mm ahead of the axle; the truth is measured at
> the axle. In a corner the bar sees the curve before the robot's centre
> reaches it, so the estimate *leads*. That lead is a free derivative term —
> and Part 3 is about what happens when it is not enough.
>
> **Measured on the host** at this speed: the true error never exceeds
> **4.8 mm** (RMS 1.6 mm) on S-BENDS.

---

## Part 3 — Take away the D term (20 min)

**Do this:** on S-BENDS, type

```
kd 0
```

and race at the default knob. Then turn the knob up until the bottom row
reads about `A1400`, press **A** twice (back to the line, then go).

**Guess first:** at 1000 mm/s, does removing D matter? At 1400?

> **Measured on the host, S-BENDS, knob 700 (1400 mm/s):**
>
> | | best lap | max error | RMS error |
> |---|---|---|---|
> | P only (`kd 0`) | 12.56 s | 61.5 mm | 27.5 mm |
> | PD (`kd 1`) | **3.80 s** | 12.8 mm | 5.4 mm |
>
> At 1000 mm/s P alone does finish cleanly — the sensor bar's 70 ms of lead
> is enough. At 1400 mm/s the lead is down to 50 ms, the motors still lag
> 40 ms, and the robot **weaves**: each swing is bigger than the last, it hits
> the friction limit, slides off the line and spins to find it again. Each lap
> takes three times as long. Halving `kp` (`kp 10`) does not save it
> (13.19 s): this is a phase problem, not a gain problem.
>
> Type `kd 1` and it is clean again. **Not yet watched in Wokwi** — but on the
> OLED you should be able to see the triangle wag.

---

## Part 4 — The leaderboard: fastest clean lap (25 min)

**The contest.** Pick a track. Using only the knob and the shell — `kp`,
`ki`, `kd`, `df`, `slow`, `thr` — set the fastest lap you can. Every auto lap
prints `$LAP,<track>,<ms>*CS`. At the end, save your serial monitor as
`yourname.txt` in a shared folder and run:

```
python host\leaderboard.py captures\*.txt
```

**The numbers to beat — measured on the host, default gains:**

| Track | Default knob 500 | Fastest knob that finishes | Its best lap |
|---|---|---|---|
| OVAL | 5.12 s | 1000 | 2.81 s |
| S-BENDS | 5.07 s | 775 | **3.72 s** |
| HAIRPINS | 7.46 s | 675 | **6.03 s** |
| FIGURE-8 | 5.99 s | 1000 | 3.29 s |

**Guess first:** on HAIRPINS, what happens if you type `slow 0`?

> **Measured on the host:** with `slow 0` the fastest finishing setting drops
> from knob 675 to 575 and the best lap gets **0.65 s worse** (6.68 s). The
> slow-down looks like caution, but it is what lets the robot carry speed on
> the straights. `ki` will not help much: `ki 2` shaved 40 ms on S-BENDS and
> doubled the worst error.
>
> The leaderboard checks each frame's checksum. Edit a lap time by hand and
> it is ignored — the same reason lesson 08 put a checksum on every frame.

---

## Part 5 — Noise, and what the D term does with it (15 min)

**Do this:** on S-BENDS at about `A1400`, with default gains, type

```
noise 80
df 1
```

(`df 1` turns the derivative filter **off**.) Capture a few laps and chart the
two wheel commands:

```
python ..\08_UART_And_Python_Host\host.py --file capture.txt --chart LINE,4,-2000,2000
```

Then `df 0.5` (the default), then `df 0.25`.

**Guess first:** with the filter off, does the lap time suffer?

> **Measured on the host** (*chatter* = RMS change in `right − left` between
> steps, mm/s):
>
> | noise 80 | best lap | RMS error | chatter |
> |---|---|---|---|
> | `df 1` | 3.82 s | 6.1 mm | **602** |
> | `df 0.5` | 3.81 s | 5.7 mm | 256 |
> | `df 0.25` | 3.85 s | **8.5 mm** | 162 |
>
> The lap time hardly moves — the motors' 40 ms lag smooths the commands
> before they move the robot. What suffers is the **motors**: the D term
> differentiates noise, so with no filter the commands jump 2.4 times as
> hard. On a real robot that is heat, current and wear. Filter too hard and
> the error grows instead: a filter is a delay, and delay is exactly what D
> was there to remove. There is a best `df` for each noise level.

---

## Part 6 — A dead sensor, a bright room (15 min)

**Do this, A:** on OVAL at the default knob, type `fail 3` (the centre sensor
reads 0). Then `fail 0`, then `fail -1` to repair it.

**Guess first:** which costs more — losing the middle sensor or an edge one?

> **Measured on the host:** an edge sensor (`fail 0` or `fail 6`) costs
> nothing at this speed — the line never gets there. The **centre** sensor
> costs 1.36 s a lap on OVAL (6.48 s vs 5.12 s). With it dead, a centred
> line lights only its two neighbours, faintly, below the threshold: the
> controller decides the line is **lost** while it is right underneath, and
> coasts. Watch the bars at the bottom of the OLED: the middle one stays flat.

**Do this, B:** on HAIRPINS, type `ambient 400` — the room got brighter, and
every reading rose by 400.

**Guess first:** does the robot notice?

> **Measured on the host: DNF.** White floor now reads 500, above `thr 300`,
> so **every** sensor votes and the line position is pulled toward the
> middle no matter where the tape is. Now type `thr 650` and race again:
> **7.46 s**, exactly the clean time. The fix is calibration, not a cleverer
> controller. A real robot measures white and black on the start line before
> it races; adding that to `control.c` is a good Part 8.

Put back `ambient 0`, `thr 300`.

---

## Part 7 — The same race, on the PC (15 min)

**Do this:** in a terminal in this folder:

```
host\run.bat race 2 700 20 0
host\run.bat race 2 700 20 1
host\run.bat ascii 4
```

The first two are Part 3 again — S-BENDS, knob 700, kp 20, kd 0 then 1 —
with every lap printed. The third draws the OLED as text.

**Guess first:** will the PC's lap times match the chip's?

> They should agree **closely, but not to the millisecond**. It is the same
> C and the same order of steps, but on the chip the knob is an ADC reading
> (half way may read 499, not 500) and the noise generator is not re-seeded
> for every race. A difference of a few tens of milliseconds is those two
> things. A difference of seconds would mean the world task is **not keeping
> its 200 Hz** — the one thing this lesson could not check without Wokwi.
> Compare your Part 3 numbers with these and say which it is.
>
> This is how the rest of the course works: you change `control.c`, race it
> a hundred times on the PC in a second, and only then load it on the chip.

---

## Part 8 — Your turn (open-ended)

Pick one:

- **Design a track.** Add a fifth entry to `track.c` as turtle commands
  (straights in mm, arcs as radius and degrees, `gap = 1` for no tape), bump
  `TRACK_COUNT`, and make it close: `host\run.bat` test 1 must say
  `closure miss 0.00 mm`. Keep it inside a 2:1 box and under 127 segments.
  Can the default controller finish it?
- **Speed scheduling.** The default slows down *in* a corner, once the error
  is already large. Use the history of `pos` — or the difference between the
  two encoder rates, which is the robot's turn rate — to recognise a corner
  earlier and brake **before** it. Beat 6.03 s on HAIRPINS.
- **Calibrate.** At the start of a race, before the clock runs, record each
  sensor's lowest and highest reading and set `thr` per sensor half way.
  Then survive Part 6B with no shell command.
- **Survive a dead sensor.** Detect a sensor that never changes and leave
  it out of the average, filling the hole from its neighbours.

> Whatever you pick, change only `control.c` (or `track.c` for a new track),
> prove it on the host first — `host\run.bat` must still say `PASS` — and
> only then load it on the chip.

---

## Where to go next

- Type `stats`. How many cycles does a world step take, out of the 240 000
  it is allowed? How many does `control_step()` take? Lesson 10 measured one
  soft-float multiply at tens of cycles — count the multiplies in
  `control_step()` and check the estimate.
- The display flush runs at priority 1 and the I²C driver is polled. How many
  milliseconds does `stats` say the slowest flush took? Would it still be safe
  at priority 4?
- Set `noise 0`. The chatter in Part 5 does not go to zero. Why not? (Seven
  sensors make a staircase, and D sees every step.)
