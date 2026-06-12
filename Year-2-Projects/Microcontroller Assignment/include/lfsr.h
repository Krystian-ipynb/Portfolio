/*
 * lfsr.h
 * Linear-Feedback Shift Register PRNG for the Simon sequence
 * (Assessment 2, Section B).
 *
 * Algorithm:
 *   BIT   <- lsbit(STATE)
 *   STATE <- STATE >> 1
 *   if BIT: STATE ^= MASK
 *   STEP  <- STATE & 0b11
 *
 * MASK = 0xE2026E6B (per spec).
 * Default seed = 0x12455466 (student n12455466).
 */
#ifndef LFSR_H
#define LFSR_H

#include <stdint.h>

/* Default LFSR seed for student n12455466. The seed must be non-zero;
 * an all-zero state is degenerate (the LFSR would stay at zero).      */
#define LFSR_DEFAULT_SEED 0x12455466UL

/* Advance the LFSR pointed to by *state by one step, returning the
 * 2-bit STEP value (0-3) used to drive the buzzer tone and display
 * segments. The state is updated in place.                           */
uint8_t lfsr_step(uint32_t *state);

#endif /* LFSR_H */
