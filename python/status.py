#!/usr/bin/env python3

import sys
import math

import lpcsdr.adc_status
import lpcsdr.device
import lpcsdr.tuner
from lpcsdr.util import *

def show_status(status, file):
    print(f'Serial: {status.serial_number:016X}', file=file)
    print(f'Flags:  {flag_string(status.flags)}', file=file)
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
        lpcsdr.tuner.print_status(status.tuner_xtal, status.tuner_regs)

    print(f'USB:', file=file)
    print(f'  Free buffers:   {status.usb_free_buffers}', file=file)
    print(f'  Filled buffers: {status.usb_filled_buffers}', file=file)
    print(f'  Samples/block:  {status.usb_samples_per_block}', file=file)
    print(f'  Bytes/block:    {status.usb_bytes_per_block}', file=file)
    print(f'', file=file)

    print(f'M4:', file=file)
    print(f'  Core clock:     {format_frequency(status.m4_freq)}', file=file)
    print(f'  Mean load:      {100-status.m4_mean_idle/status.m4_mean_idle_scale*100:.1f}%', file=file)
    print(f'  Peak load:      {100-status.m4_min_idle/status.m4_min_idle_scale*100:.1f}%', file=file)
    print(f'', file=file)

    if status.clock_irc:
        print(f'Measured clock source frequencies:', file=file)
        print(f'  IRC:       {format_frequency(status.clock_irc)}', file=file)
        print(f'  PLL0USB:   {format_frequency(status.clock_pll0usb)}', file=file)
        print(f'  PLL0AUDIO: {format_frequency(status.clock_pll0audio)}', file=file)
        print(f'  PLL1:      {format_frequency(status.clock_pll1)}', file=file)
        print(f'  IDIV_A:    {format_frequency(status.clock_idiv_a)}', file=file)
        print(f'  IDIV_B:    {format_frequency(status.clock_idiv_b)}', file=file)
        print(f'  IDIV_C:    {format_frequency(status.clock_idiv_c)}', file=file)
        print(f'  IDIV_D:    {format_frequency(status.clock_idiv_d)}', file=file)
        print(f'  IDIV_E:    {format_frequency(status.clock_idiv_e)}', file=file)
        print(f'', file=file)

    if status.intr_systick:
        print(f'Interrupt counters:')
        print(f'  SysTick: {status.intr_systick}')
        print(f'  DMA:     {status.intr_dma}')
        print(f'  USART0:  {status.intr_usart0}')
        print(f'  USB0:    {status.intr_usb0}')
        print(f'  WWDT:    {status.intr_wwdt}')
        print(f'  M0APP:   {status.intr_m0app}')
        print(f'  M4:      {status.intr_m4}')
        print(f'', file=file)

    reasons = {
        lpcsdr.device.ResetReason.POR: 'Power-on reset',
        lpcsdr.device.ResetReason.UNEXPECTED: 'Unexpected reset (watchdog etc)',
        lpcsdr.device.ResetReason.FIRMWARE: 'Firmware was asked to reset',
        lpcsdr.device.ResetReason.PANIC: 'Firmware panic'
    }
    if status.reset_reason in reasons:
        print(f'Last reset cause: {status.reset_reason.name} ({reasons[status.reset_reason]})')
    else:
        print(f'Last reset cause: unrecognized (0x{status.reset_reason:08x})')

    if status.reset_code:
        print(f'Reset code: {status.reset_code}')


def main():
    import argparse

    parser = argparse.ArgumentParser(description='Show lpcsdr board status')
    parser.add_argument('--clocks', help="Measure clock frequencies (may cause data loss if streaming)", action='store_true')

    args = parser.parse_args()

    dev = lpcsdr.device.find()
    if not dev:
        print('no lpcsdr found')
        return

    show_status(dev.board_status(measure_clocks=args.clocks), file=sys.stderr)

if __name__ == '__main__':
    main()
