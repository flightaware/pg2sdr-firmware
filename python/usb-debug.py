#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

def dump_debug_info(dev):
    data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                       type=usb.util.CTRL_TYPE_VENDOR,
                                                                       recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                             bRequest=0x0a,
                             wValue=0,
                             wIndex=0,
                             data_or_wLength=13*4,
                             timeout=2000)

def dump_debug_info(dev):
    data = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                       type=usb.util.CTRL_TYPE_VENDOR,
                                                                       recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                             bRequest=0x0a,
                             wValue=0,
                             wIndex=0,
                             data_or_wLength=16*4,
                             timeout=2000)

    parts = struct.unpack('<IIIIIIIIIIIIIIII', data)
    print(f'ADC CONFIG:         {parts[0]:08x}')
    print(f'ADC INTS[0].STATUS: {parts[1]:08x}')
    print(f'ADC FIFO_STS:       {parts[2]:08x}')
    print(f'ADC DSCR_STS:       {parts[3]:08x}')
    print()
    print(f'DMA CONFIG:         {parts[4]:08x}')
    print(f'DMA ENBLDCHNS:      {parts[5]:08x}')
    print(f'DMA TCSTAT:         {parts[6]:08x}')
    print(f'DMA ERRSTAT:        {parts[7]:08x}')
    print(f'DMA CH[0].CONFIG:   {parts[8]:08x}')
    print(f'DMA CH[0].CONTROL:  {parts[9]:08x}')
    print(f'DMA CH[0].SRC:      {parts[10]:08x}')
    print(f'DMA CH[0].DEST:     {parts[11]:08x}')
    print(f'DMA CH[0].LLI:      {parts[12]:08x}')
    print()
    print(f'current LLI:        {parts[13]:08x}')
    print(f'next sequence:      {parts[14]:08x}')
    print()
    print(f'sanity check:       {parts[15]:08x}')

if __name__ == '__main__':
    dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    dev.set_configuration()

    dump_debug_info(dev)
