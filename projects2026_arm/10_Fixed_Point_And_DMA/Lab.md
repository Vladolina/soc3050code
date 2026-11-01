# Lab 10 — Fixed Point and DMA

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A race, then a heist.** First you race number formats on a chip with no
FPU: guess how slow `float` is, measure it three ways, break fixed point on
purpose, and pick the cheapest format that is still right. Then you try to
take work off the CPU with DMA — in a simulator that does not have DMA — and
watch the program notice.

Each part says **Do this**, asks you to **guess**, then explains **what you
see and why**. Nothing is marked or collected. Some predictions come from the
**host tests** (`host\run.bat`), which really ran; some are **Wokwi
predictions nobody has watched yet** — each one says which.

**Put every change back before the next part** unless the part says otherwise.

---

## Part 0 — Build it, run it, read the scoreboard (20 min)

### Step 1. Build and simulate

```
build.bat
simulate.bat
```

Zero warnings, and:

```
Building 10_Fixed_Point_And_DMA  [target c031c6] ...
           FLASH:       27368 B        32 KB     83.52%
             RAM:        8016 B        12 KB     65.23%
```

83% of flash. Remember that number; Part 5 explains where a quarter of it
went.

Paste this folder's `diagram.json` into Wokwi's `diagram.json` tab (the app
board: joystick, knob, buttons, OLED), then `F1` → **Upload Firmware and
Start Simulation…** → `Main.elf`.

### Step 2. Read the banner, then the arena

```
== SOC3050 lesson 10: Fixed Point and DMA ==
Cortex-M0+ at 48000000 Hz, no FPU.  SysTick cycle counter: counting
ADC: ready.  OLED: found at 0x3C

== THE ARENA ==  seed 2026, 64 iterations, best of 3, DMA scan off
  job    format      cyc/iter   net    x winner   checksum
  loop   baseline      ...
  ...
```

**Check the second line first.** If it says `FROZEN`, SysTick does not count
in your simulator and nothing can be timed — write that down; it is a
finding.

### Step 3. Run the host side too

```
host\run.bat
```

> It prints three things: accuracy against `double` (15 checks, all
> `[ ok ]`), the **Cortex-M0+ cycle model's** leaderboard computed from the
> same `Main.elf`, and the flash each format costs. The model's last line:
>
> ```
> PASSED: 0 checksum mismatch(es) between the M0+ model and the PC
> ```
>
> Every workload's checksum in the firmware's table should match the
> model's and the PC's — **check three of them**. `sin / libm sinf` is the
> one allowed to differ between the PC and the others (different `sinf`
> code); the firmware and the model run the *same* `sinf` and must agree.

---

## Part 1 — Guess first, then look (15 min)

**Do this before reading the arena output** — cover it with your hand.

Fill in your guesses for cycles per iteration (the int32 add loop is
**15**, to calibrate you):

| job | int32 | Q15 | Q16.16 | float |
|---|---|---|---|---|
| mul | | | | |
| div | | — | | |
| sin | — | | — | `f_sin`: |
| fir16 | | | — | |

**Now look.** Score yourself: 1 point for every guess within a factor of 2.

> Host model values (`host\run.bat`, 0 wait states): mul 15 / 34 / 104 / 162,
> div 62 / — / 663 / 457, sin Q15 58 and `f_sin` 1669, fir16 241 / 302 / — / 4271.
>
> Most people guess float too *cheap* and Q16.16 division too cheap. The
> M0+ has no divide instruction, and Q16.16 division needs a 64-bit divide —
> **it loses to float** (slide 11). Most people also guess the FIR's float
> penalty at 2–3×; it is 14× Q15, because the FIR is 16 multiplies *and* 16
> adds, each a library call.

---

## Part 2 — Three witnesses (15 min)

You now have the same leaderboard from up to three sources: the **host
model**, **Wokwi's** SysTick, and — if your group has one — a **real
Nucleo**.

**Do this:** copy five rows (`loop`, `mul float`, `div Q16.16`, `sin f_sin`,
`fir16 Q15`) into a table, one column per witness.

**Guess first:** will Wokwi's numbers match the model's 0-wait-state column,
its 1-wait-state column, or neither?

> **Not yet watched — this is a real open question.** Possibilities, and what
> each would mean:
>
> - **Matches the 0WS column**: Wokwi counts cycles from an M0+ timing table
>   and ignores flash wait states.
> - **Matches neither, but the ratios match**: it counts instructions, or uses
>   its own timings — rankings are still right, absolute numbers are not.
> - **All rows identical, or 0**: SysTick advances by something other than
>   CPU cycles.
>
> Silicon will be *above* the 0WS column — the flash needs one wait state at
> 48 MHz — and probably below the crude 1WS column, because the M0+ fetches
> 32 bits at a time. **Write down what you found and tell the instructor.**
> Nobody knows the Wokwi answer yet; you are the first to look.

**Then try:** in `startup.c`'s spirit, enable the flash prefetch buffer at the
top of `main()`:

```c
FLASH->ACR |= FLASH_ACR_PRFTEN;
```

On silicon this should pull the numbers towards the 0WS column. In Wokwi,
does anything change at all? (Unwatched.)

---

## Part 3 — Break the rounding (10 min)

In `fix.h`, comment out the rounding line of `q15_mul`:

```c
    /* p += 1 << 14; */
```

Run `host\run.bat`.

**Guess first:** which checks fail?

> **Measured on the host** (this is what the test is for):
>
> ```
>   [FAIL] q15_mul within half an LSB (rounded)
>    round to nearest  mean error -0.4992 LSB
>    truncate (>> 15)  mean error -0.4992 LSB   <- always low: a drift
>   [FAIL] rounded products have no bias
> FAILED: 2 check(s) failed
> ```
>
> The "rounded" multiply now has exactly the truncating one's bias. Every product is now biased low by half an LSB.
> One multiply, nobody could tell. A thousand of them in an integrator, and
> the output walks downwards with no input at all.
>
> The arena's checksums for `mul Q15` and `rot2d Q15` change too: the
> firmware is computing different answers, a fraction of an LSB each.

Put the line back.

---

## Part 4 — Break Q15 with overflow (15 min)

The live filter converts the joystick's 12-bit reading to Q15 by shifting it
to the top of 16 bits:

```c
ring_q[...] = (q15_t)(((int32_t)v[0] - 2048) * 16);
```

`(4095 − 2048) × 16 = 32752` — just inside `int16_t`. Now "gain it up" by
changing `16` to `32`.

**Guess first:** push the joystick fully left and hold it. What does the
`fir16 q15` value on the serial line do? What does the `float` value beside
it do?

> **Wokwi prediction, not yet watched.** Near the centre both agree (they
> are the same filter, slide 19). Past half deflection the Q15 sample
> exceeds 32767, the cast **wraps**, and a big positive reading becomes a big
> **negative** one: the Q15 output swings to the opposite sign while the float
> one keeps rising. That is slide 9's table, live.
>
> Now fix it properly: replace the cast with `q15_sat32(((int32_t)v[0] - 2048) * 32)`.
> Full deflection now **clips** at +32767 instead of flipping. Clipping is
> still wrong — but wrong in the safe direction.

**Bonus:** `host/test_fix.c` part 3 already shows the same thing with a gain-4
filter. Change its `* 4` to `* 2`. Does the 32-bit accumulator still
overflow? (Work it out from slide 10 before you run it: 16 products of up to
2³⁰ each, coefficients summing to 2.)

Put everything back.

---

## Part 5 — The cheapest format that is good enough (20 min)

Your client gives you three jobs and an accuracy target for each. For each,
choose the **cheapest** format (fewest cycles, from `m0sim`) whose **max
error** (from `test_fix`'s accuracy table) meets the target.

| job | target max error | your choice | cycles | error |
|---|---|---|---|---|
| sine for a servo angle | 1 × 10⁻³ | | | |
| rotate a point on the OLED (128 px) | half a pixel of 128: 4 × 10⁻³ | | | |
| PI controller, 0.01% accuracy | 1 × 10⁻⁴ | | | |

> From the host tables: the sine is the **Q15 table** (1.1 × 10⁻⁴, 58
> cycles) — 51× cheaper than `sinf`. The rotation is **Q15** (3 × 10⁻⁵, 127
> cycles). The PI controller is the trap: Q16.16's error is **1.5 × 10⁻⁴**
> (9.5 LSB) because `dt = 0.01` itself rounds to 0.009995 in Q16.16. It
> **fails** the target, so the answer is **float** (1.2 × 10⁻⁷, 784 cycles)
> — and slide 18 says 784 cycles at 500 Hz is 0.8% of the CPU. Fine.

**Flash, too.** Comment out the `{ "sin", "libm sinf", b_sin_libm },` row in
`bench.c`'s table (and add `__attribute__((unused))` to `b_sin_libm`, or the
build warns). Rebuild.

**Guess first:** how many bytes does the build shrink by?

> `host\flashcost.py` says libm `sinf` brings in **4742 bytes**; expect the
> build to shrink by about that much (a few bytes either way for the table
> row and the loop itself). Nothing else in the program called `sinf`, so
> `--gc-sections` drops the whole chain.

**Mini-challenge:** `x / 10` in Q16.16 costs a 64-bit divide. Write
`q16_mul(x, Q16(0.1))` instead. How many cycles did you save (slide 11), and
what is the error? `Q16(0.1)` is 6554, not 6553.6.

---

## Part 6 — DMA: on, off, and missing (20 min)

Scroll back to the `== DMA ==` section of the boot output.

**Guess first:** Wokwi's documentation for this board lists DMA as **not
implemented**. What will the program print?

> **Wokwi prediction, not yet watched** — but it is what the documentation
> implies:
>
> ```
>   DMA1_Channel1->CCR   = 0x........  (expect 0x000025A1)
>   ...
>   scan: DMA NEVER MOVED - buffer still 0xFFFF after the time limit.
>         EXPECTED IN WOKWI: its C031C6 model does not implement DMA (docs.wokwi.com).
>         On a real Nucleo this line says RUNNING.  Falling back to POLLED adc_read().
>   one 3-channel read: DMA 0 cycles, polled ... cycles
>   1 KB copy: memcpy ... cycles; DMA ... cycles (NEVER COMPLETED, data WRONG) ...
> ```
>
> **Write down what the register lines read back.** If Wokwi ignores writes
> to an unimplemented block, they read `0x00000000`; if it does not, they read
> what was written — and *still* the buffer never changes, because there is no
> engine behind the registers. Either way, **the sentinel test is what tells
> you**, not the register values. That is slide 24.
>
> If instead you see `!! HardFault during: the DMA demo` and nothing more, the
> simulator faults on the DMA's address. Set `USE_DMA` to `0` in `Main.c` and
> carry on — and tell the instructor, because that is a finding too.

**Do this:** press **B** (key `b`). It tries `scan_start()` again.

> In Wokwi: `DMA never moved - still POLLED`, every time. The live line keeps
> showing `POLL` and a read cost of roughly a thousand cycles: three
> conversions of ~7.2 µs each, which the CPU spends **waiting**.

**On a real Nucleo** (if your group has one), the same build should say
`RUNNING`, show `scan_buf` values that follow the joystick, read
`CNDTR` as 1, 2 or 3 on the live line, and report a DMA read cost of a few
dozen cycles against the polled thousand. Pressing **B** then toggles between
the two, and the live line's `read ... cyc` shows the difference. Slide 23b
predicts the numbers; nobody has measured them yet.

**Think:** the stuck-DMA test fills the buffer with `0xFFFF` because a 12-bit
ADC can never write it. What sentinel would you use for an 8-bit SPI
receive buffer, where every byte value is possible?

---

## Part 7 — Read the FPU you do not have (15 min)

```
host\fpu_compare.bat
```

It compiles `host\kernel.c` twice — for this chip and for a Cortex-M4F — and
disassembles both. Compare `pi_step` in the two listings.

**Guess first:** add a **division** to `kernel.c`:

```c
float ratio(float a, float b) { return a / b; }
```

What do the two CPUs emit?

> **Measured** (compile only): the M4F emits a single `vdiv.f32` — its FPU
> has a divider. The M0+ emits `bl __aeabi_fdiv`, the 457-cycle routine from
> slide 11.
>
> Now try `double` instead of `float`: `double ratio(double a, double b)`.
> The M4F's FPU is **single precision only** (`fpv4-sp`, *sp*), so it is back
> to a library call too: `bl __aeabi_ddiv`. An FPU is not a free pass —
> `double` on an M4F is as soft as `float` on an M0+.

**Also try** the cycle model with the *small* multiplier option:

```
python host\m0sim.py Main.elf --mul 32 --ws 0
```

> **Measured on the host:** with a 32-cycle `MULS`, Q16.16 multiply rises
> from 104 to **290** cycles and float multiply to **286** — fixed point's
> advantage vanishes, because `__aeabi_lmul` does four multiplies. The FIR's
> int and Q15 versions rise from 241 / 302 to 737 / 798. Whether a chip has
> the fast multiplier changes which format wins.

---

## Part 8 — Make the FIR 2× faster (open-ended)

**The challenge:** make `fir_q15()` (or `fir_float()`) at least **twice as
fast** while computing **exactly the same answers**.

Scoring, all on the host so everyone's numbers are comparable:

```
build.bat
host\run.bat
```

- **Speed:** `fir16 Q15` cycles in the m0sim table (0WS column). Start:
  **302**. Target: **under 200**. Stretch goal: **151**, a true 2x.
  For calibration, **measured on the host**: ideas 1 and 2 below together
  (symmetry, plus an unrolled path for windows that do not wrap) scored
  **178** with the checksum intact. Symmetry alone scored only 295 - the
  loop overhead, not the multiplies, was the cost. 151 needs more than that.
- **Correctness:** the checksum must still be `0000F3C3`, and the run must
  end `PASSED`. A fast wrong filter scores zero.

Ideas, roughly in order of payoff:

1. **The coefficients are symmetric** — `h[j] == h[15 − j]`. Add the two
   samples first, multiply once: 8 multiplies instead of 16. The answer is
   bit-identical, because integer addition is exact.
2. **The `& 63` on every index** costs an `ands` per tap. Keep the ring twice
   as long and write each sample twice, so a window never wraps.
3. **Unroll** the loop: no counter, no branch per tap.
4. **The float FIR** (4271 cycles): the same symmetry halves the
   `__aeabi_fmul` calls — but are the answers still bit-identical? (Float
   addition is not associative. Run the checksums and find out.)

**Bonus round — beat `memcpy`.** newlib-nano's `memcpy` copies one byte at a
time: **10 251 cycles** for 1 KB in the host model (slide 25). Write
`copy_words(uint32_t *d, const uint32_t *s, uint32_t n)` and time it in
`dma_demo()`. How close to 1 cycle per byte can you get with `ldm`/`stm`?
(That is also the number a working DMA has to beat.)
