/*
 * uart.h
 * USART0 driver — 9600-8-N-1.
 *
 * Hardware (QUTy schematic):
 *   TXD = PB2   (UART TX to CP2102N)
 *   RXD = PB3   (UART RX from CP2102N)
 */

#ifndef UART_H
#define UART_H

#include <stdint.h>

/* Initialise USART0 at 9600 baud, 8 data bits, no parity, 1 stop bit.*/
void uart_init(void);

/* Transmit one byte, blocking until the TX buffer is ready.           */
void uart_putc(uint8_t c);

/* Transmit a null-terminated string.                                  */
void uart_puts(const char *str);

/* Transmit a uint16_t as decimal ASCII (no leading zeros).            */
void uart_put_uint16(uint16_t val);

/* Return 1 if a received byte is waiting, 0 otherwise (non-blocking). */
uint8_t uart_rx_ready(void);

/* Read one byte from the receive register (call after uart_rx_ready).*/
uint8_t uart_getc(void);

#endif /* UART_H */
