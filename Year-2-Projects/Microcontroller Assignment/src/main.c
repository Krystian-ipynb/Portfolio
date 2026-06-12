/*
 * main.c: EGB202 Assessment 2: Simon Game
 * Student: n12455466
 *
 * This file holds the game's top-level state machine and the
 * UART command parser. Self-contained subsystems live in their
 * own modules:
 *
 *   timer.c: 1 ms periodic tick (TCB0)
 *   display.c: 7-segment driver + multiplexer (SPI0, TCB1)
 *   buzzer.c: tone generator (TCA0 PWM, prescaler switching)
 *   uart.c: USART0 driver (9600-8-N-1)
 *   adc.c: potentiometer reader (ADC0 / PA2)
 *   buttons.c: S1-S4 debouncer (PA4-PA7)
 *   lfsr.c: PRNG (mask 0xE2026E6B)
 *   highscore.c: top-5 score table (UART output, SRAM)
 *   initialisation.c: hardware setup, called once from main()
 *
 * State transitions:
 *   SIMON_GENERATE -> SIMON_PLAY_ON -> SIMON_PLAY_OFF -> SIMON_GENERATE
 *   -> (seq_idx==seq_len) -> AWAITING_INPUT
 *
 *   AWAITING_INPUT -> HANDLE_INPUT -> EVALUATE_INPUT
 *     -> (correct, more steps) -> AWAITING_INPUT
 *     -> (correct, all done) -> ST_SUCCESS -> SIMON_GENERATE
 *     -> (wrong) -> ST_FAIL
 *
 *   ST_FAIL -> DISP_SCORE -> DISP_BLANK -> SIMON_GENERATE
 *   -> NAME_ENTRY -> SIMON_GENERATE
 *
 * High-score entry is only prompted when UART gameplay keys were
 * used in the current game (spec Section A: high score is only
 * required when gameplay through UART is implemented).
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include "initialisation.h"
#include "timer.h"
#include "display.h"
#include "buzzer.h"
#include "uart.h"
#include "lfsr.h"
#include "adc.h"
#include "buttons.h"
#include "highscore.h"

/* 
 * LFSR state (Section B)
 * The LFSR algorithm itself lives in lfsr.c; this file owns the
 * three running-state variables and the pending-seed bookkeeping.
 *
 * game_lfsr — advances freely through the PRNG stream.
 *            Snapshotted into seq_lfsr at each new game start.
 *             Advanced seq_len steps on SUCCESS/FAIL.
 * seq_lfsr — snapshot of game_lfsr at game start. Fixed for the
 *            game; Simon and user both regenerate steps from a
 *            LOCAL copy of seq_lfsr, never modifying it here.
 * current_seed — the seed currently in effect. Updated whenever a
 *              pending SEED is applied. RESET resets the LFSR back
 *              to current_seed (spec: "back to the seed").
 * */
static uint32_t game_lfsr = LFSR_DEFAULT_SEED;
static uint32_t seq_lfsr = LFSR_DEFAULT_SEED;
static uint32_t current_seed = LFSR_DEFAULT_SEED;

/* Pending seed from a UART SEED command. Applied at the next game
 * boundary (SUCCESS, FAIL/DISP_BLANK, NAME_ENTRY, or RESET) per spec. */
static uint32_t pending_seed = LFSR_DEFAULT_SEED;
static uint8_t pending_seed_set = 0u;

/* queued_input: a button press received in HANDLE_INPUT must survive
 * until the next AWAITING_INPUT tick so back-to-back presses are not
 * dropped (e.g. simultaneous release of S4 and press of S3).         */
static uint8_t queued_input = 0xFFu;

/* 
 * STATE MACHINE
 * */
typedef enum {
    SIMON_GENERATE,
    SIMON_PLAY_ON,
    SIMON_PLAY_OFF,
    AWAITING_INPUT,
    HANDLE_INPUT,
    EVALUATE_INPUT,
    ST_SUCCESS,
    ST_FAIL,
    DISP_SCORE,
    DISP_BLANK,
    NAME_ENTRY
} game_state_t;

static game_state_t state = SIMON_GENERATE;
static uint16_t seq_len = 1u;
static uint16_t seq_idx = 0u;
static uint8_t step = 0u;
static uint8_t user_step = 0u;
static uint16_t pb_ms = 250u;
static uint32_t deadline = 0u;
static uint16_t fail_score = 0u;

/* Track whether UART gameplay keys were used this game.
 * High score is only prompted when UART gameplay was active (spec A/E). */
static uint8_t uart_gameplay_used = 0u;

/* UART SEED collection.                                               */
static uint8_t seed_buf[8u];
static uint8_t seed_bidx = 0u;
static uint8_t seed_active = 0u;
static uint8_t seed_valid = 1u;

/* NAME ENTRY.                                                         */
static char name_buf[HS_NLEN + 1u];
static uint8_t  name_len = 0u;
static uint32_t name_deadline = 0u;

/* 
 * uart_poll — consume one UART byte if ready.
 * Returns gameplay step (0-3) or 0xFF.
 * RESET sets state = SIMON_GENERATE as a side effect.
 * */
static uint8_t uart_poll(void)
{
    uint8_t c;
    if (!uart_rx_ready()) { return 0xFFu; }
    c = uart_getc();

    /* SEED payload collection: exactly 8 characters are consumed after
     * the SEED key, regardless of their value (spec Section E). If ANY
     * of those 8 characters is not a lowercase hex digit, the new seed
     * is discarded — but all 8 characters are still consumed so that a
     * following gameplay key (the 9th character) is handled correctly. */
    if (seed_active)
    {
        uint8_t nibble = 0u;
        uint8_t valid  = 0u;
        if (c >= '0' && c <= '9') { nibble = (uint8_t)(c - '0'); valid = 1u; }
        else if (c >= 'a' && c <= 'f') { nibble = (uint8_t)(c - 'a' + 10u); valid = 1u; }

        if (!valid) { seed_valid = 0u; }      /* mark payload invalid     */
        seed_buf[seed_bidx++] = nibble;

        if (seed_bidx == 8u)
        {
            if (seed_valid)
            {
                uint32_t ns = 0u;
                uint8_t  i;
                for (i = 0u; i < 8u; i++) { ns = (ns << 4) | (uint32_t)seed_buf[i]; }
                pending_seed = ns;
                pending_seed_set = 1u;
            }
            seed_active = 0u;
            seed_bidx = 0u;
        }
        return 0xFFu;
    }

    switch (c)
    {
        /* Gameplay keys: set uart_gameplay_used flag.                 */
        case '1': case 'q': uart_gameplay_used = 1u; return 0u;
        case '2': case 'w': uart_gameplay_used = 1u; return 1u;
        case '3': case 'e': uart_gameplay_used = 1u; return 2u;
        case '4': case 'r': uart_gameplay_used = 1u; return 3u;

        /* INC/DEC FREQ: immediate effect (spec Section D/E).          */
        case ',': case 'k': buzzer_inc_octave(); return 0xFFu;
        case '.': case 'l': buzzer_dec_octave(); return 0xFFu;

        /* SEED: begin collecting 8-character hex payload.             */
        case '9': case 'o':
            seed_active = 1u;
            seed_bidx = 0u;
            seed_valid = 1u;
            return 0xFFu;

        /* RESET: end game, reset LFSR/frequencies/sequence.
         * Resets the sequence seed back to the original student seed,
         * but a pending SEED is RETAINED — spec: a new seed applies to
         * the next game "whether it is a result of a win, a loss, or
         * the RESET function". High score table is NOT cleared.        */
        case '0': case 'p':
            buzzer_stop();
            display_blank();
            buzzer_reset_frequencies();
            /* Reset the sequence seed back to the seed currently in
             * effect. If a SEED command is still pending, it takes
             * effect now (spec: a new seed applies to the next game,
             * "whether ... a win, a loss, or the RESET function").    */
            if (pending_seed_set)
            {
                current_seed = pending_seed;
                pending_seed_set = 0u;
            }
            game_lfsr = current_seed;
            seq_lfsr = current_seed;
            seed_active = 0u;
            seed_bidx = 0u;
            seq_len = 1u;
            seq_idx = 0u;
            uart_gameplay_used = 0u;
            queued_input = 0xFFu;
            state = SIMON_GENERATE;
            return 0xFFu;

        default: return 0xFFu;
    }
}

/* 
 * state_machine — called every main loop iteration (non-blocking).
 *  */
static void state_machine(void)
{
    /* Button-poll timestamp is local to this function only.           */
    static uint32_t pb_last_ms = 0u;

    uint32_t now = tick_now();
    uint8_t btn = 0u;
    uint8_t uart_in;
    uint8_t input;

    /* Poll buttons every 5ms (tutorial08 rate).                       */
    if ((now - pb_last_ms) >= 5u)
    {
        pb_last_ms = now;
        btn = buttons_tick();
    }

    /* Poll UART (skipped during NAME_ENTRY which handles it inline).  */
    uart_in = 0xFFu;
    if (state != NAME_ENTRY) { uart_in = uart_poll(); }

    /* Merge physical button and UART into a single input step value.  */
    input = 0xFFu;
    if (uart_in != 0xFFu)
    {
        input = uart_in;
    }
    else if (btn)
    {
        if (btn & 0x01u) { input = 0u; }
        else if (btn & 0x02u) { input = 1u; }
        else if (btn & 0x04u) { input = 2u; }
        else { input = 3u; }
    }

    /* If a valid input arrived while we were in HANDLE_INPUT (not       *
     * AWAITING_INPUT), save it so it isn't lost when the state machine  *
     * transitions HANDLE_INPUT->EVALUATE_INPUT->AWAITING_INPUT across   *
     * consecutive ticks (e.g. simultaneous release of S4 + press of S3) */
    if (input != 0xFFu && state == HANDLE_INPUT)
    {
        queued_input = input;
    }
    /* In AWAITING_INPUT, fall back to the saved input if nothing new.   */
    if (input == 0xFFu && state == AWAITING_INPUT && queued_input != 0xFFu)
    {
        input = queued_input;
        queued_input = 0xFFu;
    }

    switch (state)
    {
        /* 
         * SIMON_GENERATE
         * When seq_idx == seq_len: Simon's turn is done; go to user.
         * Otherwise: regenerate step seq_idx on-the-fly from seq_lfsr
         * using a local copy, play tone and display for 50% of delay.
         * Potentiometer read at the start of each tone (spec C).
         * */
        case SIMON_GENERATE:
        {
            if (seq_idx == seq_len)
            {
                /* Sequence fully played. seq_lfsr stays fixed.        */
                seq_idx = 0u;
                state = AWAITING_INPUT;
                break;
            }

            /* Regenerate step for seq_idx using a local LFSR copy.   */
            {
                uint32_t rs = seq_lfsr;
                uint16_t j;
                for (j = 0u; j <= seq_idx; j++) { step = lfsr_step(&rs); }
            }

            pb_ms = read_playback_ms();
            now = tick_now();          /* re-capture: ADC may block  */
            buzzer_play(step);
            display_step(step);
            deadline = now + (uint32_t)(pb_ms >> 1);
            state = SIMON_PLAY_ON;
            break;
        }

        /* 
         * SIMON_PLAY_ON
         * Tone and display active for 50% of playback delay.
         *  */
        case SIMON_PLAY_ON:
        {
            if (now < deadline) { break; }
            buzzer_stop();
            display_blank();
            deadline = now + (uint32_t)(pb_ms >> 1);
            state = SIMON_PLAY_OFF;
            break;
        }

        /* 
         * SIMON_PLAY_OFF
         * Silent/blank for 50% of playback delay, then next step.
         *  */
        case SIMON_PLAY_OFF:
        {
            if (now < deadline) { break; }
            seq_idx++;
            state = SIMON_GENERATE;
            break;
        }

        /* 
         * AWAITING_INPUT
         * Wait for user button press or UART gameplay key.
         * No mandatory delay — user can press immediately after Simon.
         *  */
        case AWAITING_INPUT:
        {
            if (input == 0xFFu) { break; }
            user_step = input;
            pb_ms = read_playback_ms();
            now = tick_now();          /* re-capture: ADC may block  */
            buzzer_play(user_step);
            display_step(user_step);
            deadline = now + (uint32_t)(pb_ms >> 1);
            state = HANDLE_INPUT;
            break;
        }

        /* 
         * HANDLE_INPUT
         * Keep tone/display active for at least 50% of playback delay.
         * Extend deadline while physical button is held (spec A).
         * UART keys are instantaneous — no extension (spec E).
         * When deadline expires: stop, get expected step, advance idx.
         *  */
        case HANDLE_INPUT:
        {
            /* Extend while physically held: prevent expiry, don't add  *
             * extra 125ms after release. Implements max(hold, 50%del). */
            if (button_held(user_step) && (now >= deadline))
            {
                deadline = now + 1u;
            }
            if (now < deadline) { break; }

            buzzer_stop();
            display_blank();

            /* Regenerate expected step at seq_idx from seq_lfsr.      */
            {
                uint32_t rs = seq_lfsr;
                uint16_t j;
                for (j = 0u; j <= seq_idx; j++) { step = lfsr_step(&rs); }
            }
            seq_idx++;
            state = EVALUATE_INPUT;
            break;
        }

        /* 
         * EVALUATE_INPUT
         * Compare user_step to the expected step.
         *  */
        case EVALUATE_INPUT:
        {
            if (user_step == step)
            {
                if (seq_idx < seq_len)
                {
                    /* More steps to validate.                         */
                    state = AWAITING_INPUT;
                    break;
                }

                /* Full sequence matched — SUCCESS.
                 * Display and UART while SUCCESS pattern is shown.   */
                display_success();
                uart_puts("SUCCESS\n");
                uart_put_uint16(seq_len);
                uart_putc('\n');
                deadline = now + (uint32_t)pb_ms;

                /* Advance game_lfsr seq_len steps from seq_lfsr.
                 * seq_lfsr stays unchanged — only game_lfsr advances. */
                game_lfsr = seq_lfsr;
                {
                    uint16_t k;
                    for (k = 0u; k < seq_len; k++)
                    {
                        (void)lfsr_step(&game_lfsr);
                    }
                }
                seq_len++;
                seq_idx = 0u;

                state = ST_SUCCESS;
            }
            else
            {
                /* Wrong input — GAME OVER.
                 * Display FAIL and send UART while pattern is shown.  */
                fail_score = seq_len;
                display_fail();
                uart_puts("GAME OVER\n");
                uart_put_uint16(fail_score);
                uart_putc('\n');
                deadline = now + (uint32_t)pb_ms;

                /* Advance game_lfsr seq_len steps past seq_lfsr so
                 * the next game continues from there (spec Section B).*/
                game_lfsr = seq_lfsr;
                {
                    uint16_t k;
                    for (k = 0u; k < seq_len; k++)
                    {
                        (void)lfsr_step(&game_lfsr);
                    }
                }

                state = ST_FAIL;
            }
            break;
        }

        /* 
         * ST_SUCCESS
         * SUCCESS pattern (all segments) shown for full playback delay.
         * Apply any pending seed so next level uses the new sequence.
         *  */
        case ST_SUCCESS:
        {
            if (now < deadline) { break; }
            display_blank();

            /* Apply pending seed: new level plays from new seed.       */
            if (pending_seed_set)
            {
                current_seed = pending_seed;
                game_lfsr = pending_seed;
                seq_lfsr = pending_seed;
                pending_seed_set = 0u;
            }

            state = SIMON_GENERATE;
            break;
        }

        /* 
         * ST_FAIL
         * FAIL pattern (G segment dash) shown for full playback delay.
         *  */
        case ST_FAIL:
        {
            if (now < deadline) { break; }
            display_score(fail_score);
            deadline = now + (uint32_t)pb_ms;
            state = DISP_SCORE;
            break;
        }

        /* 
         * DISP_SCORE
         * Score shown on display for full playback delay.
         * */
        case DISP_SCORE:
        {
            if (now < deadline) { break; }
            display_blank();
            deadline = now + (uint32_t)pb_ms;
            state = DISP_BLANK;
            break;
        }

        /* 
         * DISP_BLANK
         * Blank display for full playback delay. Apply pending seed.
         * Only prompt for high score if UART gameplay was used (spec A).
         *  */
        case DISP_BLANK:
        {
            if (now < deadline) { break; }

            /* Apply pending SEED before next game.                    */
            if (pending_seed_set)
            {
                current_seed = pending_seed;
                game_lfsr = pending_seed;
                pending_seed_set = 0u;
            }

            seq_lfsr = game_lfsr;
            seq_len = 1u;
            seq_idx = 0u;
            queued_input = 0xFFu;

            /* High score only applies when UART gameplay was used.    */
            if (uart_gameplay_used && hs_qualifies(fail_score))
            {
                uart_puts("Enter name: \n");
                name_len = 0u;
                name_buf[0u] = '\0';
                name_deadline = now + 5000u;
                state = NAME_ENTRY;
            }
            else
            {
                uart_gameplay_used = 0u;
                state = SIMON_GENERATE;
            }
            break;
        }

        /* 
         * NAME_ENTRY
         * Collect player name via UART (max 20 chars).
         * Timeout: 5s from prompt or 5s from last char received.
         * SEED collection pauses during name entry (spec Section E).
         *  */
        case NAME_ENTRY:
        {
            uint8_t done = (now >= name_deadline) ? 1u : 0u;

            while (uart_rx_ready() && !done)
            {
                uint8_t ch = uart_getc();
                if (ch == '\r') { continue; }
                if (ch == '\n') { done = 1u; break; }
                if (name_len < HS_NLEN)
                {
                    name_buf[name_len++] = (char)ch;
                    name_buf[name_len] = '\0';
                    /* Reset 5s inactivity timer on each new char.     */
                    name_deadline = now + 5000u;
                }
            }

            if (done)
            {
                hs_insert(name_buf, fail_score);
                hs_print();

                /* Apply pending SEED if collected during name entry.  */
                if (pending_seed_set)
                {
                    current_seed = pending_seed;
                    game_lfsr = pending_seed;
                    pending_seed_set = 0u;
                }

                seq_lfsr = game_lfsr;
                seq_len = 1u;
                seq_idx = 0u;
                uart_gameplay_used = 0u;
                state = SIMON_GENERATE;
            }
            break;
        }

        /* 
         * Default: recover from any invalid state gracefully.
         *  */
        default:
        {
            buzzer_stop();
            display_blank();
            state = SIMON_GENERATE;
            break;
        }
    }
}

/* 
 * main
 *
 * All state variables are file-scope static and already initialised
 * at their declaration sites, so main() does not re-assign them
 * (Criterion 2c: redundant writes eliminated). main() only performs
 * the actions that cannot be done at declaration time: disable
 * interrupts, run hardware initialisation, enable interrupts, then
 * enter the main loop.
 *  */
int main(void)
{
    cli();
    initialisation();
    sei();

    for (;;) { state_machine(); }
    return 0;
}