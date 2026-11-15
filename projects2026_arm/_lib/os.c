/*
 * os.c - shared copy of lesson 07's kernel.  Link with LIBS=os.
 *
 * Lesson 07 is where this file is written and taken apart; keep this copy in
 * step with 07_RTOS/os.c.  From lesson 08 on it is infrastructure.
 *
 * DELIBERATE DIFFERENCES from lesson 07's copy, all because WOKWI IGNORES
 * PRIMASK.  _diag_primask measured it (2026-11-16): under cpsid i or
 * msr PRIMASK, SysTick, PendSV and a peripheral IRQ all still ran.  Lesson
 * 14 froze from it: a task was switched out between "state = BLOCKED" and
 * "waiting_on = q", so the put that should have woken it matched nothing.
 *   1. OS_SIM_GATES: a critical section also closes SysTick's TICKINT,
 *      which Wokwi does honour - see enter().  This is the fix.  On silicon
 *      it is redundant: PRIMASK already did it.  ISRs must therefore not
 *      call the kernel in Wokwi; _lib/uart.c no longer does.
 *   2. OS_REWAKE_MS: every 10 ticks SysTick wakes every blocked task.  Every
 *      wait below sits in a while loop that re-checks its condition, so a
 *      wake for nothing is harmless - it looks, and blocks again.  A safety
 *      net: any lost wake-up becomes a pause of at most 10 ms.
 *   3. Blocking writes waiting_on BEFORE state, so a task caught between the
 *      two is never "blocked on nothing".
 * Lesson 07 keeps the textbook kernel; its measured figures depend on it.
 *
 * Three mechanisms make it work, and each is a piece of hardware you have
 * already met:
 *
 *   SysTick   (lesson 06)  interrupts 1000 times a second.  Its handler wakes
 *                          sleeping tasks and decides who should run next.
 *   PendSV    (new)        a software-triggered exception at the LOWEST
 *                          priority.  Setting its pending bit asks for a
 *                          context switch "as soon as nothing more urgent is
 *                          running" - it is the only place a switch happens.
 *   PSP       (new)        the second stack pointer.  Tasks run on PSP, each
 *                          with its own stack; every handler runs on MSP.
 *
 * A context switch is: the hardware has already stacked r0-r3, r12, lr, pc
 * and xPSR on the task's stack (lesson 03, slide 2).  PendSV_Handler stacks
 * the other eight, r4-r11, beside them, saves the stack pointer in the task's
 * control block, loads the next task's stack pointer, unstacks its r4-r11,
 * and returns.  The hardware unstacks the rest - and the next task resumes
 * exactly where it was preempted, without ever knowing.
 */

#include <string.h>
#include "stm32c031xx.h"
#include "os.h"

static os_task_t tasks[OS_MAX_TASKS];
static uint32_t  n_tasks;
static volatile uint32_t ticks;

/* Read by PendSV_Handler's assembly, by name - so not static. */
os_task_t *volatile os_current;
os_task_t *volatile os_next;
volatile uint32_t   os_switch_count;

static uint32_t idle_stack[64] __attribute__((aligned(8)));

/* ---- critical sections ---------------------------------------------------
 * The kernel's lists are shared by tasks, SysTick and any ISR that uses a
 * queue.  On a single core the whole of "mutual exclusion" is: mask
 * interrupts (lesson 03, slide 5).  PRIMASK is saved and restored rather than
 * just cleared, so these nest correctly and work from inside a handler. */
#if !OS_SIM_GATES
typedef uint32_t cs_t;
static inline cs_t enter(void) { uint32_t pm = __get_PRIMASK(); __disable_irq(); return pm; }
static inline void leave(cs_t pm) { __set_PRIMASK(pm); }
#else
/* OS_SIM_GATES (os.h): Wokwi ignores PRIMASK - _diag_primask measured
 * SysTick, PendSV and TIM14 all breaking into cpsid i and msr PRIMASK
 * sections, but SysTick stayed out while its TICKINT bit was off.  So a
 * critical section ALSO shuts SysTick's TICKINT: SysTick is the only thing
 * that preempts one task with another, so that is what keeps the kernel's
 * lists consistent between tasks.  Peripheral IRQs are NOT gated: holding
 * the USART's off made Wokwi drop received bytes (with no overrun flag), so
 * instead no ISR in lessons 09-19 calls the kernel (_lib/uart.c receives
 * into a lock-free ring).  On silicon PRIMASK already did the job and this
 * is a few register writes.  Closing TICKINT can lose a tick that falls
 * inside the section; COUNTFLAG says one did, and leave() pends it by hand. */
typedef struct { uint32_t pm, tick; } cs_t;

static __attribute__((noinline)) cs_t enter(void)
{
    cs_t s;
    s.pm = __get_PRIMASK();
    __disable_irq();
    uint32_t c = SysTick->CTRL;                  /* reading clears COUNTFLAG  */
    s.tick = c & SysTick_CTRL_TICKINT_Msk;
    SysTick->CTRL = c & ~SysTick_CTRL_TICKINT_Msk;
    __DSB(); __ISB();
    return s;
}

static inline void reopen_tick(void)
{
    uint32_t c = SysTick->CTRL;
    SysTick->CTRL = c | SysTick_CTRL_TICKINT_Msk;
    if (c & SysTick_CTRL_COUNTFLAG_Msk) { SCB->ICSR = SCB_ICSR_PENDSTSET_Msk; }
}

static __attribute__((noinline)) void leave(cs_t s)
{
    if (s.tick) { reopen_tick(); }               /* only the outermost opens  */
    __set_PRIMASK(s.pm);
}

/* Called by PendSV_Handler before it returns into the next task.  In Wokwi
 * PendSV runs the moment schedule() pends it, INSIDE the outgoing task's
 * section, so the incoming task would inherit a closed tick; open it, as
 * the handler's cpsie i opens PRIMASK.  On silicon PendSV only runs outside
 * every section, so the tick is already open. */
void os_gates_open(void) { reopen_tick(); }
#endif

/* ---- the scheduler -------------------------------------------------------
 * The highest-priority READY task runs.  Among equals, the search starts just
 * after the current task, so they take turns - one tick each - which is
 * round-robin time slicing for free.  The idle task, priority 0, is always
 * READY, so there is always an answer. */
static os_task_t *pick(void)
{
    uint32_t cur = os_current ? (uint32_t)(os_current - tasks) : 0u;
    os_task_t *best = 0;
    for (uint32_t i = 1; i <= n_tasks; i++) {
        os_task_t *t = &tasks[(cur + i) % n_tasks];
        if (t->state == OS_READY && (best == 0 || t->prio > best->prio)) {
            best = t;
        }
    }
    return best;
}

/* Call with interrupts masked.  Pends PendSV if someone else should run - and
 * CANCELS a pending switch if, after all, the current task should keep going.
 * Without the cancel, a switch requested earlier would still happen, to a
 * task chosen from stale information. */
static void schedule(void)
{
    os_task_t *n = pick();
    os_next = n;
    if (n != os_current) {
        SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
    } else {
        SCB->ICSR = SCB_ICSR_PENDSVCLR_Msk;
    }
}

static void wake_waiters(const void *obj)
{
    for (uint32_t i = 0; i < n_tasks; i++) {
        if (tasks[i].state == OS_BLOCKED && tasks[i].waiting_on == obj) {
            tasks[i].state = OS_READY;
            tasks[i].waiting_on = 0;
        }
    }
}

/* ---- the tick ------------------------------------------------------------ */
static void task_exit(void);                     /* below: an ended task      */
void SysTick_Handler(void)
{
    cs_t pm = enter();
    ticks++;
    if (os_current) { os_current->ticks++; }     /* who was running: CPU use */
    for (uint32_t i = 0; i < n_tasks; i++) {
        if (tasks[i].state == OS_SLEEPING && (int32_t)(ticks - tasks[i].wake) >= 0) {
            tasks[i].state = OS_READY;
        }
    }
#if OS_REWAKE_MS
    if (ticks % OS_REWAKE_MS == 0u) {            /* see the header comment   */
        for (uint32_t i = 0; i < n_tasks; i++) {
            if (tasks[i].state == OS_BLOCKED
             && tasks[i].waiting_on != (const void *)task_exit) {   /* ended: stay */
                tasks[i].state = OS_READY;
                tasks[i].waiting_on = 0;
            }
        }
    }
#endif
    schedule();                                  /* wake-ups, time slices    */
    leave(pm);
}

/* ---- the switch ----------------------------------------------------------
 * naked: no compiler prologue or epilogue - every instruction is below.
 * Thumb-1 cannot store r8-r11 directly, so they are copied through r4-r7,
 * which have already been saved.  The saved layout, lowest address first:
 *
 *   tcb->sp -> r4 r5 r6 r7 | r8 r9 r10 r11 | r0 r1 r2 r3 r12 lr pc xPSR
 *              ---- PendSV saves these ---   ---- hardware saved these ---
 *
 * 0xFFFFFFFD is EXC_RETURN for "back to Thread mode, using PSP".  Every task
 * runs on PSP, so every switch returns that way - including the very first,
 * which interrupted main() on MSP and never goes back to it. */
#if OS_SIM_GATES        /* r4-r11 survive the call (AAPCS); lr is reloaded */
#define PENDSV_GATES "   bl    os_gates_open        \n"
#else
#define PENDSV_GATES ""
#endif
__attribute__((naked)) void PendSV_Handler(void)
{
    __asm volatile(
        "   .syntax unified            \n"   /* GCC leaves inline asm in the  */
        "   cpsid i                    \n"   /* old syntax, which rejects     */
                                             /* 'subs r0, #32' on this core   */
        PENDSV_GATES                         /* OS_SIM_GATES: see enter()     */
        "   ldr   r3, =os_current      \n"
        "   ldr   r1, [r3]             \n"   /* r1 = the task being left      */
        "   cmp   r1, #0               \n"
        "   beq   1f                   \n"   /* first switch: nothing to save */
        "   mrs   r0, psp              \n"
        "   subs  r0, #32              \n"
        "   str   r0, [r1]             \n"   /* tcb->sp = psp - 32            */
        "   stmia r0!, {r4-r7}         \n"
        "   mov   r4, r8               \n"
        "   mov   r5, r9               \n"
        "   mov   r6, r10              \n"
        "   mov   r7, r11              \n"
        "   stmia r0!, {r4-r7}         \n"
        "1: ldr   r2, =os_next         \n"
        "   ldr   r1, [r2]             \n"
        "   str   r1, [r3]             \n"   /* os_current = os_next          */
        "   ldr   r2, =os_switch_count \n"
        "   ldr   r0, [r2]             \n"
        "   adds  r0, #1               \n"
        "   str   r0, [r2]             \n"
        "   ldr   r0, [r1]             \n"   /* r0 = the new task's saved sp  */
        "   adds  r0, #16              \n"
        "   ldmia r0!, {r4-r7}         \n"   /* its r8-r11, via r4-r7         */
        "   mov   r8, r4               \n"
        "   mov   r9, r5               \n"
        "   mov   r10, r6              \n"
        "   mov   r11, r7              \n"
        "   msr   psp, r0              \n"   /* psp -> its hardware frame     */
        "   subs  r0, #32              \n"
        "   ldmia r0!, {r4-r7}         \n"   /* its r4-r7                     */
        "   cpsie i                    \n"
        "   ldr   r0, =0xFFFFFFFD      \n"
        "   bx    r0                   \n"
        "   .align 2                   \n"
        "   .ltorg                     \n"
    );
}

/* ---- tasks ---------------------------------------------------------------- */

/* A task function that returns lands here, through the lr planted in its
 * first frame.  It is parked for good rather than allowed to run off into
 * whatever follows in memory. */
static void task_exit(void)
{
    cs_t pm = enter();
    os_current->state = OS_BLOCKED;
    os_current->waiting_on = (const void *)task_exit;   /* nobody wakes this */
    schedule();
    leave(pm);
    for (;;) { }
}

/* Builds the stack a task would have if it had been running and had just
 * been preempted - so the very first switch to it looks like any other. */
os_task_t *os_task_create(const char *name, void (*fn)(void *), void *arg,
                          uint32_t *stack, uint32_t stack_words, uint8_t prio)
{
    if (n_tasks >= OS_MAX_TASKS) { return 0; }
    os_task_t *t = &tasks[n_tasks];

    for (uint32_t i = 0; i < stack_words; i++) { stack[i] = OS_STACK_FILL; }

    uint32_t *sp = (uint32_t *)((uintptr_t)(stack + stack_words) & ~7u); /* AAPCS: 8-aligned */
    *--sp = 0x01000000u;                  /* xPSR: only the Thumb bit set       */
    *--sp = (uint32_t)(uintptr_t)fn & ~1u;/* pc:   where it starts              */
    *--sp = (uint32_t)(uintptr_t)task_exit; /* lr: where it goes if it returns  */
    *--sp = 0u;                           /* r12                                */
    *--sp = 0u;                           /* r3                                 */
    *--sp = 0u;                           /* r2                                 */
    *--sp = 0u;                           /* r1                                 */
    *--sp = (uint32_t)(uintptr_t)arg;     /* r0:   its argument                 */
    for (int i = 0; i < 8; i++) { *--sp = 0u; }   /* r11..r4                    */

    t->sp          = sp;
    t->name        = name;
    t->prio        = prio;
    t->base_prio   = prio;
    t->state       = OS_READY;
    t->stack       = stack;
    t->stack_words = stack_words;
    n_tasks++;
    return t;
}

static void idle_task(void *arg)
{
    (void)arg;
    for (;;) { }        /* the CPU time nobody wanted; the report measures it */
}

void os_start(void)
{
    os_task_create("idle", idle_task, 0, idle_stack, 64, 0);

    NVIC_SetPriority(PendSV_IRQn, 3);            /* lowest: never preempts a  */
    SysTick_Config(SystemCoreClock / OS_TICK_HZ);/* handler; SysTick also 3   */

    __disable_irq();
    os_current = 0;
    os_next    = pick();
    SCB->ICSR  = SCB_ICSR_PENDSVSET_Msk;
    __enable_irq();                              /* the first switch happens  */
    for (;;) { }                                 /* here; main() is over      */
}

/* ---- time ------------------------------------------------------------------ */
uint32_t os_ticks(void) { return ticks; }

void os_delay(uint32_t ms)
{
    cs_t pm = enter();
    os_current->wake  = ticks + ms;
    os_current->state = OS_SLEEPING;
    schedule();
    leave(pm);                   /* PendSV is pending: the switch happens here */
}

/* Like lesson 06's `at += period`: wakes on a fixed grid however long the
 * task's own work took, so its period does not drift. */
void os_delay_until(uint32_t *last, uint32_t period)
{
    cs_t pm = enter();
    *last += period;
    if ((int32_t)(*last - ticks) > 0) {
        os_current->wake  = *last;
        os_current->state = OS_SLEEPING;
        schedule();
    }
    leave(pm);
}

void os_yield(void)
{
    cs_t pm = enter();
    schedule();
    leave(pm);
}

/* ---- mutex ----------------------------------------------------------------- */
void os_mutex_lock(os_mutex_t *m)
{
    cs_t pm = enter();
    while (m->owner != 0) {
#if OS_PRIORITY_INHERIT
        /* The owner is in our way.  Lend it our priority so nothing of middle
         * priority can keep it from finishing - slide 13. */
        if (m->owner->prio < os_current->prio) { m->owner->prio = os_current->prio; }
#endif
        os_current->waiting_on = m;              /* first: see the header    */
        __COMPILER_BARRIER();                    /* ...and keep that order   */
        os_current->state      = OS_BLOCKED;
        schedule();
        leave(pm);               /* switched out here; back when woken */
        pm = enter();
    }
    m->owner = os_current;
    leave(pm);
}

void os_mutex_unlock(os_mutex_t *m)
{
    cs_t pm = enter();
    m->owner = 0;
    os_current->prio = os_current->base_prio;    /* give back anything lent */
    wake_waiters(m);
    schedule();
    leave(pm);
}

/* ---- queue: fixed-size items, copied in and out --------------------------- */
void os_queue_init(os_queue_t *q, void *storage, uint16_t item_size, uint16_t capacity)
{
    q->buf = storage; q->item_size = item_size; q->capacity = capacity;
    q->head = 0; q->count = 0;
}

static void q_push(os_queue_t *q, const void *item)
{
    uint16_t tail = (uint16_t)((q->head + q->count) % q->capacity);
    memcpy(q->buf + (uint32_t)tail * q->item_size, item, q->item_size);
    q->count++;
}

void os_queue_put(os_queue_t *q, const void *item)
{
    cs_t pm = enter();
    while (q->count == q->capacity) {
        os_current->waiting_on = q; __COMPILER_BARRIER(); os_current->state = OS_BLOCKED;
        schedule(); leave(pm); pm = enter();
    }
    q_push(q, item);
    wake_waiters(q);
    schedule();
    leave(pm);
}

void os_queue_get(os_queue_t *q, void *item)
{
    cs_t pm = enter();
    while (q->count == 0) {
        os_current->waiting_on = q; __COMPILER_BARRIER(); os_current->state = OS_BLOCKED;
        schedule(); leave(pm); pm = enter();
    }
    memcpy(item, q->buf + (uint32_t)q->head * q->item_size, q->item_size);
    q->head = (uint16_t)((q->head + 1u) % q->capacity);
    q->count--;
    wake_waiters(q);
    schedule();
    leave(pm);
}

/* An ISR must never block, so this one reports "full" instead of waiting. */
int os_queue_put_from_isr(os_queue_t *q, const void *item)
{
    cs_t pm = enter();
    if (q->count == q->capacity) { leave(pm); return 0; }
    q_push(q, item);
    wake_waiters(q);
    schedule();                  /* PendSV runs when the last handler returns */
    leave(pm);
    return 1;
}

/* ---- inspection -------------------------------------------------------------- */
uint32_t   os_task_count(void)        { return n_tasks; }
os_task_t *os_task(uint32_t i)        { return i < n_tasks ? &tasks[i] : 0; }
uint32_t   os_switches(void)          { return os_switch_count; }
os_task_t *os_current_task(void)      { return os_current; }

/* Stacks are filled with OS_STACK_FILL at creation and grow down, so the
 * untouched words sit at the bottom.  Count them and you have the deepest
 * the task has ever gone - its high-water mark. */
uint32_t os_stack_used(const os_task_t *t)
{
    uint32_t untouched = 0;
    while (untouched < t->stack_words && t->stack[untouched] == OS_STACK_FILL) { untouched++; }
    return t->stack_words - untouched;
}
