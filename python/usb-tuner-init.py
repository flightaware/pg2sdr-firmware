#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

def init_tuner_regs(dev):
    dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                type=usb.util.CTRL_TYPE_VENDOR,
                                                                recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                      bRequest=0x15,    # set power mode
                      wValue=0,
                      wIndex=0,
                      data_or_wLength=[1],
                      timeout=2000)
    time.sleep(0.1)

    # These are the default reg values used by librtlsdr:
    init_regs = [
        0x83, 0x32, 0x75,                       # 05 to 07
        0xc0, 0x40, 0xd6, 0x6c,                 # 08 to 0b
        0xf5, 0x63, 0x75, 0x68,                 # 0c to 0f
        0x6c, 0x83, 0x80, 0x00,                 # 10 to 13
        0x0f, 0x00, 0xc0, 0x30,                 # 14 to 17
        0x48, 0xcc, 0x60, 0x00,                 # 18 to 1b
        0x54, 0xae, 0x4a, 0xc0                  # 1c to 1f
    ]

    dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                type=usb.util.CTRL_TYPE_VENDOR,
                                                                recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                      bRequest=0x16,  # write tuner regs
                      wValue=0,
                      wIndex=0,
                      data_or_wLength=[5] + init_regs,   # first register index, followed by reg contents to write
                      timeout=2000)

if __name__ == '__main__':
    dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    dev.set_configuration()

    init_tuner_regs(dev)
