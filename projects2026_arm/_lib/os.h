/*
 * os.h - shared copy of lesson 07's kernel interface.  Link with LIBS=os.
 *
 * About 300 lines of C and 30 of assembly.  Fixed-priority preemptive
 * scheduling with round-robin between equal priorities, sleeping, a mutex
 * with optional priority inheritance, and a message queue.  It uses only
 * ARM's core peripherals - SysTick, SCB and the NVIC - so nothing in it is
 * specific to ST's chip.
 *
 * PRIORITIES: a HIGHER number is MORE urgent.  0 is the idle task.
 * That is the FreeRTOS convention, and it is the OPPOSITE of the NVIC, where
 * 0 is the most urgent.  Two numbering schemes, one chip - say which you mean.
 */
#ifndef OS_H
#define OS_H

#include <stdint.h>

#define OS_MAX_TASKS          8
#define OS_TICK_HZ            1000u
#define OS_PRIORITY_INHERIT   1       /* Lab Part 4 sets this to 0         */
#define OS_STACK_FILL         0xDEADBEEFu
#define OS_REWAKE_MS          10u     /* every 10 ticks SysTick wakes every
                                         BLOCKED task to re-check its wait;
                                         0 = off, as in lesson 07 (os.c)  */
#define OS_SIM_GATES          1       /* critical sections also close
                                         SysTick's TICKINT, because Wokwi
                                         ignores PRIMASK (_diag_primask);
                                         0 = PRIMASK only, as in lesson 07 */

typedef enum { OS_READY, OS_SLEEPING, OS_BLOCKED } os_state_t;

typedef struct os_task {
    uint32_t     *sp;          /* MUST be first: PendSV_Handler reads it at [tcb + 0] */
    const char   *name;
    uint8_t       prio;        /* current priority - may be raised by inheritance     */
    uint8_t       base_prio;   /* the priority it was created with                    */
    uint8_t       state;       /* os_state_t                                          */
    uint32_t      wake;        /* tick to wake at, when SLEEPING                      */
    const void   *waiting_on;  /* the mutex or queue it is BLOCKED on                 */
    uint32_t     *stack;       /* lowest address - where an overflow shows first     */
    uint32_t      stack_words;
    uint32_t      ticks;       /* ticks during which this task was running: CPU use   */
} os_task_t;

typedef struct {
    os_task_t    *owner;
} os_mutex_t;

typedef struct {
    uint8_t      *buf;
    uint16_t      item_size, capacity, head, count;
} os_queue_t;

/* ---- setup (from main, before os_start) -------------------------------- */
os_task_t *os_task_create(const char *name, void (*fn)(void *), void *arg,
                          uint32_t *stack, uint32_t stack_words, uint8_t prio);
void       os_start(void);                    /* never returns */

/* ---- from a task -------------------------------------------------------- */
void       os_delay(uint32_t ms);             /* sleep; other tasks run      */
void       os_delay_until(uint32_t *last, uint32_t period);   /* drift-free  */
void       os_yield(void);
uint32_t   os_ticks(void);                    /* ms since os_start           */

void       os_mutex_lock(os_mutex_t *m);
void       os_mutex_unlock(os_mutex_t *m);

void       os_queue_init(os_queue_t *q, void *storage, uint16_t item_size, uint16_t capacity);
void       os_queue_put(os_queue_t *q, const void *item);   /* blocks while full  */
void       os_queue_get(os_queue_t *q, void *item);         /* blocks while empty */
int        os_queue_put_from_isr(os_queue_t *q, const void *item); /* 0 if full */

/* ---- inspection, for the report ---------------------------------------- */
uint32_t   os_task_count(void);
os_task_t *os_task(uint32_t i);
uint32_t   os_stack_used(const os_task_t *t);        /* high-water mark, words */
uint32_t   os_switches(void);                        /* context switches so far */
os_task_t *os_current_task(void);

#endif
