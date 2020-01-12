#include <em4205.h>

// TMR1 value when last comparator change
// you must clear this variable once used 
// since a status change could happen while you are proccesing last one.
// read_wait will not wait if you forget to clear this variable.
int16 last_change_lapse;
// Reader status
int8 status;
// Threshold between a whole bit and semi bit period (us)
int16 semibit_time = 190;

#INT_TIMER1
void tmr1_overflow() {
	disable_interrupts(INT_TIMER1);
	status = READ_TIMEOUT;
}


#INT_COMP
void comp_change() {
	last_change_lapse = get_timer1();
	set_timer1(0);
	
	// Inverting input	
	if (C1OUT){
		setup_vref(THR_H);
	}
	else {
		setup_vref(THR_L);
	}
}


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


void send_buff(int n, char *buff) {
	send_ffs(); send_0();
	
	for (int i = 0; i < n; i++) {
		if (shift_left(buff, IOBUFF_SIZE, 0)) {
			send_1();
		}
		else {
			send_0();	
		}
	}
}


void read_start() {
	setup_vref(THR_H);
	setup_comparator(A1_VR);
	
	status = READING; // clears previous error condition	
	set_timer1(0);
	last_change_lapse = 0;
	
	clear_interrupt(INT_TIMER1);
	clear_interrupt(INT_COMP);
	enable_interrupts(INT_TIMER1);
	enable_interrupts(INT_COMP);
}

void read_stop() {
	disable_interrupts(INT_TIMER1);
	disable_interrupts(INT_COMP);
	setup_comparator(NC_NC);	
}


/* Wait for line level change to read data.
   Return -1 if TIMER1 overflows and no change detected. 
   You must call read_start before this function
   */
signed int read_wait() {
	while (last_change_lapse == 0 && 
	       status != READ_TIMEOUT) {};

	if (status == READ_TIMEOUT) {
		read_stop();
		return -1;
	}
	else {
		return 0;
	}
}


/* Read N bits and disable reading when finish.
   Return -1 on read error (line code not compliant)
   Return -2 on read timeout (no chip)
   
   Since shift_right requires an expression tha evaluates
   as a constant, bufflen must be IOBUFF_SIZE or MAXBUFF_SIZE
   
   We read the bits always as biphase, translation to Manchester 
   is done later by software. */
signed int read_bits(int16 bits, char *buff, int bufflen) {
	int16 bits_read = 0;
	last_change_lapse = 0;
	
	while (status != READ_ERROR && bits_read < bits) {
		
		if (read_wait() < 0)
			return -2;
		
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
	}
	
	read_stop();
	
	if (status == READ_ERROR) {
		return -1;
	}
	else {
		return bits_read;
	}
}

/* Top level routine to read the response to a command 
   Return -2 if read timeout
   Return -1 if read error
 */
signed int read_response(int16 bits, char *buff, int bufflen) {
	unsigned int r;
	
	read_start();
	if (read_wait() < 0) return -2;
	r = read_bits(bits, buff, bufflen);
	read_stop();
	
	return r;	
}

/* Command "c": Send command and get a response 
   Syntax: srxxxxxxx
     s: (byte) how many bits to send
     r: (byte) how many bits to receive
     x: (7byte) 56 bits buffer Little-Endian. First bit to send is #55.
   Return: axxxxxxx
     a: (byte) error condition. 0: no error, 1: read error, 2: no response
     x: (7byte) bits read. Little Endian. Bit #0 is last bit read.
*/
void cmd_c() {
	char iobuff[IOBUFF_SIZE];
	int bits_to_send = getc();
	int bits_to_recv = getc();
	
	for (int i=0; i < IOBUFF_SIZE; i++) {
		iobuff[i] = getc();
	}

	// Send command to transponder
	send_buff(bits_to_send, &iobuff);

	// minimum processing pause
	delay_us(300);

	// read transponder response
	memset(&iobuff, 0, IOBUFF_SIZE);
	
	signed int res = read_response(bits_to_recv, &iobuff, IOBUFF_SIZE);
	
	if (res > 0) {
		putc(0); // error condition 0
		
		for (i=0; i < IOBUFF_SIZE; i++) {
			putc(iobuff[i]);
		}
	}
	else {
		putc(abs(res)); // error condition
		return;
	}
}


/* Command "r": Read 9 words (288 bits) in a row (MAXBUFF_SIZE)
   Syntax: r
   Return: a(x*36)
     a: (byte) error condition. 
     	0: no error
     	1: read error
     	2: no response
     	3: no message
     x: only if a=0 (36 bytes) bits read. 
        Little Endian. Bit #0 is the last bit read.
*/
void cmd_r() {
	char iobuff[MAXBUFF_SIZE];
	int16 semizeros = 2*MAXBUFF_SIZE*8; // message could be 288 zeros

	// read transponder default message
	memset(&iobuff, 0, MAXBUFF_SIZE);

	read_start();
	// wait for some data
	last_change_lapse = 0;
	if (read_wait() < 0) {
		putc(2);
		return;
	}
	
	// Wait for a one
	// corner case: no ones, the message is all zeros
	while (last_change_lapse < semibit_time) {
		last_change_lapse = 0;
	
		semizeros--;
		
		if (semizeros == 0) {
			read_stop();
			putc(3);
			return;
		}
		
		if (read_wait() < 0) {
			putc(2);
			return;
		}
	}

	signed int r = read_bits(288, &iobuff, MAXBUFF_SIZE);
	read_stop();

	if (r > 0) {
		putc(0); // error condition 0
		
		for (int i=0; i < MAXBUFF_SIZE; i++) {
			putc(iobuff[i]);
		}
	}
	else {
		putc(abs(r)); // read_bits error condition: 
		              // no response or not compliant
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
	putc(0);
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
		putc(0);
		putc(semibit_time >> 1);
	}
	else {
		semibit_time = arg<<1;
		putc(0);
	}
}


void main() {
	
	setup_timer_1(T1_INTERNAL|T1_DIV_BY_2); // 1us@8MHz
	setup_timer_2(T2_DIV_BY_1,15,1); // 125kHz
	set_pwm1_duty((int16)DC);
	setup_ccp1(CCP_PWM);

	disable_interrupts(INT_COMP);
	disable_interrupts(INT_TIMER1);	
	enable_interrupts(GLOBAL);

	while(TRUE) {
		int command = getc();
		
		//c: send command to transponder
		if (command == 'c') {
			cmd_c();	
		}

		//i: show id
		else if (command == 'i') {
			cmd_id();
		}
		
		//r: read raw
		else if (command == 'r') {
			cmd_r();
		}
		
		//t: set semibit threshold
		else if (command == 't') {
			cmd_t();
		}
		
		//z: disable magnetic field
		else if (command == 'z') {
			putc(0);
			set_pwm1_duty(0);
		}

		//y: enable magnetic field
		else if (command == 'y') {
			putc(0);
			set_pwm1_duty((int16)DC);
		}
		
		//k: reset CPU
		else if (command == 'k') {
			reset_cpu();
		}
		
		//anything else, error
		else {
			putc(255);
		}
	}
}
