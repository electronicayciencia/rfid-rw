#include <em4205.h>

/**************************************************************************
 * GLOBAL DATA
 **************************************************************************/
// TMR1 value when last comparator change
// you must clear this variable once used 
// since a status change could happen while you are proccesing last one.
// read_wait will not wait anymore if you forget to clear this variable.
int16 last_change_lapse;

// Reader status
int8 status;

// Default time threshold between a whole bit and semi bit period (us)
int16 semibit_time = DEFAULT_SEMI_TIME;

// Comparator hysteresis
int8 thr_h = DEFAULT_THR_H;
int8 thr_l = DEFAULT_THR_L;

//Debug of comparator on DEBUG_PIN
// debug = 1: pin change as comparator do
// debug = 2: half bit periods
int debug = 0;



/**************************************************************************
 * INTERRUPT SERVICE FUNCTIONS
 **************************************************************************/

#INT_TIMER1
void tmr1_overflow() {
	disable_interrupts(INT_TIMER1);
	status = READ_TIMEOUT;
}


#INT_COMP
/* Register the moment of last input change */
void comp_change() {
	int16 us = get_timer1();
	set_timer1(0); // reset reading watchdog
	
	// ignore changes shorter than 3 periods
	//if (us < 24)
	//	return;

	// Inverting input:
	// C1OUT = 1:  V < Thr
	// C1OUT = 0:  V > Thr
	if (C1OUT){
		if (debug == 1) output_low(DEBUG_PIN);
		setup_vref(thr_h);
	}
	else {
		if (debug == 1) output_high(DEBUG_PIN);
		setup_vref(thr_l);
	}
	
	last_change_lapse = us;
}


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
	status = READING; // clears previous error condition	
	set_timer1(0);
	last_change_lapse = 0;
	
	clear_interrupt(INT_TIMER1);
	clear_interrupt(INT_COMP);
	
	setup_vref(thr_h);
	setup_comparator(A1_VR);
	if (debug == 1) output_low(DEBUG_PIN);
	if (debug == 2) output_low(DEBUG_PIN);
	
	enable_interrupts(INT_TIMER1);
	enable_interrupts(INT_COMP);
}


/**** READ stop
 * Disable the reading mode.
 */
void read_stop() {
	disable_interrupts(INT_TIMER1);
	disable_interrupts(INT_COMP);
	setup_comparator(NC_NC);	
}


/**** READ wait
 * Wait for line level change to read data.
 * You must call read_start before this function
 * Output:
 *   1: if no error
 *   0: if Timer1 overflows before any input change
 */
int read_wait() {
	while (last_change_lapse == 0 && 
	       status != READ_TIMEOUT) {};

	if (status == READ_TIMEOUT) {
		read_stop();
		return 0;
	}
	else {
		return 1;
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
	last_change_lapse = 0;
	
	while (status != READ_ERROR && bits_read < bits) {
		if (!read_wait())
			return ERR_READ_TIMEOUT;

		if (debug == 2) output_high(DEBUG_PIN); // processing
		
		//is it half period?
		if (last_change_lapse <= semibit_time) {
			last_change_lapse = 0;
			if (status == HALF_BIT) {
				// it's a Zero!
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
		else {
			last_change_lapse = 0;
			if (status == HALF_BIT) {
				// it's not compliant :(
				status = READ_ERROR;
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
		if (debug == 2) output_low(DEBUG_PIN); //processed
	}
	
	read_stop();
	
	if (status == READ_ERROR) {
		return ERR_READ_ERROR;
	}

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
	
	// let the input settle below thr_h before start reading
	delay_us(200);

	read_start();
	if (!read_wait()) {
		putc(ERR_READ_TIMEOUT);
		return;
	}
	
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

	// read transponder default message
	memset(&iobuff, 0, MAXBUFF_SIZE);

	read_start();
	// wait for some data
	last_change_lapse = 0;
	if (!read_wait()) {
		putc(ERR_READ_TIMEOUT);
		return;
	}
	
	// Wait for a one
	// corner case: no ones, the message is all zeros
	while (last_change_lapse < semibit_time) {
		last_change_lapse = 0;
	
		semizeros--;
		
		if (semizeros == 0) {
			read_stop();
			putc(ERR_EMPTY_MESSAGE);
			return;
		}
		
		if (!read_wait()) {
			putc(ERR_READ_TIMEOUT);
			return;
		}
	}

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
	disable_interrupts(INT_COMP);
	disable_interrupts(INT_TIMER1);	
	
	// Timer1 counts us @8MHz
	// overflows at 65ms (read timeout)
	setup_timer_1(T1_INTERNAL|T1_DIV_BY_2);
	
	// Timer2 sets the oscillator frecuency: 125kHz.
	setup_timer_2(T2_DIV_BY_1,FREQ,1);
	set_pwm1_duty((int16)DC);
	setup_ccp1(CCP_PWM);

	// We don't need comparator until read
	setup_vref(thr_h);
	setup_comparator(NC_NC);

	enable_interrupts(GLOBAL);

	
	//enable_interrupts(INT_COMP);
	//while(TRUE) {}

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

		//h: set comparator levels, first low then high
		else if (command == 'h') {
			thr_l = getc();
			thr_h = getc();
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
