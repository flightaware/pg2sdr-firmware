#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time

dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
dev.set_configuration()

print('01: comms check, IN')
data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                   type=usb.util.CTRL_TYPE_VENDOR,
                                                                   recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                         bRequest=0x01,
                         wValue=0,
                         wIndex=0,
                         data_or_wLength=4,
                         timeout=None)

assert(bytes(data) == b'\xde\xad\xbe\xef')

print('01: comms check, OUT')
dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                            type=usb.util.CTRL_TYPE_VENDOR,
                                                            recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                  bRequest=0x01,
                  wValue=0,
                  wIndex=0,
                  data_or_wLength=b'\xde\xad\xbe\xef',
                  timeout=None)


print('02: SPI flash manufacturer/device ID')
data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                   type=usb.util.CTRL_TYPE_VENDOR,
                                                                   recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                         bRequest=0x02,
                         wValue=0,
                         wIndex=0,
                         data_or_wLength=2,
                         timeout=None)
print(f'{data[0]:02x}:{data[1]:02x}')

print('03: SPI flash unique ID')
data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                   type=usb.util.CTRL_TYPE_VENDOR,
                                                                   recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                         bRequest=0x03,
                         wValue=0,
                         wIndex=0,
                         data_or_wLength=8,
                         timeout=None)
print(f'{data[0]:02x}:{data[1]:02x}:{data[2]:02x}:{data[3]:02x}:{data[4]:02x}:{data[5]:02x}:{data[6]:02x}:{data[7]:02x}')

print('04: SPI flash read')
data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                   type=usb.util.CTRL_TYPE_VENDOR,
                                                                   recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                         bRequest=0x04,
                         wValue=0,
                         wIndex=0,
                         data_or_wLength=16,
                         timeout=None)
print(repr(bytes(data)))

print('05: SPI flash read (quad)')
data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                   type=usb.util.CTRL_TYPE_VENDOR,
                                                                   recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                         bRequest=0x05,
                         wValue=0,
                         wIndex=0,
                         data_or_wLength=16,
                         timeout=None)
print(repr(bytes(data)))

print('05: SPI flash read (quad) @ 0x1000')
data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                   type=usb.util.CTRL_TYPE_VENDOR,
                                                                   recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                         bRequest=0x05,
                         wValue=0x1000,
                         wIndex=0,
                         data_or_wLength=16,
                         timeout=None)
print(repr(bytes(data)))

