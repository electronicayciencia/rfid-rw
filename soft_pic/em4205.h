#include <12F683.h>
#device ADC=8 
#FUSES NOBROWNOUT, NOMCLR

#use delay(internal=8MHz)
#use rs232(baud=9600,parity=N,xmit=PIN_A4,RCV=PIN_A5,bits=8,stream=PORT1)

#define DEBUG_PIN PIN_A5

// adjust PWM duty cycle due to output stage switching time.
#define DC 20
#define FREQ 15


// 1 125kHz cycle = 8us
#define CYCLE 8

// Hysteresis for comparator
#define DEFAULT_THR_H   VREF_HIGH|6
#define DEFAULT_THR_L   VREF_HIGH|4

// RF/32 data rate as default
#define DEFAULT_SEMI_TIME 190

// Output error condition
#define ERR_NOERR 0
#define ERR_READ_TIMEOUT 1
#define ERR_READ_ERROR 2
#define ERR_EMPTY_MESSAGE 3
#define ERR_COMMAND_UNKNOWN 255

// Reading status
#define	READING      0
#define	HALF_BIT     2
#define	READ_ERROR   3
#define	READ_TIMEOUT 4

// Buffer for command c
#define IOBUFF_SIZE 7
// Buffer for command r
#define MAXBUFF_SIZE 36

// Id for command i
#define ID_STRING "Electronicayciencia's EM4205/EM4305 writer. v1.01."

