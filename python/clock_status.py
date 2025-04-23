#!/usr/bin/env python3

import sys
from enum import IntFlag
import lpcsdr_device

def _prepare_tables():
    global mdec_lut
    mdec_lut = {
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
        0x62: 1,
        0x42: 2,
    }
    x = 0x10
    for i in range(1<<5, 2, -1):
        x = ((x ^ x>>2) & 1) << 4 | x>>1 & 0x3F
        assert (x not in pdec_lut)
        pdec_lut[x] = i

_prepare_tables()

def flag_members(flags):
    # we could just use Flag.__iter__ here ..
    # .. except it's only in Python 3.11 and later
    # and the Ubuntu VMs have 3.10
    result = []
    remainder = flags
    for flag in type(flags):  # make sure to use the metaclass __iter__
        if flag in remainder:
            result.append(flag)
            remainder = remainder ^ flag
    if remainder:
        result.append(remainder)
    return result

def flag_string_parts(flags):    
    return list( (f.name if f.name else repr(f.value)) for f in flag_members(flags) )

def flag_string(flags):
    return ' '.join(flag_string_parts(flags))

class PLLStat(IntFlag):
    LOCK = 1
    FR = 2

    def __str__(self):
        return flag_string(self)

class PLLCtrl(IntFlag):
    PD = (1<<0)
    BYPASS = (1<<1)
    DIRECTI = (1<<2)
    DIRECTO = (1<<3)
    CLKEN = (1<<4)
    FRM = (1<<6)
    AUTOBLOCK = (1<<11)
    PLLFRACT_REQ = (1<<12)
    SEL_EXT = (1<<13)
    MOD_PD = (1<<14)

    def __str__(self):
        clksel_mask = 0x1F << 24
        parts = flag_string_parts(self & ~clksel_mask)
        parts.append(f'CLK_SEL={(self.value & clksel_mask) >> 24}')
        return ' '.join(parts)


class IDIVCtrl(IntFlag):
    PD = (1<<0)
    AUTOBLOCK = (1<<11)

    def __str__(self):
        idiv_mask = 0xFF << 2
        clksel_mask = 0x1F << 24
        parts = flag_string_parts(self & ~(clksel_mask | idiv_mask))
        parts.append(f'IDIV={(self.value & idiv_mask) >> 2}')
        parts.append(f'CLK_SEL={(self.value & clksel_mask) >> 24}')
        return ' '.join(parts)

    
def print_status(status, file):
    print(f'Target fADC: {status.hsadc_frequency/1e6:.6f} MHz', file=file)
    print(f'', file=file)
    if not status.hsadc_frequency:
        return

    print(f'PLL0AUDIO:', file=file)
    print(f'  STAT:  {status.pll_stat:08X}  {PLLStat(status.pll_stat)}', file=file)
    print(f'  CTRL:  {status.pll_ctrl:08X}  {PLLCtrl(status.pll_ctrl)}', file=file)

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
    
    print(f'  MDIV:  {status.pll_mdiv:08x}  MDEC={mdec} MSEL={msel} SELP={selp} SELI={seli} SELR={selr}', file=file)
    print(f'  NPDIV: {status.pll_np_div:08X}  PDEC={pdec} PSEL={psel} NDEC={ndec:3d} NSEL={nsel}', file=file)
    print(f'  FRAC:  {status.pll_frac:08X}  FRACTIONAL_M={fractional_m:.5f}', file=file)
    print(f'', file=file)

    print(f'IDIV_E:', file=file)
    print(f'  CTRL:  {status.idiv_e_ctrl:08X}  {IDIVCtrl(status.idiv_e_ctrl)}', file=file)
    print(f'', file=file)

    if status.pll_ctrl & (1<<2):
        fRef = 12e6
        n = 'bypassed'
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
        p = 'bypassed'
    elif psel is None:
        fPLL = 0
        p = 'invalid'
    else:
        fPLL = fCCO / 2 / psel
        p = psel

    if (status.idiv_e_ctrl & 1):
        fADC = fPLL
        i = 'bypassed'
    else:
        idiv_divisor = 1 + (status.idiv_e_ctrl>>2)&0xFF
        fADC = fPLL / idiv_divisor
        i = idiv_divisor

    print(f'Expected clocks with: N={n} M={m:.5f} P={p} I={i}', file=file)
    print(f'  fRef: {fRef/1e6:10.6f} MHz', file=file)
    print(f'  fCCO: {fCCO/1e6:10.6f} MHz', file=file)
    print(f'  fPLL: {fPLL/1e6:10.6f} MHz', file=file)
    print(f'  fADC: {fADC/1e6:10.6f} MHz', file=file)
    print(f'', file=file)

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
    
