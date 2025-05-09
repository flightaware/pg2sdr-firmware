#!/usr/bin/env python3

import sys
import argparse
import lpcsdr_device

def main():
    parser = argparse.ArgumentParser(description="control LPCSDR ADC settings")
    parser.add_argument('--dcinpos', help='configure DCINPOS', type=int, required=True)
    parser.add_argument('--dcinneg', help='configure DCINNEG', type=int, required=True)
    parser.add_argument('--twos', help='configure TWOS', type=int, required=True)
    args = parser.parse_args()

    dev = lpcsdr_device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    print(f'configuring with DCINPOS={args.dcinpos} DCINNEG={args.dcinneg} TWOS={args.twos}')
    dev.config_adc(dcinpos=args.dcinpos, dcinneg=args.dcinneg, twos=args.twos)
    return 0

if __name__ == '__main__':
    sys.exit(main())
