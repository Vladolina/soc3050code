# 11 — Faults, Watchdog, Power: the Crash Lab

**Part 2, lesson 2 — Systems.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz), in Wokwi, on
the app board. (The syllabus planned this lesson for an F446RE in Renode;
neither the F4 headers nor Renode are part of this course's toolset, so it
runs on the C031C6 like every other lesson — which turns its subject from
"read `CFSR`" into "what to do on a core that has no `CFSR`".)

*Crash it on purpose — then make it survive.* Fourteen deliberate failures,
picked with button A and fired with B (or `crash N` on serial): unaligned
load, wild read, store to flash, NULL callback, a jump into peripheral space,
`udf`, `bkpt`, stack overflow, two kinds of hang, a starved job, a realistic
packet-parsing bug, divide by zero, and a **mystery** crash whose verdict is
hidden until you guess it. A naked `HardFault_Handler` saves the evidence into
a `.noinit` flight record that survives the reset; after the reboot `diag.c`
decodes the instruction at the stacked pc and names the cause — the job
`CFSR` does in hardware on an M4. A watchdog fed by a **heartbeat bitmap**
catches a hang and a job that merely stopped; the idle loop sleeps in `WFI`
and measures CPU load with SysTick. A slide compares ST's HAL with one CMSIS
store.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 33 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: ten parts, ~2 hours, **nothing handed in** |
| `Main.c` | the superloop (blink, input, screen jobs), heartbeat feeding, `WFI` idle + load, shell, OLED menu, boot report, the detective game |
| `crash.c`, `crash.h` | **subject.** Naked `HardFault_Handler`, the `.noinit` record (magic + checksum), printf-free fault output, stack paint + canary, the experiments |
| `diag.c`, `diag.h` | **subject, pure C.** Armv6-M Thumb decoder, memory map, the five-question verdict, reset-cause names, SysTick arithmetic |
| `wdog.c`, `wdog.h` | **subject.** Soft dog (SysTick), WWDG and IWDG drivers behind one `dog_feed()`; pure timeout arithmetic and the heartbeat bitmap in the header |
| `link.ld` | the shared one plus a `.noinit (NOLOAD)` section and `_stack_floor` |
| `host/test.c`, `host/run.bat`, `host/run.sh` | host verification, plain gcc — see below |
| `build.bat` | `LIBS=retarget pad adc i2c oled`; shared `startup.c` |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | the app board, unchanged from `_targets/app-board-diagram.json` |

No shared file was changed. `link.ld` is a lesson copy because the shared one
has no `.noinit`; the build engine prefers a lesson's own.

## Build and run

```
build.bat        # FLASH 25340 B / 32 KB, RAM 5256 B / 12 KB, zero warnings
simulate.bat
host\run.bat     # or host/run.sh - after build.bat, so objdump has a Main.elf
```

RAM layout from this build (`nm`): `.noinit` 160 B at `0x200007E8`, heap
1 KB, `_stack_floor` `0x20000C88`, so 9080 B of stack. Flash is mostly
newlib-nano's `printf`/`snprintf` and 8 KB of strings; there is ~7 KB left.

`Main.c`'s `SLEEP_WITH_PRIMASK` (default 1) selects the idle loop of slide 25;
0 is the fallback if a simulator's `WFI` does not wake with PRIMASK set. Both
build with zero warnings (25280 B with 0). `crash.c`'s `RESET_ON_FAULT 0`
parks in the handler for GDB instead of resetting.

## Which watchdog you will see

Wokwi's Nucleo-C031C6 page lists **IWDG not implemented**, **WWDG
"Implemented, not tested yet"**, PWR, DBG, RTC, SYSCFG and DMA not implemented,
RCC partial. So:

| | armed | Wokwi | silicon |
|---|---|---|---|
| soft dog (SysTick counter → `NVIC_SystemReset()`) | at boot, 1000 ms | **bites** — the one students see | bites unless interrupts stop |
| WWDG | `arm wwdg` (≤ 699 ms) | untested by Wokwi: Lab Part 4 tests it | bites |
| IWDG | `arm iwdg` | **never bites** | bites — the one to ship |

The IWDG and DBG registers are touched **only** by `arm iwdg`, never at boot,
so an unmodelled peripheral cannot stop the lesson from starting. If `RCC->CSR2`
reads zero after a reset (impossible on silicon), the boot report says the
simulator does not model reset flags and relies on `crash_rec.reset_why`,
which the firmware writes before every reset it causes. Lesson 19 reaches the
same conclusion: with no IWDG in Wokwi, a spinning task would freeze the
simulation for good — here experiment 10 (hang, interrupts off) does exactly
that unless the WWDG is armed and Wokwi's WWDG works.

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| Wokwi C031C6: IWDG ❌, WWDG 🟡 "Implemented, not tested yet", PWR ❌, DBG ❌, RCC 🟡, core/SysTick/GPIO/USART/I2C/ADC ✔ | docs.wokwi.com/parts/board-st-nucleo-c031c6 (fetched) |
| IWDG keys `0xCCCC` start, `0x5555` unlock, `0xAAAA` reload; init order start → unlock → PR, RLR → wait SR → reload | ST `stm32c0xx_hal_iwdg.h` / `HAL_IWDG_Init()` (STM32CubeC0 HAL on GitHub) |
| "Once the IWDG is started, the LSI is forced ON and both cannot be disabled"; "~125us / ~32.7s" @ 32 kHz; DBG_IWDG_STOP | `stm32c0xx_hal_iwdg.c` header comment |
| IWDG prescaler `PR` 0..6 = /4../256 | `IWDG_PRESCALER_*` in `stm32c0xx_hal_iwdg.h` |
| WWDG clock = PCLK1/(4096 × Prescaler), timeout = (T[5:0]+1)/clock; reset at 0x40→0x3F; cannot be disabled; EWI at 0x40; init writes CR = WDGA\|T then CFR | `stm32c0xx_hal_wwdg.c`; `WWDG_PRESCALER_*` = 2^WDGTB in `stm32c0xx_hal_wwdg.h`. (Its own "typical values" line, 342 ms at 32 MHz /128, does not match its formula — 1.05 s; the formula is used.) |
| Low-power modes: Sleep / Stop 0 / Standby / Shutdown, their clocks, exits and SRAM retention; `HAL_PWR_EnterSTOPMode` sequence | `stm32c0xx_hal_pwr.c` |
| `LPMS`: STOP0 = 000, STANDBY = 011, SHUTDOWN = 100 | `stm32c0xx_ll_pwr.h` |
| `HAL_GPIO_WritePin` body (quoted verbatim on slide 26) | `stm32c0xx_hal_gpio.c`, © 2022 STMicroelectronics |
| `RCC_CSR2` `LPWRRSTF` 31 … `OBLRSTF` 25, `RMVF` 23 | `stm32c031xx.h`; `Main.c` `_Static_assert`s `diag.h`'s copies against it |
| every other register/bit used: IWDG `KR PR RLR SR`(PVU RVU WVU), WWDG `CR`(T WDGA) `CFR`(W EWI WDGTB), `RCC_APBENR1_WWDGEN/DBGEN`, `DBG->APBFZ1` `DBG_IWDG_STOP`, `FLASH->SR` bits, `USART_ISR_RXNE_RXFNE`, `USART_CR1_RXNEIE_RXFNEIE`, `USART_ISR_ORE`/`ICR_ORECF`, `PWR_CR1_LPMS/FPD_STOP/FPD_SLP` | `stm32c031xx.h` — all present |
| `SCB_SCR_SLEEPDEEP/SLEEPONEXIT/SEVONPEND`, `SCB_ICSR_PENDSTSET`, `xPSR_T_Pos = 24` | `core_cm0plus.h` |
| no `CFSR`, `HFSR`, `MMFAR`, `BFAR` on the M0+; their bit positions for slide 4 | `core_cm0plus.h` (0 occurrences); `core_cm4.h` `SCB_CFSR_*_Pos`, `SCB_HFSR_*_Pos` |
| `__MPU_PRESENT 1` on the C031 | `stm32c031xx.h` |
| base addresses: flash `0x08000000`, SRAM `0x20000000`, peripherals `0x40000000`, IOPORT `0x50000000`, RCC `0x40021000` | `stm32c031xx.h` |
| Armv6-M: every unaligned halfword/word access faults; BKPT without a debugger → HardFault; WFI wakes on a pending interrupt with PRIMASK set; a fault during HardFault (including its entry) → lockup; xPSR bit 9 = stack realigned; EXC_RETURN `0xFFFFFFF1/9/D` | Arm's Armv6-M Architecture Reference Manual and Cortex-M0+ Generic User Guide — **from the documents, not re-fetched here** (developer.arm.com does not render for a fetcher) |
| `100 / 0` → 0 via `__aeabi_idiv0` = `bx lr` | this build's listing, traced by hand (slide 12) — **not executed** |
| HAL vs CMSIS instruction counts (11 vs 4) | `HAL_GPIO_WritePin` compiled with this course's gcc and flags, `objdump` |

## Verified by execution — on the host

`host/run.bat` (or `run.sh`) compiles `host/test.c` with `diag.c` and
`wdog.h` using plain gcc, disassembles `Main.elf` with the vendored objdump,
and runs **107 checks — all pass**:

| Test | Result |
|---|---|
| decoder vs objdump over the whole `Main.elf` | 6618 16-bit instructions; **2327** loads/stores/push/pop/ldm/stm/bx/blx/udf/bkpt/svc compared as text; **0 mismatches** |
| verdicts for 21 hand-built frames, 8 of them the experiments' real bytes, pcs and registers from this build | all correct: unaligned `0x2000000D`, unmapped `0x60000000`, read-only `0x08007F00`, Thumb bit (pc 0), bad fetch (`0x40021000`), `udf #11`, `bkpt 0x0011`, `ldmia r2!, {r1}` at `0x20000016` |
| reset-cause naming, 6 `CSR2` values | correct priority (IWDG over SFT over PIN) |
| IWDG `iwdg_pick()`, every ms 1..32 768 | never early, ≤ 1 tick late, finest prescaler; 125 µs and 32.768 s limits exact |
| WWDG `wwdg_pick()`, every ms 1..699 at 48 MHz | never early; maximum 699.05 ms; 1000 ms clips to it |
| feeding policy (heartbeat / every loop / SysTick) × dog (soft / hardware) × failure (hang / hang irq-off / starve), 60 s at 1 ms, timeout 1000 ms | only heartbeat + hardware dog catches all three; heartbeat + soft misses only irq-off; loop feeding never sees a starved job; SysTick feeding catches nothing except (hardware) irq-off; no false resets |
| tightest safe heartbeat timeout | **280 ms** (worst gap 275 ms) with no long prints; **460 ms** (gap 455 ms) with one 180 ms print |
| `systick_elapsed()` | 4 cases including the reload wrap |

**Not executed anywhere:** the firmware itself. The handler's assembly, the
record surviving `NVIC_SystemReset()`, the shell, the OLED screen and the load
figure have been built and read, not run.

## Status

Builds with **zero warnings** (FLASH 25340 B, RAM 5256 B). Host test: 107
checks pass (numbers above). **Not yet watched running in Wokwi.** The first
three things to look at when someone does:

1. **Does `.noinit` survive the reset?** After `crash 1`, the banner must say
   `boot 2` and print the decoded report. If it says `boot 1` with no report,
   Wokwi clears RAM on `NVIC_SystemReset()` and the flight recorder needs
   another home. Note also what `RCC->CSR2` reads.
2. **Which faults does Wokwi raise?** Experiments 1 (unaligned), 2 (wild
   read), 3 (store to flash) and 5 (fetch from RCC) — each either resets with
   a report or prints that it did not fault. Record which; Renode, for
   comparison, does not fault on unaligned access.
3. **Does the idle loop survive `WFI` with PRIMASK set, and does the soft dog
   bite?** If the board freezes right after the banner, set
   `SLEEP_WITH_PRIMASK 0`. Then `crash 9`: a reset after ~1 s with the
   *"main LOOP itself stopped"* post-mortem. And `arm wwdg` + `crash 10` tests
   Wokwi's untested WWDG.
