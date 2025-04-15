#!/usr/bin/env python3

import sys
import lpcsdr_device

import clock_calc
import clock_status

def main():    
    if len(sys.argv) < 2:
        print(f'syntax: {sys.argv[0]} [-f|-i] <frequency in MHz>')
        return 1

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
    error, n, m, p, i, actual_fcco, actual_frequency = settings

    dev = lpcsdr_device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    print('Starting ADC/DMA ..')
    fixedpoint_m = int(round(m * 32768))
    dev.start_transfer(n_div = n,
                       m_div = fixedpoint_m,
                       p_div = p,
                       idiv_div = i)

    print('.. done.')
    print()
    print('Clock registers:')
    clock_status.query_regs(dev)
    print()

    print(f'Measured fPLL: {clock_status.measure_pll0audio(dev)/1e6:.3f} MHz')
    print(f'Measured fADC: {clock_status.measure_hsadc(dev)/1e6:.3f} MHz')

    return 0

if __name__ == '__main__':
    sys.exit(main())
