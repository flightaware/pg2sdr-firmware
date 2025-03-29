#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
dev.set_configuration()

data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                   type=usb.util.CTRL_TYPE_VENDOR,
                                                                   recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                         bRequest=0x01,
                         wValue=0,
                         wIndex=0,
                         data_or_wLength=4,
                         timeout=2000)
print(repr(bytes(data)))

dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                            type=usb.util.CTRL_TYPE_VENDOR,
                                                            recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                  bRequest=0x01,
                  wValue=0,
                  wIndex=0,
                  data_or_wLength=b'\xEF\xBE\xAD\xDE',
                  timeout=2000)

dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                            type=usb.util.CTRL_TYPE_VENDOR,
                                                            recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                  bRequest=0x2F,
                  wValue=0,
                  wIndex=0,
                  data_or_wLength=b'',
                  timeout=2000)

