# Lab 19 — Final Project Handbook

**SOC3050 ARM Edition · Nucleo-C031C6 · first session 2 hours, then six weeks**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this handbook is

Two things in one file.

**Section A — the first session (2 hours, guided, nothing handed in).** You
take the template apart the way every lab in this course has: build it, read
what it reports, break it on purpose, and beat a number. Each part says
**Do this**, asks you to **guess**, then explains **what you see and why**.
Where a predicted outcome comes from the host test, it says so; where it is a
Wokwi prediction, it says it **has not been watched** — nobody has yet run
this template in Wokwi, so your observation is the first.

**Section B — the project (six weeks, handed in and graded).** The
week-by-week checklist, the proposal template, the design-review checklist,
the report template, the demo checklist and the rubric.

---

# Section A — The first session

## Part 0 — Build it, test it on the PC (15 min)

### Do this

```
build.bat
host\run.bat
```

The build must say zero warnings and roughly:

```
           FLASH:       16648 B        32 KB     50.81%
             RAM:       10280 B        12 KB     83.66%
```

### Guess first

`host\run.bat` includes an autopilot — a little PD controller that steers the
ball at the target. **How many points does it score in 60 s of game time?**
Write a number down before you look.

> **What you see and why** (measured on the host). Fifteen checks, all PASS,
> and the bot scores **83** — more than one a second, with zero wall hits.
> The whole run plays over an hour of game time and takes well under a second,
> because it is the same `app.c` the board runs, compiled for the PC, with no
> RTOS, no display and no waiting. That speed is why every project in this
> course is required to have host tests: you can afford to run them on every
> change.

---

## Part 1 — Read the banner, then `stats` (15 min)

### Do this

`simulate.bat`, paste `diagram.json`, upload `Main.elf`. Read the banner. Play
for 20 seconds, then type `stats` in the serial monitor. Play 20 more, type
`stats` again.

### Guess first

Which task uses the most CPU? Which stack is closest to full?

> **What you see and why** (Wokwi — **not yet watched**; this is the
> prediction). The banner should show the reset cause, `SSD1306 at 0x3C` and
> `buttons + ADC ready`. In `stats`, `display` should dominate: every frame
> re-sends all 1088 bytes over polled I²C, ~24.5 ms of a 50 ms period (host
> test `[6]` counts the bytes). `idle` takes most of the rest. The stacks were
> sized from a static estimate (README, "Memory budget"); the printing tasks
> — `shell`, `telem`, `health` — are the ones to watch. **Write down what you
> actually see.** If a stack is above 90 %, that is a real finding: report it.

---

## Part 2 — Break the sticky edges (15 min)

### Do this

In `Main.c`'s `task_input`, delete the two `|=` lines so that `shared_pad = p`
simply overwrites. Rebuild, then tap **A** (brake, with a chirp) twenty times,
quickly.

### Guess first

Input runs every 10 ms, app every 20 ms. What fraction of taps go missing?

> **What you see and why** (Wokwi — **not yet watched**). About **half**. A
> press is an *edge*: `pad_read()` reports it once, on the 10 ms run where it
> happened. Every other input run happens between app steps and overwrites the
> shared copy with `pressed = 0` before the app looks. Levels (the stick)
> survive overwriting; events do not. Put the lines back.

---

## Part 3 — Kill a task, watch the watchdog (20 min)

### Do this

Type `hang display`. Watch the screen, LD4 and the serial monitor. After the
reset, type `health`. Then try `spin input`.

### Guess first

How long after `hang display` until the board resets? What happens after
`spin input`?

> **What you see and why** (the timing is **measured on the host**, the
> Wokwi behaviour **not yet watched**). The screen freezes; within 250–500 ms
> (one or two health checks — host test `[8]`: hang at 1000 ms, first starved
> check at 1250 ms) `health:` prints `missing: display`; LD4 stops blinking because it toggles
> only on a refresh. On silicon the IWDG resets the chip within **1.0 s** of the
> last refresh. **Wokwi has no IWDG**, so the template's fallback fires instead:
> 1.5 s after the last refresh — host test `[8]` measures exactly 1500 ms — it
> prints `no IWDG reset` and calls `NVIC_SystemReset()`.
>
> `spin input` is different: the highest-priority task never yields, so
> **nothing below it runs** — not the display, not the shell, not `health`. In
> Wokwi the board simply freezes, for ever, because the software fallback lives
> in a task that is starved. On silicon the IWDG resets it. That is the whole
> argument for a *hardware* watchdog, in one experiment.

---

## Part 4 — Beat the bot (15 min)

### Do this

Restart with **B**. Play for exactly 60 s of game time (the HUD shows it).
Turn the knob to set the stick's gain — find your best setting.

### Guess first

Can a human beat 83?

> **What you see and why.** Probably not at first. The bot reads the exact
> target position every 20 ms and uses a velocity term to avoid overshooting —
> a PD controller (lesson 17). Your reaction time alone is ~200 ms, ten app
> steps. **Challenge:** beat it, then improve the bot in `host/test_app.c`
> (`bot()`) until it scores more than 100. Post your best gain setting and
> score on the course board.

---

## Part 5 — Frame-cost contest (25 min)

### Do this

Host test `[6]` prints the bytes one frame puts on the bus: **1088, even when
nothing moved**. `oled_clear()` dirties every page that held anything, and the
border and HUD touch all eight. Cut it. Rules: the picture must look the same;
measure with `host\run.bat`.

### Guess first

What is the least a frame with a moving ball and a ticking clock could cost?

> **What you see and why.** A page is 129 bytes on the wire (control byte +
> 128 columns). The ball touches one or two pages; the clock touches page 0.
> So a frame where only those changed is **~3 pages ≈ 400 bytes** (an
> estimate — prove it). The route: stop the display task clearing the whole
> screen; let the app erase only what it drew last frame (`oled_fill_rect`
> with `OLED_OFF` over the old ball and the HUD), and draw the border once.
> Then `oled_flush()` sends only what changed. Record your number; the
> lowest that still looks right wins. Every byte saved is 22.5 µs of CPU
> back, 20 times a second.

---

## Part 6 — Measure a stack (10 min)

### Do this

Halve `W_SHELL` in `Main.c` (320 → 160 words). Rebuild and type `stats`.

### Guess first

Does it crash? When?

> **What you see and why** (Wokwi — **not yet watched**). Probably not at
> boot: the shell's deep path is `stats` itself — `say()` → `vprintf()` →
> newlib's `_vfprintf_r` (144 B frame) → `_printf_i` (64 B) → … → the UART
> ring, ~650 bytes before the shell's own 224. 160 words is 640 bytes. The
> overflow writes below the stack into whatever the linker put there — look it
> up with `arm-none-eabi-nm -n Main.elf`: in the shipped build `stk_shell`
> starts at `0x20000178`, and the words just below it are `print_lock`,
> `bus_lock`, `app_lock` and then `health`. The first thing an overflow
> corrupts is **a mutex's owner pointer** — and the failure appears
> **somewhere else**, later: a task blocked on a lock nobody holds, a watchdog
> that stops being fed for no visible reason. That is why the requirement is "≤ 75 % after the full demo", and why
> the high-water mark is measured, not guessed. Put it back.

---

## Part 7 — Your project's first file (the rest of the session)

### Do this

1. Copy `app.c` to `app_steer.c.txt` (keep it as a reference; `.txt` so the
   build ignores it). Empty `app.c` down to the five functions of `app.h`.
2. Change `APP_NAME`. Make `app_draw()` print your project's name.
3. Copy `host/test_app.c`'s structure: delete the steer tests, keep the fake
   platform and `CHECK`, and write **one** test of your MVP's first rule
   (a collision, a state change, a score).
4. `host\run.bat` passes; `build.bat` has zero warnings. Commit.

> This is the end of the guided part. From here the project is yours —
> Section B is the map.

---

# Section B — The project

## B1. Week-by-week checklist

### Week 1 — Proposal
- [ ] Track chosen (A game / B drone / C robot / D own idea), team formed (1–2)
- [ ] Proposal written from the template in B2, ≤ 1 page
- [ ] At least **5 measurable requirements**, each with unit, limit and method
- [ ] MVP is ≤ 3 lines and end-to-end
- [ ] Template builds and `host\run.bat` passes on your machine
- [ ] Scope agreed with the instructor (sign-off in lab)

### Week 2 — Design review
- [ ] Architecture diagram: tasks, data flow, every arrow labelled with its protection
- [ ] Task table: period, priority, work, beats? — and the priority argument
- [ ] Timing budget table, with *estimate* beside every unmeasured number
- [ ] Memory budget: flash, RAM, every stack, from your build
- [ ] Test plan: host tests named, Wokwi observations listed
- [ ] Fault list: ≥ 3 faults, each with its defined behaviour
- [ ] Review checklist (B3) all ticked; 10-minute review in lab

### Week 3 — Build
- [ ] Core logic in pure C with host tests passing
- [ ] First on-board run in Wokwi; banner and `stats` saved
- [ ] Zero warnings, every commit

### Week 4 — Mid demo
- [ ] MVP demonstrated live in Wokwi
- [ ] Host tests passing, including one fuzz or long-run test
- [ ] Updated plan: what is in / out for the final

### Week 5 — Measure
- [ ] Every requirement measured; method written down
- [ ] Fault demo works and is repeatable
- [ ] `stats` after a long run: no `over`, every stack ≤ 75 %

### Week 6 — Final
- [ ] Report (B4) submitted as PDF
- [ ] Repository tagged `final`; `build.bat` and `host\run.bat` both clean from a fresh clone
- [ ] Demo checklist (B5) rehearsed once, timed

---

## B2. Proposal template (one page)

```
TITLE:              ______________________     TRACK: A / B / C / D
TEAM:               ______________________

ONE SENTENCE:       It ___ so that ___.

MVP (<= 3 lines, end to end, demo week 4):
  1.
  2.
  3.

TARGET (what the final demo promises):
  -
STRETCH (only after target is done, tested, measured):
  -

REQUIREMENTS (>= 5):
  #  | what is measured           | unit | limit   | how (host test / stats / $SYS / recording)
  R1 |                            |      |         |
  R2 |                            |      |         |
  R3 |                            |      |         |
  R4 |                            |      |         |
  R5 |                            |      |         |

HARDWARE: app board as shipped / plus: _______ (Wokwi part name, pins)
FAULT I WILL DEMONSTRATE:  ________________  ->  defined behaviour: ________
BIGGEST RISK and the fallback if it happens: ____________________________
```

---

## B3. Design-review checklist

Bring these; the reviewer ticks them in front of you.

**Architecture**
- [ ] Diagram in the style of slide 10, for *your* project
- [ ] Every shared variable named, with its writer(s), reader(s) and protection
- [ ] Every event (button press, command, fault) is accumulated or queued, never overwritten
- [ ] One owner per peripheral (who touches TIM3? I²C? the UART?)

**Tasks and timing**
- [ ] Each task: period, priority, worst-case work, and why that priority
- [ ] Timing budget: sum of loads < 70 % of CPU, estimates marked
- [ ] Input-to-output latency computed as a sum, against your requirement
- [ ] I²C bytes per frame (track A) or control-step time (B, C) estimated

**Memory**
- [ ] Flash and RAM from your build; ≥ 1 KB RAM headroom
- [ ] Every stack sized with a reason; printing tasks ≥ ~650 B for printf

**Robustness**
- [ ] Every task that should be alive beats `health`
- [ ] Every wait is bounded (no `while (!flag) {}` without a limit)
- [ ] ≥ 3 faults with defined behaviour; one chosen for the demo

**Testing**
- [ ] Host tests named, each with the number it checks
- [ ] Wokwi observation list (slide 22's table, your rows)

---

## B4. Final report template

Maximum 10 pages plus appendices. Sections, in order:

1. **Summary** (½ page) — what it does, one screenshot, the headline numbers.
2. **Requirements and results** — the table every report must have:

   | # | Requirement | Limit | Measured | Method | Pass |
   |---|---|---|---|---|---|
   | R1 | | | | host test / `stats` / `$SYS` / recording | |

3. **Architecture** — diagram; task table; shared-data table (writer, reader,
   protection); priority argument.
4. **Timing budget** — estimated (design review) beside **measured**
   (`stats`: `resp avg/max`, `cpu%`; `$SYS`: idle). Explain every difference
   bigger than 2×.
5. **Memory budget** — flash, RAM, every stack's size and high-water mark.
6. **Testing** — host tests (what each checks, its output), the fuzz or
   long-run result, and the Wokwi observation log: *did / expected / saw*.
7. **Faults** — each fault, its defined behaviour, and evidence it happens.
8. **Facts checked** — every register and bit name you use, and the file you
   grepped it in (`stm32c031xx.h`, `core_cm0plus.h`); every Wokwi part and
   the docs page.
9. **What was not verified** — honestly. Simulator limits included.
10. **AI use** — tool, what for, what you changed. "None" is a valid answer.
11. **Appendix** — `stats` output from a long run; the host test output.

**Required measurements, every track:** `stats` after ≥ 60 s of use; idle
per mille from `$SYS`; every stack's high-water mark; worst response time of
your main task; flash and RAM. **Plus** track A: bytes per frame, frame rate;
track B: settle time, overshoot, fault-to-action time; track C: lap time or
win rate over N seeded runs; track D: the numbers your proposal promised.

---

## B5. Demo checklist

- [ ] Fresh clone (or `git clean`) → `build.bat`: zero warnings, sizes shown
- [ ] `host\run.bat`: all PASS, shown on screen
- [ ] `simulate.bat` → paste `diagram.json` → upload `Main.elf`; banner read aloud
- [ ] 30-second pitch, rehearsed and timed
- [ ] Live demo of the target behaviour
- [ ] One slide: requirement / limit / measured / pass
- [ ] Fault injected live; defined behaviour shown
- [ ] `stats` typed live after the demo
- [ ] Every team member ready to explain any line of `app.c` and the files you changed
- [ ] Report PDF and tagged commit submitted before the session

---

## B6. Grading rubric

| Area | Points | Full marks | Half marks | Zero |
|---|---|---|---|---|
| Proposal + design review | 10 | measurable requirements; budgets with estimates marked; checklist complete | requirements partly vague; budgets incomplete | missing |
| Working system | 25 | MVP and target demonstrated live, as proposed | MVP only, or target partly working | does not run |
| Engineering quality | 15 | priorities argued; all shared data protected; one owner per peripheral; zero warnings | some unprotected sharing or unargued choices | ad hoc; warnings (cap 8) |
| Measurement | 15 | every requirement measured, method stated, estimates vs measured compared | some requirements measured | numbers asserted, not measured |
| Testing | 10 | host tests with numeric checks incl. a long run; Wokwi log | a few host tests, or only Wokwi | no tests |
| Fault handling | 10 | ≥ 1 fault demonstrated with defined behaviour; watchdog covers all tasks | fault described, not demonstrated | none |
| Report | 10 | all B4 sections; honest about what was not verified | sections missing | missing; a claim the demo contradicts (cap 3) |
| Oral | 5 | every member explains the lines asked | some answers missing | cannot explain own code |
| **Stretch** | **+5** | bonus, only on top of a complete target | | |
| **Total** | **100 (+5)** | | | |

**AI use** (slide 26): allowed with disclosure in report section 10. A line you
cannot explain in the oral scores zero for that question; undisclosed AI code,
or code taken from another team or a previous year, is an integrity case.
