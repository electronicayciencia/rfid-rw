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

// Comparator levels:
//   Level up: 2.29
//   Level down: 1.72
#define DEFAULT_MIDDLE_LEVEL  VREF_HIGH|5
#define DEFAULT_TRIGGER_LEVEL VREF_LOW|11

// RF/32 data rate as default
#define DEFAULT_SEMI_TIME 190

// us to wait between command and response.
// It lets the input settle below comp_trigger_up before start reading
#define WRITE_READ_PAUSE 250

// Output error condition
#define ERR_NOERR           0
#define ERR_READ_TIMEOUT    1
#define ERR_READ_ERROR      2
#define ERR_EMPTY_MESSAGE   3
#define ERR_COMMAND_UNKNOWN 255

// Reading status
#define	READING       0
#define	HALF_BIT      2
#define	READ_ERROR    3
#define	READ_TIMEOUT  4
#define	WAITING_START 5

// Buffer for command c (octets)
#define IOBUFF_SIZE 7
// Buffer for command r (octets)
#define MAXBUFF_SIZE 36

// Id for command i
#define ID_STRING "Electronicayciencia's EM4205/EM4305 writer. v1.02."

