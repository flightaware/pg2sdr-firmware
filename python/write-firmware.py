#!/usr/bin/env python3

import sys
import lpcsdr_device
import argparse

empty_page = b'\xFF' * 256
def write_sector(dev, sector_address, blob):
    state = {}
    needs_erase = False

    for offset in range(0, len(blob), 256):
        address = sector_address + offset
        page_len = min(256, len(blob) - offset)
        existing = dev.flash_read(address, page_len)

        match = (existing == blob[offset:offset+page_len])
        empty = (existing == empty_page[:page_len])
        wants_empty = (blob[offset:offset+page_len] == empty_page[:page_len])

        if not match:
            needs_erase = True

        state[offset] = (match, empty, wants_empty)

    if needs_erase:
        print(f'Erasing sector {sector_address:08x}..{sector_address+4095:08x}')
        dev.flash_erase(sector_address)
        for offset, (match, empty, wants_empty) in state.items():
            state[offset] = (wants_empty, True, wants_empty)

    for offset in sorted(state.keys()):
        if not match:
            address = sector_address + offset
            page_len = min(256, len(blob) - offset)
            print(f'Programming page {address:08x}..{address+page_len-1:08x}')
            dev.flash_write(address, blob[offset:offset+page_len])

def write_firmware(dev, path):
    with open(path, 'rb') as f:
        address = 0
        while True:
            sector = f.read(4096)
            if len(sector) > 0:
                write_sector(dev, address, sector)
            if len(sector) < 4096:
                break
            address += len(sector)

def verify_firmware(dev, path):
    with open(path, 'rb') as f:
        address = 0
        while True:
            page = f.read(256)
            if len(page) > 0:
                existing = dev.flash_read(address, len(page))
                if page != existing:
                    for i in range(len(page)):
                        if page[i] != existing[i]:
                            print(f'Verify failed, first mismatch at 0x{address+i:04X}')
                            break
                    else:
                        print(f"Verify failed somewhere around 0x{address:04X} but I couldn't find the exact address??")
                    return False
            if len(page) < 256:
                break
            address += len(page)

    print('Verified OK')
    return True

def dryrun_flash_write(address, data):
    print(f'  dryrun: would have written {len(data)} bytes at address 0x{address:04x}')

def dryrun_flash_erase(address):
    print(f'  dryrun: would have erased sector at address 0x{address:04x}')

def main():
    parser = argparse.ArgumentParser(description='Write LPCSDR firmware to flash EEPROM')
    parser.add_argument('--dryrun', '-n', help="don't write firmware, do a dry run", action='store_true')
    parser.add_argument('--verify', '-v', help="don't write firmware, verify flash contents match requested firmware", action='store_true')
    parser.add_argument('filename', help='path to firmware image to write')

    args = parser.parse_args()
    if args.dryrun and args.verify:
        print("doesn't make sense to specify both --dryrun and --verify together")
        return 1

    dev = lpcsdr_device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    write = (not args.verify)
    verify = (not args.dryrun) or args.verify

    if args.dryrun:
        dev.flash_write = dryrun_flash_write
        dev.flash_erase = dryrun_flash_erase

    if write:
        write_firmware(dev, args.filename)

    if verify:
        if not verify_firmware(dev, args.filename):
            # verify failed, exit with error code
            return 1

    return 0


if __name__ == '__main__':
    sys.exit(main())
