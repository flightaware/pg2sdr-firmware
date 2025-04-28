#!/usr/bin/env python3

import sys
import argparse
import struct
import math

import numpy as np

import lpcsdr_device
import clock_calc

def main():
    parser = argparse.ArgumentParser(description="control LPCSDR RF power")

    parser.add_argument('--samples', help='capture this many samples', type=int, required=True)
    parser.add_argument('--rate', help='set sampling rate (in MHz), start ADC before capture, stop ADC after capture', type=float)
    parser.add_argument('filename', help='set output filename')

    args = parser.parse_args()

    dev = lpcsdr_device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    if args.rate:
        settings, _ = clock_calc.settings_for(args.rate * 1e6)
        print(f'Starting ADC with settings: {settings!r}', file=sys.stderr)
        fixedpoint_m = int(round(settings.m * 32768))
        dev.start_transfer(n_div = settings.n,
                           m_div = fixedpoint_m,
                           p_div = settings.p,
                           idiv_div = settings.i)

    status = dev.board_status()
    if lpcsdr_device.StatusFlags.EP1_ENABLED not in status.flags:
        raise RuntimeError('board does not seem to be ready for ADC transfer, specify --rate or ensure ADC is already configured')

    blocks = math.ceil(args.samples / status.usb_samples_per_block) + 8
    total = blocks * status.usb_bytes_per_block
    blocks_per_chunk = 128
    chunk_size = blocks_per_chunk * status.usb_bytes_per_block
    chunk_timeout = 500 + (1100 * blocks_per_chunk * status.usb_samples_per_block // status.hsadc_frequency)
    results = []

    print(f'Reading {blocks} blocks, {blocks * status.usb_samples_per_block} samples ..', file=sys.stderr)

    captured = 0
    while captured < total:
        result = dev.dev.read(0x81, min(chunk_size, total - captured), timeout=chunk_timeout)
        results.append(result)
        captured += len(result)
        print(f'{100*captured/total:.0f}%  \r', end='', flush=True, file=sys.stderr)
    print('100%', file=sys.stderr)

    if args.rate:
        print('Stopping ADC', file=sys.stderr)
        dev.stop_transfer()

    combined = np.empty(total, dtype='u1')
    i = 0
    for result in results:
        combined[i:i+len(result)] = result
        i += len(result)
    results = None  # allow GC of old data

    print(f'Unpacking data into {args.filename}', file=sys.stderr)
    with open(args.filename, 'wb') as outf:
        header = struct.Struct('<IIIII')
        last_seq = None

        # ignore first 8 blocks, to purge out any old data in the device buffers
        for offset in range(8*status.usb_bytes_per_block, total, status.usb_bytes_per_block):
            block = combined[offset:offset+status.usb_bytes_per_block]
            magic, block_len, samples, sequence, flags = header.unpack(block[0:header.size])
            if magic != 0xdeadbeef:
                raise IOError('wrong magic number in block header at offset {offset}')
            if block_len != status.usb_bytes_per_block:
                raise IOError('wrong block len in block header at offset {offset}')

            if last_seq is not None:
                if sequence > last_seq + 1:
                    print(f'Dropped {(sequence - last_seq - 1)*status.usb_samples_per_block} samples between blocks ({last_seq} .. {sequence})', file=sys.stderr)
            last_seq = sequence

            if flags:
                bflags = lpcsdr_device.BlockStatusFlags(flags)
                print(f"  block {sequence} status: {' '.join(flag.name for flag in lpcsdr_device.BlockStatusFlags if flag in bflags)}")

            # reinterpret bytes as little-endian uint32
            packed = block[header.size:header.size+status.usb_samples_per_block*12//8].view(dtype='<u4')

            # use slices with 12-byte (3*uint32) stride to chop up the block into chunks of 12 bytes
            # that we can operate on simultaneously (so the loop across the block can run inside the numpy
            # implementation, not in python)

            p1 = packed[0::3]  # 1st uint32 of each 12-byte chunk
            p2 = packed[1::3]  # 2nd uint32 of each 12-byte chunk
            p3 = packed[2::3]  # 3rd uint32 of each 12-byte chunk

            # build 8 uint16 samples (with 12 bits of data per sample) from each 12-byte chunk
            unpacked = np.empty(status.usb_samples_per_block, dtype='<u2')
            unpacked[0::8] = (p1 & 0x0FFF0000) >> 16
            unpacked[1::8] = (p1 & 0x00000FFF)
            unpacked[2::8] = (p2 & 0x0FFF0000) >> 16
            unpacked[3::8] = (p2 & 0x00000FFF)
            unpacked[4::8] = (p3 & 0x0FFF0000) >> 16
            unpacked[5::8] = (p3 & 0x00000FFF)
            unpacked[6::8] = ((p1 & 0xF0000000) >> 20) | ((p2 & 0xF0000000) >> 24) | ((p3 & 0xF0000000) >> 28)
            unpacked[7::8] = ((p1 & 0x0000F000) >> 4) | ((p2 & 0x0000F000) >> 8) | ((p3 & 0x0000F000) >> 12)

            # sign-extend from 12 bits to 16 bits (so the data can be loaded as signed-16-bit)
            signed = (unpacked & 0x7FF) - (unpacked & 0x800)
            signed.tofile(outf)

    return 0

if __name__ == '__main__':
    sys.exit(main())
