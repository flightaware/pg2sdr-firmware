#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

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
    # From the examples in UM10503, which we have to infer some data from ..
    doc_fractional_settings = [
        # Fout,NDEC,PDEC,FRACT
        (24.576e6, 514, 29, 0x16872b),
        (12.288e6, 1, 3, 0x1a1cac),
        (11.2896e6, 0, 24, 0x070e56),
        (8.192e6, 0, 30, 0x072b02),
        (6.144e6, 1, 6, 0x133333),
        (5.6448e6, 1, 6, 0x11a3d7),
        (49.152e6, 514, 5, 0x147ae1),
        (24.576e6, 514, 29, 0x16872b),
        (22.5792e6, 1, 14, 0x1c3958),
    ]

    doc_integer_settings = [
        # Fout,NDEC,MDEC,PDEC
        (24.576e6, 63, 13523, 14),
        (12.288e6, 63, 2665, 24),
        (11.2896e6, 45, 18810, 24),
        (8.192e6, 61, 18724, 6),
    ]

    # custom clock settings from calculate-clocks.py
    custom_settings = [
        # Fout,N,M,P,I
        (4.8e6, 0, 12, 30, 0),
        (12e6, 0, 12, 12, 0),
        (20e6, 0, 15, 9, 0),
        (24e6, 0, 12, 6, 0),
        (4.166667e6, 2, 25, 18, 2),
        (4.166667e6, 0, 12.67361, 0, 73),
    ]

    dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    dev.set_configuration()

    for Fout, ndec, pdec, fract in doc_fractional_settings:
        n = clock_status.ndec_lut[ndec]
        m = fract / (1<<15)
        p = clock_status.pdec_lut[pdec]

        print(f'N={n} M={m:.05f} P={p} I=0')
        start_clock(dev, n, m, p, 0)
        clock_status.query_regs(dev)
        pll0a = clock_status.measure_pll0audio(dev)
        print(f'Expected PLL0AUDIO: {Fout/1e6:.3f}MHz')
        print(f'Measured PLL0AUDIO: {pll0a/1e6:.3f}MHz')
        if pll0a/Fout < 0.98 or pll0a/Fout > 1.02:
            print(f' ==== check this one ====')
        print()
        time.sleep(0.5)

    for Fout, ndec, mdec, pdec in doc_integer_settings:
        n = clock_status.ndec_lut[ndec]
        m = clock_status.mdec_lut[mdec]
        p = clock_status.pdec_lut[pdec]

        print(f'N={n} M={m:.05f} P={p} I=0')
        start_clock(dev, n, m, p, 0)
        clock_status.query_regs(dev)
        pll0a = clock_status.measure_pll0audio(dev)
        print(f'Expected PLL0AUDIO: {Fout/1e6:.3f}MHz')
        print(f'Measured PLL0AUDIO: {pll0a/1e6:.3f}MHz')
        if pll0a/Fout < 0.98 or pll0a/Fout > 1.02:
            print(f' ==== check this one ====')
        print()
        time.sleep(0.5)
        
    for Fout, n, m, p, i in custom_settings:
        print(f'N={n} M={m:.05f} P={p} I={i}')
        start_clock(dev, n, m, p, i)
        clock_status.query_regs(dev)
        pll0a = clock_status.measure_pll0audio(dev)
        hsadc = clock_status.measure_hsadc(dev)
        print(f'Measured PLL0AUDIO: {pll0a/1e6:.3f}MHz')
        print(f'Expected HSADC: {Fout/1e6:.3f}MHz')
        print(f'Measured HSADC: {hsadc/1e6:.3f}MHz')
        if hsadc/Fout < 0.98 or hsadc/Fout > 1.02:
            print(f' ==== check this one ====')
        print()
        time.sleep(0.5)
        
