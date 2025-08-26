"""A raw interface to the LPCSDR USB protocol"""

import usb.core
import usb.util
from enum import IntEnum, IntFlag
from typing import ClassVar
import dataclasses
import struct
import sys
import os
import time

from . import dfu
from .util import BitFlag

# Should match defines in lpcsdr_protocol.h
class InReq(IntEnum):
    COMMS_CHECK = 0x01
    FLASH_DEVICE_ID = 0x02
    FLASH_UNIQUE_ID = 0x03
    FLASH_READ = 0x04
    FLASH_READ_QUAD = 0x05
    MEMORY_READ = 0x0B
    TUNER_READ = 0x0C
    BOARD_STATUS = 0x0D
    TUNER_LOCK = 0x0E

class OutReq(IntEnum):
    COMMS_CHECK = 0x01
    FLASH_WRITE = 0x10
    FLASH_ERASE = 0x11
    START_TRANSFER = 0x13
    STOP_TRANSFER = 0x14
    SET_RF_POWER = 0x15
    TUNER_WRITE = 0x16
    TUNER_UPDATE = 0x17
    CONFIG_ADC = 0x2C
    RESET = 0x2D
    WATCHDOG_TEST = 0x2E
    UART_TEST = 0x2F

def ctrl(klass):
    klass = dataclasses.dataclass(klass)
    struct_def = '<' + ''.join(field.metadata['struct_type'] for field in dataclasses.fields(klass))
    klass.struct = struct.Struct(struct_def)
    return klass

def typed(typechar):
    return dataclasses.field(metadata={'struct_type': typechar})

@ctrl
class CommsCheck:
    magic: int = typed('I')
    EXPECTED_MAGIC: ClassVar[int] = 0xdeadbeef

@ctrl
class FlashDeviceID:
    device_id: int = typed('H')

@ctrl
class FlashUniqueID:
    unique_id: int = typed('Q')

class StatusFlags(BitFlag):
    FAST_CPU = 1
    SW1_USBBOOT = 2
    SW2_PRESSED = 4
    RF_POWER_ON = 8
    HSADC_RUN = 16
    DMA_RUN = 32
    EP1_ENABLED = 64
    TUNER_I2C_ERROR = 128
    TUNER_PLL_LOCK = 256
    PLL0AUDIO_RUN = 512
    IS_LPCSDR = 1024
    IS_AIRSPY = 2048

class BlockStatusFlags(BitFlag):
    ADC_OVERRUN = 1
    DMA_ERROR = 2
    PACKING_OVERRUN = 4
    USB_OVERRUN = 8
    ADC_OVF = 256
    ADC_UNF = 512
    FREQ_CHANGE = 1024

@ctrl
class BoardStatus:
    flags: StatusFlags = typed('I')

    hsadc_frequency: int = typed('I')
    pll_stat: int = typed('I')
    pll_ctrl: int = typed('I')
    pll_mdiv: int = typed('I')
    pll_np_div: int = typed('I')
    pll_frac: int = typed('I')
    idiv_e_ctrl: int = typed('I')

    adchs_fifo_cfg: int = typed('I')
    adchs_config: int = typed('I')
    adchs_adc_speed: int = typed('I')
    adchs_power_control: int = typed('I')
    adchs_int0_status: int = typed('I')
    adchs_fifo_sts: int = typed('I')
    adchs_dscr_sts: int = typed('I')

    gpdma_config: int = typed('I')
    gpdma_enbldchns: int = typed('I')
    gpdma_rawinttcstat: int = typed('I')
    gpdma_rawinterrstat: int = typed('I')
    gpdma0_config: int = typed('I')
    gpdma0_control: int = typed('I')
    gpdma0_srcaddr: int = typed('I')
    gpdma0_destaddr: int = typed('I')
    gpdma0_lli: int = typed('I')
    current_lli: int = typed('I')
    next_sequence: int = typed('I')

    tuner_regs: bytes = typed('32s')

    usb_free_buffers: int = typed('I')
    usb_filled_buffers: int = typed('I')
    usb_samples_per_block: int = typed('I')
    usb_bytes_per_block: int = typed('I')

    m4_freq: int = typed('I')
    m4_mean_idle: int = typed('I')
    m4_mean_idle_scale: int = typed('I')
    m4_min_idle: int = typed('I')
    m4_min_idle_scale: int = typed('I')

    clock_32k: int = typed('I')
    clock_irc: int = typed('I')
    clock_pll0usb: int = typed('I')
    clock_pll0audio: int = typed('I')
    clock_pll1: int = typed('I')
    clock_idiv_a: int = typed('I')
    clock_idiv_b: int = typed('I')
    clock_idiv_c: int = typed('I')
    clock_idiv_d: int = typed('I')
    clock_idiv_e: int = typed('I')

    tuner_xtal: int = typed('I')
    serial_number: int = typed('Q')
    
    def __post_init__(self):
        self.flags = StatusFlags(self.flags)

@ctrl
class TunerLock:
    pll_locked: int = typed('I')

@ctrl
class StartTransfer:
    n_divisor: int = typed('I')
    m_divisor: int = typed('I')
    p_divisor: int = typed('I')
    idiv_divisor: int = typed('I')
    
def make():
    dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    return (dev and Device(dev) or None)

# tuner reg cache modes
class TunerCacheMode(IntEnum):
    USE_CACHE = 0
    BYPASS_CACHE = 1
    REFRESH_CACHE = 2

# RF power modes
class RFPowerMode(IntEnum):
    OFF = 0
    ON = 1
    RESET = 2

@ctrl
class BlockHeader:
    magic: int = typed('I')
    block_len: int = typed('I')
    samples: int = typed('I')
    sequence: int = typed('I')
    flags: BlockStatusFlags = typed('I')    
    EXPECTED_MAGIC: ClassVar[int] = 0xdeadbeef

    def __post_init__(self):
        self.flags = BlockStatusFlags(self.flags)

class Changeset:
    """A set of pending tuner register changes, suitable for passing to Device.tuner_update()"""

    def __init__(self):
        self.pending = {}

    def write(self, reg, value):
        """Update the entire value of a single register"""
        self.write_bits(reg, 0xFF, int(value))

    def write_bits(self, reg, mask, value):
        """Update specific bits of a single register"""

        mask = int(mask)
        value = int(value)

        if (value & mask) != value:
            raise ValueError('value has bits set outside of mask')
        if mask == 0:
            return

        old_mask, old_value = self.pending.get(reg, (0,0))
        self.pending[reg] = (old_mask | mask, (old_value & ~mask) | value)

    def clear(self):
        """Reset all pending updates"""
        self.pending.clear()
        
    def get_update_data(self):
        """Return a tuple of (first_reg, update_bytes) suitable for
passing to the LPCSDR device to implement the updates stored in this
changeset"""

        if not self.pending:
            return (0, b'')

        # find the range of registers to update
        sorted_keys = sorted(self.pending.keys())
        first = sorted_keys[0]
        last = sorted_keys[-1]
        count = last - first + 1

        # bytes to pass to the LPCSDR;
        # first half is values, second half is masks
        update_bytes = bytearray(2 * count)   # initially all zero

        # fill in all non-zero entries
        for reg, (mask, value) in self.pending.items():            
            offset = reg - first
            update_bytes[offset] = value
            update_bytes[offset + count] = mask

        return (first, bytes(update_bytes))

        
class Device(object):
    def __init__(self, dev):
        self.dev = dev

        try:
            cfg = dev.get_active_configuration()
        except usb.core.USBError:
            cfg = None
        if cfg is None or cfg.bConfigurationValue != 1:
            dev.set_configuration(1)

        self.comms_check()
        self.board_status()

    def close(self):
        del self.dev

    def _in_bytes(self, *, req:InReq, length:int, value:int=0, index:int=0) -> bytes:
        rt = usb.util.build_request_type(direction=usb.util.CTRL_IN,
                                         type=usb.util.CTRL_TYPE_VENDOR,
                                         recipient=usb.util.CTRL_RECIPIENT_DEVICE)
        return bytes(self.dev.ctrl_transfer(bmRequestType=rt,
                                            bRequest=int(req),
                                            wValue=value,
                                            wIndex=index,
                                            data_or_wLength=length,
                                            timeout=2000))

    def _in(self, *, req:InReq, klass, value:int=0, index:int=0):
        raw = self._in_bytes(req=req, value=value, index=index, length=klass.struct.size)
        if len(raw) != klass.struct.size:
            raise IOError(f'expected {klass.struct.size} bytes but got {len(raw)} bytes')
        return klass(*klass.struct.unpack(raw))

    def _out_bytes(self, *, req:OutReq, data:bytes, value=0, index=0):
        rt = usb.util.build_request_type(direction=usb.util.CTRL_OUT,
                                         type=usb.util.CTRL_TYPE_VENDOR,
                                         recipient=usb.util.CTRL_RECIPIENT_DEVICE)
        self.dev.ctrl_transfer(bmRequestType=rt,
                               bRequest=int(req),
                               wValue=value,
                               wIndex=index,
                               data_or_wLength=bytes(data),
                               timeout=2000)
        
    def _out(self, *, req:OutReq, data, value=0, index=0):
        raw = data.struct.pack(*(getattr(data, field.name) for field in dataclasses.fields(data)))
        self._out_bytes(req=req, value=value, index=index, data=raw)

    def comms_check(self):
        message = self._in(req=InReq.COMMS_CHECK, value=0, index=0, klass=CommsCheck)
        if message.magic != CommsCheck.EXPECTED_MAGIC:
            raise IOError(f'comms check: expected {CommsCheck.EXPECTED_MAGIC:08X}, got {message.magic:08X}')
        self._out(req=OutReq.COMMS_CHECK, data=CommsCheck(magic=CommsCheck.EXPECTED_MAGIC))

    def flash_device_id(self) -> int:
        message = self._in(req=InReq.FLASH_DEVICE_ID, klass=FlashDeviceId)
        return message.device_id

    def flash_unique_id(self) -> int:
        message = self._in(req=InReq.FLASH_UNIQUE_ID, klass=FlashUniqueId)
        return message.unique_id

    def flash_read(self, address, length) -> bytes:
        return self._in_bytes(req=InReq.FLASH_READ, value=(address & 0xFFFF), index=(address >> 16), length=length)

    def flash_read_quad(self, address, length) -> bytes:
        return self._in_bytes(req=InReq.FLASH_READ_QUAD, value=(address & 0xFFFF), index=(address >> 16), length=length)

    def memory_read(self, address: int, length: int) -> bytes:
        return self._in_bytes(req=InReq.MEMORY_READ, value=(address & 0xFFFF), index=(address >> 16), length=length)

    def tuner_read(self, first_reg: int, length: int, cache_mode:TunerCacheMode=TunerCacheMode.USE_CACHE) -> bytes:
        return self._in_bytes(req=InReq.TUNER_READ, value=first_reg, index=cache_mode, length=length)

    def board_status(self, measure_clocks=False) -> BoardStatus:
        self.last_board_status = self._in(req=InReq.BOARD_STATUS, value=(1 if measure_clocks else 0), index=0, klass=BoardStatus)
        return self.last_board_status

    def tuner_lock(self, vco_current: int, timeout_ms: int) -> bool:
        message = self._in(req=InReq.TUNER_LOCK, value=vco_current, index=timeout_ms, klass=TunerLock)
        return (message.pll_locked != 0)

    def flash_write(self, address: int, page_data: bytes):
        self._out_bytes(req=OutReq.FLASH_WRITE, value=(address & 0xFFFF), index=(address >> 16), data=page_data)

    def flash_erase(self, sector_address: int):
        self._out_bytes(req=OutReq.FLASH_ERASE, value=(sector_address & 0xFFFF), index=(sector_address >> 16), data=b'')

    def start_transfer(self, n_div, m_div, p_div, idiv_div):
        self._out(req=OutReq.START_TRANSFER, value=0, index=0,
                  data=StartTransfer(n_div, m_div, p_div, idiv_div))

    def stop_transfer(self):
        self._out_bytes(req=OutReq.STOP_TRANSFER, value=0, index=0, data=b'')

    def set_rf_power(self, mode:RFPowerMode):
        self._out_bytes(req=OutReq.SET_RF_POWER, value=int(mode), data=b'')

    def tuner_write(self, first_reg:int, data:bytes, mode:TunerCacheMode=TunerCacheMode.USE_CACHE):
        self._out_bytes(req=OutReq.TUNER_WRITE, value=first_reg, index=mode, data=data)

    def tuner_update(self, changeset:Changeset):
        if not changeset.pending:
            return  # no changes pending
        first, update_bytes = changeset.get_update_data()
        self._out_bytes(req=OutReq.TUNER_UPDATE, value=first, data=update_bytes)
        changeset.clear()

    def config_adc(self, dcinpos:bool, dcinneg:bool, twos:bool):
        flags = (dcinpos and 1 or 0) | \
            (dcinneg and 2 or 0) | \
            (twos and 4 or 0)
        self._out_bytes(req=OutReq.CONFIG_ADC, value=flags, data=b'')

    def reset(self):
        self._out_bytes(req=OutReq.RESET, data=b'')

    def watchdog_test(self):
        self._out_bytes(req=OutReq.WATCHDOG_TEST, data=b'')

    def uart_test(self):
        self._out_bytes(req=OutReq.UART_TEST, data=b'')

    def ep1_read(self, length, timeout):
        return self.dev.read(0x81, length, timeout)


def locate_firmware():
    env = os.environ.get('LPCSDR_FIRMWARE', None)
    if env is not None:
        if not os.path.exists(env):
            print(f'LPCSDR_FIRMWARE was set to "{env}", but that path does not exist', file=sys.stderr)
            return None
        return env

    # use realpath here, because e.g. soapy-scan symlinks lpcsdr -> lpcsdr_firmware/python/lpcsdr
    firmware_python_dir = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
    firmware_base_dir = os.path.join(firmware_python_dir, '..')
    candidates = [
        os.path.realpath(os.path.join(firmware_base_dir, 'Debug', 'lpcsdr.bin')),
        os.path.realpath(os.path.join(firmware_base_dir, 'images', 'lpcsdr.bin')),
    ]

    for path in candidates:
        if os.path.exists(path):
            return path

    print(f'No suitable firmware found (tried: {" ".join(candidates)}); maybe set LPCSDR_FIRMWARE environment variable?', file=sys.stderr)
    return None


def find():
    usbdev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
    if usbdev is None:
        # Look for a DFU device we can download firmware to
        dfudev = usb.core.find(idVendor=0x1fc9, idProduct=0x000c)
        if dfudev is None:
            return None

        print(f'Found a LPC DFU device at {dfudev!r}', file=sys.stderr)
        firmware_path = locate_firmware()
        if not firmware_path:
            return None

        print(f'Downloading LPCSDR firmware from {firmware_path}', file=sys.stderr)
        dfu.download_firmware(dfudev, firmware_path)
        print(f'Waiting for LPCSDR to re-enumerate', file=sys.stderr)

        # Wait for the LPCSDR firmware to boot and re-enumerate
        # pyusb doesn't have hotplug support (yet?) so just poll
        for i in range(5):
            usbdev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
            if usbdev is not None:
                print(f'New LPCSDR device appeared at {usbdev!r}', file=sys.stderr)
                break
            time.sleep(0.5)
        else:
            print(f"LPCSDR device didn't re-enumerate after firmware download", file=sys.stderr)
            return None

    return Device(usbdev)
