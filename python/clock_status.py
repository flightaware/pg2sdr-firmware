#!/usr/bin/env python3

import sys
import usb.core
import usb.util
import time
import math
import struct

def prepare_tables():
    global mdec_lut
    mdec_lut = {
        0: 0,
        0x18003: 1,
        0x10003: 2,
    }
    
    x = 0x4000
    for i in range(1<<15, 2, -1):
        x = ((x ^ x>>1) & 1) << 14 | x>>1 & 0xFFFF
        assert (x not in mdec_lut)
        mdec_lut[x] = i;

    global ndec_lut
    ndec_lut = {
        0: 0,
        0x302: 1,
        0x202: 2,
    }
    x = 0x80
    for i in range(1<<8, 2, -1):
        x = ((x ^ x>>2 ^ x>>3 ^ x>>4) & 1) << 7 | x>>1 & 0xFF
        assert (x not in ndec_lut)
        ndec_lut[x] = i

    global pdec_lut
    pdec_lut = {
        0: 0,
        0x62: 1,
        0x42: 2,
    }
    x = 0x10
    for i in range(1<<5, 2, -1):
        x = ((x ^ x>>2) & 1) << 4 | x>>1 & 0x3F
        assert (x not in pdec_lut)
        pdec_lut[x] = i

prepare_tables()
        
def stat_reg(stat):
    r = []
    if stat & 1:
        r.append('LOCK')
    if stat & 2:
        r.append('FR')
    if not (stat & 1):
        r.append('(no PLL lock)')
    return ' '.join(r)

def ctrl_reg(ctrl):
    r = []
    if ctrl & (1<<0):
        r.append('PD')
    if ctrl & (1<<1):
        r.append('BYPASS')
    if ctrl & (1<<2):
        r.append('DIRECTI')
    if ctrl & (1<<3):
        r.append('DIRECTO')
    if ctrl & (1<<4):
        r.append('CLKEN')
    if ctrl & (1<<6):
        r.append('FRM')
    if ctrl & (1<<11):
        r.append('AUTOBLOCK')
    if ctrl & (1<<12):
        r.append('PLLFRACT_REQ')
    if ctrl & (1<<13):
        r.append('SEL_EXT')
    if ctrl & (1<<14):
        r.append('MOD_PD')
    r.append(f'CLK_SEL={(ctrl>>24)&0x1F}')
    return ' '.join(r)

def idiv_reg(ctrl):
    r = []
    if ctrl & (1<<0):
        r.append('PD')
    if ctrl & (1<<11):
        r.append('AUTOBLOCK')
    r.append(f'IDIV={1 + (ctrl>>2)&0xFF}')
    r.append(f'CLK_SEL={(ctrl>>24)&0x1F}')
    return ' '.join(r)
    

def query_regs(dev):
    status = dev.pll0audio_regs()
    print(f'STAT:  {status.pll_stat:08x}  {stat_reg(status.pll_stat)}')
    print(f'CTRL:  {status.pll_ctrl:08x}  {ctrl_reg(status.pll_ctrl)}')

    mdec = status.pll_mdiv & 0x1FFFF
    selp = (status.pll_mdiv >> 17) & 0x1F
    seli = (status.pll_mdiv >> 22) & 0x3F
    selr = (status.pll_mdiv >> 28) & 0x0F
    
    pdec = status.pll_np_div & 0x7F
    ndec = (status.pll_np_div >> 12) & 0x3FF
    
    msel = mdec_lut.get(mdec, None)
    psel = pdec_lut.get(pdec, None)
    nsel = ndec_lut.get(ndec, None)

    pllfract_ctrl = status.pll_frac & 0x1FFFFF
    fractional_m = pllfract_ctrl / (1<<15)
    
    print(f'MDIV:  {status.pll_mdiv:08x}  MDEC={mdec:5d} MSEL={msel:5d}  SELP={selp} SELI={seli} SELR={selr}')
    print(f'NPDIV: {status.pll_np_div:08x}  PDEC={pdec:3d} PSEL={psel:2d}  NDEC={ndec:3d} NSEL={nsel:3d}')
    print(f'FRAC:  {status.pll_frac:08x}  FRACTIONAL_M={fractional_m:.5f}')

    print(f'IDIV:  {status.idiv_e_ctrl:08x}  {idiv_reg(status.idiv_e_ctrl)}')

    if status.pll_ctrl & (1<<2):
        fRef = 12e6
        n = '0 (disabled)'
    elif nsel is None:
        fRef = 0
        n = 'invalid'
    else:
        fRef = 12e6 / nsel
        n = nsel

    if not (status.pll_ctrl & (1<<13)):
        m = fractional_m
    elif msel is None:
        m = 'invalid'
    else:
        m = msel
    fCCO = 2 * m * fRef

    if status.pll_ctrl & (1<<3):
        fPLL = fCCO
        p = '0 (disabled)'
    elif psel is None:
        fPLL = 0
        p = 'invalid'
    else:
        fPLL = fCCO / 2 / psel
        p = psel

    if (status.idiv_e_ctrl & 1):
        fADC = fPLL
        i = '0 (disabled)'
    else:
        idiv_divisor = 1 + (status.idiv_e_ctrl>>2)&0xFF
        fADC = fPLL / idiv_divisor
        i = idiv_divisor

    print('-----')
    print(f'N={n} M={m:.5f} P={p} I={i}')
    print(f'computed fRef: {fRef/1e6:.3f} MHz')
    print(f'computed fCCO: {fCCO/1e6:.3f} MHz')
    print(f'computed fPLL: {fPLL/1e6:.3f} MHz')
    print(f'computed fADC: {fADC/1e6:.3f} MHz')

def measure_hsadc(dev):
    return dev.base_freq(12)

def measure_pll0audio(dev):
    return dev.input_freq(8)

def format_frequency(f):
    if f > 1e5:
        return f'{f/1e6:.3f} MHz'
    if f > 1e2:
        return f'{f/1e3:.3f} kHz'
    return f'{f:.0f} Hz'

def show_clocks(dev):
    clocks = [
        '32K',
        'IRC',
        'ENET_RX',
        'ENET_TX',
        'CLKIN',
        'RESERVED1',
        'CRYSTAL',
        'USBPLL',
        'AUDIOPLL',
        'MAINPLL',
        'RESERVED2',
        'RESERVED3',
        'IDIVA',
        'IDIVB',
        'IDIVC',
        'IDIVD',
        'IDIVE']
        
    for i, name in enumerate(clocks):
        print(f'{name.ljust(10)}  {format_frequency(dev.base_freq(i))}')

if __name__ == '__main__':
    dev = lpcsdr_device.find()
    if dev is None:
        print('no lpcsdr device found')
        sys.exit(1)
        
    query_regs(dev)
    print(f'Measured fPLL: {measure_pll0audio(dev)/1e6:.3f}MHz')
    print(f'Measured fADC: {measure_hsadc(dev)/1e6:.3f}MHz')

    show_clocks(dev)
    
