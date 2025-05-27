#!/usr/bin/env python3

import sys
import math

import lpcsdr.adc_status
import lpcsdr.device
import lpcsdr.tuner
from lpcsdr.util import flag_string

def show_status(status, file):
    print(f'Flags: {flag_string(status.flags)}', file=file)
    print(f'', file=file)

    lpcsdr.adc_status.print_status(status, file=file)

    if lpcsdr.device.StatusFlags.DMA_RUN in status.flags:
        print(f'DMA:', file=file)
        print(f'  CONFIG:         {status.gpdma_config:08x}', file=file)
        print(f'  ENBLDCHNS:      {status.gpdma_enbldchns:08X}', file=file)
        print(f'  TCSTAT:         {status.gpdma_rawinttcstat:08X}', file=file)
        print(f'  ERRSTAT:        {status.gpdma_rawinterrstat:08X}', file=file)
        print(f'  CH[0].CONFIG:   {status.gpdma0_config:08X}', file=file)
        print(f'  CH[0].CONTROL:  {status.gpdma0_control:08X}', file=file)
        print(f'  CH[0].SRC:      {status.gpdma0_srcaddr:08X}', file=file)
        print(f'  CH[0].DEST:     {status.gpdma0_destaddr:08X}', file=file)
        print(f'  CH[0].LLI:      {status.gpdma0_lli:08X}', file=file)
        print(f'  current LLI:    {status.current_lli:08X}', file=file)
        print(f'  next sequence:  {status.next_sequence}', file=file)
        print(f'', file=file)

    if lpcsdr.device.StatusFlags.RF_POWER_ON in status.flags:
        lpcsdr.tuner.print_regs(status.tuner_regs)

    print(f'USB:', file=file)
    print(f'  Free buffers:   {status.usb_free_buffers}', file=file)
    print(f'  Filled buffers: {status.usb_filled_buffers}', file=file)
    print(f'  Samples/block:  {status.usb_samples_per_block}', file=file)
    print(f'  Bytes/block:    {status.usb_bytes_per_block}', file=file)
    print(f'', file=file)

    print(f'M4:', file=file)
    print(f'  Core clock:     {status.m4_freq/1e6:.1f} MHz', file=file)
    print(f'  Mean load:      {100-status.m4_mean_idle/status.m4_mean_idle_scale*100:.1f}%', file=file)
    print(f'  Peak load:      {100-status.m4_min_idle/status.m4_min_idle_scale*100:.1f}%', file=file)
    print(f'', file=file)

    if status.clock_irc:
        print(f'Measured clock source frequencies (MHz):', file=file)
        print(f'  IRC:       {status.clock_irc/1e6:6.2f}', file=file)
        print(f'  PLL0USB:   {status.clock_pll0usb/1e6:6.2f}', file=file)
        print(f'  PLL0AUDIO: {status.clock_pll0audio/1e6:6.2f}', file=file)
        print(f'  PLL1:      {status.clock_pll1/1e6:6.2f}', file=file)
        print(f'  IDIV_A:    {status.clock_idiv_a/1e6:6.2f}', file=file)
        print(f'  IDIV_B:    {status.clock_idiv_b/1e6:6.2f}', file=file)
        print(f'  IDIV_C:    {status.clock_idiv_c/1e6:6.2f}', file=file)
        print(f'  IDIV_D:    {status.clock_idiv_d/1e6:6.2f}', file=file)
        print(f'  IDIV_E:    {status.clock_idiv_e/1e6:6.2f}', file=file)
        print(f'', file=file)
        

def main():
    dev = lpcsdr.device.find()
    if not dev:
        print('no lpcsdr found')
        return

    show_status(dev.board_status(measure_clocks=True), file=sys.stderr)

if __name__ == '__main__':
    main()
