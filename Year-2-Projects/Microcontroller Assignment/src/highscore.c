#include "highscore.h"
#include "uart.h"
#include <stdint.h>

/*
 * highscore.c
 * Top-5 high-score table (Assessment 2, Section E).
 *
 * The table is held sorted by score (descending). Entries with equal
 * scores keep their insertion order — the spec example explicitly
 * notes "names do not need to be sorted when scores are equal".
 *
 * Storage is file-scope static (SRAM only), private to this module.
 * Power-on zeroes the table; the RESET function does NOT touch it.
 */

static hs_entry_t hs_table[HS_MAX];
static uint8_t hs_count = 0u;

uint8_t hs_qualifies(uint16_t score)
{
    if (hs_count < HS_MAX) { return 1u; }
    if (score > hs_table[hs_count - 1u].score) { return 1u; }
    return 0u;
}

void hs_insert(const char *name, uint16_t score)
{
    uint8_t i;
    uint8_t pos = hs_count;     /* default: append at the end          */
    uint8_t newcnt;

    if (!hs_qualifies(score)) { return; }

    /* Find the first existing entry that the new score beats — that
     * is the insertion position. Entries from `pos` down shift one
     * slot to make room (a high-water mark of HS_MAX entries).        */
    for (i = 0u; i < hs_count; i++)
    {
        if (score > hs_table[i].score) { pos = i; break; }
    }

    if (hs_count < HS_MAX) {
    newcnt = (uint8_t)(hs_count + 1u);
    } else {
    newcnt = HS_MAX;
    }

    /* Shift entries down (working from the bottom upward).            */
    for (i = (uint8_t)(newcnt - 1u); i > pos; i--)
    {
        hs_table[i] = hs_table[i - 1u];
    }

    /* Copy the name (truncated to HS_NLEN), null-terminate.           */
    for (i = 0u; i < HS_NLEN && name[i] != '\0'; i++)
    {
        hs_table[pos].name[i] = name[i];
    }
    hs_table[pos].name[i] = '\0';
    hs_table[pos].score = score;
    hs_count = newcnt;
}

void hs_print(void)
{
    uint8_t i;
    for (i = 0u; i < hs_count; i++)
    {
        uart_puts(hs_table[i].name);
        uart_putc(' ');
        uart_put_uint16(hs_table[i].score);
        uart_putc('\n');
    }
}
