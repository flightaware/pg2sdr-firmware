#!/usr/bin/env python3

import sys
import os
import usb.core
import usb.util
import time

OFFSET = 0

def ll_read_page(dev, address, length):
    assert length <= 256
    assert (address & 255) == 0

    address += OFFSET    
    return bytes(dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                             type=usb.util.CTRL_TYPE_VENDOR,
                                                                             recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                                   bRequest=0x05,
                                   wValue=(address & 0xFFFF),
                                   wIndex=(address >> 16) & 0xFFFF,
                                   data_or_wLength=length,
                                   timeout=1000))

def ll_erase_sector(dev, sector_address):
    assert (sector_address & 4095) == 0

    sector_address += OFFSET    
    dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                type=usb.util.CTRL_TYPE_VENDOR,
                                                                recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                      bRequest=0x11,
                      wValue=(sector_address & 0xFFFF),
                      wIndex=(sector_address >> 16) & 0xFFFF,
                      data_or_wLength=None,
                      timeout=1000)

def ll_write_page(dev, address, data):
    assert len(data) <= 256
    assert (address & 255) == 0

    address += OFFSET
    dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                type=usb.util.CTRL_TYPE_VENDOR,
                                                                recipient=usb.util.CTRL_RECIPIENT_DEVICE),
                      bRequest=0x10,
                      wValue=(address & 0xFFFF),
                      wIndex=(address >> 16) & 0xFFFF,
                      data_or_wLength=data,
                      timeout=1000)

empty_page = b'\xFF' * 256
def write_sector(dev, sector_address, blob, go=False):
    state = {}
    needs_erase = False

    for offset in range(0, len(blob), 256):
        address = sector_address + offset
        page_len = min(256, len(blob) - offset)
        existing = ll_read_page(dev, address, page_len)

        match = (existing == blob[offset:offset+page_len])
        empty = (existing == empty_page[:page_len])
        wants_empty = (blob[offset:offset+page_len] == empty_page[:page_len])

        if not match:
            needs_erase = True
        
        state[offset] = (match, empty, wants_empty)

    if needs_erase:
        print(f'Erasing sector {sector_address:08x}..{sector_address+4095:08x}')
        if go:
            ll_erase_sector(dev, sector_address)
        for offset, (match, empty, wants_empty) in state.items():
            state[offset] = (wants_empty, True, wants_empty)

    for offset in sorted(state.keys()):
        if not match:
            address = sector_address + offset
            page_len = min(256, len(blob) - offset)
            print(f'Programming page {address:08x}..{address+page_len-1:08x}')
            if go:
                ll_write_page(dev, address, blob[offset:offset+page_len])                

def write_firmware(dev, path, go=False):
    with open(path, 'rb') as f:
        blob = f.read()

        for address in range(0, len(blob), 4096):
            write_sector(dev, address, blob[address:address+4096], go)
            
        if go:
            verify_ok = True
            for address in range(0, len(blob), 256):
                page_len = min(256, len(blob) - address)
                existing = ll_read_page(dev, address, page_len)
                if blob[address:address+page_len] != existing:
                    print(f'Verify failed, page mismatch at {address:08x}')
                    verify_ok = False

            if verify_ok:
                print('All done, verification OK')
            else:
                print('*** verification failed ***')

if __name__ == '__main__':
    dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    dev.set_configuration()
    write_firmware(dev, sys.argv[1], go=True)
    
