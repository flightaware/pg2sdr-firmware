#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

import clock_calc
import clock_status

def start_clock(dev, n, m, p, idiv):
    fixedpoint_m = int(round(m * 32768))
    clock_config = struct.pack('<IIII', n, fixedpoint_m, p, idiv)
    dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                type=usb.util.CTRL_TYPE_VENDOR,
                                                                recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                      bRequest=0x12,
                      wValue=0,
                      wIndex=0,
                      data_or_wLength=clock_config,
                      timeout=2000)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(f'syntax: {sys.argv[0]} [-f|-i] <frequency in MHz>')
        sys.exit(1)

    force_fractional = force_integer = False
    if sys.argv[1] == '-f':
        force_fractional = True
        freq = float(sys.argv[2]) * 1e6
    elif sys.argv[1] == '-i':
        force_integer = True
        freq = float(sys.argv[2]) * 1e6
    else:
        freq = float(sys.argv[1]) * 1e6

    i_settings, f_settings = clock_calc.settings_for(freq)
    if force_fractional:
        settings = f_settings
    elif force_integer:
        settings = i_settings
    else:
        if f_settings[0] < i_settings[0]:
            settings = f_settings
        else:
            settings = i_settings

    print(f'Calculated settings:')
    clock_calc.show(freq, settings)
    print()

    dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    dev.set_configuration()

    error, n, m, p, i, actual_fcco, actual_frequency = settings
    print('Programming ADC clock..')
    start_clock(dev, n, m, p, i)
    print('.. done.')
    print()
    print('Clock registers:')
    clock_status.query_regs(dev)
    print()

    print(f'Measured fPLL: {clock_status.measure_pll0audio(dev)/1e6:.3f} MHz')
    print(f'Measured fADC: {clock_status.measure_hsadc(dev)/1e6:.3f} MHz')
