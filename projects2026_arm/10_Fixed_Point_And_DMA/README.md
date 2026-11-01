# 10 — Fixed Point and DMA

**Part 2, lesson 1 — Systems.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz, **no FPU**), on the app board.

How fast is maths on a chip with no floating-point unit? A **benchmark arena**
races the same seven jobs — add, multiply, divide, sine, a 2D rotation, a PI
controller step, a 16-tap FIR filter — in `int32`, **Q15**, **Q16.16** and
`float`, timed with SysTick as a cycle counter, and prints a leaderboard with
each format's flash cost. Then **DMA**: the ADC scans the joystick and knob
continuously into RAM through DMA1 + DMAMUX, and a memory-to-memory DMA copy
races `memcpy`. Wokwi does not implement DMA, so the firmware detects a DMA
that never moves and falls back to polled reads, saying which path it used.
The FPU half of the original syllabus survives as a compile-only comparison:
the same C disassembled for a Cortex-M4F (`vmul.f32`) and for this chip
(`bl __aeabi_fmul`).

*Re-scoped from the syllabus's "F446RE_FPU_DMA" (F446RE in Renode): Renode is
not installed and no STM32F4 device headers are vendored, so the lesson runs
on the C031C6 in Wokwi like every other lesson.*

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 32 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: nine parts (0–8), ~2 hours, **nothing handed in**; ends in "make the FIR 2× faster" |
| `fix.h`, `fix.c` | **subject.** Q16.16 and Q15: rounded multiplies, saturating add, 64-bit divide, a quarter-wave sine table. Pure C |
| `bench.h`, `bench.c` | **subject.** The arena's 22 workloads, `uint32_t fn(uint32_t n)` each, returning checksums. Pure C |
| `dma.h`, `dma.c` | **subject.** ADC scan via DMA1 channel 1 in circular mode (DMAMUX request 5), the stuck-DMA detector, memory-to-memory copy on channel 2 |
| `Main.c` | SysTick cycle counter, the leaderboard, budget arithmetic, the DMA demo, a live Q15-vs-float filter on the joystick, OLED race bars |
| `build.bat` | `LIBS=retarget adc i2c oled` — no `os`: this lesson owns SysTick |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board unchanged: joystick PA0/PA1, knob PA4, buttons PB4/PB5, OLED on PB8/PB9 |
| `host/test_fix.c` | accuracy of every format vs double, rounding bias, overflow cases, checksums |
| `host/m0sim.py` | a Cortex-M0+ instruction model that runs `Main.elf`'s workloads and counts cycles (stdlib only) |
| `host/flashcost.py` | flash cost per format from `arm-none-eabi-nm --size-sort -S` |
| `host/run.bat`, `host/run.sh` | build and run all three; non-zero exit on any failure |
| `host/kernel.c`, `host/fpu_compare.bat`, `.sh` | compile one C file for the M0+ and the M4F and disassemble both |

Buttons: **A** (PB4, key `a`) re-runs the arena; **B** (PB5, key `b`)
switches the inputs between DMA and polled reads. `USE_DMA 0` in `Main.c`
skips DMA entirely, should a simulator fault on its registers.

## Build and run

```
build.bat            # FLASH 27368 B / 32 KB, RAM 8016 B / 12 KB, zero warnings
simulate.bat
host\run.bat         # after build.bat: accuracy, cycle model, flash costs
host\fpu_compare.bat # M0+ vs M4F disassembly, compile only
```

83% of flash: the soft-float library (4292 B) and libm `sinf` (4742 B) are
over a quarter of the chip, which is one of the lesson's points. Removing the
`sinf` row from the arena gives back about 4.7 KB (Lab Part 5).

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| **Wokwi's C031C6 does not implement DMA** (nor IWDG, PWR, RTC, SYSCFG, DBG); SysTick, ADC, EXTI, USART, I²C are simulated | [docs.wokwi.com/parts/board-st-nucleo-c031c6](https://docs.wokwi.com/parts/board-st-nucleo-c031c6), fetched 2026-10-03 |
| DMAMUX request **ADC1 = 5**, **MEM2MEM = 0** | ST `stm32c0xx_ll_dmamux.h`: `LL_DMAMUX_REQ_ADC1 0x00000005U`, `LL_DMAMUX_REQ_MEM2MEM 0x00000000U` (STMicroelectronics/stm32c0xx-hal-driver on GitHub) |
| DMAMUX channel *n* → DMA1 channel *n*+1 | ST `stm32c0xx_ll_dma.h`, `LL_DMA_SetPeriphRequest()`: "DMAMUX channel 0 to 6 are mapped to DMA1 channel 1 to 7" |
| conversion = sampling + **12.5** ADC cycles at 12 bits | ST `stm32c0xx_hal_adc.h` |
| with DMA, overrun "is reported whatever overrun setting" | same file, `Overrun` field |
| `SCANDIR = 0` (forward) converts lowest channel first | ST `stm32c0xx_ll_adc.h`, `LL_ADC_REG_SEQ_SCAN_DIR_FORWARD` |
| longest sampling time 160.5 cycles (`SMP = 111`) | same file, `LL_ADC_SAMPLINGTIME_160CYCLES_5` |
| every register and bit name used (`DMA1_Channel1/2`, `DMAMUX1_Channel0/1`, `DMA_CCR_*`, `DMA_ISR_TCIF2/TEIF2`, `DMA_IFCR_CGIF1/2`, `RCC_AHBENR_DMA1EN`, `ADC_CFGR1_DMAEN/DMACFG/CONT/SCANDIR/CHSELRMOD`, `ADC_CHSELR_CHSEL0/1/4`, `ADC_ISR_OVR/EOS/EOC/CCRDY`, `ADC_CR_ADSTP`, `FLASH_ACR_PRFTEN`, `SysTick_*`) | grepped in `stm32c031xx.h` / `core_cm0plus.h`; every field position in the slides' ```regs``` blocks checked against the `_Pos`/`_Msk` defines |
| `RCC->AHBENR` has no separate DMAMUX gate (bits: DMA1EN, FLASHEN, CRCEN) | `stm32c031xx.h` |
| M4F code: `vmul.f32`, `vfma.f32`, `vdiv.f32`; `double` divide is `bl __aeabi_ddiv` on the M4F | **compiled** with the vendored toolchain, `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -Os`, disassembled with `objdump` |
| M0+ cycle table used by `m0sim.py` (ALU 1, LDR/STR 2, LDM/STM/PUSH/POP 1+N, POP-with-PC 3+N, taken branch 2, BL 3, BX/BLX 2) | the Cortex-M0+ TRM's instruction summary — **partly re-checked**: ARM's own "Introducing the Cortex-M0+" article lists ADDS/CMP 1, LDRB 2, taken BNE 2; developer.arm.com's TRM pages render by JavaScript and could not be fetched. The MULS "1 or 32 cycles" implementation option is confirmed in NXP's forum quoting ARM; **which one the C031 has is not confirmed** (st.com did not answer), hence `--mul` |
| flash wait state = 1 at 48 MHz; prefetch off by default | `_startup/c031c6/startup.c` `SystemInit()`; `FLASH_ACR_PRFTEN` in the header |
| newlib-nano `memcpy` is a byte loop | disassembly of this `Main.elf` |

## Verified by execution — on the host

Nothing in this lesson has been run on a board or in Wokwi. Three host
programs did run (`host\run.bat`, 2026-10-03):

**`test_fix` — accuracy vs double, 15 checks, all pass:**

| | measured |
|---|---|
| `q15_mul` / `q16_mul` max error | 0.47 / 0.48 LSB (rounded) |
| `q15_sin`, all 65 536 angles | 1.07 × 10⁻⁴ (3.5 LSB); `f_sin` 3.6 × 10⁻⁶; newlib-vs-double `sinf` 1.3 × 10⁻⁷ |
| rot2d Q15 / Q16.16 / float | 3.0 × 10⁻⁵ / 2.0 × 10⁻⁵ / 1.3 × 10⁻⁷ |
| PI step Q16.16 | 1.5 × 10⁻⁴ = 9.5 LSB — `Q16(0.01)` itself rounds |
| fir16 int32 / Q15 | 3.9 × 10⁻³ of full scale (8 LSB) / 2.7 × 10⁻⁵ (0.88 LSB) |
| mean error of 10⁶ Q15 products | rounded −0.0000 LSB, truncated **−0.4992 LSB** |
| overflow | `−1 × −1` → +32767; `0.75 + 0.5` wraps to **−0.75**, saturates to +0.99997; Q16 `300 × 300` saturates; `/0` saturates; gain-4 FIR's 32-bit accumulator wraps to −0.0004 (64-bit: +3.9996) |

**`m0sim.py` — this `Main.elf` on a Cortex-M0+ model, cycles per iteration
(0 wait states / crude 1 wait state):**

| job | int32 | Q15 | Q16.16 | float |
|---|---|---|---|---|
| loop (baseline) | 12 / 18 | | | |
| add | 15 / 21 | | 41 / 59 | 129 / 185 |
| mul | 15 / 21 | 34 / 49 | 104 / 144 | 162 / 233 |
| div | 62 / 92 | | 663 / 954 | 457 / 675 |
| sin | | table 58 / 86 | | `f_sin` 1669 / 2418, `sinf` 2982 / 4301 |
| rot2d | | 127 / 183 | 447 / 614 | 819 / 1177 |
| PI step | | | 356 / 493 | 784 / 1127 |
| fir16 | 241 / 350 | 302 / 449 | | 4271 / 6180 |

`memcpy` of 1 KB: 10 251 cycles (0 WS). **21 of 22 checksums are
bit-identical** to the PC's for the same C; the 22nd is `sinf`, where newlib
and MinGW are different implementations. With `--mul 32` (the small
multiplier), Q16.16 multiply rises to 290 and float multiply to 286.

**`flashcost.py`** (nm on this `Main.elf`): soft float 4292 B, libm `sinf`
4742 B, Q16.16 divide 604 B, int32 divide 470 B, Q15 sine 198 B, Q16.16
multiply 78 B.

**Lab Part 8 calibration**: a symmetric, unrolled `fir_q15` scored 178 cycles
with the checksum unchanged; symmetry alone, 295.

## Status

- **Builds with zero warnings**: FLASH 27 368 B, RAM 8016 B (also zero
  warnings with `USE_DMA 0`).
- **Host tests pass**: `test_fix` 15/15; `m0sim` 0 checksum mismatches;
  `fpu_compare` reproduces the slide 2 listings.
- **Not yet watched running in Wokwi.** The DMA half is **predicted** from
  RM0490 and ST's drivers and cannot be observed there at all, since Wokwi
  does not implement DMA. A real Nucleo is the only way to see it run.

When someone does run it in Wokwi, look first at:

1. **The `== DMA ==` section**: it should print `DMA NEVER MOVED … EXPECTED
   IN WOKWI` and carry on polled. If it prints `!! HardFault during: the DMA
   demo` instead, Wokwi faults on the DMA's address — set `USE_DMA 0` and
   record that.
2. **The arena's cycle counts against `host\run.bat`'s model** (Lab Part 2):
   do they match the 0-wait-state column, neither, or are they frozen? And
   every checksum should equal the model's, `sinf` included — the firmware and
   the model run the same machine code.
3. **The live line with the joystick at full deflection**: `fir16 q15` and
   `float` should agree (scaled to the same units), and the OLED should show
   three race bars and `POLL`.
