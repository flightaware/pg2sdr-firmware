"""Helpers for processing ADC data from bulk endpoint EP1"""

__all__ = ['ADCBlock', 'read_blocks', 'unpack_blocks']

import math
import numpy as np
import numpy.typing as npt
from typing import NamedTuple, Optional
from collections.abc import Callable, Sequence, Generator

from lpcsdr.device import Device, BlockHeader, BlockStatusFlags
import lpcsdr.device

BHS = BlockHeader.struct

class ADCBlock(NamedTuple):
    sequence: int
    flags: BlockStatusFlags
    samples: npt.NDArray[np.int16]

def read_blocks(dev: Device, nsamples: int, progress_fn:Optional[Callable[[int,int],None]]=None) -> list[Sequence[int]]:
    """Read enough data for nsamples number of samples.
       Returns the raw data as a list-of-arrays.
       If progress_fn is not None, periodically call it with the current and total byte counts
       while the read is in progress."""

    status = dev.board_status(measure_clocks=False)
    
    blocks = math.ceil(nsamples / status.usb_samples_per_block)
    total = blocks * status.usb_bytes_per_block
    blocks_per_chunk = 128
    chunk_size = blocks_per_chunk * status.usb_bytes_per_block
    chunk_timeout = 500 + (1100 * blocks_per_chunk * status.usb_samples_per_block // status.hsadc_frequency)

    results = []
    captured = 0

    while captured < total:
        if progress_fn:
            progress_fn(captured, total)
        result = dev.ep1_read(min(chunk_size, total - captured), timeout=chunk_timeout)
        results.append(result)
        captured += len(result)

    if progress_fn:
        progress_fn(captured, total)
    return results

def unpack_blocks(raw: Sequence[Sequence[int]]) -> Generator[ADCBlock]:
    """Given raw data as a list-of-arrays (as read by read_blocks),
    unpack the sample data and yield a sequence of unpacked ADCBlocks"""

    data = np.empty(0, dtype=np.uint8)

    # accumulate more raw data into 'data' from 'raw'
    while len(data) < BHS.size and len(raw) > 0:
        data = np.concatenate((data, raw.pop(0)), dtype=np.uint8, casting='unsafe')
    if len(data) < BHS.size:
        # not even one block's worth
        return

    # extract the first block's header to work out the common block size
    first_header = BlockHeader(*BHS.unpack(data[:BHS.size]))
    if first_header.magic != BlockHeader.EXPECTED_MAGIC:
        raise ValueError('wrong magic number in first block header')

    bytes_per_block = first_header.block_len
    samples_per_block = first_header.samples

    if bytes_per_block % 512 != 0:
        raise ValueError(f'header bytes_per_block={bytes_per_block} is not a multiple of 512')
    if samples_per_block % 8 != 0:
        raise ValueError(f'header samples_per_block={samples_per_block} is not a multiple of 8')
    if BHS.size + (samples_per_block * 12 // 8) > bytes_per_block:
        raise ValueError(f'header samples_per_block={samples_per_block} does not fit in header bytes_per_block={bytes_per_block}')

    unpacked = np.empty(samples_per_block, dtype=np.uint16)  # temporary processing space

    while True:
        # accumulate more raw data into 'data' from 'raw'
        while len(data) < bytes_per_block and len(raw) > 0:
            data = np.concatenate((data, raw.pop(0)), dtype=np.uint8, casting='unsafe')
        if len(data) < bytes_per_block:  # not enough remaining for a complete block, we're done
            return

        # extract one block from the start of 'data'
        block = data[:bytes_per_block]
        data = data[bytes_per_block:]

        block_header = BlockHeader(*BHS.unpack(block[:BHS.size]))
        if block_header.magic != BlockHeader.EXPECTED_MAGIC:
            raise ValueError('wrong magic number in block header at offset {offset}')
        if block_header.block_len != bytes_per_block:
            raise ValueError('wrong block len in block header at offset {offset}')
        if block_header.samples != samples_per_block:
            raise ValueError('wrong sample count in block header at offset {offset}')

        # reinterpret bytes as little-endian uint32
        packed = block[BHS.size:BHS.size+samples_per_block*12//8].view(dtype='<u4')

        # use slices with 12-byte (3*uint32) stride to chop up the block into chunks of 12 bytes
        # that we can operate on simultaneously (so the loop across the block can run inside the numpy
        # implementation, not in interpreted python)
        p1 = packed[0::3]  # 1st uint32 of each 12-byte chunk
        p2 = packed[1::3]  # 2nd uint32 of each 12-byte chunk
        p3 = packed[2::3]  # 3rd uint32 of each 12-byte chunk

        # build 8 uint16 samples (with 12 bits of data per sample) from each 12-byte chunk
        unpacked[0::8] = (p1 & 0x00000FFF)
        unpacked[1::8] = (p1 & 0x0FFF0000) >> 16
        unpacked[2::8] = (p2 & 0x00000FFF)
        unpacked[3::8] = (p2 & 0x0FFF0000) >> 16
        unpacked[4::8] = (p3 & 0x00000FFF)
        unpacked[5::8] = (p3 & 0x0FFF0000) >> 16
        unpacked[6::8] = ((p1 & 0x0000F000) >> 4) | ((p2 & 0x0000F000) >> 8) | ((p3 & 0x0000F000) >> 12)
        unpacked[7::8] = ((p1 & 0xF0000000) >> 20) | ((p2 & 0xF0000000) >> 24) | ((p3 & 0xF0000000) >> 28)
        # sign-extend to 16 bits
        unpacked = (unpacked & 0x7FF) - (unpacked & 0x800)

        # convert uint16->int16, copy to result list
        yield ADCBlock(block_header.sequence,
                       block_header.flags,
                       unpacked.astype(np.int16))
