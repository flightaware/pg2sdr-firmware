#!/usr/bin/env python3

import sys
import typing
import struct
import time
import usb.core
import usb.util

class DFUStatus(typing.NamedTuple):
    bStatus: int
    bwPollTimeout: int
    bState: int
    iString: int


def dfu_dnload(dev, block, data):
    print(f'DFU_DNLOAD({block}, {len(data)} bytes)')
    dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                                                type=usb.util.CTRL_TYPE_CLASS,
                                                                recipient=usb.util.CTRL_RECIPIENT_INTERFACE),
                      bRequest=0x01,
                      wValue=block,
                      wIndex=0,
                      data_or_wLength=data)


def dfu_getstatus(dev):
    print('DFU_GETSTATUS')
    status = dev.ctrl_transfer(bmRequestType=usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                                                         type=usb.util.CTRL_TYPE_CLASS,
                                                                         recipient=usb.util.CTRL_RECIPIENT_INTERFACE),
                               bRequest=0x03,
                               wValue=0,
                               wIndex=0,
                               data_or_wLength=6)

    return DFUStatus(bStatus=int(status[0]),
                     bwPollTimeout=struct.unpack('<I', bytes(status[1:4]) + b'\x00')[0],
                     bState=int(status[4]),
                     iString=int(status[5]))


def main():
    if len(sys.argv) < 2:
        print(f'syntax: {sys.argv[0]} <path to firmware image>')
        return

    dev = usb.core.find(idVendor=0x1fc9, idProduct=0x000c)
    if dev is None:
        print('no device found')
        return

    dev.set_configuration()

    with open(sys.argv[1], 'rb') as f:
        block = 0
        while True:
            more = f.read(2048)
            if not more:
                break

            dfu_dnload(dev, block, more)
            block += 1

            status = dfu_getstatus(dev)
            while status.bStatus == 0 and status.bState == 4:  # 4: dfuDNBUSY
                time.sleep(max(status.bwPollTimeout, 50))

            if status.bStatus != 0 or status.bState != 5: # 5: dfuDNLOAD-IDLE
                raise IOError(f'DFU_GETSTATUS: {status}')

    # LPC starts the new firmware immediately on the final GETSTATUS following
    # a zero-length DFU_DNLOAD, so expect an error on that GETSTATUS
    dfu_dnload(dev, block, b'')
    try:
        dfu_getstatus(dev)
    except usb.core.USBError:
        pass


if __name__ == '__main__':
    main()
