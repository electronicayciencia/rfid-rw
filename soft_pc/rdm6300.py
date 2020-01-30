#!/usr/bin/env python3

"""
Script to interact with RDM6300 reader simulating an access control system.

The RDM6300 is a EM4100 compatible reader with UART interface (9600, 8N1)
It reads at 125kHZ, datarate is RF/64, encoding manchester.

The output is 8 hex bytes in printable format (2 bytes each)
enclosed between 0x02 and 0x03 bytes. 16 bytes in total:
\02 vv cc cc cc cc hh \03 (no spaces)
vv: version
cc: code
hh: checksum (xor of previous bytes)

Example:
 b'\x02170068D28429\x03'

Requires: PySerial

Reinoso G. - Blog Electronicayciencia - 26/01/2020
"""

import serial
import colorama
import termcolor
from datetime import datetime
from sys import exit
from struct import unpack
from time import sleep

PORT = "COM3"

STAFF = {
    179607433: "Mr. Jiminis",
    6869636: "Mr. Guzman",
    1961262: "Ms. Smith",
    3620985: "Mr. Batman"
}

# Only Batman and Mr Jiminis are allowed in the room
AUTHORIZED = (179607433, 3620985)


colorama.init()
serial_conn = serial.Serial(PORT, 9600, timeout=0.1)

print("Electronicayciencia Access Control System.")

while True:
    try:
        a = serial_conn.read_until(b'\x03')
    except KeyboardInterrupt:
        break

    if len(a) != 14:
        continue

    (start, version, code, checksum, end) = unpack('b2s8s2sb', a)

    if start != 2 or end != 3:
        continue

    version = int(version, 16)
    code = int(code, 16)
    checksum = int(checksum, 16)

    # we don't check checksum

    # print("Version: %02X   Code: %08X  (%010d)"
    #    % (version, code, code), end="")

    date = datetime.now().strftime("%d/%m/%Y %H:%M:%S")

    if code in AUTHORIZED:
        access = termcolor.colored(" ACCESS GRANTED ", "green")
    else:
        access = termcolor.colored(" ACCESS DENIED  ", "white", "on_red")

    if code in STAFF:
        name = STAFF[code]
    else:
        name = "unknown"

    print("%s - Id: %010d - %s - %s" % (
        date,
        code,
        access,
        termcolor.colored(name, "yellow")
    ))

    try:
        sleep(1)
    except KeyboardInterrupt:
        break

    serial_conn.reset_input_buffer()
