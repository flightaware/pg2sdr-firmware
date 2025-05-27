#!/usr/bin/env python3

import sys
import argparse
import lpcsdr.device

def main():
    parser = argparse.ArgumentParser(description="control LPCSDR RF power")

    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--off', help='turn off RF power', action='store_const', const=lpcsdr.device.RFPowerMode.OFF, dest='mode')
    group.add_argument('--on', help='turn on RF power', action='store_const', const=lpcsdr.device.RFPowerMode.ON, dest='mode')
    group.add_argument('--reset', help='turn off RF power, then turn on RF power', action='store_const', const=lpcsdr.device.RFPowerMode.RESET, dest='mode')

    args = parser.parse_args()

    dev = lpcsdr.device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    print(f'set RF power mode = {args.mode}')
    dev.set_rf_power(args.mode)
    print('done')
    return 0


if __name__ == '__main__':
    sys.exit(main())

