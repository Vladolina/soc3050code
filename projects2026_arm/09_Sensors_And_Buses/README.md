# 09 — Sensors and Buses

**Part 1, lesson 6 — the last of Part 1.**
Target: ST Nucleo-C031C6 (STM32C031C6, Cortex-M0+ at 48 MHz).

Three buses, three devices, one toy: an MPU6050 accelerometer on **I²C**
tilts a ball that a MAX7219 draws on an 8×8 matrix over **SPI**, and a
potentiometer on the **ADC** sets its brightness. `$IMU` frames go to lesson
08's `host.py`; a shell offers `scan` (an I²C bus scan), `who`, `vref`
(supply voltage from the factory-calibrated internal reference) and `stats`.

## Files

| | |
|---|---|
| `Slide.md` | the lecture - 24 pages as rendered: the title, the two pin-map slides, then the numbered slides, each register block's one-page map before its details |
| `Lab.md` | the lab: eight parts, ~2 hours, **nothing handed in** |
| `adc.c`, `adc.h` | **subject.** Start-up sequence (regulator, calibration, enable), `CCRDY`, VREFINT; every wait bounded |
| `i2c.c`, `i2c.h` | **subject.** I²C1 master on PB8/PB9: open drain, ST's `TIMINGR`, write + repeated START + read, NACK vs timeout, probe |
| `spi.c`, `spi.h` | **subject.** SPI1 master, 16-bit frames, 6 MHz, GPIO chip select, access width, `BSY` before CS |
| `Main.c` | MPU6050 and MAX7219 drivers, integer ball physics, sensor/telemetry/shell tasks, a bus mutex |
| `build.bat` | `LIBS=retarget os uart proto` |
| `simulate.bat`, `diagram.json`, `wokwi.toml` | pot on PA4; MPU6050 SCL PB8, SDA PB9; MAX7219 CLK PA5, DIN PA7, CS PB0 |

**Shared-code change:** `_lib/uart.*` and `_lib/proto.*` are lesson 08's, copied
once they had a second user. `08_UART_And_Python_Host/host.py` gained
`--chart NAME,INDEX[,MIN,MAX]` so it can chart this lesson's `$IMU` fields.

## Build and run

```
build.bat        # FLASH 11152 B / 32 KB, RAM 7536 B / 12 KB, zero warnings
simulate.bat
uv run ..\08_UART_And_Python_Host\host.py --file capture.txt --chart IMU,2,-16384,16384
```

## Facts checked against sources, not memory

| Fact | Source |
|---|---|
| `VREFINT_CAL` at `0x1FFF756A`, measured at 3000 mV; VREFINT = channel 10 | ST's `stm32c0xx_ll_adc.h` (note: `0x1FFF7568` is the *temperature* calibration) |
| regulator 20 µs; VREFINT 12 µs; 2 ADC cycles calibration→enable | same file: `LL_ADC_DELAY_*` |
| `TIMINGR = 0x0090273D` (400 kHz, I2CCLK 48 MHz) | ST's NUCLEO-C031C6 `I2C_TwoBoards_MasterTx_SlaveRx_Init` example, CubeMX-computed |
| probe = `SADD | START | AUTOEND`, `NBYTES = 0`, wait `STOPF`/`NACKF` | ST's `HAL_I2C_IsDeviceReady()` |
| I2C1 = PB8/PB9 AF6; SPI1 SCK PA5 / MOSI PA7 AF0; ADC IN4 = PA4 | Zephyr `hal_stm32` C031 pin table |
| every register-bit name used | `stm32c031xx.h` — all 39 checked present |
| MPU6050 at 0x68 (0x69 with AD0 high); `accelX`… sliders | Wokwi `wokwi-mpu6050` page |
| MAX7219 part pins and `parola`/`fc16` layouts | Wokwi `wokwi-max7219-matrix` page |

## Verified by execution — partly, in Renode

Renode (lesson 07's harness) models the C0's ADC, I²C and SPI blocks at their
addresses, but **not the MPU6050 or the MAX7219**. What it showed:

| Test | Result |
|---|---|
| boot | `ADC: calibrated, enabled`; `MPU6050: bus timeout`; MAX7219 13 commands sent; shell answers all four commands |
| MPU6050 absent | Renode is **silent** rather than NACKing, so the driver's **timeout** path ran — and the program carried on |
| driver pointed at Renode's **LSM6DSO** IMU at `0x6A` | **`WHO_AM_I = 0x6C`** — write, repeated START and read all correct against a real I²C device model |
| `vref` | calibration word reads 0 → refuses to divide by it |
| lab variants | "no wake" builds with `'mpu_write' defined but not used` (the lab uses the warning); "no CCRDY" builds silently |

**Two bugs this found in the lesson's own first draft, both fixed and now
taught:**

1. `delay_us()` timed itself with SysTick's `VAL`. Renode reported SysTick
   enabled but not counting before the kernel started, and it never returned.
   Replaced by a generous busy loop — the ADC's delays are minimums.
2. ADC waits allowed 200 000 polls (~20 ms). Renode's F0-derived ADC has no
   `CCRDY`, so every read timed out after a full 20 ms — longer than the sensor
   task's period — and it **starved every lower-priority task**. Now 2000
   polls, still ten times the slowest real step. Slide 17 tells this story.

**Two simulator limitations, recorded:** the missing `CCRDY` (pot reads `-3` in
Renode), and a zero-length probe of a *present* device **crashes Renode's
STM32 I²C model** with an internal `IndexOutOfRangeException` — the same probe
ST's HAL uses on silicon.

**Still to do:** watch it in Wokwi — the MPU6050 and MAX7219 have only ever
been exercised against their datasheets and Wokwi's documentation. First
questions: does the ball roll the right way (Lab Part 5), what does
`0x1FFF756A` read (Part 2), and does `scan` report exactly `0x68`?
