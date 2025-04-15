#!/usr/bin/env python3

import sys
import lpcsdr_device

def main():
    dev = lpcsdr_device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    dev.comms_check()
    print('comms check was okay')
    return 0

if __name__ == '__main__':
    sys.exit(main())

