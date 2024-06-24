#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
dev.set_configuration()

rom = bytearray()
for i in range(0, 65536, 256):
    address = 0x10400000 + i    
    data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                       type=usb.util.CTRL_TYPE_VENDOR,
                                                                       recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                             bRequest=0x0b,
                             wValue=(address & 0xFFFF),
                             wIndex=(address & 0xFFFF0000) >> 16,
                             data_or_wLength=256,
                             timeout=2000)
    rom.extend(bytes(data))

with open('rom.bin', 'wb') as f:
    f.write(rom)
    
