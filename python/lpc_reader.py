# install pyserial - pip install pyserial

import serial

bauds = [4800, 9600, 19200, 38400, 57600, 230400, 460800, 921600]


# Name of serial device
ser = serial.Serial('/dev/tty.usbserial-0001', baudrate=bauds[1], timeout=3)
while True:
    out = ser.read(100)
    if out:
        hex_val = 0
        print(f"Possible string: {out}")
        i = 0
        for b in out:
            hex_val = (hex_val| (b << (8 * i)))
            i += 1
        print(f"decimal val is {hex_val}")
