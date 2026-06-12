/*
 * uart.c
 * USART0 driver, 9600-8-N-1.
 *
 * Hardware: TXD = PB2, RXD = PB3 (QUTy schematic).
 *
 * Baud rate register at 3.333 MHz:
 *   BAUD = (F_CPU * 64) / (S * baud)  [normal async, S=16]
 *        = (3333333 * 64) / (16 * 9600)
 *        = 213333333 / 153600
 *        = 1389
 */

#include <avr/io.h>
#include <stdint.h>
#include "uart.h"

#define USART_BAUD_REG 1389u

void uart_init(void)
{
    PORTB.DIRSET = PIN2_bm;             /* PB2 = TXD output              */
    USART0.BAUD  = USART_BAUD_REG;
    USART0.CTRLC = USART_CHSIZE_8BIT_gc
                 | USART_PMODE_DISABLED_gc
                 | USART_SBMODE_1BIT_gc;
    USART0.CTRLB = USART_TXEN_bm | USART_RXEN_bm;
}

void uart_putc(uint8_t c)
{
    while (!(USART0.STATUS & USART_DREIF_bm)) {}
    USART0.TXDATAL = c;
}

void uart_puts(const char *str)
{
    while (*str) { uart_putc((uint8_t)*str); str++; }
}

void uart_put_uint16(uint16_t val)
{
    static const uint16_t POW10[5] = {10000u, 1000u, 100u, 10u, 1u};
    uint8_t i;
    uint8_t leading = 1u;
    for (i = 0u; i < 5u; i++)
    {
        uint8_t digit = 0u;
        while (val >= POW10[i]) { val -= POW10[i]; digit++; }
        if (digit != 0u || !leading || i == 4u)
        {
            uart_putc((uint8_t)('0' + digit));
            leading = 0u;
        }
    }
}

uint8_t uart_rx_ready(void)
{
    return (USART0.STATUS & USART_RXCIF_bm) ? 1u : 0u;
}

uint8_t uart_getc(void)
{
    return USART0.RXDATAL;
}
