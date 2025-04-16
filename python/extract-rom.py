#!/usr/bin/env python3

import sys
import lpcsdr_device

def main():
    if len(sys.argv) < 2:
        print(f'syntax: {sys.argv[0]} <path to output file>')
        return 2

    dev = lpcsdr_device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    with open(sys.argv[1], 'wb') as f:
        for i in range(0, 65536, 256):
            data = dev.memory_read(0x10400000 + i, 256)
            f.write(data)

    return 0

if __name__ == '__main__':
    sys.exit(main())
