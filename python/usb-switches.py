#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time

dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
dev.set_configuration()

while True:
    data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                       type=usb.util.CTRL_TYPE_VENDOR,
                                                                       recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                             bRequest=0x06,
                             wValue=0,
                             wIndex=0,
                             data_or_wLength=1,
                             timeout=None)
    sw1 = "open" if (data[0] & 1) != 0 else "closed"
    sw2 = "not pressed" if (data[0] & 2) != 0 else "pressed"
    print(f'SW1: {sw1.ljust(8)} SW2: {sw2}')
    time.sleep(0.5)

