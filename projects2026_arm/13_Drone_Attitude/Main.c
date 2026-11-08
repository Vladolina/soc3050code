/*
 * Main.c - SOC3050 lesson 13: Drone Attitude
 *          ST Nucleo-C031C6, STM32C031C6, Cortex-M0+ at 48 MHz
 *
 * A quadcopter flies on this chip - a simulated one.  Two programs share the
 * CPU and talk only through control.h's two structs:
 *
 *   task "world" 500 Hz   world.c   - the drone: rigid-body physics, motors,
 *                                     and an IMU with noise, bias, vibration
 *   task "ctrl"  250 Hz   control.c - the flight controller: complementary
 *                                     filter, angle and rate loops, mixer,
 *                                     failsafes.  YOUR code.
 *   task "pilot"  50 Hz   the app board's sticks -> setpoints; $ATT telemetry
 *   task "shell"          stats  alpha N  aw 0|1  mix 1|-1  kick  hold  quiet  loud
 *   task "disp"   15 Hz   artificial horizon, motor bars, state, loop rate
 *
 * Controls:   joystick  roll / pitch setpoint, +-30 deg (stick up = nose down)
 *             A         arm / disarm         B   gust: kick the drone
 *             SEL       SIM <-> MPU mode     knob  rate-loop Kp, x0.125 .. x8
 *
 * The station on the PC (uv run tools/station/station.py) has a key for every
 * control - arrows = joystick, a = A, b or p = B, s = SEL, 1..5 = knob, ? = the
 * list - and draws the OLED large, copied by mirror.h.  kbd.h folds its keys
 * into the same pad_t the pins fill, so task "pilot" cannot tell them apart.
 *
 * MPU mode swaps the world for the real (Wokwi) MPU6050 at 0x68: the same
 * estimator runs on its sliders and the horizon shows what it believes.
 * Nothing flies in MPU mode.
 *
 * Build:     build.bat          Host test:  host\run.bat
 * Simulate:  simulate.bat, paste diagram.json, upload Main.elf.
 * Chart:     python ..\08_UART_And_Python_Host\host.py --file cap.txt --chart ATT,2,-300,300
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stm32c031xx.h"
#include "os.h"
#include "uart.h"
#include "proto.h"
#include "i2c.h"
#include "oled.h"
#include "pad.h"
#include "beep.h"
#include "kbd.h"
#include "mirror.h"
#include "fmath.h"
#include "world.h"
#include "control.h"

#define BAUD        115200u
#define DEG         57.2957795f
#define RAD         0.0174532925f
#define HOVER_THR   0.35f            /* the pilot's throttle once armed       */
#define STICK_MAX   (30.0f * RAD)    /* full stick = 30 degrees               */

/* MPU6050 registers (Adafruit_MPU6050.h; see README "Facts checked") */
#define MPU_ADDR    0x68u
#define MPU_CONFIG  0x1Au            /* DLPF_CFG in bits 2:0                  */
#define MPU_DATA    0x3Bu            /* accel x,y,z, temp, gyro x,y,z: 14 B   */
#define MPU_PWR1    0x6Bu            /* SLEEP is set at power-up              */
#define MPU_WHOAMI  0x75u

#define STACK(name, words) static uint32_t name[words] __attribute__((aligned(8)))
STACK(stk_world, 256);  STACK(stk_ctrl, 256);  STACK(stk_pilot, 384);
STACK(stk_shell, 320);  STACK(stk_disp, 384);

/* ============================================================================
 *  Shared state.  Every struct crosses between tasks inside a critical
 *  section (lesson 03's tearing); single 32-bit words need none.
 * ============================================================================ */
static world_t        world;          /* owned by task "world"                  */
static sensors_t      g_sens;         /* world -> ctrl                          */
static actuators_t    g_act;          /* ctrl  -> world                         */
static ctrl_status_t  g_st;           /* ctrl  -> pilot, disp                   */
static volatile float g_true_roll, g_true_pitch;   /* world -> disp, telemetry  */

/* the pilot's sticks, pilot -> ctrl */
static volatile float   rc_roll, rc_pitch, rc_thr;
static volatile uint8_t rc_arm;

static volatile uint8_t mode_mpu;             /* 0 = SIM, 1 = real MPU6050     */
static volatile uint8_t req_kick, req_reset;  /* pilot -> world               */
static volatile uint8_t req_ctrl_reset;       /* pilot -> ctrl                */
static volatile uint8_t crashed, mpu_present;

/* measurements, for `stats` and the display */
static volatile uint32_t cyc_world, cyc_world_max, cyc_ctrl, cyc_ctrl_max;
static volatile uint32_t ctrl_hz, disp_fps, mpu_errors;

static os_mutex_t print_lock, bus_lock;

/* The beat.  "world" puts one item every second step (4 ms of physics)
 * and "ctrl" waits for it, so the controller runs exactly once per two
 * world steps however fast the CPU is: on silicon that is the 250 Hz a
 * 4 ms timer gives, and in a simulator slower than real time both slow
 * down together (README, Status).  The item says which mode the beat is
 * in.  In SIM mode ctrl answers on done_q when its step is finished and
 * the world waits for that answer - lock-step, so the controller never
 * sees a stale or a skipped sample.  In MPU mode nobody waits: ctrl may
 * sit on the I2C bus behind the display, and the physics is idle anyway. */
static os_queue_t beat_q, done_q;
static uint8_t    beat_buf[1], done_buf[1];
static volatile uint32_t world_overruns;

/* The station's keys: "shell" feeds them in, "pilot" applies them to the pad.
 * kbd.c has no lock of its own, so both calls go through kbd_lock. */
static kbd_t      kbd;
static os_mutex_t kbd_lock;
static volatile uint8_t telemetry_on = 1;     /* "quiet" / "loud": $ATT, $EST */

static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    os_mutex_lock(&print_lock);
    vprintf(fmt, ap);
    os_mutex_unlock(&print_lock);
    va_end(ap);
}

static void send_frame(const char *fmt, ...)
{
    char body[96];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    if (n < 0) { return; }
    if ((size_t)n >= sizeof body) { n = (int)sizeof body - 1; }
    say("$%s*%02X\n", body, (unsigned)proto_checksum(body, (size_t)n));
}

/* ============================================================================
 *  Counting cycles with SysTick
 *
 *  The kernel runs SysTick at 1 kHz: LOAD = 48000 - 1, VAL counts DOWN from
 *  47999 to 0 at 48 MHz, so (VAL before - VAL after) is cycles, provided the
 *  measured code takes under 1 ms (one wrap at most - corrected below).
 *  Interrupts are off for the measurement, so neither the tick nor another
 *  task can land in the middle of it - which is why only one call in 64 is
 *  timed: holding interrupts off for ~0.7 ms costs UART bytes if done often.
 * ============================================================================ */
static inline uint32_t cyc_elapsed(uint32_t v0, uint32_t v1)
{
    return v0 >= v1 ? v0 - v1 : v0 + (SysTick->LOAD + 1u) - v1;
}

/* ============================================================================
 *  Task "world": the drone, 500 Hz, the most urgent task.  Every second
 *  step it hands ctrl a beat and, in SIM mode, waits for ctrl's step - the
 *  one thing physics waits for, so the two stay in step at any CPU speed.
 * ============================================================================ */
static void task_world(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), n = 0, beat = 0;
    actuators_t act;
    sensors_t   sens;
    memset(&sens, 0, sizeof sens);

    for (;;) {
        /* Overrun guard.  os_delay_until() only sleeps when ahead of
         * schedule, so if a step (plus ctrl's, which we wait for) takes
         * longer than 2 ms it never sleeps, and as the highest priority it
         * starves every other task: no OLED, no sound, no shell.  That is
         * what Wokwi does - its CPU runs slower than its SysTick.  When the
         * last cycle overran, sleep as long as it took instead: the physics
         * goes into slow motion and the tasks below get about half the CPU.
         * On silicon a cycle takes under 2 ms and this never fires. */
        uint32_t took = os_ticks() - last;
        if (!mode_mpu && took >= 2u) {
            os_delay(took);                        /* as long as we took     */
            last = os_ticks();
            world_overruns++;
        } else {
            os_delay_until(&last, 2);
        }
        if (req_reset) {
            world_reset_attitude(&world);
            req_reset = 0;
            crashed = 0;
        }
        if (mode_mpu) {                            /* the real sensor rules  */
            if ((++beat & 1u) == 0) { uint8_t b = 1; os_queue_put(&beat_q, &b); }
            continue;
        }
        if (req_kick) {
            float s = (req_kick == 1) ? 1.0f : -1.0f;
            world_kick(&world, 4.0f * s, -2.0f * s);   /* 229 deg/s, -115 deg/s */
            req_kick = 0;
        }

        __disable_irq(); act = g_act; __enable_irq();

        if ((++n & 63u) == 0) {                    /* time one step in 64    */
            uint32_t pm = __get_PRIMASK();
            __disable_irq();
            uint32_t v0 = SysTick->VAL;
            world_step(&world, act.motor);
            world_sense(&world, &sens);
            uint32_t c = cyc_elapsed(v0, SysTick->VAL);
            __set_PRIMASK(pm);
            cyc_world = c;
            if (c > cyc_world_max) { cyc_world_max = c; }
        } else {
            world_step(&world, act.motor);
            world_sense(&world, &sens);
        }

        __disable_irq(); g_sens = sens; __enable_irq();

        if ((++beat & 1u) == 0) {                  /* every 2nd step: ctrl   */
            uint8_t b = 0;
            os_queue_put(&beat_q, &b);
            os_queue_get(&done_q, &b);             /* wait till it has run   */
        }

        if ((n % 10u) == 0) {                      /* truth, 50 Hz: atan2 x3 */
            float r, p;
            world_euler(&world, &r, &p, 0);
            g_true_roll = r;  g_true_pitch = p;
            if (world.crashed) { crashed = 1; }
        }
    }
}

/* ============================================================================
 *  The real MPU6050 (MPU mode).  Axes: mounted with chip X forward and chip
 *  Y to the LEFT, Z up - so body y = -chip Y and body z = -chip Z (FRD).
 * ============================================================================ */
static int mpu_write(uint8_t reg, uint8_t val)
{
    uint8_t w[2] = { reg, val };
    return i2c_write_read(MPU_ADDR, w, 2, 0, 0);
}

static int mpu_init(void)
{
    uint8_t reg = MPU_WHOAMI, who = 0;
    int rc = i2c_write_read(MPU_ADDR, &reg, 1, &who, 1);
    if (rc != I2C_OK) { return rc; }
    if (who != 0x68u) { return -10; }
    rc = mpu_write(MPU_PWR1, 0x00);                /* wake                   */
    if (rc == I2C_OK) { rc = mpu_write(MPU_CONFIG, 0x03); }   /* DLPF 44 Hz  */
    return rc;                                     /* ranges: +-2 g, 250 dps */
}

static void mpu_sense(sensors_t *s)
{
    uint8_t reg = MPU_DATA, b[14];
    os_mutex_lock(&bus_lock);
    int rc = i2c_write_read(MPU_ADDR, &reg, 1, b, sizeof b);
    os_mutex_unlock(&bus_lock);
    if (rc != I2C_OK) { s->imu_ok = 0; mpu_errors++; return; }
    int16_t v[7];
    for (int i = 0; i < 7; i++) { v[i] = (int16_t)((b[2 * i] << 8) | b[2 * i + 1]); }
    const float ka = 9.80665f / 16384.0f;          /* +-2 g:   16384 LSB/g     */
    const float kg = RAD / 131.0f;                 /* 250 dps: 131 LSB/(deg/s) */
    s->accel[0] =  ka * (float)v[0];
    s->accel[1] = -ka * (float)v[1];
    s->accel[2] = -ka * (float)v[2];
    s->gyro[0]  =  kg * (float)v[4];               /* v[3] is temperature     */
    s->gyro[1]  = -kg * (float)v[5];
    s->gyro[2]  = -kg * (float)v[6];
    s->imu_ok = 1;
    s->seq++;
}

/* ============================================================================
 *  Task "ctrl": the flight controller, 250 Hz.  It calls control_step() and
 *  nothing else that matters: same function as host/sitl.c calls.
 * ============================================================================ */
static void task_ctrl(void *arg)
{
    (void)arg;
    uint32_t n = 0, sec = os_ticks(), count = 0;
    sensors_t   in;
    actuators_t out;
    ctrl_status_t st;
    memset(&in, 0, sizeof in);

    for (;;) {
        uint8_t b;
        os_queue_get(&beat_q, &b);                 /* every 2nd world step   */
        uint8_t mpu = b;                           /* the beat's mode, fixed */
        if (req_ctrl_reset) { control_reset(); req_ctrl_reset = 0; }

        if (mpu) {
            if (mpu_present) { mpu_sense(&in); } else { in.imu_ok = 0; }
            in.rc_arm = 0;                         /* never fly on a real IMU */
        } else {
            __disable_irq(); in = g_sens; __enable_irq();
            in.rc_arm = rc_arm;
        }
        in.rc_roll = rc_roll;  in.rc_pitch = rc_pitch;
        in.rc_yaw_rate = 0.0f;
        in.rc_throttle = rc_thr;

        if ((++n & 63u) == 0) {
            uint32_t pm = __get_PRIMASK();
            __disable_irq();
            uint32_t v0 = SysTick->VAL;
            control_step(&in, &out);
            uint32_t c = cyc_elapsed(v0, SysTick->VAL);
            __set_PRIMASK(pm);
            cyc_ctrl = c;
            if (c > cyc_ctrl_max) { cyc_ctrl_max = c; }
        } else {
            control_step(&in, &out);
        }
        control_status(&st);

        __disable_irq(); g_act = out; g_st = st; __enable_irq();
        if (!mpu) { os_queue_put(&done_q, &b); }   /* world: go on           */

        count++;
        if (os_ticks() - sec >= 1000u) { ctrl_hz = count; count = 0; sec += 1000u; }
    }
}

/* ============================================================================
 *  Task "pilot": sticks, buttons, knob -> setpoints; telemetry; sounds
 * ============================================================================ */

/* 2^x for x in [-3, 3]: the knob is logarithmic, as gain knobs should be -
 * each eighth of a turn doubles or halves the gain.  2^f on [0,1) by a
 * quadratic that is exact at both ends (error < 0.3 %). */
static float pow2(float x)
{
    int   i = (int)(x + 3.0f) - 3;                 /* floor, for x >= -3     */
    float f = x - (float)i;
    float r = 1.0f + f * (0.6565f + 0.3435f * f);
    for (; i > 0; i--) { r *= 2.0f; }
    for (; i < 0; i++) { r *= 0.5f; }
    return r;
}

static void task_pilot(void *arg)
{
    (void)arg;
    uint32_t last = os_ticks(), n = 0, crash_t = 0;
    uint8_t was_armed = 0, kick_sign = 1, sw = 0;
    uint32_t fails_seen = 0;
    pad_t p;
    ctrl_status_t st;

    for (;;) {
        os_delay_until(&last, 20);
        uint32_t now = os_ticks();
        pad_read(&p);
        os_mutex_lock(&kbd_lock);
        kbd_apply(&kbd, &p, now);                  /* the station's keys, same pad */
        os_mutex_unlock(&kbd_lock);
        beep_poll(now);
        __disable_irq(); st = g_st; __enable_irq();

        /* sticks: right = roll right; UP = nose DOWN, as on any RC transmitter */
        rc_roll  =  STICK_MAX * (float)p.x / 100.0f;
        rc_pitch = -STICK_MAX * (float)p.y / 100.0f;
        if (p.adc_ok) { ctrl_tune.kp_scale = pow2(f_clamp(((float)p.knob - 500.0f) / 166.67f, -3.0f, 3.0f)); }

        if (p.pressed & PAD_A) { sw = (uint8_t)!sw; }
        if (p.pressed & PAD_B) {
            req_kick = kick_sign;  kick_sign = (uint8_t)(kick_sign == 1 ? 2 : 1);
            beep(1200, 30, now);
        }
        if (p.pressed & PAD_SEL) {
            mode_mpu = (uint8_t)!mode_mpu;
            sw = 0;
            req_reset = 1;  req_ctrl_reset = 1;
            say("mode: %s\n", mode_mpu ? "MPU6050 - estimator only, no flight" : "SIM - fly");
        }

        /* The controller refused or dropped the arm: put the switch down, so
         * re-arming is a deliberate press (control.c also insists). */
        if (st.fails != fails_seen) {
            fails_seen = st.fails;
            sw = 0;
            beep(400, 400, now);
            say("failsafe: %s\n", st.fail == FS_TILT ? "tilt > 60 deg" : st.fail == FS_SENSOR ? "sensor"
                                : st.fail == FS_STALE ? "IMU stale" : "throttle not low");
        }
        rc_arm = sw;

        /* throttle: zero until the controller has ARMED, then up to hover in
         * 0.5 s - the arm rule "throttle low" is met by construction */
        if (st.armed) {
            float t = rc_thr + HOVER_THR / 25.0f;
            rc_thr = t > HOVER_THR ? HOVER_THR : t;
        } else {
            rc_thr = 0.0f;
        }
        if (st.armed != was_armed) {
            beep(st.armed ? 1500 : 800, st.armed ? 80 : 150, now);
            was_armed = st.armed;
        }

        /* crash: tell the world, wait 2 s, stand it back up */
        if (crashed && crash_t == 0) {
            crash_t = now;  sw = 0;  rc_arm = 0;
            beep(200, 600, now);
            say("CRASH - resetting in 2 s\n");
        }
        if (crash_t && now - crash_t > 2000u) { req_reset = 1; crash_t = 0; }

        /* telemetry, 25 Hz: angles x10 (0.1 deg), motors x1000 */
        if ((++n & 1u) == 0 && telemetry_on) {
            if (!mode_mpu) {
                actuators_t a;
                __disable_irq(); a = g_act; __enable_irq();
                send_frame("ATT,%lu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", (unsigned long)now,
                           (int)(st.roll * DEG * 10.0f), (int)(st.pitch * DEG * 10.0f),
                           (int)(st.roll_sp * DEG * 10.0f), (int)(st.pitch_sp * DEG * 10.0f),
                           (int)(g_true_roll * DEG * 10.0f), (int)(g_true_pitch * DEG * 10.0f),
                           (int)(a.motor[0] * 1000.0f), (int)(a.motor[1] * 1000.0f),
                           (int)(a.motor[2] * 1000.0f), (int)(a.motor[3] * 1000.0f),
                           st.armed, (int)(ctrl_tune.kp_scale * 100.0f));
            } else {
                send_frame("EST,%lu,%d,%d,%d,%d", (unsigned long)now,
                           (int)(st.roll * DEG * 10.0f), (int)(st.pitch * DEG * 10.0f),
                           (int)(st.roll_acc * DEG * 10.0f), (int)(st.pitch_acc * DEG * 10.0f));
            }
        }
    }
}

/* ============================================================================
 *  Task "disp": the artificial horizon, 15 Hz, lowest priority
 * ============================================================================ */
static void draw_horizon(float roll, float pitch, int dotted)
{
    /* Roll right -> the horizon tilts the other way: its right end rises.
     * Pitch up -> the horizon drops.  1 pixel per degree of pitch. */
    float s = f_sin(roll), c = f_cos(roll);
    if (c < 0.05f) { c = 0.05f; }                  /* past 87 deg: clamp     */
    float slope = s / c, cy = 32.0f + pitch * DEG;
    for (int x = 1; x < 63; x += dotted ? 3 : 1) {
        int y = (int)(cy - ((float)x - 32.0f) * slope);
        if (dotted) {
            oled_pixel(x, y, OLED_XOR);
        } else if (y < 63) {
            if (y < 1) { y = 1; }
            oled_vline(x, y, 63 - y, OLED_ON);     /* fill the ground        */
        }
    }
}

static void task_disp(void *arg)
{
    (void)arg;
    static const char *const fs[] = { "SAFE", "TILT!", "SENS!", "STALE", "THR!" };
    uint32_t last = os_ticks(), sec = os_ticks(), frames = 0;
    ctrl_status_t st;
    actuators_t a;

    for (;;) {
        os_delay_until(&last, 66);
        __disable_irq(); st = g_st; a = g_act; __enable_irq();

        oled_clear();
        oled_rect(0, 0, 64, 64, OLED_ON);
        draw_horizon(st.roll, st.pitch, 0);
        if (!mode_mpu) { draw_horizon(g_true_roll, g_true_pitch, 1); }
        oled_hline(16, 32, 10, OLED_XOR);          /* the aircraft symbol    */
        oled_hline(38, 32, 10, OLED_XOR);
        oled_fill_rect(31, 31, 2, 2, OLED_XOR);

        oled_text(67, 0, mode_mpu ? "MPU" : "SIM", OLED_ON);
        oled_text(91, 0, crashed ? "CRASH" : st.armed ? "ARMED" : fs[st.fail <= 4 ? st.fail : 0], OLED_ON);
        if (mode_mpu && !mpu_present) { oled_text(67, 12, "no MPU6050", OLED_ON); }
        else {
            /* motors, laid out as seen from above: FL FR / RL RR */
            static const uint8_t bx[4] = { 67, 98, 98, 67 }, by[4] = { 11, 11, 19, 19 };
            for (int i = 0; i < 4; i++) {
                oled_rect(bx[i], by[i], 29, 6, OLED_ON);
                oled_fill_rect(bx[i] + 1, by[i] + 1, (int)(a.motor[i] * 27.0f), 4, OLED_ON);
            }
        }
        oled_printf(67, 28, "R%d P%d", (int)(st.roll * DEG), (int)(st.pitch * DEG));
        oled_printf(67, 37, "Kp x%d.%02d", (int)ctrl_tune.kp_scale,
                    (int)(ctrl_tune.kp_scale * 100.0f) % 100);
        oled_printf(67, 46, "a%4d %s", (int)(ctrl_tune.alpha * 1000.0f + 0.5f),
                    ctrl_tune.alpha >= 0.9995f ? "gyr" : ctrl_tune.anti_windup ? "aw" : "AW!");
        oled_printf(67, 55, "%luHz %luf", (unsigned long)ctrl_hz, (unsigned long)disp_fps);

        os_mutex_lock(&bus_lock);
        oled_flush();
        os_mutex_unlock(&bus_lock);

        /* The station's copy of the screen (mirror.h), at most 2 frames a
         * pass.  The room check and the print share print_lock, so "pilot"
         * cannot slip a $ATT in between and leave this frame waiting for a
         * full ring.  The buffer is static: this stack is 384 words. */
        static char fb[MIRROR_FRAME_MAX];
        for (int i = 0; i < 2; i++) {
            os_mutex_lock(&print_lock);
            size_t len = uart_tx_room() >= MIRROR_FRAME_MAX ? mirror_next(fb, sizeof fb, os_ticks()) : 0;
            if (len) { printf("%s", fb); }
            os_mutex_unlock(&print_lock);
            if (len == 0) { break; }
        }

        frames++;
        if (os_ticks() - sec >= 1000u) { disp_fps = frames; frames = 0; sec += 1000u; }
    }
}

/* ============================================================================
 *  Task "shell": stats  alpha N  aw 0|1  mix 1|-1  kick  hold  quiet  loud
 * ============================================================================ */
static void print_keys(void)
{
    say("keys: arrows = stick, a = arm, b/p = gust, s = SIM/MPU, 1-5 = Kp knob, ? = this\n"
        "      quiet / loud + Enter = stop / restart $ATT and $EST\n");
}

/* A line that starts with '$' is a frame from the station, never a command,
 * and is never answered - not even a bad one: the station sends $MIRROR every
 * second, and a reply to each would bury the telemetry. */
static void station_line(const char *line)
{
    if (kbd_is_key(line)) {
        os_mutex_lock(&kbd_lock);
        int r = kbd_frame(&kbd, line, os_ticks());
        os_mutex_unlock(&kbd_lock);
        if (r == KBD_HELP) { print_keys(); }       /* '?' asks, so it answers */
        /* - and + (KBD_SLOWER / KBD_FASTER) mean nothing here: the digit
         * keys 1..5 already step the one gain worth stepping. */
    } else {
        (void)mirror_frame(line, os_ticks());      /* $MIRROR, or dropped     */
    }
}

static void cmd_stats(void)
{
    uint32_t now = os_ticks();
    say("world_step+sense : %lu cycles (max %lu) = %lu us of every 2000 us\n",
        (unsigned long)cyc_world, (unsigned long)cyc_world_max, (unsigned long)(cyc_world / 48u));
    say("control_step     : %lu cycles (max %lu) = %lu us of every 4000 us\n",
        (unsigned long)cyc_ctrl, (unsigned long)cyc_ctrl_max, (unsigned long)(cyc_ctrl / 48u));
    say("control loop %lu Hz, world overruns %lu, display %lu fps, OLED bytes %lu, MPU errors %lu\n",
        (unsigned long)ctrl_hz, (unsigned long)world_overruns, (unsigned long)disp_fps,
        (unsigned long)oled_bytes_sent(), (unsigned long)mpu_errors);
    say("task   CPU%%  stack used/size (words)\n");
    for (uint32_t i = 0; i < os_task_count(); i++) {
        os_task_t *t = os_task(i);
        say("%-6s %3lu   %lu/%lu\n", t->name, (unsigned long)(t->ticks * 100u / (now ? now : 1u)),
            (unsigned long)os_stack_used(t), (unsigned long)t->stack_words);
    }
}

static void task_shell(void *arg)
{
    (void)arg;
    char line[32];
    uint32_t n = 0;
    for (;;) {
        char c = (char)uart_getc();
        if (c != '\r' && c != '\n') { if (n < sizeof line - 1u) { line[n++] = c; } continue; }
        if (n == 0) { continue; }
        line[n] = '\0';
        n = 0;
        if (line[0] == '$') { station_line(line); }      /* fits: $KEY <= 24 */
        else if (strcmp(line, "stats") == 0) { cmd_stats(); }
        else if (strncmp(line, "alpha ", 6) == 0) {
            int v = atoi(line + 6);
            if (v < 0) { v = 0; }
            if (v > 1000) { v = 1000; }
            ctrl_tune.alpha = (float)v / 1000.0f;
            say("alpha = %d/1000%s\n", v, v == 1000 ? " - gyro only" : v == 0 ? " - accel only" : "");
        } else if (strncmp(line, "aw ", 3) == 0) {
            ctrl_tune.anti_windup = (uint8_t)(atoi(line + 3) != 0);
            say("anti-windup %s\n", ctrl_tune.anti_windup ? "on" : "OFF");
        } else if (strncmp(line, "mix ", 4) == 0) {
            ctrl_tune.mix_roll_sign = (int8_t)(atoi(line + 4) < 0 ? -1 : 1);
            say("mixer roll sign %d%s\n", ctrl_tune.mix_roll_sign,
                ctrl_tune.mix_roll_sign < 0 ? " - BROKEN on purpose" : "");
        } else if (strcmp(line, "kick") == 0) { req_kick = 1; }
        else if (strcmp(line, "hold") == 0) {           /* a hand on the drone */
            world.locked = (uint8_t)!world.locked;
            say("%s\n", world.locked ? "held - it cannot rotate; type hold again to let go" : "released");
        }
        else if (strcmp(line, "quiet") == 0) { telemetry_on = 0; say("quiet: $ATT/$EST off - type loud to resume\n"); }
        else if (strcmp(line, "loud") == 0)  { telemetry_on = 1; say("loud: $ATT/$EST on\n"); }
        else { say("commands: stats  alpha 0..1000  aw 0|1  mix 1|-1  kick  hold  quiet  loud\n"); }
    }
}

int main(void)
{
    uart_init(SystemCoreClock, BAUD);
    printf("\n=== SOC3050 lesson 13 - Drone Attitude (SITL on the chip) ===\n");

    int pr = pad_init();
    printf("  pad      : %s\n", pr == 0 ? "joystick, knob, A, B, SEL" : "ADC failed - buttons only");
    beep_init();
    i2c_init();
    int o = oled_init();
    printf("  OLED     : %s\n", o == I2C_OK ? "0x3C ready" : "no answer at 0x3C");
    int m = mpu_init();
    mpu_present = (uint8_t)(m == I2C_OK);
    printf("  MPU6050  : %s\n", m == I2C_OK ? "0x68 awake, DLPF 44 Hz (MPU mode available)"
                                            : "not found - SIM mode only");

    world_init(&world, 0x1234567u);
    world_sense(&world, &g_sens);
    control_init();
    printf("  world    : 500 Hz quad on a gimbal; control 250 Hz\n");
    printf("  A arm, B gust, SEL sim/mpu, knob Kp.  Shell: stats alpha aw mix kick hold quiet loud\n");
    printf("  station  : uv run tools/station/station.py - arrows stick, a arm, b gust,\n"
           "             s SIM/MPU, 1-5 Kp, ? keys; the OLED drawn large\n\n");
    kbd_init(&kbd);

    os_queue_init(&beat_q, beat_buf, 1, 1);
    os_queue_init(&done_q, done_buf, 1, 1);
    os_task_create("world", task_world, 0, stk_world, 256, 5);
    os_task_create("ctrl",  task_ctrl,  0, stk_ctrl,  256, 4);
    os_task_create("pilot", task_pilot, 0, stk_pilot, 384, 3);
    os_task_create("shell", task_shell, 0, stk_shell, 320, 2);
    os_task_create("disp",  task_disp,  0, stk_disp,  384, 1);
    os_start();
}
