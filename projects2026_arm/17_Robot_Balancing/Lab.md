# Lab 17 — Robot Balancing: PID vs LQR

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A guided walkthrough with a contest in the middle, not a test.** A
two-wheeled robot balances on the OLED. You will push it, load it, starve it of
volts, blind its sensors, and race three controllers against each other —
then design your own LQR gain and try to beat the default.

Two places to run things:

- **The host** (`host\run.bat` or `host/run.sh`): the firmware's own
  `world.c` and `control.c` on your PC. Fast, repeatable, and the source of
  every number marked *measured on the host* below.
- **Wokwi**: the real firmware on a simulated Nucleo. **Nobody has watched this
  lesson run in Wokwi yet.** Where a part predicts what Wokwi shows, the
  prediction comes from the host; write down what you actually see. If the two
  disagree, that is a finding.

Nothing is marked or collected. **Put every change back before the next part**
unless the part says otherwise.

---

## Part 0 — Build it, run it, time it (20 min)

```
build.bat
simulate.bat
```

Zero warnings:

```
           FLASH:       23960 B        32 KB     73.12%
             RAM:       10144 B        12 KB     82.55%
```

Paste `diagram.json` (the app board: OLED, joystick, A, B, knob, buzzer) and
upload `Main.elf`. The banner:

```
=== SOC3050 lesson 17 - Robot Balancing: PID vs LQR ===
  the robot is simulated INSIDE this chip: world.c at 1 kHz
  pad      : joystick, knob, buttons A/B/SEL
  OLED     : SSD1306 at 0x3C
  world    : 800 g body, 70 mm wheels, 7.4 V supply, IMU 1.0 deg off
  ...
```

Now type **`stats`**.

**Guess first:** how many microseconds does one world step take on a 48 MHz
Cortex-M0+ with no FPU? What share of the CPU do the world and the controller
use together?

> Slide 22 *estimates* ~240 µs and ~25 %, by counting float operations and
> pricing each at ~70 cycles. **Nobody has measured it** — your `stats` line is
> the first real number. It also prints each task's stack high-water mark:
> `world` and `control` were given 160 words on the strength of
> `-fstack-usage`; check they used well under that.
>
> If the world step is over 1000 µs, the 1 kHz world cannot keep up, physics
> runs slower than real time, and the robot falls in slow motion. Write the
> number down: it decides how much physics this chip can afford.

Also run the host test once, so you know it works:

```
host\run.bat          (or:  bash host/run.sh)
```

It ends `all checks passed` and exits 0.

---

## Part 1 — Meet the robot: the push contest (15 min)

The robot starts under **LQR**. Press **`b`** (button B): a 0.20 N s shove,
forward, at the body's centre of mass. It leans back, rolls, and returns to the
caret under the ground — its target.

Make the pushes bigger with the shell: `push 400`, then `push 600` (milli-
newton-seconds), pressing `b` each time. **`a`** stands it up after a fall.

**Guess first:** what is the largest push LQR survives? And the cascade
(press **space**, joystick SEL, until the top line says `cascade`)?

> **Measured on the host:** LQR **0.78 N s**, cascade **0.60 N s**, PID tilt
> 0.62 N s (it is falling anyway — Part 2). 0.78 N s gives the 0.8 kg body
> about 1 m/s, which is close to the motors' 1.3 m/s no-load speed: the limit
> is the **motor**, not the maths. Above it the controller asks for more than
> 7.4 V, the supply clips it, and nothing can save it.
>
> Find your own number in Wokwi with `push` and binary search. The host's is
> exact for seed 12345; Wokwi's noise sequence differs, so expect within a
> few hundredths.

---

## Part 2 — PID on tilt: it stands, then it leaves (15 min)

Press space until the top line reads `PID tilt`, then `a` to start clean.

**Guess first:** how long does it stay up with nobody touching it? Which way
does it go?

> **Measured on the host:** it balances — while rolling — and falls after
> **7.8 s, 3.3 m backwards**. Watch the ground ticks scroll and the tilt chart
> stay calm almost to the end: it *looks* balanced right up to the moment the
> motor runs out of volts. Backwards, because the IMU is mounted 1° nose-up
> (`IMU_TILT`): "level" by the sensor is a 1° backward lean in truth.
>
> Slide 12 gives the chain of reasoning: rolling needs volts, this controller
> only makes volts when tilted, tilted means accelerating.

Now prove it without a simulation:

```
python host\lqr.py --pid 150,50,4
python host\lqr.py --pid 150,0,4
python host\lqr.py --pid 25,0,2
```

> An eigenvalue at exactly 1 in all three — position, which nothing controls.
> Without I, one outside the unit circle: the run-away, e-fold every 3.3 s at
> KP = 150 and every 0.59 s at KP = 25. With I, an 89 s swing just inside it.

**Challenge:** find KP, KI, KD that keep PID tilt up longest from upright.
The gains are `#ifndef`-guarded in `control.c`, so the host can try them
without editing anything:

```
cd host
gcc -O2 -I.. -I../../_lib -DTILT_KP=200.0f -DTILT_KI=50.0f -DTILT_KD=5.0f sitl.c ../world.c ../control.c ../view.c ../../_lib/oled.c -lm -o sitl
./sitl
```

Read the `30 s, no command` line under `--- PID tilt ---`. 7.8 s is the number
to beat. Can anyone make it to 30 s? (Slide 13 says no linear choice of three
tilt gains controls position. Prove it wrong, or find out why it is right.)

---

## Part 3 — Cascade: the clamp that bit (15 min)

Press space to `cascade`. Push the joystick **up** and hold it: the robot
leans forward and drives at up to 0.4 m/s; the caret runs ahead of it.

**Guess first:** the outer loop's lean is clamped at ±0.30 rad (17°). What
happens if a careful engineer tightens that to ±0.10 rad (6°) "for safety"?

```
cd host
gcc -O2 -I.. -I../../_lib -DCAS_LEAN=0.10f sitl.c ../world.c ../control.c ../view.c ../../_lib/oled.c -lm -o sitl
./sitl
```

> **Measured on the host:** with ±0.10 rad the cascade **falls** in the drive
> test (0.3 m/s). The outer loop wants more lean than it may have, so the robot
> accelerates too slowly; the target keeps moving away; the position error
> piles up — windup through the set-point — and when the robot finally catches
> up it is doing 1 m/s, overshoots, and runs out of volts. A "safety" limit
> made it unsafe.
>
> With ±0.30 rad it drives fine: 1.72 m in the 5 s test.

Put `CAS_LEAN` back (just rebuild without the `-D`).

---

## Part 4 — The LQR contest: design your own K (25 min)

The default `Q = diag(30, 2, 50, 0.5)`, `R = 1`. Change them on the command
line — `params.h` is not touched:

```
python host\lqr.py --Q 30,2,500,0.5 --R 1
```

It prints K, the closed-loop eigenvalues and one line of C. **Paste that line
over `LQR_K` in `control.c`**, then run the host test — which will now say
`DIFFERS ... re-paste` at the `--check` step, because params.h still has the
old weights. That is the check working. Run `sitl` directly instead:

```
cd host
gcc -O2 -I.. -I../../_lib sitl.c ../world.c ../control.c ../view.c ../../_lib/oled.c -lm -o sitl
./sitl
```

**Guess first** for each row, then measure (all **measured on the host**,
slide 19):

| Q | R | settle 6° | largest push | payload |
|---|---|---|---|---|
| 30, 2, 50, 0.5 (default) | 1 | 3.00 s | 0.78 N s | 1.36 kg |
| 300, 2, 50, 0.5 | 1 | 6.18 s | 0.58 | 0.80 |
| 30, 2, 500, 0.5 | 1 | 1.91 s | 0.84 | ≥ 2.00 |
| 30, 2, 50, 0.5 | 10 | 2.88 s | 0.84 | 1.54 |

> Caring more about position made everything else worse; caring more about
> tilt made everything better. LQR minimises the cost you **wrote**, and the
> tests measure something else.

**The contest.** Score = largest push (N s) — but your design is disqualified
if its `30 s, no command` drift exceeds **±10 cm**, or the eigenvalue check
says anything but `stabilising`. Default scores 0.78. The best found while
writing this lab was 0.84. Beat it.

Winner: paste your K into the **firmware** too, build, and push the robot in
Wokwi. Does the number hold? Then put `control.c` back (or set your Q and R
into `params.h` so `--check` passes — that is the honest way to keep it).

---

## Part 5 — Turn up the noise (10 min)

Under LQR, in the shell: `noise 400`, then `noise 800`, then `noise 1600`
(percent of nominal). Watch the tilt chart and the body's twitching.

**Guess first:** at what multiple of the nominal noise does it fall?

> **Measured on the host**, LQR, standing still, 30 s:
>
> | noise | tilt estimate error | result | largest push |
> |---|---|---|---|
> | ×1 | 1.08° rms | upright | 0.78 N s |
> | ×4 | 1.75° | upright | 0.78 |
> | ×8 | 3.38° | upright | 0.78 |
> | ×16 | — | **fell in 0.4 s** | — |
>
> Robust up to ×8 — then a cliff. The estimate degrades smoothly; the robot
> does not. Somewhere between ×8 and ×16 the voltage noise from `K·θ_est`
> saturates the motor often enough that balance is lost. `stats` counts the
> clipped steps — watch that number climb.

`noise 100` to put it back.

---

## Part 6 — Blind it: no complementary filter (15 min)

`alpha 100` is gyro only; `alpha 0` is accelerometer only. Try each, then
`alpha 98` to restore.

**Guess first:** which one falls, and how fast?

> **Measured on the host** (LQR, 30 s from upright; push = largest survived):
>
> | alpha | meaning | result | estimate error | push |
> |---|---|---|---|---|
> | 0 | accelerometer only | **fell at 0.2 s** | — | — |
> | 0.50 | mostly accelerometer | **fell at 1.3 s** | 22.7° | 0 |
> | 0.90 | | upright | 2.68° | 0.38 |
> | 0.95 | | **fell at 13.7 s** | 6.97° | 0.26 |
> | 0.98 | default | upright | 1.08° | 0.78 |
> | 0.995 | | upright | 1.22° | 0.86 |
> | 0.999 | almost gyro only | upright | 2.02° | 0.88 |
> | 1 | gyro only | upright, 3.8° error and growing | | |
>
> The accelerometer alone cannot be trusted while the robot accelerates — and
> catching itself *is* accelerating, so it chases its own tail. The gyro alone
> works for a while: its 0.005 rad/s bias adds 0.15 rad (8.6°) in 30 s, and
> LQR simply holds a slowly moving wrong "upright". Leave it long enough and it
> falls.
>
> Note 0.95 falling while 0.90 survives: not every result is monotonic. With
> noise, one seed is one sample. Run a few seeds before you believe a trend
> (`base()` in `sitl.c` sets the seed).

Write down Wokwi's answer for `alpha 100` after 60 s — the host only ran 30.

---

## Part 7 — Starve the motor (10 min)

`vbat 74` is the default (7.4 V, in tenths). Lower it — `vbat 50`, `vbat 30`,
`vbat 20` — pushing with `b` (0.20 N s) at each.

**Guess first:** at what supply voltage does a 0.20 N s push win?

> **Measured on the host:** LQR survives a 0.20 N s push down to about 2.2 V
> and falls at **1.8 V** (the test steps in 0.4 V). Below the supply the
> controller asks for, `world_step()` clips — the controller does not know.
> `stats` reports how many world steps were clipped. A real robot sees this as
> a battery running down: it balances fine, until one bump it does not.

`vbat 74` to restore. Then turn the **knob**: the payload box on top grows.
Push at each setting. Host says LQR carries 1.36 kg through a 0.10 N s push;
the knob only reaches 0.6 kg, so it should never fall from the knob alone at
the default push. Does it?

---

## Part 8 — Open-ended: drive a route (rest of the session)

The joystick's up/down is a speed command (±0.4 m/s), slewed at 0.5 m/s².
The caret is the target.

**The course:** from the start, drive forward exactly **1.0 m**, stop, come
back to the start, stop. Score = |final position| in cm (from `$BAL` field 5,
x in mm) plus 5 × the number of falls. Lowest wins. Capture the serial output
and chart your run:

```
python ..\08_UART_And_Python_Host\host.py --file capture.txt --chart BAL,5,-200,1200
```

**Something to notice first.** In the host's drive test both cascade and LQR
covered **1.72 m** when the target moved only **1.41 m** — they run *ahead* of
the target. Only a position error makes voltage, and moving needs voltage
against the motors' back-EMF.

**A one-line fix (measured on the host):** add the voltage a motor needs at
the target speed, k·v/r, as **feed-forward** at the end of the LQR case in
`control.c`:

```c
        u = -(LQR_K[0] * (est.x - est.x_ref) ... + LQR_K[3] * in->gyro)
            + MOT_K * est.v_ref / BOT_R;            /* feed-forward */
```

> With it, the robot covered **1.41 m — exactly the target's 1.41 m** — and
> the push survival stayed 0.78 N s. Feedback corrects errors; feed-forward
> stops you making them.

**Further, if you finish:** give PID tilt the same feed-forward and see whether
it saves it (Part 2's chain says why it might help — and why it cannot fix
position). Or add a second shell command `turn` and a yaw state: the model is
planar now, and a real two-wheeler steers by driving its wheels differently.
