#include <em4205.h>

/**************************************************************************
 * GLOBAL DATA
 **************************************************************************/
// Default time threshold between a whole bit and semi bit period (us)
int16 semibit_time = DEFAULT_SEMI_TIME;

// Comparator levels
int comp_middle = DEFAULT_MIDDLE_LEVEL;
int comp_trigger = DEFAULT_TRIGGER_LEVEL;

//Debug of comparator on DEBUG_PIN
// debug = 1: pin change as comparator do
// debug = 2: user defined (hardcoded)
int debug = 0;


/**************************************************************************
 * WRITE FUNCTIONS
 **************************************************************************/


/**** FIRST FIELD STOP
 * A first field stop of 55 RF clocks will be detected in all
 * cases regardless of tag Q factor.
 */
void send_ffs() {
	set_pwm1_duty(0);
	delay_us(55 * CYCLE);
	set_pwm1_duty((int16)DC);
}


/**** SEND 0
 * It is proposed to send a logic "0" by keeping the reader
 * field ON for 18 RF periods and switching it OFF for 14 RF
 * periods. Increasing the field stops up to 23 RF periods 
 * improves communication robustness.
 */
void send_0() {
	delay_us(18 * CYCLE);
	set_pwm1_duty(0);
	delay_us(20 * CYCLE);
	set_pwm1_duty((int16)DC);
}


/**** SEND 1
 * For sending a logic "1", the reader field shall stay ON 
 * for 32 RF periods.
 */
void send_1() {
	delay_us(31 * CYCLE);
}

/**** SEND a buffer
 * Transmit a First Field Stop followed by a Zero and 
 * buffer passed as argument.
 * Input:
 *   n: number of bits.
 *   buff: buffer, left most bit is transmitted first
 */
void send_buff(int n, char *buff) {
	send_ffs();
	send_0();
	
	for (int i = 0; i < n; i++) {
		if (shift_left(buff, IOBUFF_SIZE, 0)) {
			send_1();
		}
		else {
			send_0();	
		}
	}
}

/**************************************************************************
 * READ FUNCTIONS
 **************************************************************************/

/**** READ start
 * Prepare the hardware for reading. Then wait for high level.
 */
void read_start() {
	set_timer1(0);
	clear_interrupt(INT_TIMER1);
	setup_vref(comp_trigger);
	setup_comparator(A1_VR);
	output_low(DEBUG_PIN);
}


/**** READ stop
 * Disable the reading mode.
 */
void read_stop() {
//	disable_interrupts(INT_COMP);
	setup_comparator(NC_NC);	
}


/**** READ wait
 * Wait for line level change to read data.
 * You must call read_start before this function
 * Output:
 *   0: if Timer1 overflows before any input change
 *   1: if it was a short wait (below semibit time limit)
 *   2: if was a long wait
 */
int read_wait() {
	int v = C1OUT;

	while (v == C1OUT) {
		if (interrupt_active(INT_TIMER1)) {
			read_stop();
			return 0;
		}
	}
		
	if (get_timer1() < semibit_time) {
		set_timer1(0);
		return 1;
	}
	else {
		set_timer1(0);
		return 2;
	}
}
	

/**** READ bits
 * Read a number of bits into a buffer. Disable the read mode when finish.
 * We read the bits always as biphase, translation to Manchester is done 
 * later by software.
 *
 * Input:
 *   bits: the number of bits to read
 *   buff: buffer to store the bits
 *   bufflen: buffer length (in bytes)
 *
 *   Note: 
 *     Since shift_right requires an expression tha evaluates
 *     as a constant, bufflen must be IOBUFF_SIZE or MAXBUFF_SIZE
 *
 * Output:
 *   ERR_NOERR        ok, no error
 *   ERR_READ_TIMEOUT read timeout (no chip)
 *   ERR_READ_ERROR   read error (line code not compliant) 
 */
int read_bits(int16 bits, char *buff, int bufflen) {
	int16 bits_read = 0;
	int status = WAITING_START;
	
	while (bits_read < bits) {
		int w = read_wait();

		output_high(DEBUG_PIN); // processing
		
		if (!w) {
			read_stop();
			return ERR_READ_TIMEOUT;
		}

		if (status == WAITING_START) {
			setup_vref(comp_middle);
			status = READING;
			continue;
		}
		
		//is it half period?
		if (w == 1) {
			if (status == HALF_BIT) {
				// it was a Zero!
				status = READING;

				// Warning: shift_right 2nd arg must be a constant
				if (bufflen == IOBUFF_SIZE) {
					shift_right(buff, IOBUFF_SIZE, 0);
				}
				else {
					shift_right(buff, MAXBUFF_SIZE, 0);
				}
				bits_read++;
			}
			else {
				// it is half zero?
				status = HALF_BIT;
			}
		}
		
		//it's whole period
		else if (w == 2) {
			if (status == HALF_BIT) {
				// it's not compliant :(
				status = READ_ERROR;
				read_stop();
				return ERR_READ_ERROR;
			}
			else {
				//it's a One!
				status = READING;

				// Warning: shift_right 2nd arg must be a constant
				if (bufflen == IOBUFF_SIZE) {
					shift_right(buff, IOBUFF_SIZE, 1);
				}
				else {
					shift_right(buff, MAXBUFF_SIZE, 1);
				}
				bits_read++;
			}
		}

		output_low(DEBUG_PIN); // processing

	}
	
	read_stop();
	return ERR_NOERR;
}


/**************************************************************************
 * READER COMMANDS
 **************************************************************************/


/**** Command "c"
 * Send a command to transponder and get the response
 *
 * Syntax: srxxxxxxx
 *    s: (byte) how many bits to send
 *    r: (byte) how many bits to receive
 *    x: (7byte) 56 bits buffer Little-Endian. First bit to send is #55.
 *
 *  Return: axxxxxxx
 *    a: (byte) error condition. 
 *       ERR_NOERR: no error
 *       ERR_READ_TIMEOUT: no response, read timeout
 *       ERR_READ_ERROR: read error, no compliant
 *    x: (7byte) bits read. Little Endian. Bit #0 is last bit read.
 */
void cmd_c() {
	char iobuff[IOBUFF_SIZE];
	int bits_to_send = getc();
	int bits_to_recv = getc();
	
	for (int i=0; i < IOBUFF_SIZE; i++) {
		iobuff[i] = getc();
	}

	send_buff(bits_to_send, &iobuff);
	
	memset(&iobuff, 0, IOBUFF_SIZE); // reuse the same buffer
	
	delay_us(WRITE_READ_PAUSE);

	read_start();
	int res = read_bits(bits_to_recv, &iobuff, IOBUFF_SIZE);
	read_stop();
	
	if (res == ERR_NOERR) {
		putc(ERR_NOERR);
		
		for (i=0; i < IOBUFF_SIZE; i++) {
			putc(iobuff[i]);
		}
	}
	else {
		putc(res);
		return;
	}
}


/**** Command "r"
 * Read 9 words (288 bits) in a row of size MAXBUFF_SIZE
 *
 *  Syntax: r
 *
 *  Return: a(x*36)
 *    a: (byte) error condition. 
 *      ERR_NOERR: no error
 *      ERR_READ_TIMEOUT: no response
 *      ERR_READ_ERROR: read error
 *    	ERR_EMPTY_MESSAGE: no message
 *    x: only if a=0 (36 bytes) bits read. 
 *       Little Endian. Bit #0 is the last bit read.
 */
void cmd_r() {
	char iobuff[MAXBUFF_SIZE];
	int16 semizeros = 2*MAXBUFF_SIZE*8; // message could be 288 zeros

	// read transponder's default message
	memset(&iobuff, 0, MAXBUFF_SIZE);

	read_start();
	
	// Wait for a one
	// corner case: no ones, the message is all zeros
	while (true) {
		int w = read_wait();
		
		if (w == 2)
			break;

		if (w == 0) {
			putc(ERR_READ_TIMEOUT);
			return;
		}
		
		if (w == 1) {
			semizeros--;
		
			if (semizeros == 0) {
				read_stop();
				putc(ERR_EMPTY_MESSAGE);
				return;
			}
		}
	}

	// this prevents read_bits to wait for start
	// va a dar error esta de momento

	int r = read_bits(288, &iobuff, MAXBUFF_SIZE);
	read_stop();

	if (r == 0) {
		putc(ERR_NOERR); // error condition 0
		
		for (int i=0; i < MAXBUFF_SIZE; i++) {
			putc(iobuff[i]);
		}
	}
	else {
		putc(r);
		return;
	}
}


/* Command "i": Identify firmware reader 
   Syntax: None
   Return: a"<Identification>"
     a: (byte) error condition. 0: no error
     Identification: (stringz) Identification string zero-end
*/
void cmd_id() {
	putc(ERR_NOERR);
	puts(ID_STRING);
	putc(0);
}


/* Command "t": Set the semibit threshold in order to change read speed.
   Syntax: rx
     x: 8 bit num. Half of us above than it is considered 1 bit period.
   Return: a
     a: (byte) error condition. 
     	0: no error
     x: only if xx=0 bits read. 
        Little Endian. Bit #0 is the last bit read.
*/
void cmd_t() {
	int arg = getc();
	
	if (arg == 0) {
		putc(ERR_NOERR);
		putc(semibit_time >> 1);
	}
	else {
		semibit_time = arg<<1;
		putc(ERR_NOERR);
	}
}



/**************************************************************************
 * MAIN LOOP
 **************************************************************************/

void main() {
	// Timer1 counts us @8MHz, we use it as read timer
	// overflows at 65ms (we use it as read timeout)
	setup_timer_1(T1_INTERNAL|T1_DIV_BY_2);
	
	// Timer2 sets the oscillator frecuency: 125kHz.
	setup_timer_2(T2_DIV_BY_1,FREQ,1);
	set_pwm1_duty((int16)DC);
	setup_ccp1(CCP_PWM);

	// We won't use the comparator until read
	setup_comparator(NC_NC);

	while(TRUE) {
		int command = getc();
		
		//c: send command to transponder
		if (command == 'c') {
			cmd_c();	
		}
		
		//d: activate debug
		else if (command == 'd') {
			debug = getc();
			putc(ERR_NOERR);
		}

		//h: set comparator levels, first middle then trigger
		else if (command == 'h') {
			comp_middle = getc();
			comp_trigger = getc();
			putc(ERR_NOERR);
		}

		//i: show id
		else if (command == 'i') {
			cmd_id();
		}
		
		//k: reset CPU
		else if (command == 'k') {
			putc(ERR_NOERR);
			reset_cpu();
		}
		
		//r: read raw
		else if (command == 'r') {
			cmd_r();
		}
		
		//t: set semibit threshold
		else if (command == 't') {
			cmd_t();
		}

		//y: enable magnetic field
		else if (command == 'y') {
			set_pwm1_duty((int16)DC);
			putc(ERR_NOERR);
		}
		
		//z: disable magnetic field
		else if (command == 'z') {
			set_pwm1_duty(0);
			putc(ERR_NOERR);
		}

		//anything else
		else {
			putc(ERR_COMMAND_UNKNOWN);
		}
	}

}
