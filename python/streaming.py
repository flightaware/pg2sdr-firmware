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

    parser.add_argument('--samples', help='capture this many samples per read block', type=int, required=True)
    parser.add_argument('--rate', help='set sampling rate, start ADC before capture', type=frequency_value)


    args = parser.parse_args()

    dev = lpcsdr.device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    if args.rate:
        print(f'Starting ADC at {format_frequency(args.rate)}', file=sys.stderr)
        lpcsdr.adc.start_transfer(dev, args.rate)

    status = dev.board_status()
    if lpcsdr.device.StatusFlags.EP1_ENABLED not in status.flags:
        raise RuntimeError('board does not seem to be ready for ADC transfer, specify --rate or ensure ADC is already configured')

    def show_progress(current, total):
        print(f'Reading data: {100*current/total:.0f}%  \r', end='', flush=True, file=sys.stderr)
        if current >= total:
            print(file=sys.stderr)
    while args.samples == 0:
        raw_blocks = lpcsdr.bulk.read_blocks(dev, 10000 + args.skip * status.usb_samples_per_block, show_progress)
    raw_blocks = lpcsdr.bulk.read_blocks(dev, args.samples + args.skip * status.usb_samples_per_block, show_progress)

    if args.rate:
        print('Stopping ADC', file=sys.stderr)
        dev.stop_transfer()


    return 0

if __name__ == '__main__':
    sys.exit(main())
