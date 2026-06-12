/*
 * adc.h
 * Potentiometer reader on ADC0 / PA2 / AIN2 (Assessment 2, Section C).
 *
 * The ADC peripheral is configured for 8-bit free-running mode in
 * initialisation.c; this module only reads the result and converts
 * it to a playback delay in milliseconds.
 *
 * Mapping (linear interpolation, spec C):
 *   adc = 0   -> 250 ms
 *   adc = 255 -> 2000 ms
 *   delay_ms = 250 + (adc * 1750) / 256   (computed via bit shift)
 */
#ifndef ADC_H
#define ADC_H

#include <stdint.h>

/* Start an ADC conversion, wait for it to complete, and return the
 * resulting playback delay in milliseconds (range 250-2000).        */
uint16_t read_playback_ms(void);

#endif /* ADC_H */
