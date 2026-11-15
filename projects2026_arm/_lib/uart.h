/*
 * uart.h - shared copy of lesson 08's interrupt-driven USART2.  LIBS=uart os
 *
 * Receive:  the USART2 interrupt puts each byte into a lock-free ring, and
 *           one task reads it with uart_getc(), sleeping 1 ms at a time
 *           while it is empty (lesson 08's copy blocks on a kernel queue;
 *           uart.c says why this one does not).
 * Transmit: printf() -> _write() -> a lock-free ring buffer -> the TXE
 *           interrupt feeds the UART one byte at a time.  printf() returns
 *           as soon as the text is in the ring, not when it has been sent.
 */
#ifndef UART_H
#define UART_H

#include <stdint.h>

typedef struct {
    uint32_t rx_bytes;        /* bytes received                                   */
    uint32_t rx_overruns;     /* the UART's own 1-byte buffer overflowed (ORE)    */
    uint32_t rx_dropped;      /* our queue was full: byte thrown away             */
    uint32_t tx_bytes;        /* bytes sent                                       */
    uint32_t tx_waits;        /* times a writer found the ring full and waited    */
} uart_stats_t;

void uart_init(uint32_t pclk, uint32_t baud);   /* call before os_start()          */
uint8_t uart_getc(void);                         /* from ONE task: waits for a byte  */
const volatile uart_stats_t *uart_stats(void);
uint32_t uart_tx_room(void);                    /* bytes printf can add now without waiting */

#endif
