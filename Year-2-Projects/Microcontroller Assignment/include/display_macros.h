#ifndef DISPLAY_MACROS_H
#define DISPLAY_MACROS_H

/*
 * display_macros.h
 * Segment bitmasks for the QUTy 7-segment display.
 *
 * Hardware (QUTy schematic, 74HC595 outputs):
 *   Q0=E, Q1=D, Q2=C, Q3=G, Q4=B, Q5=A, Q6=F, Q7=DIGIT_SELECT
 *
 * SPI0 sends MSB first, so byte bit N maps to Q N:
 *   bit7 = Q7 = DIGIT SELECT  (1 = left digit active)
 *   bit6 = Q6 = F
 *   bit5 = Q5 = A
 *   bit4 = Q4 = B
 *   bit3 = Q3 = G
 *   bit2 = Q2 = C
 *   bit1 = Q1 = D
 *   bit0 = Q0 = E
 *
 * Active-LOW: 0 = segment ON, 1 = segment OFF.
 * Bits 6-0 carry segment data; bit 7 is the digit select.
 */

/* Digit select: set bit7 to drive the LEFT digit.                    */
#define DISP_LHS 0x80u

/* Blank / fully-on / dash patterns (bits 6-0 only).                  */
#define DISP_OFF 0x7Fu   /* All segments OFF (blank)                */
#define DISP_ON 0x00u   /* All 7 segments ON (SUCCESS)             */
#define DISP_DASH 0x77u   /* G segment only ON (FAIL dash)           */
                           /* G=bit3 -> 0x7F & ~(1<<3) = 0x77        */

/*
 * Simon step bar patterns (bits 6-0 only, no DISP_LHS).
 *
 * E,F on — left vertical bar (S1 on digit 1, S3 on digit 2):
 *   E=bit0, F=bit6 -> clear bits 0,6 in 0x7F -> 0x3E
 *
 * B,C on — right vertical bar (S2 on digit 1, S4 on digit 2):
 *   B=bit4, C=bit2 -> clear bits 4,2 in 0x7F -> 0x6B
 */
#define DISP_BAR_LEFT 0x3Eu   /* E,F on  (left  vertical bar)      */
#define DISP_BAR_RIGHT 0x6Bu   /* B,C on  (right vertical bar)      */

#endif /* DISPLAY_MACROS_H */
