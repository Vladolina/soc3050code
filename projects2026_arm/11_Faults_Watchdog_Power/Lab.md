# Lab 11 — The Crash Lab

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A guided walkthrough, not a test.** You will crash the board on purpose,
about thirty times, and every time read the report it writes about itself
after the reset. Then you will hang it, starve it and watch a watchdog bring
it back — or fail to. The last parts are a game: decode a crash dump with the
verdict hidden, and beat the tightest watchdog timeout in the room.

Each part says **Do this**, asks you to **guess**, then explains **what you
see and why**. Nothing is marked or collected.

**Two kinds of prediction appear below, and they are labelled.** *Measured on
the host* means `host/test.c` computed it with this lesson's own code — trust
it. *Not yet watched* means it is what silicon does, and nobody has yet
checked what Wokwi does: **write down what you actually see**. Where Wokwi
and the chip disagree, you have found a fact about your simulator, which is
worth knowing.

Keep a scratch file open. You will want to paste dumps into it.

---

## Part 0 — Build it, run it, read the banner (15 min)

### Step 1. Build and simulate

```
build.bat
simulate.bat
```

Zero warnings, and:

```
Building 11_Faults_Watchdog_Power  [target c031c6] ...
           FLASH:       25340 B        32 KB     77.33%
             RAM:        5256 B        12 KB     42.77%
```

This folder **does** carry a `link.ld` — open it and find the two lines
marked `NEW`. Paste `diagram.json` into Wokwi (the app board: OLED, buttons A
and B, joystick, knob), upload `Main.elf`.

### Step 2. Read the boot banner

```
=== 11 Crash Lab - crash it on purpose, then make it survive ===
boot 1 since power-on   reset cause: ...   (RCC->CSR2 = 0x........: ...)
```

**Guess first:** on a real chip straight after power-on, which flags are set?

> On silicon, power-on sets `PWRRSTF` — slide 14. If your banner instead says
> **`RCC->CSR2 has NO reset flag set: this simulator does not model them`**,
> Wokwi's partial RCC model has told you something: from now on the
> `.noinit` record is your only witness. **Not yet watched — write down which
> you got.**

### Step 3. Look around

Type `list`, then `stats`. Press **A** a few times and watch the OLED's menu
line move. Note `stack ... used`, `heap ... used` and `CPU load` — you will
come back to all three.

---

## Part 1 — The unaligned load (15 min)

**Do this:** `crash 1`.

**Guess first:** does this line fault? It would not on a Cortex-M4.

```c
sink = *(volatile uint32_t *)((uintptr_t)words + 1u);
```

> On the M0+ it must (slide 9): Armv6-M has no unaligned word access at all.
> You should see one line from inside the handler, the reset, and then:
>
> ```
> *** HardFault  pc=0x080016EE  lr=0x...
> ...
>   code at pc: 681A ....
>   instruction: ldr r2, [r3, #0]
>   VERDICT:     UNALIGNED ACCESS
>   because:     ldr r2, [r3, #0]: 4-byte access to 0x2000000D, which is not a multiple of 4.
> ```
>
> The pc, the instruction and the verdict are **measured on the host** from
> these bytes and `r3 = 0x2000000D`. If instead the firmware prints
> *"unaligned load returned ... this core did NOT fault"*, your simulator
> forgives unaligned access (Renode does — slide 9). **Not yet watched.**

**Then:** paste the `addr2line` line the report printed into a terminal in
this folder. It names a line of `crash.c`. Open it. That is the whole job of
a fault reporter: **from a reset to a line number without a debugger.**

**Check the record survived:** the banner said `boot 2`. If it says `boot 1`
again, Wokwi's reset cleared RAM — `.noinit` cannot help in that simulator,
and you have learned why real products also keep a copy in flash. **Not yet
watched.**

---

## Part 2 — Where is memory, really? (15 min)

Run each, read each report, and fill in the table:

| `crash` | Guess | Verdict / what printed |
|---|---|---|
| 0 — read address 0 | | |
| 2 — read `0x60000000` | | |
| 3 — store to flash `0x08007F00` | | |
| 5 — call into RCC | | |

> **0** does not fault: it prints `0x20003000`, the initial stack pointer,
> because address 0 aliases flash (slide 12). Every C programmer is taught
> that NULL dereference crashes. **On this chip it does not.**
>
> **2** should report `NOTHING MAPPED THERE`, address `0x60000000` —
> recomputed from `r3`, where an M4 would have used `BFAR`.
>
> **3** is the open question of the lesson. If it faults, the verdict is
> `STORE TO READ-ONLY`. If it does not, the firmware prints what the flash now
> holds and `FLASH->SR`: find which error bit is set on slide 10's diagram.
> **Not yet watched, on Wokwi or silicon — what you see is the answer.**
>
> **5** reports `BAD INSTRUCTION FETCH`: pc = `0x40021000`, RCC's base.
> Peripheral space is Execute Never.

---

## Part 3 — Crashes that do not crash (10 min)

**Do this:** `crash 13` (divide by zero), then `crash 4` (a NULL callback).

**Guess first:** what does `100 / 0` print? And for the NULL call, what will
`addr2line` say about the stacked **pc**?

> `100 / 0 = 0`. There is no divide instruction to trap; libgcc's
> `__divsi3` calls `__aeabi_idiv0`, which is a bare `bx lr`, and returns 0 —
> traced by hand through this build's listing (slide 12). Your C code carries
> on with a wrong number. Not yet run — check that it really prints 0.
>
> The NULL call faults with **pc = 0x00000000** and `xPSR.T = 0`.
> `addr2line 0x00000000` says `??:0`. Now try the **lr** it printed
> (`0x0800173B`): it names the line in `crash.c` that made the call. **When pc
> is nonsense, read lr.**

**Make it trap:** `__aeabi_idiv0` is weak. Define your own in `Main.c` that
calls `__asm volatile("udf #0x0D")`. Rebuild, `crash 13`, and read the report:
which function does `lr` lead to now?

---

## Part 4 — Who feeds the dog? (25 min)

Slide 21's table was **measured on the host**. Now run its *soft dog* rows on
the board. For each mode — `wdmode hb`, `wdmode loop`, `wdmode isr` — run
`crash 11` (starve), wait 3 s; then `crash 9` (hang), wait 3 s. Reset by hand
(`reset`, or the Wokwi restart button) if nothing happens.

| `wdmode` | starve (11) | hang (9) | host prediction |
|---|---|---|---|
| `hb` | | | reset after ~1 s · reset after ~0.75–1 s |
| `loop` | | | **never** · reset after ~1 s |
| `isr` | | | **never** · **never** |

**Guess first:** in `loop` mode, during the starve, what does the board look
like?

> **Alive.** LD4 blinks, the serial port answers `stats` — and the OLED is
> frozen. `stats` even says `missing now: screen`. Nothing resets, because the
> loop keeps feeding. That is the failure a watchdog exists for, and the
> naive placement cannot see it.
>
> After each reset that *does* happen, read the post-mortem:
>
> ```
> ---- a WATCHDOG reset us (armed: soft timeout 1000 ms) ----
>   running: starve
>   main loop's last pass at ... ms; last check-ins:  blink -.. ms  input -.. ms  screen -.... ms
>   diagnosis: a JOB stopped while the loop kept running (starved)
> ```
>
> For the hang, every job is current: *the main LOOP itself stopped*.
> **Not yet watched.**

**Now the soft dog's blind spot:** `wdmode hb`, then `crash 10` — hang with
interrupts off.

> Host prediction: **never** — the soft dog counts in SysTick, and SysTick is
> off. The board is frozen for good. Restart the simulation.

**Then arm a real one:** after the restart, type `arm wwdg` and `crash 10`
again.

> On silicon the WWDG resets the chip in ≤ 699 ms (slide 18) and the banner
> says `WWDG`. Wokwi's page calls its WWDG "implemented, not tested yet" —
> **you are the test.** Write down what happens. Do **not** expect
> `arm iwdg` to do anything in Wokwi: its IWDG is not modelled (slide 19).

---

## Part 5 — The tightest-timeout contest (15 min)

**Do this:** `wdmode hb`. Then lower the dog: `wdt 600`, `wdt 400`, `wdt 300`
… After each, use the board normally for a minute — press buttons, type
`stats`, `list`, `report`.

**Guess first:** host prediction (slide 22) is 280 ms with no long prints,
460 ms with one 180 ms print. Which of your commands is the 180 ms print?

> `stats` shows `worst gap` — the longest the loop went without a feed since
> your last `wdt`. Every long `printf` holds the loop for 87 µs per character.
> `list` prints about 900 characters: ~80 ms on its own. The board resets the
> first time a command's print plus the slowest job (blink, 250 ms) exceeds
> the timeout.

**Score:** the lowest `wdt` that survives **five minutes** of anyone in your
row using the board, including `report` after a crash. Then fix the
firmware so a lower one survives — hint: what decides the slowest check-in?

---

## Part 6 — Crash detective: the packet bug (15 min)

A tester sent you this from a board running the **unmodified** build (check
yours: `arm-none-eabi-nm Main.elf | grep packet_sum` must print `08001684`):

```
---- last crash: #3, at 48211 ms of uptime ----
  r0  00000000  r1  00000000  r2  20000016  r3  2000001E
  r4  200007E8  r5  0000000C  r6  00000000  r7  00000000
  r8  00000000  r9  00000000  r10 00000000  r11 00000000
  r12 00000000
  sp  20002F48  lr  0800178D  pc  08001696  xPSR 81000000
  EXC_RETURN FFFFFFF9  = thread mode, main stack (MSP)
  code at pc: CA02 1840
```

(A constructed dump: the pc, lr, code and `r2`/`r3` are what this build
produces; the other registers are plausible fill.)

**Do this, without running anything on the board:**

1. `arm-none-eabi-addr2line -f -e Main.elf 0x08001696` — which function,
   which line?
2. Decode `CA02` with slide 7's table. What register is the address in, and
   what is its value?
3. Which of slide 8's five questions fires?
4. What is the bug, in one sentence of C? Fix it (two correct fixes exist —
   find both).

> 1. `packet_sum`, `crash.c` — the `sum += payload[i]` line.
> 2. `1100 1 010 00000010` = `ldmia r2!, {r1}`. Address `r2 = 0x20000016`.
> 3. Question 4: `0x20000016` is not a multiple of 4 — `UNALIGNED ACCESS`.
>    (`xPSR` 0x81000000: T = 1, thread mode, N set by the loop's `cmp`.)
> 4. `(const uint32_t *)(pkt + 2)` points two bytes into an aligned buffer.
>    Fix with `memcpy` into a `uint32_t` per word, or assemble each word from
>    bytes. Both are what `crash 12` should then survive.
>
> Run `crash 12` on the board and compare its dump with the one above.

---

## Part 7 — The mystery game (15 min)

**Do this:** `mystery`. After the reset the report shows only the raw dump:
`** MYSTERY: the verdict is hidden`. Decode it with slides 5, 7 and 8, then
`guess N` (`N` from `list`). `reveal` gives up.

Seven possible causes: 1, 2, 4, 5, 6, 7, 12. The score survives every crash
(it is in `.noinit`) until the simulation stops. **Get seven right in a
row.** The OLED shows your score bottom right.

> Tells, if you are stuck: `T = 0` in xPSR → 4. pc outside flash → 5. Code
> `DExx` → 6, `BExx` → 7. A load: compute the address — odd, or `0x6000_0000`?
> And `CA02` you met in Part 6.

---

## Part 8 — Sleep, and what load costs (10 min)

**Do this:** `stats` twice, a second apart. Note `CPU load` and
`wakeups/s`. Then `sleep off`, `stats` again.

**Guess first:** does the load change when the core stops sleeping?

> It should not — both modes measure the same thing, the time from "no work
> left" to the next tick, with SysTick's own counter (slide 25). What changes
> on silicon is that `sleep on` stops the core clock in that time: the
> current drops. No simulator here measures current. **Not yet watched**:
> also check `wakeups/s` — about 1000 (one per SysTick), plus one per
> received character.
>
> If the board **freezes right after the banner**, Wokwi's `WFI` does not
> wake on an interrupt held off by PRIMASK. Set `SLEEP_WITH_PRIMASK 0` in
> `Main.c` and rebuild — and write down that you needed to.

**Then:** make the screen job expensive — call `oled_invalidate()` at the top
of `job_screen()` so every frame resends all 1024 bytes. Rebuild. How much
did the load rise? Does Part 5's tightest timeout still survive?

---

## Part 9 — Your turn (open-ended)

Pick one:

- **The window.** In `wdog.c`, set the WWDG window `W` to `0x60` instead of
  `0x7F`. `arm wwdg`. Which `wdmode` now resets the chip for feeding *too
  early*? (Predict first: `isr` feeds every millisecond.)
- **A fourth job.** Add a job that reads the joystick at 50 Hz and checks in
  as `HB_STICK`. Then starve *it* — does the post-mortem name it?
- **Early warning.** Enable the WWDG's `EWI` and write `WWDG_IRQHandler` to
  save `crash_rec` with the pc of whatever was running (it is in the
  interrupt's stacked frame — slide 2). Now a hang tells you *where* it hung.
- **A real reporter.** Make `report` also print the 8 words *above* the
  stacked frame — the caller's saved registers — and find a second return
  address in them.

---

## Where to go next

- `RESET_ON_FAULT 0` in `crash.c` parks in the handler instead of resetting.
  Attach GDB (F5 in VS Code) and run `crash 1`: read the frame by hand with
  `x/8wx $sp`. The soft dog cannot bite while you look — HardFault blocks
  SysTick. What would the IWDG do?
- Lesson 07's kernel puts tasks on PSP. Link `os` and move the experiments
  into a task: does `EXC_RETURN` in the report change to `0xFFFFFFFD`?
