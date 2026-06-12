#include "adc.h"
#include <avr/io.h>
#include <stdint.h>

/*
 * adc.c
 * Potentiometer reader on ADC0 / PA2 / AIN2 (Assessment 2, Section C).
 *
 * The ADC is configured for 8-bit free-running mode in
 * initialisation.c. read_playback_ms() triggers a fresh conversion,
 * waits for RESRDY, and converts the 8-bit result to a delay in
 * milliseconds using the spec's linear interpolation:
 *
 *   delay_ms = 250 + (adc * 1750) / 256
 *
 * The "/ 256" is implemented as a right shift by 8 to satisfy the
 * Criterion 2c "no integer division". With adc spanning 0-255
 * the delay spans 250 ms to 1992 ms, within +-2% of the spec's
 * 250-2000 ms target across the full pot range.
 */

uint16_t read_playback_ms(void)
{
    uint8_t  adc;
    uint32_t off;

    ADC0.COMMAND  = ADC_MODE_SINGLE_8BIT_gc | ADC_START_IMMEDIATE_gc;
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)) { }
    adc = ADC0.RESULT0;
    ADC0.INTFLAGS = ADC_RESRDY_bm;

    off = (uint32_t)adc * 1750u;
    return (uint16_t)(250u + (off >> 8));
}
