#include <12F683.h>
#device ADC=8

#FUSES NOBROWNOUT, NOMCLR

#use delay(internal=8MHz)
#use rs232(baud=9600,parity=N,xmit=PIN_A4,RCV=PIN_A5,bits=8,stream=PORT1)

// adjust PWM duty cycle due to output stage switching time.
#define DC 20

// 1 125kHz cycle = 8us
#define CYCLE 8

// Hysteresis for comparator
#define THR_H   VREF_HIGH|6
#define THR_L   VREF_HIGH|4

// Reading status
#define	READING    0
#define	HALF_BIT   2
#define	READ_ERROR 3

// Buffer por command c
#define IOBUFF_SIZE 7

// Id for command i
#define ID_STRING "Electronicayciencia's EM4205/EM4305 writer. v1.00."
