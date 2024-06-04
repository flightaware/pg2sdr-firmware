#!/usr/bin/env python3

import sys
import usb.core
import time

dev = usb.core.find(idVendor=0xdead, idProduct=0xbeef)
dev.set_configuration()

bytecount = 0
start = time.monotonic_ns()
recent = [ (start, bytecount) ]
last_report = start

while True:    
    data = dev.read(endpoint=0x81, size_or_buffer=512*1024, timeout=None)
    now = time.monotonic_ns()
    bytecount += len(data)
    recent.append( (now, bytecount) )
    if len(recent) > 64:
        del recent[0]        

    if (now - last_report) > 1e9:
        last_report = now
        elapsed = (now - start) / 1e9
        rate = bytecount / elapsed        
        print(f'Total:  {bytecount/1048576.0:8.3f} MiB / {elapsed:7.3f}s   {rate/1048576:5.1f} MiB/s   {rate*8/1e6:5.1f} Mbit/s', file=sys.stderr)

        recent_elapsed = (recent[-1][0] - recent[0][0]) / 1e9
        recent_bytes = recent[-1][1] - recent[0][1]
        recent_rate = recent_bytes / recent_elapsed
        recent_bps = recent_rate * 8
        print(f'Recent: {recent_bytes/1048576.0:8.3f} MiB / {recent_elapsed:7.3f}s   {recent_rate/1048576.0:5.1f} MiB/s   {recent_rate*8/1e6:5.1f} Mbit/s', file=sys.stderr)

        if False:
            for i in range(0, 8192, 16):
                line = data[i:i+16]
                hexbytes = ( f"{line[j]:02x}" for j in range(len(line)) )
                print(f"{i:04x} ", ' '.join(hexbytes), file=sys.stdout)
