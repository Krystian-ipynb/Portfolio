#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* Initialise TCB0 for 1ms periodic interrupt tick counter.
 * NOTE: TCB1 is initialised inside display_init() for display mux.   */
void timer_init(void);

/* Return current millisecond count since power-on (atomic read).     */
uint32_t tick_now(void);

#endif /* TIMER_H */
