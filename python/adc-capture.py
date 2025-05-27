#!/usr/bin/env python3

import sys
import argparse
import struct
import math

import lpcsdr.device
import lpcsdr.adc
import lpcsdr.bulk
import lpcsdr.util

def main():
    parser = argparse.ArgumentParser(description="capture ADC data")

    parser.add_argument('--samples', help='capture this many samples', type=int, required=True)
    parser.add_argument('--rate', help='set sampling rate (in MHz), start ADC before capture, stop ADC after capture', type=float)
    parser.add_argument('--format', help='Set output format to s16 (signed 16-bit) or tsv (text)', choices=['s16', 'tsv'], default='s16')
    parser.add_argument('filename', help='set output filename')

    args = parser.parse_args()

    dev = lpcsdr.device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    if args.rate:
        print(f'Starting ADC at {args.rate}MHz', file=sys.stderr)
        lpcsdr.adc.start_transfer(dev, args.rate * 1e6)

    status = dev.board_status()
    if lpcsdr.device.StatusFlags.EP1_ENABLED not in status.flags:
        raise RuntimeError('board does not seem to be ready for ADC transfer, specify --rate or ensure ADC is already configured')

    def show_progress(current, total):
        print(f'Reading data: {100*current/total:.0f}%  \r', end='', flush=True, file=sys.stderr)
        if current >= total:
            print(file=sys.stderr)
    raw_blocks = lpcsdr.bulk.read_blocks(dev, args.samples, show_progress)

    if args.rate:
        print('Stopping ADC', file=sys.stderr)
        dev.stop_transfer()

    print(f'Unpacking data into {args.filename}', file=sys.stderr)

    unpacked = lpcsdr.bulk.unpack_blocks(raw_blocks)
    with open(args.filename, ('wt' if args.format == 'tsv' else 'wb')) as outf:
        if args.format == 'tsv':
            outf.write("sample_index\tvalue\n")

        last_seq = first_seq = None
        for index, adc_block in enumerate(lpcsdr.bulk.unpack_blocks(raw_blocks)):
            # ignore first 8 blocks, to purge out any old data in the device buffers
            if index < 8:
                continue

            if first_seq is None:
                first_seq = adc_block.sequence
            elif adc_block.sequence > last_seq + 1:
                dropped = (adc_block.sequence - last_seq - 1)*status.usb_samples_per_block
                print(f'Dropped {dropped} samples between blocks ({last_seq} .. {adc_block.sequence})', file=sys.stderr)
            last_seq = adc_block.sequence

            if adc_block.flags:
                print(f'  block {adc_block.sequence} status: {lpcsdr.util.flag_string(adc_block.flags)}')

            if args.format == 'tsv':
                base = (adc_block.sequence - first_seq) * status.usb_samples_per_block
                for i, v in enumerate(adc_block.samples):
                    print(f'{base+i}\t{v}', file=outf)
            elif args.format == 's16':
                adc_block.samples.view(dtype='<i2').tofile(outf)
            else:
                raise AssertionError('impossible format')

    return 0

if __name__ == '__main__':
    sys.exit(main())
