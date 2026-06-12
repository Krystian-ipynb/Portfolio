#include "initialisation.h"
#include "timer.h"
#include "display.h"
#include "buzzer.h"
#include "uart.h"
#include <avr/io.h>
#include <stdint.h>

/*
 * initialisation.c
 * Hardware peripheral setup for the QUTy Simon game.
 *
 * Order:
 *   1. timer_init(): TCB0 1ms tick
 *   2. display_init(): SPI0 + TCB1 5ms mux + PA1 latch + PB1 enable
 *   3. buzzer_init(): TCA0 WO0 on PB0
 *   4. ADC init: ADC0 on PA2/AIN2 (potentiometer)
 *   5. Buttons init: PA4-PA7 with internal pull-ups
 *   6. uart_init(): USART0 9600-8-N-1
 *
 * NOTE: sei() is called in main() AFTER this function returns.
 */

void initialisation(void)
{
    /* 1. 1ms tick counter (TCB0).                                     */
    timer_init();

    /* 2. Display (SPI0 + TCB1 multiplexing).                          */
    display_init();

    /* 3. Buzzer (TCA0 WO0 on PB0).                                    */
    buzzer_init();

    /* 4. ADC: PA2 = AIN2 = potentiometer.
     * Using 8-bit free-running mode (matches tutorial09). RESULT0
     * holds the 8-bit result directly (range 0-255). The emulator
     * only simulates 8-bit mode; 12-bit mode causes RESULT1 to read
     * 0 permanently, which would lock the playback delay at 250 ms.
     *
     * Register values:
     *   PIN2CTRL = INPUT_DISABLE — analogue pins should not run the
     *                           digital input buffer.
     *   CTRLB = DIV2          — ADC clock = CLK_PER / 2.
     *   CTRLC = TIMEBASE(4) | REFSEL_VDD
     *                           TIMEBASE = 4 CLK_PER cycles per
     *                           microsecond (correct for 3.33MHz);
     *                           reference = VDD.
     *   CTRLE = 64            — SAMPLEN: sample duration in ADC
     *                           clocks (matches tutorial09).
     *   CTRLF = FREERUN       — continuous conversions; foreground
     *                           just reads RESULT0 when needed.
     *   MUXPOS = AIN2          — PA2 pad.                            */
    PORTA.PIN2CTRL = PORT_ISC_INPUT_DISABLE_gc;
    ADC0.CTRLA = ADC_ENABLE_bm;
    ADC0.CTRLB = ADC_PRESC_DIV2_gc;
    ADC0.CTRLC = (4u << ADC_TIMEBASE_gp) | ADC_REFSEL_VDD_gc;
    ADC0.CTRLE = 64u;
    ADC0.CTRLF = ADC_FREERUN_bm;
    ADC0.MUXPOS = ADC_MUXPOS_AIN2_gc;
    ADC0.COMMAND = ADC_MODE_SINGLE_8BIT_gc | ADC_START_IMMEDIATE_gc;

    /* Wait for the first conversion so RESULT0 is valid before the   *
     * main loop runs. Without this, the first Simon tone always uses  *
     * 250ms delay regardless of pot position (Playback Delay test B/C/D). */
    while (!(ADC0.INTFLAGS & ADC_RESRDY_bm)) {}
    ADC0.INTFLAGS = ADC_RESRDY_bm;

    /* 5. Buttons: PA4-PA7, internal pull-ups (tutorial08 style).      */
    PORTA.PIN4CTRL = PORT_PULLUPEN_bm;
    PORTA.PIN5CTRL = PORT_PULLUPEN_bm;
    PORTA.PIN6CTRL = PORT_PULLUPEN_bm;
    PORTA.PIN7CTRL = PORT_PULLUPEN_bm;

    /* 6. UART.                                                         */
    uart_init();
}