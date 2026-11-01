# Lab 14 — Drone Missions

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf) ·
**Protocol**: [MAVLink mission protocol](https://mavlink.io/en/services/mission.html)

---

## What this lab is

**A guided walkthrough with three contests, not a test.** You fly a simulated
drone two ways: on your PC, where `host/sitl` flies the lesson's own C files
faster than real time, and in Wokwi, where the same code runs on the
simulated chip with the OLED map, the joystick and the buttons. You tune its
position hold against a wind, design a mission in Python, set off every
failsafe on purpose, and finally race: fastest mission that still passes the
accuracy rules.

Each part says **Do this**, asks you to **guess**, then explains **what you
see and why**. Where the answer came from the host test, it says so. Where it
is a Wokwi prediction, it says it **has not been watched** — nobody has run
this lesson in Wokwi yet, so if you are first, write down what you see.

Nothing is marked or collected. **Put every change back before the next part**
unless the part says otherwise.

All host commands run from the lesson's `host\` folder.

---

## Part 0 — Fly it on the PC first (15 min)

### Step 1. The host test

```
cd host
run.bat            (or: sh run.sh  in Git Bash)
```

It builds `out\sitl.exe` from `world.c`, `flight.c`, `mavlite.c`, `ui.c` and
`fm.c` — the firmware's own files — and flies them. It should end:

```
PASS: 0 check(s) failed
...
ALL HOST TESTS PASSED
```

**Guess first:** the default mission in still air takes 57.7 s from START to
touchdown. In an 8 m/s wind with gusts, longer or shorter — and by how much?

> **Measured on the host:** 61.0 s. Only 3.3 s more, because the wind helps
> on some legs and hurts on others; what really grows is the **cross-track
> error**, 1.75 → 2.07 m max, 0.56 → 1.00 m RMS. Wind costs accuracy before
> it costs time.

### Step 2. Look at the screen it drew

Open `host\out\oled_wind.txt` in an editor with a fixed-width font and squint:
`#` is a lit pixel. That is the OLED, 40 s into the windy flight, drawn by
`ui.c` on your PC. Find the fence, the five crosses, the trail and the drone.

---

## Part 1 — Build it, read the banner, read the budget (15 min)

```
build.bat
simulate.bat
```

Zero warnings, and:

```
           FLASH:       27496 B        32 KB     83.91%
             RAM:       10520 B        12 KB     85.61%
```

Paste `diagram.json` into Wokwi's `diagram.json` tab (the app board), then
`F1` → **Upload Firmware and Start Simulation…** → `Main.elf`. Expect:

```
=== SOC3050 lesson 14 - Drone Missions ===
  pad      : joystick, knob, A/B/SEL
  OLED     : 0x3C answered
  drone    : simulated in this chip - world 100 Hz, control 50 Hz, GPS 10 Hz
```

then `$POS` and `$TRU` frames five times a second. Type `tel` to silence them,
`stats` to see the budget.

**Guess first:** slide 26 predicts "a few hundred µs" for one
`control_step()`. Will `stats` agree? And which task will have used the most
stack?

> **Not watched yet.** The numbers to record: `ctl` last / max / mean µs,
> `world` the same, and each task's `stack N of M words`. If any task's N is
> within 20 words of M, that stack is too small — say so; the sizes on slide
> 25 are estimates from `-fstack-usage`, and this is the first measurement.
> If `OLED` says *no answer*, the map stays dark but everything else flies.

---

## Part 2 — Press A (10 min)

**Do this:** wait a second, click the diagram, press **a**. Leave the knob
where it is (500 = **5 m/s** wind).

**Guess first:** what does the serial monitor print if you press A in the
first half second after reset?

> **Predicted, not watched:** `button A (fly): PREARM`. Arming needs ten GPS
> fixes — one second — and the GPS only starts reporting after its 200 ms
> delay line has filled. Every real autopilot refuses to arm without a GPS
> lock; this one does too.
>
> After a second, A should print `button A (fly): OK`, then
> `$EVT,...,MODE,TAKEOFF,CMD`, `$EVT,...,MISSION,5` and five `$MIS` frames,
> and the OLED should show the drone climbing (the altitude bar on the
> right), turning to face waypoint 1, and leaving dots behind it. Each
> waypoint chirps; RTL blips; touchdown is a lower blip.

Copy the whole serial monitor into `capture.txt` when it lands (`DISARMED`),
and score it:

```
python mission.py --score ..\capture.txt --race
```

> **Predicted from the host:** `sitl --fly 5` flies this mission in 5 m/s in
> **57.7 s**, max cross-track 1.67 m. The board runs the same code from the
> same seed, but its wind comes from the knob through the ADC, and its
> GPS fixes are counted from reset rather than from takeoff, so expect close,
> not identical.

---

## Part 3 — Contest 1: hold your position in a gale (20 min)

Slide 14 split the hold error into three: **seen** (what tuning can fix),
**blind** (what the GPS allows) and **true**. Default gains, 8 m/s:

```
out\sitl --hold 8
  hold 30 s, wind 8 m/s:  seen RMS 0.76 m   blind 0.67 m   true 1.01 m (max 2.19)
```

**Do this:** beat **seen RMS 0.60 m** by changing gains on the command line:

```
out\sitl --hold 8 KP_POS=1200 KP_VEL=2500 KI_VEL=800
```

Values are `$PARAM` integers — gains ×1000 (the `params` table at the top of `flight.c`, or type
`params` in Wokwi). `KP_POS`, `KP_VEL`, `KI_VEL`, `EST_A`, `EST_B`, `TILT`
are all fair game.

**Guess first:** can you also push **true** RMS below 0.60 m?

> **Measured on the host:** a seen RMS of 0.56 m was found while writing this
> lab. True RMS barely follows: the **blind** 0.67 m does not move at all with
> controller gains, because it is the GPS's own wander. Square root of the
> sum of squares: you cannot hold better than you can see. Turning `EST_A`
> down to "smooth the GPS" makes the blind error *worse* (0.73 m in one try):
> the accelerometer bias gets more time to drift.
>
> Then try `KI_VEL=0` and read the **mean offset**: the drone settles 1.26 m
> downwind (slide 13). Put the integral back and read the tilt: about 13° into
> the wind, held by the integrator alone.

**In Wokwi (not watched):** fly to HOLD (press A, then the joystick button),
turn the knob to 1000 (10 m/s) and paste `$PARAM,KI_VEL,0*..` — build the
frame with `python ..\..\08_UART_And_Python_Host\host.py --frame "PARAM,KI_VEL,0"`.
The drone on the map should slide downwind and stop about 1.5–2 m away.

---

## Part 4 — Forget the GPS is late (10 min)

**Do this:** in `flight.c`, change

```c
#define GPS_LAG_STEPS 10u
```

to `0u`. Run `run.bat` again.

**Guess first:** the GPS is only 200 ms late. How much worse can that make
an estimate that is updated ten times a second?

> **Measured on the host:** estimate error rises from **0.69 to 1.08 m** RMS
> in still air (0.68 to 0.98 m in wind), and the flight checks still pass —
> which is the dangerous part. Comparing a 200 ms-old fix with *now* blames
> the GPS for the 0.8 m flown at 4 m/s while the fix was in the post, every
> tenth of a second. Slide 9. Put it back.

---

## Part 5 — Design a mission: a figure-8 survey (20 min)

`mission.py --gen` knows `circle` and `lawnmower`. Add `figure8`:

1. In `generate()`, add a branch `kind == "figure8"` taking `R ALT N`.
2. A figure-8 is a **lemniscate**: for `t` from 0 to 2π,
   `x = R sin t`, `y = R sin t cos t`. Print `N + 1` points so it closes.
3. Keep it inside the fence (50 m) and under 16 waypoints.

Then fly it on the PC and score it:

```
python mission.py --gen figure8 25 8 12 > my8.mission
python mission.py my8.mission > my8.txt
out\sitl --fly 8 my8.txt
python mission.py --score out\fly.log --race
```

**Guess first:** where on the 8 will the cross-track error be worst — on the
long straight-ish arms or at the crossing?

> **Measured on the host** for comparison, a 4-lane lawnmower
> (`--gen lawnmower 40 30 4 8`) in 8 m/s: 85.4 s, max cross-track 3.16 m —
> just over the race limit, at the 180° turns where each leg ends. The
> lemniscate has no 180° turns, but with 12 points its legs change direction
> by up to ~60° near the crossing — that is where to look. Then paste
> `my8.txt` into Wokwi line by line and watch the 8 drawn on the OLED (not
> watched yet).

---

## Part 6 — Set off every failsafe (15 min)

Each one has a host measurement (slide 18) and a Wokwi version to try. In
Wokwi, build frames with `host.py --frame` as in Part 3, or `mission.py`.

| Failsafe | In Wokwi (not watched) | Measured on the host |
|---|---|---|
| **geofence** | press A, then SEL for HOLD, then hold the joystick right | RTL at 51.6 m (fence 50), lands 0.45 m from home |
| **low battery** | in flight, `$PARAM,BAT,24` | RTL with reason `BAT`, mission abandoned, lands at home |
| **critical battery** | in flight, far from home, `$PARAM,BAT,9` | `LAND` in place, no RTL, touchdown 0.39 m/s |
| **fence, on the ground** | `$MISSION,CLEAR`, `$WP,0,600,0,50`, `$MISSION,START` | `$ACK,MISSION,ERR,FENCE` |
| **pre-arm** | press A right after reset | `PREARM` |

**Guess first:** you are at 9% battery, 30 m from home, and the fence is fine.
Should the drone go home?

> No: at the drain of a hover, 9% is about 25 s of flight, and RTL climbs to
> 10 m first. `failsafes()` checks the critical level **first** and returns,
> so nothing else gets a vote. On the OLED the panel's second line should
> read `!BATCRIT`, and the buzzer growl at 300 Hz.
>
> **Now break it — on the host:** make the battery drain six times faster and
> fly the mission:
>
> ```
> out\sitl --fly 4 race.txt DRAIN=210
> ```
>
> (`race.txt` is `default.mission`'s frames, made in Part 7's first line.)
> **Measured on the host:** RTL on `BAT` at 35.1 s, `LAND` on `BATCRIT` at
> 41.7 s — every failsafe fired correctly — and the drone still hit the
> ground at **8.7 m/s** with 0.0% left. 10% at six times the drain is about
> 5 s of flight; descending from the RTL's 10 m takes about 13. **A threshold
> in percent is really a threshold in seconds**, and it was set for the
> normal drain. Worse: the log ends `DISARMED (LANDED)`. The flight computer
> cannot tell a landing from a crash — only `$TRU`'s touchdown speed, which no
> real drone has, gives it away. How would you set `BAT_CRIT` from the drain
> rate instead?

---

## Part 7 — Contest 2: the mission race (open-ended)

**The rules** (`mission.py --race`): fly the **default mission** (its five
waypoints, unchanged) in **8 m/s wind, seed 1**, as fast as you can, while

- every waypoint is passed within **2.0 m** (true position),
- the cross-track error never exceeds **3.0 m**,
- it lands within **1.5 m** of home,
- no failsafe fires.

Break any rule and the time does not count. Change only parameters:

```
python mission.py default.mission > race.txt
out\sitl --fly 8 race.txt CRUISE=500
python mission.py --score out\fly.log --race
```

**Beat these** (measured on the host):

| | time | verdict |
|---|---|---|
| default parameters | **61.0 s** | legal |
| `CRUISE=800 ACCEPT=250` — the obvious idea | 54.2 s | **disqualified**: 7.85 m cross-track |
| the best found while writing this lab | **44.2 s** | legal |

**Guess first:** which single parameter is holding the default drone back in
an 8 m/s wind? (Hint: slide 13 says the hold uses 13° of tilt just to stand
still.)

> No answer here — that is the contest. Tell the class your time and
> parameters. Then try your winning parameters in Wokwi with the knob at 800:
> paste `race.txt`, add your `$PARAM` frames, capture, score. Does Wokwi agree
> with the host? Nobody knows yet.

---

## Part 8 — Live link and the missing handshake (optional, 20 min)

**Live route** (Wokwi VS Code extension; needs pyserial): `wokwi.toml` opens
an RFC 2217 port, so

```
python mission.py default.mission --port rfc2217://localhost:4000 --log flight.log
```

sends each frame, **waits for its `$ACK`**, resends up to three times, and
records telemetry until the drone lands. Not watched yet.

**Think about it:** real MAVLink's upload is a handshake: the ground sends
`MISSION_COUNT`, then the **vehicle** asks for each item by number
(`MISSION_REQUEST_INT`) and ends with `MISSION_ACK`. Ours just sends and waits
for ACKs. On a radio link that loses one frame in twenty:

1. What happens to our upload if an `$ACK` (not the `$WP`) is lost? Is a
   resent `$WP,3,...` harmful? (Look at `fc_wp_set()`.)
2. What can the vehicle-driven handshake detect that ours cannot? (Hint: what
   if the *ground station* restarts halfway?)
3. Add `$MISSION,COUNT,n` to `mavlite.c` so `START` is refused unless exactly
   `n` waypoints arrived. Test it on the host with `sitl --parse`.

---

## Where to go next

Lesson 15 drops the drone to the ground: a line follower is slide 16's
cross-track loop with a camera instead of a GPS. Lesson 16 brings the Python
side back: a planner that sends waypoints exactly as `mission.py` does.
