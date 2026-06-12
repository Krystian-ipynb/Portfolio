#include "timer.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

/*
 * timer.c
 * TCB0: 1ms periodic interrupt for tick_now() millisecond counter.
 *
 * NOTE: TCB1 is used by display.c for 5ms display multiplexing.
 * TCB1 is initialised inside display_init() — do NOT configure it here.
 */

static volatile uint32_t tick_ms = 0;

void timer_init(void)
{
    TCB0.CTRLB = TCB_CNTMODE_INT_gc; /* Periodic interrupt mode      */
    TCB0.CCMP = 3333u;              /* 1ms @ 3.333 MHz              */
    TCB0.INTCTRL = TCB_CAPT_bm;
    TCB0.CTRLA = TCB_ENABLE_bm;
}

ISR(TCB0_INT_vect)
{
    tick_ms++;
    TCB0.INTFLAGS = TCB_CAPT_bm;
}

uint32_t tick_now(void)
{
    uint32_t t;
    cli();
    t = tick_ms;
    sei();
    return t;
}
