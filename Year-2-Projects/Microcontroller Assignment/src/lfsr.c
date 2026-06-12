#include "lfsr.h"
#include <stdint.h>

/*
 * lfsr.c
 * Linear-Feedback Shift Register PRNG (Assessment 2, Section B).
 * Implements the spec's next() function exactly: advance state by one
 * step and return the bottom two bits as the STEP value.
 *
 * The mask 0xE2026E6B is fixed by the spec.
 */
#define LFSR_MASK 0xE2026E6BUL

uint8_t lfsr_step(uint32_t *state)
{
    uint8_t bit = (uint8_t)(*state & 0x01u);
    uint32_t s = *state >> 1;
    if (bit) { s ^= LFSR_MASK; }
    *state = s;
    return (uint8_t)(s & 0x03u);
}
