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
        TransponderError if error condition non 0
        ReceivedMessageError if response format is unexpected
        ReaderError if invalid or no response received
    """

    (err, data) = (resp[0], resp[1:])

    # check error condition reported by the reader
    if err == 1:
        raise TransponderError("Chip not detected")

    if err != 0:
        raise TransponderError("Error condition %d unknown" % err)

    try:
        data_bin = bytes2num(data)
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

    resp = _serial_conn.read(8)
    if len(resp) < 8:
        raise ReaderError("Unexpected response lenght")

    if _DEBUG:
        print("Raw Response: {}".format(resp))

    return parse_response(btr, resp)


#####################################################################
# Basic commands
#
def read(addr):
    """
    Compose and send a read command message to reader
    Input:  address of the word to read
    Output: returns the value stored at word addr
    Error:  CommandRejected if password protection is active
    """
    # cmd: (4 bits) + address (7 bits)
    # res: preamble (8bits) + data struct (45 bits)
    msg = (cmd2cmdf(0b001) << 7) + num2addr(addr)
    data = do_cmd(msg, 4+7, 8+45)
    return data2word(data)


def write(addr, word):
    """
    Compose and send a write command message to reader
    Input:  address of the word to write; word: value to write
    Output: None
    Error:  CommandRejected if password protected or protected word
    """
    # msg: cmd (4 bits) + address (7 bits) + data structure (45 bits)
    # res: preamble (8bits)
    msg = (cmd2cmdf(0b010) << 52) + (num2addr(addr) << 45) + word2data(word)
    do_cmd(msg, 4+7+45, 8)


def login(pwd):
    """
    Compose and send a login command message to reader
    Input:  pwd: password value
    Output: None
    Error:  CommandRejected if incorrect password
    """
    # msg: cmd (4 bits) + password as data structure (45 bits)
    # res: preamble (8bits)
    msg = (cmd2cmdf(0b100) << 45) + word2data(pwd)
    do_cmd(msg, 4+45, 8)


def disable():
    """
    Compose and send a disable command message to reader
    Input:  None
    Output: Trigger a ChipNotDetected exception if ok
    Error:  CommandRejected if disable command is not enabled
    """
    # msg: cmd (4 bits) + all 1's data structure (45 bits)
    # res: preamble (8bits)
    msg = (cmd2cmdf(0b101) << 45) + word2data(0xFFFFFFFF)
    do_cmd(msg, 4+45, 8)


#####################################################################
# Complex commands
#
def set_password(pwd):
    """
    Set the login password (but not enable it)
    Input:  pwd: password value
    Output: None
    Error:  Raise exception
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


def reset_config(pwd=0):
    DEFAULT_CONFIG = 0x0002008f
    try:
        login(pwd)
    except (ResponseError, TransponderError):
        pass  # Transponder config migth be unknown at this point

    write(WORD_CONF, DEFAULT_CONFIG)


def dump_all():
    for addr in range(16):
        print("Word at position {0:2d}:  ".format(addr), end='')

        try:
            value = read(addr)
            print("0x{0:08x} ({0:032b})".format(value))
        except Exception as err:
            print(err)


def init(serial_port="COM3"):
    """
    Open a serial port and fills the "_serial_conn" class variable
    Ask for identification string and check the response.
    Input:  serial_port: serial port name (e.g. /dev/ttyUSB0, COM3)
    Output: Identification string
    Error:  ReaderError if no response
            Other exceptions raised by PySerial
    """
    global _serial_conn
    _serial_conn = serial.Serial(serial_port, 9600, timeout=1)

    _serial_conn.write(b'i')
    if _serial_conn.read(1) != b'\x00':
        raise ReaderError("Reader initialization failed")

    id = b''
    while True:
        try:
            c = _serial_conn.read(1)
        except TimeoutError:
            raise ReaderError("Error reading Id string")

        if c == b'\x00':
            break

        id += c

    return id.decode("ascii").rstrip()

    # TODO: read init string until 0
#####################################################################
# Main code
#
#
if __name__ == "__main__":
    _DEBUG = 0

    print(init('COM3'))

    # read(0)
    # exit()

    dump_all()
    exit()
    # write(2, 0xdeadbeef)

    try:
        login(0xdeadbeef)
        print("Login ok")
    except CommandRejected:
        print("Wrong password")

    # dump_all()
    # set_password(0xdeadbeef)
    # dump_all()
    # enable_read_password(1)
    # dump_all()
    # enable_read_password(0)
    # dump_all()

    """
	try:
	#	write(2,0xdeadbeef)
	#	write(7, read(7) & ~Conf_ReadLogin & 0xFFFFFFFF)
	#	write(ConfWord, read(ConfWord) | Conf_ReadLogin)
		print("Writing Successful")
	except Exception as e:
		print("Write error: %s" % e)
	"""

    exit()