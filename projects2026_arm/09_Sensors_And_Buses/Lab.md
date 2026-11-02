# Lab 09 — Sensors and Buses

**SOC3050 ARM Edition · Nucleo-C031C6 · allow 2 hours**

**Reference**: [RM0490, STM32C0x1 Reference Manual](https://www.st.com/resource/en/reference_manual/rm0490-stm32c0x1-advanced-armbased-32bit-mcus-stmicroelectronics.pdf) ·
**Board**: [ST Nucleo-C031C6 on Wokwi](https://docs.wokwi.com/parts/board-st-nucleo-c031c6) ·
**Board manual**: [UM2953, STM32 Nucleo-64 boards (MB1717)](../_docs/UM2953_Nucleo64_MB1717.pdf)

---

## What this lab is

**A guided walkthrough, not a test.** Three devices on three buses: a
potentiometer on the ADC, an MPU6050 on I²C, a MAX7219 matrix on SPI. You will
tilt the board and watch a ball roll, scan a bus, break each driver in the way
that returns *plausible* data rather than an error, and chart the sensor in
Python.

**Renode has none of these three devices**, so unlike lessons 07 and 08 most
answers here are **predictions** — what silicon and Wokwi's device models
should do. Where Renode did show something, the part says *measured*. Write
down what Wokwi actually does; it is the first observation anyone will have
made.

---

## Part 0 — Build it, tilt it (20 min)

```
build.bat
simulate.bat
```

Zero warnings:

```
           FLASH:       11152 B        32 KB     34.03%
             RAM:        7536 B        12 KB     61.33%
```

Paste `diagram.json` — a potentiometer, an MPU6050, a MAX7219 matrix — and
upload `Main.elf`. The banner reports each device:

```
  ADC      : calibrated, enabled
  MPU6050  : WHO_AM_I 0x68, awake
  MAX7219  : initialised over SPI (it sends nothing back to check)
```

A lit dot sits on the matrix. **Click the MPU6050**: sliders appear for
acceleration. Drag `accelX` to 0.5 g.

**Guess first:** how long does the ball take to reach the wall?

> The code gives it `16384 / 2 / 1024 = 8` units (8/256 px) of acceleration per
> 20 ms step, with friction taking 1/16 of the velocity each step, so speed
> levels off near 128 units — half a pixel per step, 25 px/s. Crossing four
> pixels takes a few tenths of a second. It bounces and settles against the
> wall. If it goes the *wrong way*, or along the other axis, see Part 5.

Turn the **potentiometer**: the matrix dims and brightens (intensity =
`pot >> 8`, 0..15).

---

## Part 1 — Scan the bus (15 min)

Type `scan` in the serial monitor.

> Predicted: `I2C scan, 0x08..0x77: 0x68` — one device, the MPU6050.
>
> Each address was offered to the bus with nothing else — an address byte, then
> STOP — and only 0x68 acknowledged. ST's own `HAL_I2C_IsDeviceReady()` does
> exactly this.

Now open `diagram.json`, add a wire from the MPU6050's `AD0` pin to
`nucleo:3V3.1`, paste, restart, and scan again.

> Predicted: `0x69`. The MPU6050 reads AD0 at power-up and moves its address,
> so that two can share one bus. The banner now says
> `no ACK at 0x68 - not connected?` — the driver asked the right question of
> the wrong address. The ball does not move.
>
> This is the most common I²C fault in practice: a working device at an
> address the code does not expect. `scan` finds it in one line.

Remove the wire.

---

## Part 2 — What is VDDA, really? (10 min)

Type `vref`.

> **Measured in Renode:** `cal 0` and *no usable factory value here*. Renode
> has nothing at `0x1FFF756A`, where ST's factory writes VREFINT's calibration.
>
> **In Wokwi** — nobody has checked. There are three possibilities: a real-
> looking value and a VDDA near 3300 mV; `0` or `0xFFFF` and the same refusal
> as Renode; or a fault, if that address is not mapped at all (the board then
> stops — lesson 07's HardFault reporter is not in this program). Whichever it
> is, write it down.
>
> On a real Nucleo this prints the supply voltage to a few millivolts. It is
> how battery-powered devices know their own battery.

---

## Part 3 — Forget to wake the sensor (10 min)

In `mpu_init()`, replace the last line so it never clears SLEEP:

```c
    return I2C_OK;               /* was: return mpu_write(MPU_PWR_MGMT1, 0x00); */
```

**Guess first:** what does the banner say, and does the ball move?

> The build now warns — **measured**: `'mpu_write' defined but not used`. The
> only write this program ever sends to the MPU6050 was the wake-up; the
> compiler noticed it had gone. Read warnings: this one is the whole bug.
>
> Predicted at run time: the banner still says `WHO_AM_I 0x68, awake` — the driver *thinks*
> it woke it. Every transfer is acknowledged; `stats` shows no I²C errors. And
> the ball never moves, because a sleeping MPU6050 returns **zeros** for every
> axis.
>
> A perfectly connected, perfectly answering, perfectly useless sensor. The
> MAX7219 does the same thing (it starts in shutdown). After any chip's reset,
> look for its power-down bit.

Put it back.

---

## Part 4 — Start the ADC too soon (10 min)

In `adc_read()`, delete the line that waits for `CCRDY`:

```c
    if (wait_set(&ADC1->ISR, ADC_ISR_CCRDY)) { return -3; }
```

> With one channel in use you may see no difference at all — the "old" channel
> and the new one are the same. The failure appears when you alternate: type
> `vref`, then `stats`, several times. Predicted on silicon: the first reading
> after a channel change can come from the **previous** channel — a
> potentiometer value that looks like VREFINT, or the reverse.
>
> A wrong channel returns a perfectly valid-looking number. That is why the
> reference manual's "wait for CCRDY" is not optional, and why it is worth
> testing with *changing* inputs, not one.

Put it back.

---

## Part 5 — Which way is X? (10 min)

Tilt with `accelX` alone, then `accelY` alone, and note which way the ball
goes each time.

> The MAX7219's rows are its registers `0x01`..`0x08`, and each row's bits are
> columns — but *which physical side* is row 1 and which bit is the left column
> depends on how the matrix module is wired. Wokwi's part offers two layouts
> (`"parola"` and `"fc16"`), rotated and mirrored relative to each other.
>
> If the ball rolls the wrong way, fix it **in one place**: swap or negate `ax`
> and `ay` where the physics reads them, or change the `layout` attribute.
> Every IMU on every drone has this step — the *mounting* transform from the
> sensor's axes to the vehicle's.

---

## Part 6 — Chart the sensor in Python (15 min)

Let it run while you tilt back and forth for ten seconds. Copy the serial
monitor's text into `capture.txt`, then use lesson 08's `host.py`:

```
uv run ..\08_UART_And_Python_Host\host.py --file capture.txt --chart IMU,2,-16384,16384
```

> Field 2 of each `$IMU` frame is `ax`. `--chart` draws it as a bar scaled from
> −1 g to +1 g. Every frame's checksum is checked first; the summary line says
> how many were good.
>
> Try `--chart IMU,5,0,4095` for the potentiometer, and `--chart IMU,4,200,400`
> for the temperature in tenths of a degree — set the MPU6050's `temperature`
> slider and watch it move.

---

## Part 7 — Your turn (open-ended)

Pick one:

- **A spirit level.** Replace the ball with a bar: light the whole row the tilt
  points to, so the display shows angle, not motion.
- **A second I²C device.** Add another part from Wokwi's library to the same
  two wires, find its address with `scan`, and read its ID register.
- **Tilt-to-brightness.** Drive the intensity from how far the board is tilted
  instead of from the knob, and use the knob for friction.

> Whatever you pick, keep the bus lock around every I²C transfer, and keep every
> new wait bounded. Those two rules are the whole of Slide 15 and Slide 9.

---

## Where to go next

- Time the MPU6050 read: the kernel's tick is 1 ms, too coarse — but TIM14 from
  lesson 06 counts microseconds. Is it ~250 µs, as slide 7 predicts?
- Change `I2C_TIMING` to a 100 kHz value (ST's examples and CubeMX give them).
  How much longer does each read take, and does the ball feel any different at
  50 Hz?
- `delay_us()` in `adc.c` has a comment explaining why it is a crude loop. Read
  it. What was the bug, and why does "wait at least" make a crude loop correct?
