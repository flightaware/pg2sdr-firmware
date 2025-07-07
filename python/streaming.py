#!/usr/bin/env python3

import sys
import argparse
import struct
import math

import lpcsdr.device
import lpcsdr.adc
import lpcsdr.bulk
from lpcsdr.util import *

def main():
    parser = argparse.ArgumentParser(description="stream ADC data to the host without processing")

    parser.add_argument('--samples', help='capture this many samples per read block', type=int)
    parser.add_argument('--rate', help='set sampling rate, start ADC before capture', type=frequency_value)


    args = parser.parse_args()

    dev = lpcsdr.device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    if args.rate:
        print(f'Starting ADC at {format_frequency(args.rate)}', file=sys.stderr)
        lpcsdr.adc.start_transfer(dev, args.rate)

    if args.samples:
        read_size = args.samples
    elif args.rate:
        read_size = args.rate * 0.1
    else:
        read_size = 100000

    status = dev.board_status()
    if lpcsdr.device.StatusFlags.EP1_ENABLED not in status.flags:
        raise RuntimeError('board does not seem to be ready for ADC transfer, specify --rate or ensure ADC is already configured')

    spinner = '\|/-'

    print('Reading samples forever; control-C to end', file=sys.stderr)

    try:
        i = 0
        first_seq = last_seq = None
        dropped_blocks = 0
        adc_overrun = 0
        packing_overrun = 0
        usb_overrun = 0
        while True:
            raw_blocks = lpcsdr.bulk.read_blocks(dev, read_size)
            for block in lpcsdr.bulk.unpack_blocks(raw_blocks, unpack_samples=False):
                if first_seq is None:
                    first_seq = block.sequence
                if last_seq is not None:
                    dropped_blocks += (block.sequence - last_seq - 1)
                last_seq = block.sequence

                if lpcsdr.device.BlockStatusFlags.ADC_OVERRUN in block.flags:
                    adc_overrun += 1
                if lpcsdr.device.BlockStatusFlags.PACKING_OVERRUN in block.flags:
                    packing_overrun += 1
                if lpcsdr.device.BlockStatusFlags.USB_OVERRUN in block.flags:
                    usb_overrun += 1

            total = (block.sequence - first_seq + 1) * status.usb_samples_per_block
            dropped = dropped_blocks * status.usb_samples_per_block
            received = total - dropped
            dropped_pct = 100.0 * dropped / total

            spin = spinner[i % len(spinner)]
            i += 1

            print(f'\r{received}/{total} samples ({dropped_pct:.1f}% dropped) overruns: USB {usb_overrun} packing {packing_overrun} ADC {adc_overrun} {spin}     ', end='', file=sys.stderr)
    except KeyboardInterrupt:
        pass
    finally:
        print(file=sys.stderr)
        if args.rate:
            print('Stopping ADC', file=sys.stderr)
            dev.stop_transfer()

    return 0

if __name__ == '__main__':
    sys.exit(main())
