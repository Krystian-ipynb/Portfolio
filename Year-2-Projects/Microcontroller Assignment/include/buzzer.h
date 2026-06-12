/*
 * buzzer.h
 * Tone generation using TCA0 single-slope PWM on PB0 (TCA0 WO0).
 *
 * Hardware: Piezo buzzer connected to PB0 via TCA0 WO0 (QUTy schematic).
 *
 * Student number: n12455466  →  xy = 66  →  A = 4×66 = 264 Hz
 *
 * Default frequencies (rounded to nearest integer):
 *   Step 0  E(high) = 264 × 2^(5/12)  = 352 Hz
 *   Step 1  C#      = 264 × 2^(-8/12) = 166 Hz  (wait — see below)
 *   Step 2  A       = 264 Hz
 *   Step 3  E(low)  = 264 × 2^(-17/12) = 99 Hz
 *
 * Exact formula: 4xy × 2^(offset/12) where xy=66:
 *   E(high): 264 × 2^(5/12)  ≈ 352 Hz
 *   C#:      264 × 2^(-8/12) ≈ 166 Hz
 *   A:       264 Hz
 *   E(low):  264 × 2^(-17/12) ≈ 99 Hz
 */

#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

/* Initialise TCA0 and PB0 for PWM tone output.                        */
void buzzer_init(void);

/* Play the tone for the given step (0–3). Starts the PWM immediately. */
void buzzer_play(uint8_t step);

/* Stop the buzzer (disables PWM output, pin goes low).                */
void buzzer_stop(void);

/* Increase all tone frequencies by one octave (×2), max 20000 Hz.    */
void buzzer_inc_octave(void);

/* Decrease all tone frequencies by one octave (÷2), min 20 Hz.       */
void buzzer_dec_octave(void);

/* Reset all frequencies to the default values for student n12455466.  */
void buzzer_reset_frequencies(void);

#endif /* BUZZER_H */
