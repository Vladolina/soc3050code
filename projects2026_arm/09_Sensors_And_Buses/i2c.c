/*
 * i2c.c - I2C1 master, polled   (SOC3050 lesson 09)
 *
 * Two wires, many devices, one address byte each.  The STM32C0's I2C block
 * does the bit-level work - start, address, acknowledge, stop - and the
 * software loads CR2 with WHAT to do (address, direction, byte count) and then
 * feeds or drains one byte at a time as ISR asks:
 *
 *   TXIS   ready for the next byte to send       -> write TXDR
 *   RXNE   a byte has arrived                     -> read RXDR
 *   TC     all NBYTES done, no STOP yet (AUTOEND=0) - time for a repeated START
 *   STOPF  a STOP went out: the transaction is over
 *   NACKF  the device did not acknowledge - it is absent, or busy, or refused
 *
 * Nothing can wait forever.  A device that is missing answers with NACK; a bus
 * that is stuck answers with nothing at all, and only a timeout ends that.
 */
#include "stm32c031xx.h"
#include "gpio.h"
#include "i2c.h"

#define SCL_PIN     8u                /* PB8, Arduino D15 */
#define SDA_PIN     9u                /* PB9, Arduino D14 */
#define AF_I2C1     6u                /* both pins: AF6 (datasheet AF table) */

/* Fast mode, 400 kHz, with a 48 MHz I2C clock.  Not derived here: it is the
 * value ST's own NUCLEO-C031C6 examples use, computed by STM32CubeMX for
 * I2CCLK = 48 MHz, rise 100 ns, fall 10 ns. */
#define I2C_TIMING  0x0090273Du

#define SPIN_LIMIT  100000u           /* ~10 ms of polling: longer than any transfer here */

void i2c_init(void)
{
    RCC->IOPENR  |= RCC_IOPENR_GPIOBEN;
    RCC->APBENR1 |= RCC_APBENR1_I2C1EN;

    /* I2C lines are OPEN-DRAIN: a device may only pull a line low, never
     * drive it high, so two devices can never fight.  Something must pull the
     * lines up - on a real board, 4.7 k resistors (most sensor breakouts carry
     * them); here the chip's weak internal pull-ups as well. */
    GPIOB->OTYPER |= (1u << SCL_PIN) | (1u << SDA_PIN);
    pin_pull(GPIOB, SCL_PIN, PULL_UP);
    pin_pull(GPIOB, SDA_PIN, PULL_UP);
    pin_af(GPIOB, SCL_PIN, AF_I2C1);
    pin_af(GPIOB, SDA_PIN, AF_I2C1);

    I2C1->CR1     = 0;                /* TIMINGR may only change with PE = 0 */
    I2C1->TIMINGR = I2C_TIMING;
    I2C1->CR1     = I2C_CR1_PE;
}

/* Wait for any flag in `want`; a NACK or the time limit ends the wait early. */
static int wait_for(uint32_t want)
{
    for (uint32_t i = 0; i < SPIN_LIMIT; i++) {
        uint32_t isr = I2C1->ISR;
        if (isr & I2C_ISR_NACKF) { return I2C_NACK; }
        if (isr & want)          { return I2C_OK; }
    }
    return I2C_TIMEOUT;
}

/* After a NACK the transfer must still be closed with a STOP, or the bus is
 * left claimed and the next START fails.  Then the flags are cleared. */
static int finish(int result)
{
    if (result == I2C_NACK && !(I2C1->ISR & I2C_ISR_STOPF)) {
        I2C1->CR2 |= I2C_CR2_STOP;
    }
    for (uint32_t i = 0; i < SPIN_LIMIT && !(I2C1->ISR & I2C_ISR_STOPF); i++) { }
    I2C1->ICR = I2C_ICR_STOPCF | I2C_ICR_NACKCF;
    return result;
}

/* Every transaction starts from a freshly reset I2C block: PE low resets the
 * state machine and its flags (TIMINGR and the rest of CR1 are kept).  A real
 * chip does not need it - between transactions TXIS is clear - but Wokwi
 * raises TXIS again a little after the STOP (seen 2026-11-01), and a stale
 * TXIS makes the next transfer write byte 1 before the address has gone out:
 * the count is off by one, the transfer hangs, and the OLED stayed black.
 * It also recovers a block left mid-transfer by a timeout.  RM0490: PE must
 * stay low for 3 APB clocks; the read-backs give more than that. */
static void begin(void)
{
    I2C1->CR1 &= ~I2C_CR1_PE;
    while (I2C1->CR1 & I2C_CR1_PE) { }
    (void)I2C1->CR1; (void)I2C1->CR1;
    I2C1->CR1 |= I2C_CR1_PE;
}

static uint32_t cr2(uint8_t addr, size_t n, uint32_t flags)
{
    return ((uint32_t)addr << 1)                         /* 7-bit address in SADD[7:1] */
         | ((uint32_t)n << I2C_CR2_NBYTES_Pos)
         | flags | I2C_CR2_START;
}

int i2c_write_read(uint8_t addr, const uint8_t *w, size_t wn, uint8_t *r, size_t rn)
{
    int rc;
    begin();
    if (wn > 0) {
        /* AUTOEND only if nothing is read afterwards: otherwise stop at TC and
         * turn the bus around with a repeated START - no STOP in between, so
         * no other master can slip in between "which register" and "read it". */
        I2C1->CR2 = cr2(addr, wn, rn ? 0u : I2C_CR2_AUTOEND);
        for (size_t i = 0; i < wn; i++) {
            if ((rc = wait_for(I2C_ISR_TXIS)) != I2C_OK) { return finish(rc); }
            I2C1->TXDR = w[i];
        }
        if (rn == 0) { return finish(wait_for(I2C_ISR_STOPF)); }
        if ((rc = wait_for(I2C_ISR_TC)) != I2C_OK) { return finish(rc); }
    }
    I2C1->CR2 = cr2(addr, rn, I2C_CR2_RD_WRN | I2C_CR2_AUTOEND);
    for (size_t i = 0; i < rn; i++) {
        if ((rc = wait_for(I2C_ISR_RXNE)) != I2C_OK) { return finish(rc); }
        r[i] = (uint8_t)I2C1->RXDR;
    }
    return finish(wait_for(I2C_ISR_STOPF));
}

/* A zero-byte write: START, the address, and the device's ACK or NACK.
 * The whole of an I2C bus scan. */
int i2c_probe(uint8_t addr)
{
    begin();
    I2C1->CR2 = cr2(addr, 0, I2C_CR2_AUTOEND);
    return finish(wait_for(I2C_ISR_STOPF));
}
