#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

def dump_tuner_regs(dev):
    dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                type=usb.util.CTRL_TYPE_VENDOR,
                                                                recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                      bRequest=0x15,    # set power mode
                      wValue=0,
                      wIndex=0,
                      data_or_wLength=[1],
                      timeout=2000)
    time.sleep(0.1)

    data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                       type=usb.util.CTRL_TYPE_VENDOR,
                                                                       recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                             bRequest=0x0C,
                             wValue=0,
                             wIndex=0,
                             data_or_wLength=36,
                             timeout=2000)


    status_names = {
        0: "I2C_STATUS_DONE",
        1: "I2C_STATUS_NAK",
        2: "I2C_STATUS_ARBLOST",
        3: "I2C_STATUS_BUSERR",
        4: "I2C_STATUS_BUSY",
        5: "I2C_STATUS_SLAVENAK"
    }

    status, = struct.unpack('<i', data[0:4])
    print(f'Status: {status_names.get(status, "unknown")} ({status})')
    if status == 0:
        for n, v in enumerate(data[4:]):
            print(f'Register #{n:02x} = 0x{v:02x}')

if __name__ == '__main__':
    dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    dev.set_configuration()

    dump_tuner_regs(dev)
