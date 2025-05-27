#!/usr/bin/env python3

import sys
import lpcsdr.device
import argparse

empty_page = b'\xFF' * 256
def write_sector(dev, sector_address, blob, force):
    # Write one 4096-byte sector, taking into account what's currently stored to minimize
    # erase/write cycles

    # We can:
    #
    #  1) write data in 256-byte pages, but only to a page that was previously erased
    #     so that it contains all FF bytes (an "empty page")
    #  2) erase the entire 4096-byte sector, which erases all of the 16 pages it contains
    #
    # Note that we cannot selectively erase only a single page, we can only erase the
    # whole sector in one go.
    #
    # Jump through some hoops to work out whether we need to erase the sector, and
    # what writes we need to do (either with or without the sector erase happening) to
    # bring everything up to date

    needs_erase = False  # do we need to erase the whole sector?
    not_empty = set()    # what pages should end up with non-empty data?
    needs_write = set()  # what pages do we need to write?

    if force:
        # Just erase and rewrite everything
        needs_erase = True
        needs_write = set(offset for offset in range(0, len(blob), 256))
    else:
        for offset in range(0, len(blob), 256):
            address = sector_address + offset
            page_len = min(256, len(blob) - offset)
            existing = dev.flash_read_quad(address, page_len)

            match = (existing == blob[offset:offset+page_len])
            is_empty = (existing == empty_page[:page_len])
            wants_empty = (blob[offset:offset+page_len] == empty_page[:page_len])

            if not is_empty:
                # remember all non-empty pages;
                # if we erase the sector, we must write all of these
                not_empty.add(offset)

            if not match:
                # this page does not match what we want
                if not wants_empty:
                    # this page needs new, non-empty, data written to it
                    needs_write.add(offset)
                if not is_empty:
                    # this page is not already erased, and we need to write new data to it,
                    # so we must erase the whole sector
                    needs_erase = True

    if needs_erase:
        print(f'Erasing sector {sector_address:08x}..{sector_address+4095:08x}')
        dev.flash_erase(sector_address)
        # we wiped everything and must rewrite all pages that aren't meant to be empty
        needs_write.update(not_empty)

    for offset in sorted(needs_write):
        address = sector_address + offset
        page_len = min(256, len(blob) - offset)
        print(f'Programming page {address:08x}..{address+page_len-1:08x}')
        dev.flash_write(address, blob[offset:offset+page_len])

    return (needs_erase or needs_write)

def write_firmware(dev, path, force):
    print(f'Writing firmware from {path} to flash..')
    changes = 0
    with open(path, 'rb') as f:
        address = 0
        while True:
            sector = f.read(4096)
            if len(sector) > 0:
                if write_sector(dev, address, sector, force):
                    changes += 1
            if len(sector) < 4096:
                break
            address += len(sector)
    if changes:
        print(f'Updated {changes} sectors')
    else:
        print('Existing flash contents match the firmware file, no changes written')

def verify_firmware(dev, path):
    print('Verifying..')
    with open(path, 'rb') as f:
        address = 0
        while True:
            page = f.read(256)
            if len(page) > 0:
                existing = dev.flash_read(address, len(page))
                if page != existing:
                    for i in range(len(page)):
                        if page[i] != existing[i]:
                            print(f'Page verify failed, first mismatch at 0x{address+i:04X}')
                            print(page.hex())
                            print(existing.hex())
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
    parser.add_argument('--force', '-f', help="force erasing and rewriting all sectors, rather than being selective", action='store_true')
    parser.add_argument('--reset', '-r', help="after writing new firmware, reset device to use the new firmware", action='store_true')
    parser.add_argument('filename', help='path to firmware image to write')

    args = parser.parse_args()
    if args.dryrun and args.verify:
        print("doesn't make sense to specify both --dryrun and --verify together")
        return 1

    dev = lpcsdr.device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    write = (not args.verify)
    verify = (not args.dryrun) or args.verify

    if args.dryrun:
        print('dryrun mode, no changes will be made')
        dev.flash_write = dryrun_flash_write
        dev.flash_erase = dryrun_flash_erase

    if write:
        write_firmware(dev, args.filename, args.force)

    if verify:
        if not verify_firmware(dev, args.filename):
            # verify failed, exit with error code
            return 1

    if args.reset and not args.dryrun:
        switches = dev.switch_state()
        if switches & 1:
            print('ignoring --reset as the boot-mode switch is set to boot from USB')
        else:
            print('Resetting device..')
            dev.reset()

    return 0


if __name__ == '__main__':
    sys.exit(main())
