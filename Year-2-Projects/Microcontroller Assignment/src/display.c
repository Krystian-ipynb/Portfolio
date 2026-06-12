#include "display.h"
#include "display_macros.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

/*
 * display.c — 7-segment display driver for the QUTy via SPI0/74HC595.
 *
 * Hardware:
 *   SPI0 MOSI = PC2 -> 74HC595 DS
 *   SPI0 SCK = PC0 -> 74HC595 SHCP
 *   DISP LATCH = PA1 -> 74HC595 STCP (GPIO)
 *   DISP EN  = PB1 -> transistor (HIGH = displays enabled)
 *
 * 74HC595 Q0=E, Q1=D, Q2=C, Q3=G, Q4=B, Q5=A, Q6=F, Q7=DIGIT_SELECT
 * SPI MSB first -> byte bit N drives Q N.
 * Active-LOW segments (0 = ON).
 *
 * Multiplexing: TCB1 ISR fires every 5 ms, alternating digits.
 * SPI0 ISR pulses PA1 latch after each transfer completes.
 */

volatile uint8_t left_byte  = DISP_OFF | DISP_LHS;
volatile uint8_t right_byte = DISP_OFF;

static volatile uint8_t show_left = 1u;

/*
 * Digit encoding (active-LOW, bits 6-0).
 * bit6=F, bit5=A, bit4=B, bit3=G, bit2=C, bit1=D, bit0=E
 */
static const uint8_t SEG_NUM[10u] = {
    0x08u, /* 0: A B C D E F        */
    0x6Bu, /* 1: B C                */
    0x44u, /* 2: A B D E G          */
    0x41u, /* 3: A B C D G          */
    0x23u, /* 4: B C F G            */
    0x11u, /* 5: A C D F G          */
    0x10u, /* 6: A C D E F G        */
    0x4Bu, /* 7: A B C              */
    0x00u, /* 8: all on             */
    0x01u  /* 9: A B C D F G        */
};

ISR(SPI0_INT_vect)
{
    PORTA.OUTSET = PIN1_bm;
    PORTA.OUTCLR = PIN1_bm;
    SPI0.INTFLAGS = SPI_IF_bm;
}

ISR(TCB1_INT_vect)
{
    if (show_left) {
        SPI0.DATA = left_byte;
    } else {
        SPI0.DATA = right_byte;
    }
    show_left ^= 1u;
    TCB1.INTFLAGS = TCB_CAPT_bm;
}

void display_init(void)
{
    /* Remap SPI0 to alternate pins: PC0=SCK, PC2=MOSI.
     * Without this, SPI0 defaults to PORTA and never reaches the
     * 74HC595. (Applying QUTy schematic)        */
    PORTMUX.SPIROUTEA = PORTMUX_SPI0_ALT1_gc;

    /* PA1 = DISP LATCH (STCP): output, start LOW.                    */
    PORTA.OUTCLR = PIN1_bm;
    PORTA.DIRSET = PIN1_bm;

    /* PC0 = SCK, PC2 = MOSI: outputs.                                */
    PORTC.DIRSET = PIN0_bm | PIN2_bm;

    /* PB1 = DISP EN: output HIGH to enable common anodes.            */
    PORTB.OUTSET = PIN1_bm;
    PORTB.DIRSET = PIN1_bm;

    SPI0.CTRLB = SPI_SSD_bm;
    SPI0.INTCTRL = SPI_IE_bm;
    SPI0.CTRLA = SPI_MASTER_bm | SPI_ENABLE_bm;

    /* TCB1: 5 ms @ 3.333 MHz = 16667 clocks.                         */
    TCB1.CTRLB = TCB_CNTMODE_INT_gc;
    TCB1.CCMP = 16667u;
    TCB1.INTCTRL = TCB_CAPT_bm;
    TCB1.CTRLA = TCB_ENABLE_bm;

    display_blank();
}

static void display_set(uint8_t left_seg, uint8_t right_seg)
{
    left_byte = left_seg | DISP_LHS;
    right_byte = right_seg;
}

/* Step patterns:
 *   step 0 (S1): left  E,F on, right blank
 *   step 1 (S2): left  B,C on, right blank
 *   step 2 (S3): left  blank,  right E,F on
 *   step 3 (S4): left  blank,  right B,C on               */
static const uint8_t STEP_L[4u] = {
    DISP_BAR_LEFT, DISP_BAR_RIGHT, DISP_OFF, DISP_OFF
};
static const uint8_t STEP_R[4u] = {
    DISP_OFF, DISP_OFF, DISP_BAR_LEFT, DISP_BAR_RIGHT
};

void display_step(uint8_t step)
{
    if (step >= 4u) { return; }
    display_set(STEP_L[step], STEP_R[step]);
}

void display_blank(void) { display_set(DISP_OFF,  DISP_OFF);  }
void display_success(void) { display_set(DISP_ON,   DISP_ON);   }
void display_fail(void) { display_set(DISP_DASH, DISP_DASH); }

void display_score(uint16_t score)
{
    uint16_t tmp = score;
    while (tmp >= 100u) { tmp -= 100u; }
    uint8_t tens = 0u;
    uint8_t units = (uint8_t)tmp;
    while (units >= 10u) { units -= 10u; tens++; }
    uint8_t d1 = ((score < 100u) && (tens == 0u)) ? DISP_OFF : SEG_NUM[tens];
    display_set(d1, SEG_NUM[units]);
}

void display_raw(uint8_t d1, uint8_t d2)
{
    display_set((uint8_t)(d1 ^ 0x7Fu), (uint8_t)(d2 ^ 0x7Fu));
}
