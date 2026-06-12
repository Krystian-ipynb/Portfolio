#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

/* Initialise SPI0, PA1 latch, PB1 enable, and TCB1 for multiplexing. */
void display_init(void);

/* Show the segment pattern for a given STEP value (0-3).              */
void display_step(uint8_t step);

/* Blank both digits (all segments off).                               */
void display_blank(void);

/* SUCCESS pattern: all 7 segments on both digits.                     */
void display_success(void);

/* FAIL pattern: segment G (dash) on both digits.                      */
void display_fail(void);

/* Display a decimal score (leading zero suppressed unless score>=100).*/
void display_score(uint16_t score);

/* Write active-HIGH segment masks directly (bit0=A...bit6=G).         */
void display_raw(uint8_t digit1_segs, uint8_t digit2_segs);

#endif /* DISPLAY_H */
