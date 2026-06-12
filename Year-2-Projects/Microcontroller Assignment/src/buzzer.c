#include "buzzer.h"
#include <avr/io.h>
#include <stdint.h>

/*
 * buzzer.c
 * Tone generation via TCA0 single-slope PWM on PB0 (TCA0 WO0).
 *
 * QUTy default clock: CLK_PER = 3.333 MHz.
 *
 * Student n12455466: xy = 66.
 * "4xy" in Table 2 is the 3-digit decimal number 466 (NOT 4*66=264).
 * Confirmed by the spec example: xy = 40 gives A = 440 Hz.
 *
 * Base A = 466 Hz. Offsets from Table 2 are NEGATIVE semitones.
 * Default tone frequencies (freq = 466 * 2^(offset/12)):
 *   Step 0  E(high) = 466 * 2^(-5/12)  = 349.10 Hz
 *   Step 1  C# = 466 * 2^(-8/12)  = 293.56 Hz
 *   Step 2  A = 466 Hz
 *   Step 3  E(low) = 466 * 2^(-17/12) = 174.54 Hz
 *
 * 
 * Octave shifting and the TCA0 prescaler
 * 
 * A single-slope PWM period requires PER <= 65535 (16-bit register).
 * With prescaler DIV1 the lowest reachable frequency is only ~51 Hz,
 * which is NOT low enough: one octave down from E(low) is ~87 Hz, and
 * two octaves down is ~43.6 Hz — below the DIV1 floor.
 *
 * To cover the full 20 Hz - 20 kHz hearing range (spec Section D), the
 * tone period is stored as a 32-bit CLK_PER tick count. Octave down
 * doubles this tick count; octave up halves it. At buzzer_play() time
 * the smallest TCA0 prescaler that brings PER (= ticks/prescaler - 1)
 * within the 16-bit range is selected.
 *
 *   period_ticks = round(F_CPU / freq)
 *   for prescaler P: PER = period_ticks / P - 1   (need PER <= 65535)
 *   CMP0 = (PER + 1) / 2 (50 % duty cycle)
 *
 * Range check:
 *   20 Hz -> ticks = 166667 -> DIV4  -> PER = 41665   (ok)
 *   20000 Hz -> ticks = 167 -> DIV1  -> PER = 166     (ok)
 */

#define NUM_TONES 4u

/* Tick-count limits for the 20 Hz - 20 kHz hearing range.            *
 * ticks = F_CPU / freq.                                              */
#define TICKS_MIN 167UL        /* 20 kHz ceiling                      */
#define TICKS_MAX 166667UL     /* 20 Hz floor                       */

/* Largest period (in prescaled ticks) that still fits PER (+1).       */
#define PER_SPAN 65536UL

/* Default tone periods in CLK_PER ticks (round(F_CPU / freq)).        *
 *   E(high): round(3333333 / 349.10) = 9549                           *
 *   C#: round(3333333 / 293.56) = 11355                          *
 *   A: round(3333333 / 466.00) = 7153                           *
 *   E(low): round(3333333 / 174.54) = 19097                          */
#define TICKS_EHIGH_DEFAULT 9549UL
#define TICKS_CS_DEFAULT 11355UL
#define TICKS_A_DEFAULT 7153UL
#define TICKS_ELOW_DEFAULT 19097UL

/* Per-tone period, in CLK_PER ticks (prescaler-independent).          */
static uint32_t tone_ticks[NUM_TONES];

/* Step currently sounding, or 0xFF when the buzzer is silent.         */
static uint8_t active_step = 0xFFu;

/* 
 * tca_clksel_for — return the CTRLA CLKSEL group code for the smallest
 * prescaler that yields period_ticks / divisor <= PER_SPAN, and write
 * the matching divisor back through *divisor.
 *  */
static uint8_t tca_clksel_for(uint32_t period_ticks, uint32_t *divisor)
{
    if (period_ticks <= PER_SPAN)
    {
        *divisor = 1UL; return TCA_SINGLE_CLKSEL_DIV1_gc;
    }
    if (period_ticks <= PER_SPAN * 2UL)
    {
        *divisor = 2UL; return TCA_SINGLE_CLKSEL_DIV2_gc;
    }
    if (period_ticks <= PER_SPAN * 4UL)
    {
        *divisor = 4UL; return TCA_SINGLE_CLKSEL_DIV4_gc;
    }
    if (period_ticks <= PER_SPAN * 8UL)
    {
        *divisor = 8UL; return TCA_SINGLE_CLKSEL_DIV8_gc;
    }
    if (period_ticks <= PER_SPAN * 16UL)
    {
        *divisor = 16UL; return TCA_SINGLE_CLKSEL_DIV16_gc;
    }
    if (period_ticks <= PER_SPAN * 64UL)
    {
        *divisor = 64UL; return TCA_SINGLE_CLKSEL_DIV64_gc;
    }
    if (period_ticks <= PER_SPAN * 256UL)
    {
        *divisor = 256UL; return TCA_SINGLE_CLKSEL_DIV256_gc;
    }
    *divisor = 1024UL; return TCA_SINGLE_CLKSEL_DIV1024_gc;
}

/* 
 * load_period — compute PER/CMP0/prescaler for `step` and write them
 * to TCA0. Does not change the ENABLE bit or the WO0 output enable.
 *  */
static void load_period(uint8_t step, uint8_t *clksel_out)
{
    uint32_t divisor;
    uint8_t clksel = tca_clksel_for(tone_ticks[step], &divisor);
    uint32_t per = tone_ticks[step] / divisor;

    if (per != 0u) { per -= 1u; }
    if (per > 65535u) { per  = 65535u; }

    TCA0.SINGLE.PER = (uint16_t)per;
    TCA0.SINGLE.CMP0 = (uint16_t)((per + 1u) >> 1);  /* 50 % duty       */

    *clksel_out = clksel;
}

/* Reset all tone periods to the defaults for student n12455466.       */
void buzzer_reset_frequencies(void)
{
    tone_ticks[0] = TICKS_EHIGH_DEFAULT;
    tone_ticks[1] = TICKS_CS_DEFAULT;
    tone_ticks[2] = TICKS_A_DEFAULT;
    tone_ticks[3] = TICKS_ELOW_DEFAULT;
}

/* Initialise TCA0 and PB0. Timer disabled until first buzzer_play().  */
void buzzer_init(void)
{
    buzzer_reset_frequencies();

    /* PB0 = TCA0 WO0 output, start low (silent).                      */
    PORTB.OUTCLR = PIN0_bm;
    PORTB.DIRSET = PIN0_bm;

    /* Single-slope PWM mode. WO0 disabled until play() is called.     */
    TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc;

    /* DIV1 prescaler, timer disabled until first play().              */
    TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc;
}

/* Play tone for step 0-3. Enables WO0 output and starts the timer.    */
void buzzer_play(uint8_t step)
{
    uint8_t clksel;

    if (step >= NUM_TONES) { return; }

    load_period(step, &clksel);
    TCA0.SINGLE.CNT = 0u;         /* restart period for immediate tone  */

    /* Enable WO0 output.                                              */
    TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc
                      | TCA_SINGLE_CMP0EN_bm;
    /* Select prescaler and start the timer.                           */
    TCA0.SINGLE.CTRLA = clksel | TCA_SINGLE_ENABLE_bm;

    active_step = step;
}

/* Silence the buzzer: disable WO0, stop the timer, force PB0 low.     */
void buzzer_stop(void)
{
    /* Remove WO0 override by clearing CMP0EN_bm.                      */
    TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc;
    /* Stop timer (clear ENABLE, keep DIV1).                           */
    TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc;
    /* Force pin low to guarantee silence.                             */
    PORTB.OUTCLR = PIN0_bm;
    active_step = 0xFFu;
}

/* Increase all tone frequencies by one octave (halve the period).
 * Frequencies are clamped to stay at or below 20 kHz.                 */
void buzzer_inc_octave(void)
{
    uint8_t i;
    for (i = 0u; i < NUM_TONES; i++)
    {
        uint32_t new_ticks = tone_ticks[i] >> 1;
        if (new_ticks >= TICKS_MIN) { tone_ticks[i] = new_ticks; }
    }
    /* Spec D/E: apply immediately if a tone is currently sounding.    */
    if (active_step < NUM_TONES)
    {
        uint8_t clksel;
        load_period(active_step, &clksel);
        TCA0.SINGLE.CTRLA = clksel | TCA_SINGLE_ENABLE_bm;
    }
}

/* Decrease all tone frequencies by one octave (double the period).
 * Frequencies are clamped to stay at or above 20 Hz.                  */
void buzzer_dec_octave(void)
{
    uint8_t i;
    for (i = 0u; i < NUM_TONES; i++)
    {
        uint32_t new_ticks = tone_ticks[i] << 1;
        if (new_ticks <= TICKS_MAX) { tone_ticks[i] = new_ticks; }
    }
    /* Spec D/E: apply immediately if a tone is currently sounding.    */
    if (active_step < NUM_TONES)
    {
        uint8_t clksel;
        load_period(active_step, &clksel);
        TCA0.SINGLE.CTRLA = clksel | TCA_SINGLE_ENABLE_bm;
    }
}