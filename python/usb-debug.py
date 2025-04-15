#!/usr/bin/env python3

import lpcsdr_device

def main():
    dev = lpcsdr_device.find()
    if not dev:
        print('no lpcsdr found')
        return
    
    status = dev.adc_dma_status()
    print(f'ADC CONFIG:         {status.adchs_config:08X}')
    print(f'ADC INTS[0].STATUS: {status.adchs_int0_status:08X}')
    print(f'ADC FIFO_STS:       {status.adchs_fifo_sts:08X}')
    print(f'ADC DSCR_STS:       {status.adchs_dscr_sts:08X}')
    print()
    print(f'DMA CONFIG:         {status.gpdma_config:08x}')
    print(f'DMA ENBLDCHNS:      {status.gpdma_enbldchns:08X}')
    print(f'DMA TCSTAT:         {status.gpdma_rawinttcstat:08X}')
    print(f'DMA ERRSTAT:        {status.gpdma_rawinterrstat:08X}')
    print(f'DMA CH[0].CONFIG:   {status.gpdma0_config:08X}')
    print(f'DMA CH[0].CONTROL:  {status.gpdma0_control:08X}')
    print(f'DMA CH[0].SRC:      {status.gpdma0_srcaddr:08X}')
    print(f'DMA CH[0].DEST:     {status.gpdma0_destaddr:08X}')
    print(f'DMA CH[0].LLI:      {status.gpdma0_lli:08X}')
    print()
    print(f'current LLI:        {status.current_lli:08X}')
    print(f'next sequence:      {status.next_sequence}')

if __name__ == '__main__':
    main()
