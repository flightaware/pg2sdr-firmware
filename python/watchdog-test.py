#!/usr/bin/env python3

import sys
import lpcsdr.device

def main():
    dev = lpcsdr.device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    dev.watchdog_test()
    print('triggered watchdog tests')
    return 0

if __name__ == '__main__':
    sys.exit(main())
