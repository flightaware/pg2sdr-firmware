#!/usr/bin/env python3

import sys
import math
import lpcsdr_device
import clock_status

def show_status(status, file):
    print(f"Flags: {' '.join(flag.name for flag in lpcsdr_device.StatusFlags if flag in status.flags)}", file=file)
    print(f'', file=file)

    clock_status.print_status(status, file=file)

    if lpcsdr_device.StatusFlags.HSADC_RUN in status.flags:
        print(f'HSADC:', file=file)
        print(f'  CONFIG:         {status.adchs_config:08X}', file=file)
        print(f'  INTS[0].STATUS: {status.adchs_int0_status:08X}', file=file)
        print(f'  FIFO_STS:       {status.adchs_fifo_sts:08X}', file=file)
        print(f'  DSCR_STS:       {status.adchs_dscr_sts:08X}', file=file)
        print(f'', file=file)

    if lpcsdr_device.StatusFlags.DMA_RUN in status.flags:
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

    if lpcsdr_device.StatusFlags.RF_POWER_ON in status.flags:
        print(f'Tuner:', file=file)
        for i in range(0, 32, 4):
            print(f'  {i:2d}: ' + ' '.join(f'{r:02X}' for r in status.tuner_regs[i:i+4]))
        print(f'', file=file)

    print(f'USB:', file=file)
    print(f'  Free buffers:   {status.usb_free_buffers}', file=file)
    print(f'  Filled buffers: {status.usb_filled_buffers}', file=file)
    print(f'  Samples/block:  {status.usb_samples_per_block}', file=file)
    print(f'  Bytes/block:    {status.usb_bytes_per_block}', file=file)
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
    dev = lpcsdr_device.find()
    if not dev:
        print('no lpcsdr found')
        return

    show_status(dev.board_status(measure_clocks=True), file=sys.stderr)

if __name__ == '__main__':
    main()
