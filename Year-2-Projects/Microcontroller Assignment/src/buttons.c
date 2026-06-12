#include "buttons.h"
#include <avr/io.h>
#include <stdint.h>

/*
 * buttons.c
 * Vertical-counter debouncer for the four QUTy pushbuttons.
 *
 * The vertical-counter algorithm (Ganssle / Tutorial 08) maintains a
 * 2-bit counter per button across successive samples. A button is
 * considered "settled" only after both counter bits change in the
 * same tick — at the 5 ms sample rate this gives roughly 10-15 ms of
 * mechanical-bounce rejection.
 *
 * PA4..PA7 are active-LOW with internal pull-ups, so a pressed
 * button reads 0 and a released button reads 1.
 */

uint8_t buttons_tick(void)
{
    /* Function-local statics keep the debouncer state private.       *
     * pb_state mirrors the debounced level of all 8 PORTA bits;      *
     * pb_cnt0/pb_cnt1 form a 2-bit per-pin sample counter.           */
    static uint8_t pb_state = 0xFFu;   /* assume all released at boot */
    static uint8_t pb_cnt0  = 0u;
    static uint8_t pb_cnt1  = 0u;

    uint8_t raw = PORTA.IN;
    uint8_t changed = raw ^ pb_state;
    uint8_t old_state;
    uint8_t delta;
    uint8_t pressed;

    pb_cnt1 = (uint8_t)((pb_cnt1 ^ pb_cnt0) & changed);
    pb_cnt0 = (uint8_t)((uint8_t)(~pb_cnt0) & changed);
    old_state = pb_state;
    pb_state ^= (uint8_t)(pb_cnt1 & pb_cnt0);

    /* delta marks the bits whose DEBOUNCED state just changed.
     * Mask with ~pb_state to keep only release->press transitions
     * (active-LOW: 0 = pressed), so release events don't fire.       */
    delta = old_state ^ pb_state;
    pressed = (uint8_t)(delta & (uint8_t)(~pb_state));

    /* Buttons are on the high nibble of PORTA — shift down to bits
     * 0..3 so caller sees S1->bit0, S2->bit1, S3->bit2, S4->bit3.    */
    return (uint8_t)((pressed >> 4) & 0x0Fu);
}

uint8_t button_held(uint8_t n)
{
    /* Pressed = pin reads LOW. n is 0..3 -> port pin (n + 4).        */
    uint8_t mask = (uint8_t)(1u << (n + 4u));
    return (PORTA.IN & mask) ? 0u : 1u;
}
