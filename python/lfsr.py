#!/usr/bin/env python3

def show_mdec():
    x = 0x4000
    i = 1<<15
    while True:
        x = ((x ^ x>>1) & 1) << 14 | x >> 1 & 0x3FFF
        print(f'M={i}: {x:015b}')
        if x == 0x4000:
            break
        i -= 1

def show_ndec():
    x = 0x80
    i = 1<<8
    while True:
        x = ((x ^ x>>2 ^ x>>3 ^ x>>4) & 1) << 7 | x>>1 & 0xFF
        print(f'N={i}: {x:08b}')
        if x == 0x80:
            break
        i -= 1

def show_pdec():
    x = 0x10
    i = 1<<5
    while True:
        x = ((x ^ x>>2) & 1) << 4 | x>>1 & 0x3F
        print(f'P={i}: {x:05b}')
        if x == 0x10:
            break
        i -= 1

if __name__ == '__main__':
    show_mdec()
    show_ndec()
    show_pdec()
