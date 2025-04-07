#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

def main():
    if len(sys.argv) < 2 or sys.argv[1] not in ('off', 'on', 'reset'):
        print(f'syntax: ')
        print(f'  {sys.argv[0]} off       # turn off RF power')
        print(f'  {sys.argv[0]} on        # turn on RF power')
        print(f'  {sys.argv[0]} reset     # turn off RF power, wait a bit, turn on RF power')
        return

    if sys.argv[1] == 'off':
        mode = 0
    elif sys.argv[1] == 'on':
        mode = 1
    elif sys.argv[1] == 'reset':
        mode = 2
    else:
        assert False, 'should be unreachable'

    dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    if dev is None:
        print("can't find a suitable USB device")
        return
    
    dev.set_configuration()
    data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                       type=usb.util.CTRL_TYPE_VENDOR,
                                                                       recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                             bRequest=0x01,
                             wValue=0,
                             wIndex=0,
                             data_or_wLength=4,
                             timeout=2000)
    assert bytes(data) == b'\xEF\xBE\xAD\xDE', 'communication error, wrong magic number received'

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
                      bRequest=0x15,
                      wValue=mode,
                      wIndex=0,
                      data_or_wLength=b'',
                      timeout=2000)


if __name__ == '__main__':
    main()

