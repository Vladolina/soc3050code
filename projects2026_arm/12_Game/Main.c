/*
 * Main.c - SOC3050 lesson 12: Game
 *          ST Nucleo-C031C6, STM32C031C6, Cortex-M0+ at 48 MHz
 *
 * An arcade: SNAKE, BREAKOUT, FLAP, DOOM and PRINCE on a 128x64 SSD1306 OLED,
 * a joystick, buttons A and B, a knob for the speed, and a buzzer.
 *
 * This file is the PLATFORM - the only one that knows it is on a chip.  The
 * games (snake.c, breakout.c, flappy.c, doom.c, prince.c), the engine
 * (engine.c) and the menu (arcade.c) are pure C and run unchanged on a PC in host/test_games.c.
 * Main.c gives them three things:
 *
 *   time     SysTick at 1 kHz, and a microsecond stopwatch read from it
 *   the loop "update at a fixed rate, render when time allows"
 *   output   sound effects on the buzzer, $SCORE and $PERF frames on serial
 *
 * One loop, no RTOS - slide 18 says why.  So this lesson defines its own
 * SysTick_Handler, and does not link `os`, which would want SysTick too.
 *
 * Controls: stick = steer / choose,  A = play / serve / flap,  B = pause,
 *           knob = speed (in the menu),  stick press (SEL) = profiler mode:
 *           toggles "dirty pages" vs "repaint everything" so you can measure
 *           what dirty pages save (Lab Part 2).
 *
 * Build:     build.bat
 * Simulate:  simulate.bat, paste diagram.json, upload Main.elf.
 */
#include <stdarg.h>
#include <stdio.h>
#include "stm32c031xx.h"
#include "retarget.h"
#include "i2c.h"
#include "oled.h"
#include "pad.h"
#include "beep.h"
#include "proto.h"
#include "engine.h"
#include "arcade.h"

#define BAUD         115200u
#define MAX_CATCHUP  4               /* at most 4 updates (80 ms) per frame */

/* SLEEP_WFI 1: the idle loop sleeps in WFI until SysTick wakes it - what
 * silicon should do.  0: it spins until the tick instead.  Wokwi needs 0:
 * with WFI and no timer running, its SysTick never woke the core and the
 * board froze right after the banner (2026-11-01).  Same behaviour either
 * way; only the current drawn on real hardware differs (lesson 11). */
#define SLEEP_WFI    0

/* ============================================================================
 *  Time: SysTick at 1 kHz, and a microsecond stopwatch from the same counter
 * ============================================================================
 * SysTick counts DOWN from LOAD to 0 at the core clock, 48 MHz, then reloads
 * and interrupts.  So between two interrupts, (LOAD - VAL) is how many 1/48 us
 * have passed - a microsecond clock for free, with no timer spent on it. */
static volatile uint32_t ms;

void SysTick_Handler(void) { ms++; }

static uint32_t now_us(void)
{
    uint32_t m, v;
    do {                     /* ms ticked while we read VAL? again */
        m = ms;
        v = SysTick->VAL;
    } while (m != ms);
    return m * 1000u + (SysTick->LOAD - v) / (SystemCoreClock / 1000000u);
}

/* ============================================================================
 *  Sound: a tiny sequencer on top of beep.c
 * ============================================================================
 * beep() plays ONE tone and the hardware (TIM3) keeps it going with no CPU.
 * A sound effect is a short list of {Hz, ms}; Hz = 0 is a rest.  The loop
 * calls sound_poll() every pass, which starts the next note when the last one
 * has ended.  A new effect simply replaces the one playing. */
typedef struct { uint16_t hz, ms; } note_t;

static const note_t T_MOVE[]   = {{1400, 12}, {0, 0}};
static const note_t T_SELECT[] = {{900, 25}, {0, 0}};
static const note_t T_START[]  = {{523, 60}, {659, 60}, {784, 90}, {0, 0}};
static const note_t T_EAT[]    = {{1200, 25}, {1600, 25}, {0, 0}};
static const note_t T_BOUNCE[] = {{700, 12}, {0, 0}};
static const note_t T_BRICK[]  = {{1100, 20}, {0, 0}};
static const note_t T_FLAP[]   = {{500, 15}, {700, 15}, {0, 0}};
static const note_t T_POINT[]  = {{1500, 30}, {2000, 40}, {0, 0}};
static const note_t T_LOSE[]   = {{300, 80}, {200, 120}, {0, 0}};
static const note_t T_LEVEL[]  = {{784, 60}, {988, 60}, {1175, 60}, {1568, 120}, {0, 0}};
static const note_t T_OVER[]   = {{392, 150}, {0, 30}, {330, 150}, {0, 30},
                                  {262, 300}, {0, 0}};
static const note_t T_HIGH[]   = {{523, 80}, {659, 80}, {784, 80}, {1047, 80},
                                  {0, 40}, {1047, 200}, {0, 0}};
static const note_t T_SHOT[]   = {{1800, 8}, {900, 10}, {300, 30}, {0, 0}};      /* DOOM: a pistol */
static const note_t T_HIT[]    = {{420, 20}, {260, 35}, {0, 0}};
static const note_t T_HURT[]   = {{250, 60}, {180, 90}, {0, 0}};
static const note_t T_DOOR[]   = {{300, 40}, {450, 40}, {620, 60}, {0, 0}};
static const note_t T_SWORD[]  = {{2200, 12}, {1500, 10}, {2600, 18}, {0, 0}};   /* PRINCE: steel on steel */
static const note_t T_THUD[]   = {{150, 45}, {0, 0}};

static const note_t *const tunes[SFX_COUNT] = {
    [SFX_MOVE] = T_MOVE, [SFX_SELECT] = T_SELECT, [SFX_START] = T_START,
    [SFX_EAT] = T_EAT,   [SFX_BOUNCE] = T_BOUNCE, [SFX_BRICK] = T_BRICK,
    [SFX_FLAP] = T_FLAP, [SFX_POINT] = T_POINT,   [SFX_LOSE] = T_LOSE,
    [SFX_LEVEL] = T_LEVEL, [SFX_OVER] = T_OVER,   [SFX_HIGH] = T_HIGH,
    [SFX_SHOT] = T_SHOT, [SFX_HIT] = T_HIT,       [SFX_HURT] = T_HURT,
    [SFX_DOOR] = T_DOOR, [SFX_SWORD] = T_SWORD,   [SFX_THUD] = T_THUD,
};

static const note_t *playing;
static uint32_t      note_end;

void sfx(int id)                       /* the engine's hook - see engine.h */
{
    if (id >= 0 && id < SFX_COUNT) { playing = tunes[id]; note_end = ms; }
}

static void sound_poll(uint32_t now)
{
    beep_poll(now);
    if (playing == 0 || (int32_t)(now - note_end) < 0) { return; }
    if (playing->ms == 0) { playing = 0; beep(0, 0, now); return; }   /* end */
    beep(playing->hz, playing->ms, now);         /* hz = 0 is silence: a rest */
    note_end = now + playing->ms;
    playing++;
}

/* ============================================================================
 *  Serial: $SCORE when a game ends, $PERF once a second
 * ============================================================================ */
void on_score(const char *game, int32_t score)     /* the arcade's hook */
{
    char line[48];
    score_frame(line, sizeof line, game, score);
    printf("%s\n", line);
}

static void send_frame(const char *fmt, ...)
{
    char body[64];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    if (n < 0) { return; }
    if ((size_t)n >= sizeof body) { n = (int)sizeof body - 1; }
    printf("$%s*%02X\n", body, (unsigned)proto_checksum(body, (size_t)n));
}

/* ============================================================================
 *  The loop
 * ============================================================================ */
int main(void)
{
    uart2_init(SystemCoreClock, BAUD);
    SysTick_Config(SystemCoreClock / 1000u);       /* 1 ms tick, interrupt on */

    printf("\n=== SOC3050 lesson 12 - Game ===\n");
    i2c_init();
    int o = oled_init();
    printf("  OLED     : %s\n", o == 0 ? "SSD1306 at 0x3C, initialised (16 commands)"
                              : o == I2C_NACK ? "no ACK at 0x3C - check SCL PB8 / SDA PB9"
                              : "I2C timeout - bus stuck?");
    int p0 = pad_init();
    printf("  pad      : %s\n", p0 == 0 ? "ADC ready: stick PA0/PA1, knob PA4; A PB4, B PB5, SEL PB3"
                                       : "ADC FAILED - buttons only");
    beep_init();
    printf("  buzzer   : PA6, TIM3_CH1\n");
    printf("  frames   : $SCORE,game,score  per game;  $PERF,fps,ups,flush_us,max_us,bytes,busy%%,mode  per second\n");
    printf("  stick press toggles the profiler mode: dirty pages / full repaint\n\n");

    arcade_init();
    oled_contrast(0xCF);

    pad_t pad;
    int full_repaint = 0;
    uint32_t next = ms;                /* when the next update is due       */
    uint32_t sec_start = ms;
    uint32_t frames = 0, updates = 0, busy_us = 0, flush_sum = 0, flush_max = 0;
    uint32_t bytes_at_sec = oled_bytes_sent();

    for (;;) {
        sound_poll(ms);

        /* ---- UPDATE: run every tick that is due, at exactly 20 ms each ---- */
        int ran = 0;
        uint32_t t0 = now_us();
        while ((int32_t)(ms - next) >= 0) {
            pad_read(&pad);
            if (pad.pressed & PAD_SEL) { full_repaint ^= 1; }
            arcade_step(&pad);
            updates++;
            next += TICK_MS;
            if (++ran == MAX_CATCHUP) {           /* hopelessly behind: give up */
                next = ms + TICK_MS;              /* the lost time, rather than */
                break;                            /* spiral into catching up    */
            }
        }
        if (ran == 0) {               /* nothing due: sleep until the next tick */
#if SLEEP_WFI
            __WFI();
#else
            uint32_t m = ms;
            while (ms == m) { }       /* Wokwi: spin to the tick (see SLEEP_WFI) */
#endif
            continue;
        }

        sound_poll(ms);               /* start any effect the update asked for */

        /* ---- RENDER: once, however many updates just ran ----------------- */
        arcade_draw();                /* RENDER: once, however many updates ran */
        if (full_repaint) { oled_invalidate(); }  /* the profiler: no dirty pages */
        uint32_t t1 = now_us();
        oled_flush();
        uint32_t t2 = now_us();
        frames++;
        flush_sum += t2 - t1;
        if (t2 - t1 > flush_max) { flush_max = t2 - t1; }
        busy_us += t2 - t0;

        /* ---- once a second: the profile ---------------------------------- */
        uint32_t el = ms - sec_start;
        if (el >= 1000u) {
            uint32_t bytes = oled_bytes_sent() - bytes_at_sec;
            send_frame("PERF,%lu,%lu,%lu,%lu,%lu,%lu,%s",
                       (unsigned long)(frames * 1000u / el),
                       (unsigned long)(updates * 1000u / el),
                       (unsigned long)(flush_sum / frames),
                       (unsigned long)flush_max,
                       (unsigned long)(bytes / frames),
                       (unsigned long)(busy_us / (el * 10u)),        /* percent */
                       full_repaint ? "FULL" : "DIRTY");
            sec_start = ms;
            frames = updates = busy_us = flush_sum = flush_max = 0;
            bytes_at_sec = oled_bytes_sent();
        }
    }
}
