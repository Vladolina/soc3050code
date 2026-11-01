# Lab 13 — Drone Attitude

**SOC3050 ARM Edition · Nucleo-C031C6 + app board · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**IMU**: [wokwi-mpu6050](https://docs.wokwi.com/parts/wokwi-mpu6050) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A flight test, not a test.** You fly a simulated quadcopter, tune it with a
knob, and then break it four different ways — each one a mistake real drone
builders make, and each one survivable here. Then you put a real (Wokwi)
MPU6050 in the loop and watch the estimator think. The last part is yours.

Each part says **Do this**, asks you to **guess**, then explains **what you
see and why**. Nothing is marked or collected — but Part 1 has a scoreboard.

**Where the predictions come from.** Every number marked *host* was measured
by `host/sitl.c`, which links this lesson's own `world.c` and `control.c`
into a PC program. **None of this lab has been watched running in Wokwi.**
Where Wokwi might disagree, the part says so — and if it does, write down
what you saw: that is a fact about the simulator or about the lesson, and
both are worth knowing.

**Put every change back before the next part** unless the part says
otherwise. The shell commands (`alpha`, `aw`, `mix`) change things live; a
restart puts them back.

---

## Part 0 — Build it, fly it (20 min)

### Step 1. Build, test on the PC, simulate

```
build.bat
host\run.bat
simulate.bat
```

`build.bat` must end with zero warnings, **FLASH 26 292 B and RAM 10 848 B**
— most of the chip. `host\run.bat` must end `PASSED: 0 of 24 checks
failed`. Paste `diagram.json` into Wokwi (it adds an MPU6050 to the app
board), then upload `Main.elf`.

### Step 2. Read the banner

```
=== SOC3050 lesson 13 - Drone Attitude (SITL on the chip) ===
  pad      : joystick, knob, A, B, SEL
  OLED     : 0x3C ready
  MPU6050  : 0x68 awake, DLPF 44 Hz (MPU mode available)
  world    : 500 Hz quad on a gimbal; control 250 Hz
```

The OLED shows a horizon (left), `SIM SAFE`, four empty motor bars, and the
measured loop rate — **it should say 250Hz**. If it says less, the CPU is
overloaded: write down the number.

### Step 3. Fly

Press **A** (or the `a` key). You should hear a beep, `ARMED`, and the four
motor bars rise to about a third in half a second. Push the joystick right:
the horizon tilts — **which way?** Push it up: does the nose go up or down?

**Guess first:** roll right. Does the horizon line's right end go up or down?

> **Up.** You are inside the aircraft: when it banks right, the world outside
> appears to tilt the other way. Every attitude indicator in every cockpit
> does this. Stick **up** is nose **down** — the RC convention, so pushing
> forward flies forward.
>
> The **dotted** line is the true horizon, straight from the physics. In SIM
> mode it should sit on top of the solid one within a fraction of a degree
> (host: 0.20° RMS). The gap between them is your estimator's error.

### Step 4. Gust

Press **B**. The world kicks the drone at 229 °/s in roll and −115 °/s in
pitch.

> Host: **11.2° peak, back inside 3° in 0.17 s.** On the OLED that is about
> two or three frames — a twitch. The four motor bars jump apart and come
> back together: that is the mixer doing its job. Press it repeatedly: the
> kick alternates direction.

---

## Part 1 — The tuning contest (25 min)

The knob multiplies the rate loop's Kp, ×0.125 to ×8 (the OLED shows it).

**Do this:** with the drone armed and the stick centred, turn the knob slowly
all the way down, then all the way up, giving each position a few gusts.

**Guess first:** what does *too little* gain look like? Slow and lazy?

> **No — it wobbles.** Host, 20° step: at ×0.125 the overshoot is **194 %**
> and it never settles; it swings out to 59° and back, just under the 60°
> failsafe. When the inner (rate) loop is slower than the outer (angle)
> loop, the outer one asks for a rate, the inner one delivers it late, and
> the outer one asks harder. A cascade needs its inner loop *faster*.
>
> At ×8 it **buzzes**: rate RMS 19 °/s with the stick still (×1: 1.1 °/s).
> Watch the motor bars flicker. Lag in the loop — the sensor filter, 4 ms
> sampling, 30 ms motors — turns a big gain's correction into a push in the
> wrong direction.
>
> Between ×0.5 and ×4 it flies well, which is why the knob is logarithmic.

**The contest.** The knob tunes one number; `control_init()` in `control.c`
has five. Score a set on the PC:

```
host\run.bat                                  # builds host\sitl.exe
host\sitl.exe score 9 0.12                    # angle_kp rate_kp [rate_ki [rate_kd [alpha]]]
```

```
SCORE 0.620 s   (roll + pitch settling; default gains score 0.620)
```

The score is roll settling plus pitch settling for a 20° step, lower is
better. **Disqualified** for more than 5 % overshoot, buzzing (rate RMS above
3 °/s), a gust peak above 15°, or any disarm or crash.

| Score | |
|---|---|
| 0.620 | the defaults |
| 0.454 | `score 12 0.12` — only the angle gain raised |
| **0.278** | the lesson author's best so far |

Beat 0.278. Then put your gains into `control_init()`, rebuild, and fly them
in Wokwi with the knob at the middle. **Does it feel better?** A score is one
step on one seed; a pilot feels the gusts.

---

## Part 2 — Gyro only: the slow betrayal (15 min)

**Do this:** fly level, hands off. In the serial monitor type

```
alpha 1000
```

The OLED shows `a1000 gyr`. Now wait, and watch the two horizon lines.

**Guess first:** the gyro bias is +0.8 °/s in roll. After 30 s, how far apart
are the solid and dotted lines? Does the failsafe save it?

> Host, 30 s: the estimate is **14.5° RMS wrong, 26° at the end**, and the
> drone *truly* leans 26°. The **solid** line (the estimate) stays perfectly
> level — the controller is holding it there — while the **dotted** line
> (the truth) slowly rotates away.
>
> The failsafe never fires. It checks the estimate, and the estimate says 0°.
> At 0.8 °/s the truth reaches 90° after about two minutes, and the world
> latches `CRASH`. (That last figure is arithmetic, not a host run.)
>
> *A failsafe is only as good as the number it checks.* Type `alpha 980` to
> recover — **before** it crashes, and watch how fast the lines snap back:
> about τ = 0.2 s.

---

## Part 3 — Accelerometer only: the jitter (10 min)

**Do this:** `alpha 0`. Fly level, then tilt with the stick.

**Guess first:** with no gyro in the estimate, is the drone still flyable?

> Host, 30 s hover: estimate **3.2° RMS wrong, 7.2° worst**, and it jumps
> **6° between consecutive 4 ms steps** — the motor vibration, 0.19 g, read
> as tilt. The solid horizon trembles and the motor bars flicker as the
> controller chases the noise. It still flies (the true tilt stayed within
> 0.7°) because the rate loop runs on the gyro directly; only the angle loop
> is fed noise.
>
> Now find the best alpha yourself — `alpha 900`, `alpha 980`, `alpha 995` —
> and compare with slide 11's table. The minimum error is not at the
> extremes, and it is not at 0.995 either.

---

## Part 4 — Remove the anti-windup (15 min)

**Do this:** arm and hover. Type `hold` — a hand now holds the drone level.
Push the stick fully right and keep it there for 3 seconds. Then type `hold`
again (let go), still holding the stick.

Do it once with the default, then type `aw 0` (the OLED shows `AW!`) and do
it again. If it flips, it resets after 2 s.

**Guess first:** with anti-windup off, how far past 30° does it go?

> Host (2 s held, 30° request): **with** anti-windup it peaks at **29.8°** and
> is within 2° in 0.28 s. **Without**, the integrator has stored 2 s of
> "push harder" — it rolls to **180°**, upside down, and the tilt failsafe
> disarms it on the way.
>
> The whole difference is one `if` in `rate_pid()`. Find it.

---

## Part 5 — Measure it from the host (15 min)

**Do this:** fly, press B a few times, move the stick. Copy the serial
monitor's text into `cap.txt` and run:

```
python ..\08_UART_And_Python_Host\host.py --file cap.txt --chart ATT,2,-300,300
python ..\08_UART_And_Python_Host\host.py --file cap.txt --chart ATT,6,-300,300
```

Field 2 is the estimated roll in tenths of a degree, field 6 the true roll,
fields 8–11 the motors ×1000.

**Guess first:** in your chart, how high does a gust's peak go, and how many
frames (40 ms each) does recovery take?

> Host says 11.2° (≈ 112 on the chart) and about 4 frames. Every `$ATT` line
> must pass its checksum — `host.py` counts the bad ones. If Wokwi's serial
> monitor drops characters, you will see it here.

Then type `stats`:

```
world_step+sense : ..... cycles (max .....) = ... us of every 2000 us
control_step     : ..... cycles (max .....) = ... us of every 4000 us
```

> Lesson 10's M0+ model predicts **about 33 900 cycles** for world step +
> sense and **about 26 000** for a flying control step, at one flash wait
> state (slide 22). Write down what Wokwi says. Is Wokwi's cycle count a
> model of the flash wait state? Of the M0+ timing at all? Its number is a
> third opinion — record it.

---

## Part 6 — Break the mixer (10 min)

**Do this:** arm and hover. Type `mix -1`.

**Guess first:** it now corrects roll the wrong way. Slow drift, or sudden?

> Host: **tilt failsafe 0.36 s later, then a crash.** Not a drift — an
> exponential run-away: every correction makes the error bigger, which makes
> the correction bigger. The `CRASH` beep, a 2 s pause, and the world stands
> it back up — with the mixer still broken. `mix 1` to fix.
>
> Real first flights start with this check, props **off**: tilt the frame by
> hand and watch which motors speed up.

---

## Part 7 — The real sensor (20 min)

**Do this:** press the joystick's **SEL** (space). The OLED says `MPU`; the
horizon now shows what the estimator makes of the Wokwi MPU6050. Nothing
flies in this mode. Click the MPU6050 to get its sliders.

**Step 1 — signs.** Move `accelY` to +0.5 g (keep `accelZ` at 1).

**Guess first:** roll left or right? By how much?

> The lesson mounts the chip with X forward, **Y left**, Z up. A +Y reading
> means gravity's reaction leans left — the drone's **right** side is down:
> roll **+**, atan2(0.5, 1) = **26.6°**. If your horizon rolls the other way,
> the mounting assumption and Wokwi disagree; fix the signs in `mpu_sense()`
> and note it in your log.

**Step 2 — lag.** Snap `accelY` from 0 to 0.5 g.

> The estimate takes about τ = 0.2 s to get most of the way — the
> accelerometer is the slow input. A real tilt would arrive through the gyro
> instantly; a slider moves only the accelerometer.

**Step 3 — a bias you set.** Put `accelY` back to 0, then set `rotationX` to
**5** °/s and leave it.

**Guess first:** a constant gyro reading with a level accelerometer. Where does
the estimate settle?

> Slide 10: error = bias × τ = 5 × 0.196 = **0.98°** — and it *stays*
> there instead of ramping. Try 20 °/s: 3.9°. Then `alpha 1000`: now it
> ramps without limit, 5° every second. This is the whole complementary
> filter, measured with two sliders.
>
> **Not watched in Wokwi.** Whether the Wokwi model honours `DLPF_CFG`, and
> how much the display's 25 ms bus hold delays the reads (`stats`: late
> wake-ups), are open questions — write down what you see.

---

## Part 8 — Yours: yaw hold or altitude hold (open)

Pick one.

- **Yaw hold.** The controller damps yaw *rate* only, so a gust leaves the
  nose pointing somewhere new. Integrate `gyro[2]` into a heading, hold it
  with a P loop that feeds `rc_yaw_rate`'s place, and add a host check:
  after a yaw kick (extend `world_kick`), the heading returns within 5° in
  2 s. Watch the gyro-only lesson of Part 2 come back: with no magnetometer,
  your heading *will* drift. By how much per minute?
- **Altitude hold.** The world is on a gimbal. Give it a vertical axis in
  `world.c` — *z*, *vz*, gravity minus thrust·cos(tilt)/mass, a floor at 0 —
  and a barometer in `sensors_t` with 0.5 m of noise. Then hold 1 m with a
  PID on throttle. Notice what happens to altitude when you roll 30°, and
  why the cos(tilt) term belongs in the controller as well.

Either way: **a host check first, then Wokwi.** If it is not measured on the
host, it does not fly.

---

## Where to go next

- Lower `CTRL_HZ` to 125 (and the task period to 8 ms). Re-run the host test.
  Which check fails first, and is it the one you expected?
- `world.c` spends ~17 000 cycles a step on floats. Rewrite `gauss()` and the
  quaternion update in Q16.16 (lesson 10) and measure with `host/cycles.py`.
  Then check the host results did not change by more than the noise.
- Make the plant harder: double `tau_motor` to 60 ms in `world_init()`. Which
  default gain do you have to change, and which way?
