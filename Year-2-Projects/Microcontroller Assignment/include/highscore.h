/*
 * highscore.h
 * Top-5 high-score table for the Simon game (Assessment 2, Section E).
 *
 * The table is stored in SRAM only — it is cleared on power-on but
 * NOT cleared by the UART RESET function (per spec).
 *
 * Names may be up to 20 characters; longer input is truncated.
 */
#ifndef HIGHSCORE_H
#define HIGHSCORE_H

#include <stdint.h>

#define HS_MAX 5u    /* maximum number of stored scores              */
#define HS_NLEN 20u   /* maximum name length                          */

typedef struct
{
    char name[HS_NLEN + 1u];   /* null-terminated                 */
    uint16_t score;
} hs_entry_t;

/* Return 1 if `score` qualifies for inclusion (table has room or
 * score beats the current lowest entry), 0 otherwise.               */
uint8_t hs_qualifies(uint16_t score);

/* Insert (name, score) into the table in score order. The name is
 * truncated to HS_NLEN characters; equal scores keep insertion
 * order (older entry stays above the new one). Silently does
 * nothing if the score does not qualify.                            */
void hs_insert(const char *name, uint16_t score);

/* Transmit the full table via UART, one entry per line, formatted
 * "<name> <score>\n" (descending score order, as the table is
 * already kept sorted).                                             */
void hs_print(void);

#endif /* HIGHSCORE_H */
