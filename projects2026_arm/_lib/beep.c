/*
 * beep.c - tones on PA6 / TIM3_CH1   LIBS=beep
 *
 *   timer clock 48 MHz / (PSC + 1 = 48)  = 1 MHz: one count per microsecond
 *   ARR + 1 = 1 000 000 / hz             period, in microseconds
 *   CCR1    = (ARR + 1) / 2              high for half of it: a square wave
 *
 * 440 Hz is ARR = 2272.  Anything from 16 Hz to 20 kHz fits in 16 bits.
 *
 * Between notes the counter is STOPPED and the output forced low, not left
 * running at 0% duty.  A real chip would not mind either way (a stopped timer
 * only saves a little power), but Wokwi does: a timer counting at 1 MHz
 * forever starved its simulated CPU, and lesson 12 ran ~800x slower than its
 * own code - 96 ms for one pad_read() - with every button press lost
 * (measured 2026-11-01).
 */
#include "stm32c031xx.h"
#include "gpio.h"
#include "beep.h"

#define BUZZ_PIN  6u            /* PA6 */
#define AF_TIM3   1u            /* PA6 = TIM3_CH1 on AF1 (lesson 06)  */

static uint32_t stop_at;
static uint8_t  on;

/* Silence: OC1M = 100 "force inactive" pulls the pin low at once, whatever
 * the counter was doing, and the counter stops. */
static void silence(void)
{
    TIM3->CCMR1 = (4u << TIM_CCMR1_OC1M_Pos);
    TIM3->CR1  &= ~TIM_CR1_CEN;
    on = 0;
}

void beep_init(void)
{
    RCC->IOPENR  |= RCC_IOPENR_GPIOAEN;
    RCC->APBENR1 |= RCC_APBENR1_TIM3EN;

    TIM3->PSC   = 48u - 1u;                                 /* 1 MHz            */
    TIM3->CCER  = TIM_CCER_CC1E;
    TIM3->CR1   = TIM_CR1_ARPE;
    silence();
    pin_af(GPIOA, BUZZ_PIN, AF_TIM3);                       /* after: pin low   */
}

void beep(uint32_t hz, uint32_t ms, uint32_t now_ms)
{
    if (hz < 16u || hz > 20000u) { silence(); return; }
    uint32_t period = 1000000u / hz;
    TIM3->ARR  = period - 1u;        /* preloaded: takes effect at the next update, */
    TIM3->CCR1 = period / 2u;        /* so a pitch change never makes a runt pulse  */
    if (!on) {                       /* stopped: load now, PWM mode 1, and go       */
        TIM3->CCMR1 = (6u << TIM_CCMR1_OC1M_Pos) | TIM_CCMR1_OC1PE;
        TIM3->EGR   = TIM_EGR_UG;    /* copy PSC/ARR/CCR1 in, counter to 0          */
        TIM3->CR1  |= TIM_CR1_CEN;
    }
    stop_at = now_ms + ms;
    on = 1;
}

void beep_poll(uint32_t now_ms)
{
    if (on && (int32_t)(now_ms - stop_at) >= 0) { silence(); }
}

int beep_busy(void) { return on; }
