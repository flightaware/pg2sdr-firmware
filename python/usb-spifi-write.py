#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time

dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
dev.set_configuration()

if False:
    print('11: SPI sector erase')
    data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                       type=usb.util.CTRL_TYPE_VENDOR,
                                                                       recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                             bRequest=0x11,
                             wValue=0x1000,
                             wIndex=0,
                             data_or_wLength=None,
                             timeout=None)
    

if False:
    print('10: SPI flash write')
    data = bytearray(b'\xDE\xAD\xBE\xEF')
    while len(data) < 256:
        data.append(len(data))
    print(repr(data))

    data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                       type=usb.util.CTRL_TYPE_VENDOR,
                                                                       recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                             bRequest=0x10,
                             wValue=0x1000,
                             wIndex=0,
                             data_or_wLength=data,
                             timeout=None)
    
