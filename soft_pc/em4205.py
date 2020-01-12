#!/usr/bin/env python3

"""
Module to interact with EM4x05 chips

Requires: PySerial

Reinoso G. - Blog Electronicayciencia - 23/12/2019
"""

import serial


WORD_CONF = 4
WORD_PASSWD = 2
BIT_READLOGIN = 18
BIT_WRITELOGIN = 20
BIT_DISABLECMD = 23
BIT_ENC_BIPHASE = 7
BIT_ENC_MANCHESTER = 6


#####################################################################
class Error(Exception):
    pass


class ReaderError(Error):
    """Reader is not ready. The reader's response is not what we expected,
    no reponse from reader or malformed one."""


class TransponderError(Error):
    """The reader could not communicate with the transponder or it did not
    respond (e.g. no chip detected)."""


class ResponseError(Error):
    """The response from chip has some error, bad parity or bad preamble."""


class CommandRejected(ResponseError):
    """The transponder refuses to comply. Command error or login required."""


# module variable: serial connection object by PySerial
_serial_conn = None
_DEBUG = 0


#####################################################################
# Auxiliary and bit-wise functions
#
def word2data(w):
    """
    Convert a 32bit Word to Data Structure format

    Input
        32bit number
    Output
        45bit number
    """

    cp = 0  # even column parity byte
    data = 0

    for _ in range(4):
        o = 0  # current octet in LSB first order
        p = 0  # current even parity bit

        for _ in range(8):
            o <<= 1
            o |= (w & 1)
            p ^= (w & 1)
            w >>= 1

        cp ^= o
        data <<= 9
        data |= (o << 1 | p)

    data <<= 9  # cp + stop bit
    data |= (cp << 1)

    return data


def data2word(d):
    """
    Convert a 45 bit Data Structure format to a 32bit Word

    Input
        45 bit number
    Output
        32 bit number
    Error
        ResponseError
    """

    # Stop bit is always Zero
    if d & 1:
        raise ResponseError("Stop bit should be always Zero")

    d >>= 1

    # Check parity column
    cp = d & 0xFF
    d >>= 8
    if (cp ^ (d >> 1 & 0xFF) ^
        (d >> 10 & 0xFF) ^
        (d >> 19 & 0xFF) ^
            (d >> 28 & 0xFF)):
        raise ResponseError("Bad parity")

    word = 0
    for _ in range(4):
        o = 0        # current octet in MSB first order
        p = d & 0x1  # current even parity bit
        d >>= 1

        for _ in range(8):
            o <<= 1
            o |= (d & 1)
            p ^= (d & 1)
            d >>= 1

        if p:
            raise ResponseError("Bad parity")

        word <<= 8
        word |= o

    return word


def num2addr(n):
    """
    Convert a 4bit number to Address bit format

    Input
        4 bit number
    Output
        7 bit number
    """

    p = 0  # even parity bit
    a = 0  # address

    for _ in range(4):
        a <<= 1
        a |= (n & 1)
        p ^= (n & 1)
        n >>= 1

    a <<= 3  # 00 + parity
    a |= p

    return a


def cmd2cmdf(i):
    """
    Convert a 3bit number to Command bit format

    Input
        3 bit number
    Output
        4 bit number
    """

    p = 0  # even parity bit
    o = 0  # output

    for _ in range(3):
        o <<= 1
        o |= (i & 1)
        p ^= (i & 1)
        i >>= 1

    o <<= 1  # parity
    o |= p

    return o


def num2bytes(nbytes, num):
    """
    Pack a bytes string formed by nbytes of the num in Little Endian order.

    Input
        nbytes: the number of bytes
           num: the num to translate
    Output
        a nbytes length bytes array
    """

    s = b""

    for _ in range(nbytes):
        s += bytes([num & 0xFF])
        num >>= 8

    return s


def bytes2num(data_bytes):
    """
    Return a number from the input bytes Little Endian order.

    Input
        data_bytes: the bytes string
    Output
        a nbits length number
    """

    o = 0

    for i in data_bytes:
        for _ in range(8):
            o <<= 1
            o |= (i & 1)
            i >>= 1

    return o


def _set_bit(value, bit):
    return value | (1 << bit)


def _clear_bit(value, bit):
    return value & ~(1 << bit)


def biphase2manchester(nbits, number):
    """
    Correct the bits of a message that has been read as it were biphase
    encoded but it was indeed manchester.

    Input:
           nbits: length of the message in bits
          number: message read as bihpase

    Output:
        message read as manchester
    """

    number >>= 1
    man = 0

    b = 0
    for i in range(nbits-1, -1, -1):
        if (number >> i) & 1:
            b ^= 1
        man <<= 1
        man |= b

    return man


#####################################################################
# Reader communication
#
def parse_response(bits, resp):
    """
    Parse the response to a c command "axxxxxxxx"

    Input
        bits: response length in bits;
        resp: response bytes in format b'axxxxxxx'

    Output:
        unprocessed dataStruct (could be empty)

    Error
        ReceivedMessageError if response format is unexpected
        ReaderError if invalid or no response received
    """

    try:
        data_bin = bytes2num(resp)
    except:
        raise ReaderError("Invalid response: " + resp)

    if _DEBUG:
        print("Response from chip: {0:056b}".format(data_bin))

    # check preamble reported by the EM chip
    pre = (data_bin >> (bits - 8) & 0xFF)

    if _DEBUG:
        print("Preamble: {0:08b}".format(pre))

    # Might be manchester
    if pre in (0b00011111, 0b00011110, 0b00000011, 0b00000010):
        if _DEBUG:
            print("Seems Manchester")
        data_bin = biphase2manchester(bits, data_bin)
        pre = (data_bin >> (bits - 8) & 0xFF)

    if pre == 0b00000001:
        raise CommandRejected("Command rejected")

    if pre != 0b00001010:
        raise ResponseError("Preamble {0:08b} unknown".format(pre))

    # some commands return a datastruct, others do not
    # This is the datastruct but only if command actually returns a datastruct
    # 45 bits
    datastruct = data_bin & 0x1FFFFFFFFFFF

    return datastruct


def do_cmd(msg, bts, btr):
    """
    Send a TX message to the reader and process the response.
    Must call init() first.

    Input
        msg: message to send
        bts: number, bits to send
        btr: number, bits to receive
    Output:
        Response data.
    Error:
        ReaderError
    """
    if _DEBUG:
        print("Message to send ({0:d} bits): {1:056b}".format(bts, msg))

    msg <<= 56 - bts  # pad the message to 56 bits
    string_to_send = b'c' + bytes([bts, btr]) + num2bytes(7, msg)

    if _serial_conn is None:
        raise ReaderError("Please, initialize serial connection")

    try:
        _serial_conn.write(string_to_send)
    except Exception as err:
        raise ReaderError(err)

    err = _serial_conn.read(1)

    if len(err) < 1:
        raise ReaderError("Reader not responding")

    err = err[0]

    # check error condition reported by the reader
    if err == 1:
        raise TransponderError("Chip not detected")

    if err == 2:
        raise TransponderError("Communication error")

    if err != 0:
        raise TransponderError("Error condition %d unknown" % err)

    resp = _serial_conn.read(7)

    if len(resp) < 7:
        raise ReaderError("Unexpected response lenght")

    if _DEBUG:
        print("Raw Response: {}".format(resp))

    return parse_response(btr, resp)


#####################################################################
# Primitive EM4205 commands
#
def read(addr):
    """
    Compose and send a read command message to reader

    Input
        address of the word to read
    Output
        returns the value stored at word addr
    Error
        CommandRejected if password protection is active
        ValueError if word is not between 0 and 15
    """
    if addr < 0 or addr > 15:
        raise ValueError("Address must be between 0 and 15")

    # cmd: (4 bits) + address (7 bits)
    # res: preamble (8bits) + data struct (45 bits)
    msg = (cmd2cmdf(0b001) << 7) + num2addr(addr)
    data = do_cmd(msg, 4+7, 8+45)
    return data2word(data)


def write(addr, word):
    """
    Compose and send a write command message to reader

    Input
        address of the word to write; word: value to write
    Output
        None
    Error  
        CommandRejected if password protected or protected word
        ValueError if word is above 32 bits.
    """
    # msg: cmd (4 bits) + address (7 bits) + data structure (45 bits)
    # res: preamble (8bits)
    if word >= 1<<32:
        raise ValueError("Word must be below 2^32")

    msg = (cmd2cmdf(0b010) << 52) + (num2addr(addr) << 45) + word2data(word)
    do_cmd(msg, 4+7+45, 8)


def login(pwd):
    """
    Compose and send a login command message to reader

    Input
        pwd: password value
    Output
        None
    Error
        CommandRejected if incorrect password
        ValueError if password is over 32 bits
    """
    if pwd >= 1<<32:
        raise ValueError("Password must be 32 bits")

    # msg: cmd (4 bits) + password as data structure (45 bits)
    # res: preamble (8bits)
    msg = (cmd2cmdf(0b100) << 45) + word2data(pwd)
    do_cmd(msg, 4+45, 8)


def cmd_protect(word):
    """
    Compose and send a protect command message to reader.

    Input
        word: protection word value
    Output 
        None
    Error
        CommandRejected if not accepted (parity error or Power Check fail)
    """
    # msg: cmd (4 bits) + protect_word as data structure (45 bits)
    # res: preamble (8bits)

    # for i in protected:
    #    word += 1 << i
    msg = (cmd2cmdf(0b011) << 45) + word2data(word)
    do_cmd(msg, 4+45, 8)


def disable():
    """
    Compose and send a disable command message to reader

    Input
        None
    Output
        Trigger a ChipNotDetected exception if ok
    Error
        CommandRejected if disable command is not enabled or error
    """
    # msg: cmd (4 bits) + all 1's data structure (45 bits)
    # res: preamble (8bits)
    msg = (cmd2cmdf(0b101) << 45) + word2data(0xFFFFFFFF)
    do_cmd(msg, 4+45, 8)


#####################################################################
# High level commands
#
def set_password(pwd):
    """
    Set the login password (but not enable it)

    Input
        pwd: password value
    Output
        None
    Error
        Exceptions from write()
    """
    write(WORD_PASSWD, pwd)
    pass


def enable_read_password(ena=1):
    config = read(WORD_CONF)
    if ena:
        config = _set_bit(config, BIT_READLOGIN)
    else:
        config = _clear_bit(config, BIT_READLOGIN)

    write(WORD_CONF, config)


def enable_write_password(ena=1):
    config = read(WORD_CONF)
    if ena:
        config = _set_bit(config, BIT_WRITELOGIN)
    else:
        config = _clear_bit(config, BIT_WRITELOGIN)

    write(WORD_CONF, config)


def enable_disablecmd(ena=1):
    config = read(WORD_CONF)
    if ena:
        config = _set_bit(config, BIT_DISABLECMD)
    else:
        config = _clear_bit(config, BIT_DISABLECMD)

    write(WORD_CONF, config)


def set_encoder(enc):
    config = read(WORD_CONF)

    if enc == "biphase":
        config = _set_bit(config, BIT_ENC_BIPHASE)
        config = _clear_bit(config, BIT_ENC_MANCHESTER)

    elif enc == "manchester":
        config = _set_bit(config, BIT_ENC_MANCHESTER)
        config = _clear_bit(config, BIT_ENC_BIPHASE)

    else:
        raise ValueError("Encoder must be manchester or biphase")

    write(WORD_CONF, config)


def set_datarate(datarate):
    dr = {
        8:  0b000011,
        16: 0b000111,
        32: 0b001111,
        40: 0b010011,
        64: 0b011111
    }

    if datarate not in dr:
        raise ValueError("Rates available are 8, 16, 32, 40 or 64 RF cycles.")

    # reader must be configured with the correct datarate in order to
    # read the configuration word.
    config = read(WORD_CONF)
    config &= 0xFFFFFFE0
    config |= dr[datarate]

    reader_datarate(datarate)

    write(WORD_CONF, config)


def reset_config(pwd=0):
    """
    Reconfig the chip is initialized to Bi-phase data encoding, RF/32
    clock data rate. Its LWR value is set to 8. No password.
    Password is set to 0, words 5 to 13 are also cleared.
    """
    DEFAULT_CONFIG = 0x0002008f
    try:
        login(pwd)
    except (ResponseError, TransponderError):
        pass  # Transponder config migth be unknown at this point

    write(WORD_CONF, DEFAULT_CONFIG)
    
    for i in (2,5,6,7,8,9,10,11,12,13):
        write(i,0)


def protect(words=(1)):
    """
    Protect words (or some) against writing. 
    Note that in most chips these bits cannot be cleared.

    Input
        words: list of words to protect (default is all words)
    Error
        Value error if some word is no between 0 and 14.
    """
    prot_word = 0
    for i in words:
        if i > 14 or i < 0:
            raise ValueError("Words must be between 0 and 14")
        prot_word += 1<<i

    cmd_protect(prot_word)


def dump_all():
    for addr in range(16):
        print("Word at position {0:2d}:  ".format(addr), end='')

        try:
            value = read(addr)
            print("0x{0:08x} ({0:032b})".format(value))
        except Exception as err:
            print(err)


def reader_datarate(rf_cycles=0):
    """
    Set the reading speed of the reader.

    Input
        rf_cycles: 0, 8, 16, 32, 40 or 64
        If 0, do not alter the current speed.
    Output
        Reader semibit period (in us).
    Error
        ReaderError if no response
    """
    if rf_cycles not in (0,8,16,32,40,64):
        raise ValueError("Speed must be 0, 8, 16, 32, 40 or 64")

    if rf_cycles != 0:
        semibit = int(rf_cycles/125 * 1e3 * 3/4)
        _serial_conn.write(b't' + bytes([semibit>>1]))
        resp = _serial_conn.read(1)

        if resp[0] != 0:
            raise ReaderError("Command 't' unkown")
    
    _serial_conn.write(b't\x00')

    resp = _serial_conn.read(2)
    if resp[0] != 0:
        raise ReaderError("Command 't' unkown")
    
    return resp[1]<<1


def init(serial_port="COM3"):
    """
    Open a serial port and fills the "_serial_conn" class variable
    Ask for identification string and check the response.

    Input
        serial_port: serial port name(e.g. / dev/ttyUSB0, COM3)
    Output
        Reader identification string
    Error
        ReaderError if no response
        Other exceptions raised by PySerial
    """
    global _serial_conn
    _serial_conn = serial.Serial(serial_port, 9600, timeout=1)

    return reader_id()


def reader_id():
    """
    Get the reader identification string. 
    Uses _serial_conn module variable.

    Output
        Reader identification string
    Error
        ReaderError if timeout or unexpected response from reader.
    """
    _serial_conn.write(b'i')
    if _serial_conn.read(1) != b'\x00':
        raise ReaderError("Id command now known")

    try:
        id = _serial_conn.read_until(b'\x00')
    except TimeoutError:
        raise ReaderError("Error reading Id string")

    return id[:-1].decode("ascii").rstrip()


def read_stream():
    """
    Read 288 bits (9 x 32 bit words) in a row.

    Output
        string with 288 bits (101001...) in order or reception
    Error
        ReaderError if reader response is not ok
        TransponderError if empty message, no chip or communication error
    """
    _serial_conn.write(b'r')
    errc = _serial_conn.read(1)

    errc = errc[0]

    if errc == 1:
        raise TransponderError("Communication error")
    elif errc == 2:
        raise TransponderError("No response from chip")
    elif errc == 3:
        raise TransponderError("Empty message")
    
    msg = _serial_conn.read(36)

    #num = 0
    #for i in msg[::-1]:
    #    num <<= 8
    #    num += i
    
    #print("{0:0288b}".format(num))
    return bytes2num(msg)
    


#####################################################################
# Main code
#
#
if __name__ == "__main__":
    _DEBUG = 0

    print(init('COM3'))
  
    def keyfob_64_manchester():
        reader_datarate(64)
        a = read_stream()
        msg = "{0:0288b}".format(biphase2manchester(288,a))
        start = msg.find("111111111")
        if start < 0:
            print("Format unknown")

        msg = msg[start:start+64]
        print(msg)

    keyfob_64_manchester()
    #for i in range(5,14):
    #    write(i,0x0)

    #dump_all()
    #write( 5, 0b10000000000000000000000000000010)
    #write( 6, 0b10000000000000000000000000000100)
    #write( 7, 0b10000000000000000000000000001000)
    #write( 8, 0b10000000000000000000000000010000)
    #write( 9, 0b10000000000000000000000000100000)
    #write(10, 0b10000000000000000000000001000000)
    #write(11, 0b10000000000000000000000010000000)
    #write(12, 0b10000000000000000000000100000000)
    #write(13, 0b10000000000000000000001000000000)

    #dump_all()






#    exit()

