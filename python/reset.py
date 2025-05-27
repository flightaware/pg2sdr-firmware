#!/usr/bin/env python3

import sys
import lpcsdr.device

def main():
    dev = lpcsdr.device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    dev.reset()
    print('sent device reset request')
    return 0

if __name__ == '__main__':
    sys.exit(main())
