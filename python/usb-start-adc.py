#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

def start_conversion(dev):
    dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                type=usb.util.CTRL_TYPE_VENDOR,
                                                                recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                      bRequest=0x13,
                      wValue=0,
                      wIndex=0,
                      data_or_wLength=[],
                      timeout=2000)

if __name__ == '__main__':
    dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    dev.set_configuration()

    start_conversion(dev)

    HEADER = struct.Struct('<IIIII')

    while True:
        data = dev.read(endpoint=0x81, size_or_buffer=512, timeout=None)
        magic, block_len, samples, sequence, status = HEADER.unpack(data[0:HEADER.size]) 
        if magic != 0xdeadbeef:
            continue
        data = data + dev.read(endpoint=0x81, size_or_buffer=10240 * 50 - 512, timeout=None)
        for i in range(0, len(data), 10240):
            magic, block_len, samples, sequence, status = HEADER.unpack(data[i:i+HEADER.size]) 
            if magic != 0xdeadbeef:
                continue
            if status != 0:
                print(f'{magic:08x} {block_len} {samples} {sequence} {status}')

            if False:
                for k in range(i+HEADER.size, i+HEADER.size+samples//8 * 12, 12):
                    p1, p2, p3 = struct.unpack('<III', data[k:k+12])
                    s = [ (p1 & 0x0FFF0000) >> 16,
                          (p1 & 0x00000FFF),
                          (p2 & 0x0FFF0000) >> 16,
                          (p2 & 0x00000FFF),
                          (p3 & 0x0FFF0000) >> 16,
                          (p3 & 0x00000FFF),
                          ((p1 & 0xF0000000) >> 20) | ((p2 & 0xF0000000) >> 24) | ((p3 & 0xF0000000) >> 28),
                          ((p1 & 0x0000F000) >> 4) | ((p2 & 0x0000F000) >> 8) | ((p3 & 0x0000F000) >> 12) ]
                    for j in range(8):
                        if s[j] & 0x0800:
                            s[j] = s[j] - 0x1000
                    if k < 50:
                        print(f'{p1:08x} {p2:08x} {p3:08x} {s}')
