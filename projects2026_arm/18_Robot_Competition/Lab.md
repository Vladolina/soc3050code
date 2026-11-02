# Lab 18 — Robot Competition

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours, then the tournament**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**This lab is a competition.** You start with `student.c` — a sumo robot
brain that is deliberately bad — and five opponents in `bots.c`. Part by part
you teach your robot one sensor at a time, and after each part you measure
whether it got better. The last part is the class tournament, and this time
**something is handed in**: one `$RESULT` line and your `student.c`.

Each part says **Do this**, asks you to **guess**, then explains **what you see
and why**. Predictions marked *host* were measured with `host\run.bat`;
predictions about Wokwi have **not been watched** — if Wokwi disagrees, write
down what it did.

Two ways to run a tournament, and you should use both:

| | Firmware (Wokwi) | Host (`host\run.bat`) |
|---|---|---|
| needs | a browser | `gcc` on PATH (MinGW-w64) |
| speed | x1–x8, or MAX | ~36 000x real time |
| shows | the fight, on the OLED | tables |
| measures | **cycles on the real CPU** | thousands of matches |

No gcc? Everything in this lab can be done on the board's own tournament
(menu item TOURNAMENT, or type `tour` in the serial monitor). It is just slower.

---

## Part 0 — Build it, run it, run the league (20 min)

### Step 1. Build and simulate

```
build.bat
simulate.bat
```

Zero warnings, and about:

```
           FLASH:       29244 B        32 KB     89.25%
             RAM:        9872 B        12 KB     80.34%
```

Paste `diagram.json` into Wokwi's `diagram.json` tab (the app board: OLED,
joystick, A, B, knob, buzzer), then `F1` → **Upload Firmware and Start
Simulation…** → `Main.elf`.

### Step 2. The menu

The OLED shows `> vs Bull`. **B** cycles Bull → Matador → Spinner → Turtle →
Coward → TOURNAMENT. **A** starts. Turn the knob to the left quarter (x1).

Press **A**. Two beeps, a high beep: GO. Your robot is the **open** circle,
the bot the **filled** one; the short lines from each nose are the three
distance sensors — a long line means it sees something. **SEL** toggles them.

### Step 3. The host league

```
host\run.bat
```

**Guess first:** of the six strategies (yours and five bots), where does the
starter finish?

> Measured on the host: **5th of 6**, 152 wins / 105 draws / 243 losses in 500
> matches — above only Coward. Note the `s-out` column: **27** rounds lost by
> driving out with nobody touching. Every check prints `[PASS]` and the script
> ends with `ALL CHECKS PASSED` and identical hashes at -O0, -O2 and -Os.

### Step 4. Run it from the station (optional)

With the simulation running in VS Code, start

```
uv run tools\station\station.py
```

from the repository root and click its picture. The dohyo appears large —
both robots with their headings and names, the round score and the clock —
with the standings beside it, kept from every `$MATCH` and `$RESULT`. The
board's controls are keys: **a** = A, **b** = B, **s** = SEL, **1**–**5** =
speed x1…MAX, arrows = the stick in fun mode, **?** = the list. Run a
tournament with the station connected: its `$RESULT` hash must be the one you
got without it — the station only watches. Not yet tried against Wokwi: if
the station keeps saying *waiting for rfc2217://localhost:4000*, write that
down and use the board's buttons.

---

## Part 1 — Measure the machine (15 min)

Nothing in this lesson has been timed on the Cortex-M0+. You are first.

**Do this:** in Wokwi, set the knob to x8 and start any match. After a round,
type `stats` in the serial monitor.

**Guess first:** one `match_step()` — physics for two robots, and every 4th
step two sensor syntheses and two strategy calls — costs how many cycles? Is
x8 achievable?

> Record: the mean and worst step cycles, each bot's worst `strategy_step`, and
> `achieved speed`. A soft-float operation costs ~50–100 cycles (lesson 10);
> a world step is a few hundred of them, plus 3 `atan2`s per sensor every 4th
> step. If `achieved speed` is under x8, the sim task is hitting its 14 ms cap
> (slide 19) — the match is slowed, never skipped.

**Then the big one.** Choose TOURNAMENT, knob to MAX, press A, wait for the
`$RESULT` line, and compare its hash with the host's for the same seed:

```
host\run.bat -- -s 2026
...   $RESULT,student,5,4,11,3AA81370,host,host*43
```

> If the chip prints `3AA81370` too, the ARM soft-float and the PC's SSE agree
> bit for bit over 20 matches — and the class tournament can trust the host.
> If they differ, **tell the instructor**: it means the host cannot re-check
> submissions and the tournament must be run on the board. This has not been
> checked yet. You are the check.

---

## Part 2 — The edge first (20 min)

Losing by driving out on your own is the commonest loss in sumo, and the
starter does it 27 times in the league.

**Do this:** read `student.c`. It backs off when a *front* corner sees white —
for 300 ms, straight back, then searches clockwise whatever side the line was.

**Guess first:** what happens when the robot is shoved backwards towards the
line? And when it backs off straight, then spins clockwise towards the side
that saw the line?

> It ignores `EDGE_BL | EDGE_BR` completely — pushed back over the line, it
> never knows. And straight-back-then-clockwise turns it *into* the line half
> the time. Fix both: rear corner white → drive **forward**; front-left white
> → back off, then turn **right** (and front-right → left). `bots.c`'s
> `edge_reflex()` is one way.

**Beat this number:** run `host\run.bat` and get `s-out` for `student` from
**27** down. Below 10 is good; 0 is possible (Matador manages 1).

---

## Part 3 — Use the side sensors (15 min)

**Do this:** the starter only looks at `dist_mm[DIST_CENTRE]`. Make the side
sensors steer: left sees it → turn left while driving; right → right. Keep the
direction it was last seen in your state, and search that way when it is lost.

**Guess first:** against Bull, which charges straight at you, does steering
matter at all?

> Bull and the starter are dead even today (28-37-35, host). Most of those 37
> draws are two robots that lose each other. Steering is how you *keep* the
> opponent in the centre cone — and the centre cone is where you push from.

**Beat this number:** host head-to-head against Bull better than 28 wins out
of 100.

---

## Part 4 — Never trust one reading (15 min)

**Do this:** look at slide 9's measurement: 3.9 % of reads of a target right in
front of you say `DIST_NONE`. Count how many calls in a row your robot has *not*
seen the opponent, and only give up the chase after, say, 3.

**Guess first:** at 50 Hz, how long is a 3-call memory?

> 60 ms. Enough to ride out a miss; short enough not to chase a ghost. One
> `uint8_t` of your 64 bytes.

---

## Part 5 — Bumped from the side (15 min)

**Do this:** `bump` has four bits. On `BUMP_LEFT` or `BUMP_RIGHT`, spin to
face the hit. Then pick **Turtle**, press SEL in the menu to take the robot
yourself, and try to push Turtle out with the joystick. Then let your strategy
try.

**Guess first:** why is Turtle — which never leaves its start spot — the
3rd-best bot?

> Slide 7: a robot that faces you can push back along its wheels, and two equal
> robots nose to nose do not move (host: 1 mm in 6 s). Turtle always faces
> you. To beat it you must reach its side — it turns at 50 %, so circle faster
> than it can track. The starter scores 7-23-70 against it (host).

---

## Part 6 — Encoders: am I winning this push? (15 min)

**Do this:** when `BUMP_FRONT` is set and you command 100/100, compare the
encoder travel since the last call with what full speed would give (16 mm per
call). Near 0: stalemate. Much more than your robot could move while touching:
your wheels are **slipping** — you are losing the push.

**Guess first:** in a nose-to-nose stalemate, what is the smart move?

> Not more power — there is none (traction-limited, slide 6). Break the
> symmetry: swing off-centre with one wheel so the contact moves to their
> side. Matador's whole personality is this idea.

**Beat this number:** host head-to-head against Spinner, the strongest bot,
better than 13 wins in 100. `host/strategies/alice.c` — the starter with Parts
2–5 done once, plainly — gets 17.

---

## Part 7 — The rules have teeth (10 min)

**Do this:** run the cheater:

```
host\run.bat host\strategies\cheater.c
```

> ```
> 0000000000000000 b calls.0
> REJECT host\strategies\cheater.c: the symbols above are variables outside strategy_mem_t
> ```
>
> A `static` inside a function is still one variable for *every* robot running
> that code, and never reset between matches. The league refuses to link it.

**Do this:** now the time budget. Add a deliberately slow loop to your
strategy — `for (volatile int i = 0; i < 5000; i++) { }` — build, run a match in
Wokwi and type `stats`.

**Guess first:** how many cycles is that loop?

> Not yet measured. A `volatile` loop on the M0+ is a load, an add, a store, a
> compare and a branch per turn — figure about 10 cycles, so ~50 000 cycles:
> **over** the 20 000 budget, and `stats` should flag `OVER BUDGET`. Take it out.

---

## Part 8 — The class tournament (open-ended)

**The rules** (they are the comments at the top of `student.c`):

1. Only `student.c` is submitted. Only `strategy.h` and C library headers.
2. State only in `*mem` — 64 bytes. No globals, no `static` variables.
3. Deterministic: no clocks, no `rand()`.
4. Worst call under **20 000 cycles**, as the firmware measures it.
5. `strategy_name` up to 12 characters — your tag on the OLED.

**Submit:**

- `student.c`, renamed to your tag (`kim_mj.c`) — it must be a C identifier;
- the `$RESULT` line from the **firmware** tournament at the seed the
  instructor announces (`tour 4711` in the serial monitor), copied whole,
  checksum included.

**How it is checked:** the instructor runs
`host\run.bat -- -s 4711 --tour kim_mj` with your file and compares the
`<hash>` with your line; a mismatch means the line did not come from this
file. Identical hashes from two different students mean identical strategies.
Then every file goes into one host league, round robin, 100 matches a pair:

```
host\run.bat entries\kim_mj.c entries\lee_sh.c ...     (Git Bash: host/run.sh entries/*.c)
```

**Ideas, if you are stuck:**

- **The lure.** Bull ignores the edge while it can see you. Stand near the
  line, let it charge, step aside.
- **The flank.** Arc so the opponent appears in a *side* cone, then turn in:
  you arrive at its side, where it cannot push back.
- **The clock.** `t_ms`, `my_rounds`, `their_rounds` are in the view. One
  round up with 3 s left? A draw is enough.
- **The opening.** You know your heading relative to the line through the
  centre? You do not — but in the countdown you *can* see whether they are in
  front of you, and plan the first move.

> Whatever you do: edge first, every call. More matches are lost to the line
> than to the opponent.

---

## Where to go next

- Add a gyroscope to `robot_view_t` (the world knows `w`): what would heading
  hold do for the flank?
- Give the robots a **wedge**: in `contact()`, a hit within ±20° of a robot's
  nose lifts the other's wheels — scale its grip down. How much does the league
  change?
- `bob.c` loses 823 of his 1313 lost rounds by driving out (host, with alice).
  Explain why with slide 10's arithmetic, then fix him.
