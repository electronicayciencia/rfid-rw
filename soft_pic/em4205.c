#include <em4205.h>

// TMR1 value when last comparator change
int16 last_change_lapse;
// Reader status
int8 status;

#INT_TIMER1
void tmr1_overflow() {
	disable_interrupts(INT_TIMER1);
	status = READ_ERROR;
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
   You must call read_start previous to call this function
   */
signed int read_wait() {
	last_change_lapse = 0;
	while (last_change_lapse == 0 && 
	       status != READ_ERROR) {};

	if (status == READ_ERROR) {
		read_stop();
		return -1;
	}
	else {
		return 0;
	}
}


/* Reads N bits and disable reading when finish.
   Returns -1 on error 
   We read the bits always as biphase, translation to Manchester 
   is done by software. */
signed int read_bits(int bits, char *buff) {
	int bits_read = 0;
	
	while (status != READ_ERROR && bits_read < bits) {
		int bit_value;
		
		if (read_wait() < 0)
			break;
		
		//is it half period?
		if (last_change_lapse <= 200) {
			if (status == HALF_BIT) {
				// it's a Zero!
				status = READING;
				bit_value = 0;
				shift_right(buff, IOBUFF_SIZE, bit_value);
				bits_read++;
			}
			else {
				// it is half zero...
				status = HALF_BIT;
			}
		}
		
		//it's whole period
		else {
			if (status == HALF_BIT) {
				// it's not compliant :(
				status = READ_ERROR;
			}
			else {
				//it's a One!
				status = READING;
				bit_value = 1;
				shift_right(buff, IOBUFF_SIZE, bit_value);
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

/* Top level routine to read the response to a command */
signed int read_response(int bits, char *buff) {
	unsigned int r;
	
	read_start();
	if (read_wait() < 0) return -1;
	r = read_bits(bits, buff);
	read_stop();
	
	return r;	
}

/* Command "c": Send command and get a response 
   Syntax: srxxxxxxx
     s: (byte) how many bits to send
     r: (byte) how many bits to receive
     x: (7byte) 56 bits buffer Little-Endian. First bit to send is #55.
   Return: axxxxxxx
     a: (byte) error condition. 0: no error, 1: no response or read error
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
	if (read_response(bits_to_recv, &iobuff) > 0) {
		putc(0); // error condition 0
	}
	else {
		putc(1); // error condition 1: no response
		return;
	}
	
	for (i=0; i < IOBUFF_SIZE; i++) {
		putc(iobuff[i]);
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
		
		//anything else, error
		else {
			putc(255);
		}
	}
}
