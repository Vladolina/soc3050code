/*
 * _diag_primask/Main.c - does the simulator honour PRIMASK?
 *                        ST Nucleo-C031C6, STM32C031C6, Cortex-M0+ at 48 MHz
 *
 * A DIAGNOSTIC, not a lesson (the leading underscore keeps it out of the
 * lesson scripts).  Written 2026-11-16 because lesson 14, read with GDB from
 * a frozen Wokwi, had three tasks switched out at addresses inside cpsid i
 * sections - something a Cortex-M0+ never does.  See 14_Drone_Missions/
 * README.md, Status.
 *
 * The test: main() spends about half its time inside a "masked" section,
 * with the global flag in_cs = 1 for exactly that time.  Every handler below
 * counts its entries, and counts separately the ones that arrive while
 * in_cs == 1 - i.e. that broke INTO a masked section.  It also counts how
 * often it finds PRIMASK = 1 on entry (exception entry does not change
 * PRIMASK, so 1 there means it was taken while masked).
 *
 * Three ways of masking, each with four sources of exception:
 *   mask:   A  cpsid i / cpsie i                (what os.c does)
 *           B  msr PRIMASK, 1 / msr PRIMASK, 0  (__set_PRIMASK)
 *           C  SysTick->CTRL TICKINT off / on   (gates SysTick only)
 *           D  NVIC ICER / ISER                 (gates peripheral IRQs only;
 *              what _lib/os.c's OS_SIM_GATES relies on)
 *   source: SysTick at 10 kHz, priority 3       (the kernel's tick)
 *           PendSV, pended INSIDE the section    (the kernel's switch)
 *           TIM14 IRQ, pended INSIDE the section via NVIC ISPR
 *           USART2 RXNE - only if you type while it runs (optional)
 *
 * On silicon every "in masked" column is 0: a masked exception waits and
 * is taken at cpsie i / msr PRIMASK, 0, when in_cs is already back to 0.
 *
 * Build: build.bat     Run: open this folder in VS Code, start Wokwi, read
 * the serial monitor.  The report repeats every few seconds; copy one.
 */

#include <stdio.h>
#include <stdint.h>
#include "stm32c031xx.h"
#include "retarget.h"

#define BAUD     115200u
#define ROUNDS   4000u          /* sections per test                      */
#define SPIN     600u           /* loop turns inside and between sections */

typedef struct { volatile uint32_t all, in_cs, pm1; } count_t;
static count_t c_tick, c_pendsv, c_tim14, c_usart;
static volatile uint32_t in_cs, saved_iser;

static void note(count_t *c)
{
    c->all++;
    if (in_cs)            { c->in_cs++; }
    if (__get_PRIMASK())  { c->pm1++; }
}

void SysTick_Handler(void)  { note(&c_tick); }
void PendSV_Handler(void)   { note(&c_pendsv); }
void TIM14_IRQHandler(void) { note(&c_tim14); NVIC_ClearPendingIRQ(TIM14_IRQn); }
void USART2_IRQHandler(void)
{
    note(&c_usart);
    if (USART2->ISR & USART_ISR_RXNE_RXFNE) { (void)USART2->RDR; }
    USART2->ICR = USART_ICR_ORECF;
}

static void spin(uint32_t n) { for (volatile uint32_t i = 0; i < n; i++) { } }

static void zero(void)
{
    count_t z = { 0, 0, 0 };
    c_tick = z; c_pendsv = z; c_tim14 = z; c_usart = z;
}

static void mask(char how)
{
    if      (how == 'A') { __disable_irq(); }
    else if (how == 'B') { __set_PRIMASK(1u); }
    else if (how == 'C') { SysTick->CTRL &= ~SysTick_CTRL_TICKINT_Msk; }
    else                 { saved_iser = NVIC->ISER[0]; NVIC->ICER[0] = saved_iser; __DSB(); __ISB(); }
    __COMPILER_BARRIER();
    in_cs = 1u;
}

static void unmask(char how)
{
    in_cs = 0u;
    __COMPILER_BARRIER();
    if      (how == 'A') { __enable_irq(); }
    else if (how == 'B') { __set_PRIMASK(0u); }
    else if (how == 'C') { SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk; }
    else                 { NVIC->ISER[0] = saved_iser; }
}

static void row(const char *what, const count_t *c)
{
    printf("    %-8s entries %6lu   in masked %6lu   PRIMASK=1 on entry %6lu%s\n", what,
           (unsigned long)c->all, (unsigned long)c->in_cs, (unsigned long)c->pm1,
           c->in_cs ? "   <-- BROKE IN" : "");
}

static void test(char how)
{
    zero();
    for (uint32_t r = 0; r < ROUNDS; r++) {
        mask(how);
        if (how == 'A' || how == 'B') { SCB->ICSR = SCB_ICSR_PENDSVSET_Msk; }   /* PendSV now */
        if (how != 'C') { NVIC_SetPendingIRQ(TIM14_IRQn); }                     /* TIM14 now  */
        spin(SPIN);
        unmask(how);
        spin(SPIN);
    }
    static const char *const name[] = { "cpsid i / cpsie i", "msr PRIMASK 1 / 0",
                                        "SysTick TICKINT off / on", "NVIC ICER / ISER" };
    printf("  %c  %s, %lu sections:\n", how, name[how - 'A'], (unsigned long)ROUNDS);
    if (how != 'D') { row("SysTick", &c_tick); }
    if (how == 'A' || how == 'B') { row("PendSV", &c_pendsv); }
    if (how != 'C') { row("TIM14", &c_tim14); }
    if (c_usart.all) { row("USART2", &c_usart); }
}

int main(void)
{
    uart2_init(SystemCoreClock, BAUD);
    NVIC_SetPriority(PendSV_IRQn, 3);                /* as the kernel does   */
    NVIC_SetPriority(SysTick_IRQn, 3);
    NVIC_SetPriority(TIM14_IRQn, 1);
    NVIC_EnableIRQ(TIM14_IRQn);
    USART2->CR1 |= USART_CR1_RXNEIE_RXFNEIE;         /* type to add USART2   */
    NVIC_EnableIRQ(USART2_IRQn);
    SysTick->LOAD = SystemCoreClock / 10000u - 1u;   /* 10 kHz               */
    SysTick->VAL  = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;

    for (uint32_t pass = 1; ; pass++) {
        printf("\n=== _diag_primask, pass %lu - on silicon every 'in masked' is 0 ===\n",
               (unsigned long)pass);
        test('A');
        test('B');
        test('C');
        test('D');
        printf("=== end of pass %lu ===\n", (unsigned long)pass);
        spin(2000000u);
    }
}
