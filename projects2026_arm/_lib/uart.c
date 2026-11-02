/*
 * uart.c - shared copy of lesson 08's interrupt-driven USART2.  LIBS=uart
 *
 * Lesson 08 is where this file is written and taken apart; keep this copy in
 * step with 08_UART_And_Python_Host/uart.c.  Needs LIBS=os as well.
 *
 * The UART is the Part 0 peripheral model once more:
 *   clock gate  RCC->APBENR1 USART2EN
 *   control     CR1 (enable, TE, RE, and the two interrupt enables used here)
 *   status      ISR (RXNE: a byte arrived; TXE: room to send; ORE: overrun)
 *   data        RDR (in), TDR (out)
 * and its rendezvous is, for the first time in this course, an interrupt in
 * BOTH directions.
 *
 * Two different buffers, on purpose:
 *   RX  - a kernel queue (lesson 07).  The ISR produces with the ISR-safe put;
 *         a task consumes and BLOCKS while it is empty.  Locking inside.
 *   TX  - a lock-free single-producer / single-consumer ring.  Tasks write
 *         head, the ISR writes tail, nobody writes both: lesson 03 slide 7's
 *         one-writer rule, applied to each index separately.  No lock at all.
 */

#include "stm32c031xx.h"
#include "gpio.h"
#include "os.h"
#include "uart.h"

#define TX_PIN      2u            /* PA2 - USART2 TX, AF1 */
#define RX_PIN      3u            /* PA3 - USART2 RX, AF1 */
#define AF_USART2   1u

#define RX_DEPTH    64u
#define TX_SIZE     256u          /* a power of two: % becomes a mask */

static volatile uart_stats_t stats;

/* ---- RX: interrupt -> queue -> task --------------------------------------- */
static uint8_t    rx_store[RX_DEPTH];
static os_queue_t rx_queue;

/* ---- TX: task -> lock-free ring -> interrupt ------------------------------- */
static uint8_t           tx_ring[TX_SIZE];
static volatile uint16_t tx_head;      /* written ONLY by the producer (tasks)   */
static volatile uint16_t tx_tail;      /* written ONLY by the consumer (the ISR) */

void uart_init(uint32_t pclk, uint32_t baud)
{
    RCC->IOPENR  |= RCC_IOPENR_GPIOAEN;
    RCC->APBENR1 |= RCC_APBENR1_USART2EN;
    pin_af(GPIOA, TX_PIN, AF_USART2);
    pin_af(GPIOA, RX_PIN, AF_USART2);

    os_queue_init(&rx_queue, rx_store, 1, RX_DEPTH);

    USART2->BRR = (pclk + baud / 2u) / baud;             /* 48 MHz / 115200 = 417 */
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE
                | USART_CR1_RXNEIE_RXFNEIE               /* interrupt per byte in  */
                | USART_CR1_UE;                          /* TXEIE: only when needed */

    /* Above the kernel (SysTick and PendSV are 3).  At 115200 baud a byte
     * arrives every 87 us, and the UART holds only one: miss the window and
     * the next byte overwrites it (ORE). */
    NVIC_SetPriority(USART2_IRQn, 1);
    NVIC_EnableIRQ(USART2_IRQn);
}

uint8_t uart_getc(void)
{
    uint8_t c;
    os_queue_get(&rx_queue, &c);                          /* blocks while empty */
    return c;
}

const volatile uart_stats_t *uart_stats(void) { return &stats; }

/* Free slots in the transmit ring (one is always kept empty).  A caller that
 * must never wait - the OLED mirror - checks this before it prints.  Each
 * '
' becomes two bytes on the way in: count it twice. */
uint32_t uart_tx_room(void)
{
    return (uint32_t)((tx_tail + TX_SIZE - tx_head - 1u) % TX_SIZE);
}

/* ---- the one interrupt, three jobs ----------------------------------------- */
void USART2_IRQHandler(void)
{
    uint32_t isr = USART2->ISR;

    /* Overrun: a second byte arrived before we read the first.  The flag
     * must be cleared - through ICR, write 1 - or it blocks further reception
     * and, with RXNEIE set, keeps the interrupt asserted. */
    if (isr & USART_ISR_ORE) {
        USART2->ICR = USART_ICR_ORECF;
        stats.rx_overruns++;
    }

    if (isr & USART_ISR_RXNE_RXFNE) {
        uint8_t c = (uint8_t)USART2->RDR;                /* reading RDR clears RXNE */
        stats.rx_bytes++;
        if (!os_queue_put_from_isr(&rx_queue, &c)) {     /* never blocks in an ISR  */
            stats.rx_dropped++;
        }
    }

    if ((USART2->CR1 & USART_CR1_TXEIE_TXFNFIE) && (isr & USART_ISR_TXE_TXFNF)) {
        if (tx_tail != tx_head) {
            USART2->TDR = tx_ring[tx_tail];              /* writing TDR clears TXE */
            tx_tail = (uint16_t)((tx_tail + 1u) % TX_SIZE);
            stats.tx_bytes++;
        } else {
            /* Nothing left.  TXE stays set while the UART is idle, so leave
             * TXEIE on and this handler runs forever - lesson 05's endless
             * interrupt, in a new form.  Switch it off until there is data. */
            USART2->CR1 &= ~USART_CR1_TXEIE_TXFNFIE;
        }
    }
}

/* One byte into the ring.  The data is written BEFORE head moves, so the ISR
 * can never see an index pointing at a byte that is not there yet. */
static void tx_put(uint8_t c)
{
    uint16_t next = (uint16_t)((tx_head + 1u) % TX_SIZE);
    while (next == tx_tail) {                  /* full: one slot is kept empty */
        stats.tx_waits++;
        if (os_current_task()) { os_delay(1); }   /* let the ISR drain it       */
    }                                              /* (before os_start: just spin) */
    tx_ring[tx_head] = c;
    tx_head = next;
}

/* The C library's single door to the outside (lesson 04's retarget.c).  This
 * strong definition replaces retarget.c's weak, polled one at link time. */
int _write(int fd, const char *buf, int len)
{
    (void)fd;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n') { tx_put('\r'); }
        tx_put((uint8_t)buf[i]);
    }
    /* Make sure the ISR is draining.  CR1 is also written by the ISR (which
     * clears TXEIE), so the read-modify-write here is a critical section. */
    __disable_irq();
    USART2->CR1 |= USART_CR1_TXEIE_TXFNFIE;
    __enable_irq();
    return len;
}
