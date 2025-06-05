#!/usr/bin/env python3

import sys
import math
import argparse
import numpy as np
import scipy.signal
import scipy.fft

from lpcsdr.util import *

def main():
    parser = argparse.ArgumentParser(description="run FFT over ADC data")
    parser.add_argument('--inform', help='set input format', choices=['s16', 'tsv'], default='tsv')
    parser.add_argument('--rate', help='set sampling rate (just scales x axis)', type=frequency_value)
    parser.add_argument('--bins', help='number of sampling bins', type=int, default=1024)
    parser.add_argument('--window', help='window to apply', choices=['boxcar', 'blackman', 'hamming', 'flattop'], default='blackman')
    parser.add_argument('infile', help='input file')
    parser.add_argument('outfile', help='output file')

    args = parser.parse_args()

    match args.inform:
        case 's16':
            with open(args.infile, 'rb') as infile:
                adc_data = np.fromfile(infile, dtype='<i2') / 2048.0

        case 'tsv':
            with open(args.infile, 'rt') as infile:
                adc_data = np.loadtxt(infile, skiprows=1, usecols=(1,)) / 2048.0

        case _:
            raise NotImplemented

    n = args.bins * 2   # number of samples per FFT input block
    window = scipy.signal.windows.get_window(args.window, n)

    print(f'calculate FFT (with {args.window} window) of ADC data from {args.infile}', file=sys.stderr)
    print(f'  with {args.bins} frequency bins')
    if args.rate:
        print(f'  sampling rate {format_frequency(args.rate)}', file=sys.stderr)
        print(f'  bin width {format_frequency(args.rate / n, precision=0)}', file=sys.stderr)
    print(f'  output to {args.outfile}', file=sys.stderr)

    freqs = scipy.fft.rfftfreq(n)
    if args.rate:
        freqs *= args.rate
    results = np.zeros(len(freqs), dtype=np.float32)

    for i in range(0, len(adc_data), n):
        block = adc_data[i:i+n]
        if len(block) < n:
            break

        block *= window
        this_fft = scipy.fft.rfft(block)
        results += np.abs(this_fft)

    with open(args.outfile, 'w') as outfile:
        for freq, result in zip(freqs, results):
            print(f'{freq}\t{result}', file=outfile)

    print(f'gnuplot commands to plot results:', file=sys.stderr)
    print(f'  set ylabel "power/dB"', file=sys.stderr)
    if args.rate:
        print(f'  set xlabel "frequency/MHz"', file=sys.stderr)
        print(f'  plot "{args.outfile}" using ($1/1e6):(20*log10($2)) with lines title "{args.outfile}"', file=sys.stderr)
    else:
        print(f'  set xlabel "FFT bin"', file=sys.stderr)
        print(f'  plot "{args.outfile}" using ($1/1e6):(20*log10($2)) with lines title "{args.outfile}"', file=sys.stderr)

    return 0


if __name__ == '__main__':
    sys.exit(main())

