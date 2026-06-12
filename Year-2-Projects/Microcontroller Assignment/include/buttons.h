/*
 * buttons.h
 * Pushbutton debouncer and direct-read for the QUTy.
 *
 * Hardware (QUTy schematic):
 *   S1 = PA4   S2 = PA5   S3 = PA6   S4 = PA7
 * All active-LOW with internal pull-ups (enabled in initialisation.c).
 *
 * The mapping from button to step value:
 *   S1 -> step 0   S2 -> step 1   S3 -> step 2   S4 -> step 3
 */
#ifndef BUTTONS_H
#define BUTTONS_H

#include <stdint.h>

/* Run one tick of the vertical-counter debouncer. Call once every
 * ~5 ms. Returns a bitmask of buttons that have just transitioned
 * from released to pressed (bit0 = S1, bit1 = S2, bit2 = S3,
 * bit3 = S4). Bits for all other transitions are zero.              */
uint8_t buttons_tick(void);

/* Return 1 if the physical button for step n (0-3) is currently
 * held down, 0 otherwise. Reads PORTA.IN directly (no debounce);
 * suitable for "is the user still holding the button" checks.       */
uint8_t button_held(uint8_t n);

#endif /* BUTTONS_H */
