"""Helpers to inspect the ADC clock state"""

__all__ = ['print_status']

from enum import IntFlag
from lpcsdr.device import StatusFlags
from lpcsdr.util import *

def _prepare_tables():
    # The hardware appears to use LFSR counters to implement the
    # M, N, and P dividers.
    #
    # LFSR counters generate a predictable, looping sequence of values
    # with a known period, but the individual values in the sequence
    # don't follow a normal binary counting pattern. The hardware
    # needed to implement them is simpler and faster than a regular
    # binary counter, so they're well-suited for implementing
    # high-speed dividers like the ones required in the PLL.
    #
    # To count to M (for a divide-by-M divider), an initial value
    # is loaded into the counter such that after M cycles of the
    # LFSR, the counter value becomes a particular final value.
    # When the hardware sees this final value, it emits one output
    # count and re-loads the initial value.
    #
    # The forward calculation, from an integer divisor to the
    # correct value to load, is relatively simple, given that
    # the structure and period of the LFSR is known: for a
    # given value of M and a LFSR period of T, start from the
    # "final value" and update the LFSR (T - M) times. Because
    # the LFSR sequence repeats every T updates, it will loop back
    # to the "final value" we started from after an additional M
    # updates, which is exactly what we want for a divide-by-M counter.
    #
    # The reverse calculation is trickier, and for simplicity
    # here we simply precalculate a full look-up table by
    # iterating across all possible divisors and recording
    # the corresponding LFSR value they generate. Then, to
    # interpret a LFSR value stored in the hardware, we just
    # look up the corresponding divisor directly in that
    # table.

    global mdec_lut
    mdec_lut = {
        0x18003: 1,
        0x10003: 2,
    }

    x = 0x4000
    for i in range(1<<15, 2, -1):
        x = ((x ^ x>>1) & 1) << 14 | x>>1 & 0xFFFF
        assert (x not in mdec_lut)
        mdec_lut[x] = i;

    global ndec_lut
    ndec_lut = {
        0x302: 1,
        0x202: 2,
    }
    x = 0x80
    for i in range(1<<8, 2, -1):
        x = ((x ^ x>>2 ^ x>>3 ^ x>>4) & 1) << 7 | x>>1 & 0xFF
        assert (x not in ndec_lut)
        ndec_lut[x] = i

    global pdec_lut
    pdec_lut = {
        0x62: 1,
        0x42: 2,
    }
    x = 0x10
    for i in range(1<<5, 2, -1):
        x = ((x ^ x>>2) & 1) << 4 | x>>1 & 0x3F
        assert (x not in pdec_lut)
        pdec_lut[x] = i

_prepare_tables()


class PLLStat(BitFlag):
    """Contents of the PLL0AUDIO_STAT register (UM10503 12.6.4.1)"""
    LOCK = onebit(0)
    FR = onebit(1)
    # bits 2-31 reserved


class PLLCtrl(BitFlag):
    """Contents of the PLL0AUDIO_CTRL register (UM10503 12.6.4.2)"""
    PD = onebit(0)
    BYPASS = onebit(1)
    DIRECTI = onebit(2)
    DIRECTO = onebit(3)
    CLKEN = onebit(4)
    # bit 5 reserved
    FRM = onebit(6)
    # bits 7-10 reserved
    AUTOBLOCK = onebit(11)
    PLLFRACT_REQ = onebit(12)
    SEL_EXT = onebit(13)
    MOD_PD = onebit(14)
    # bits 15-23 reserved
    CLK_SEL = bitrange(24,28)
    # bits 29-31 reserved


class PLLMDiv(BitFlag):
    """Contents of the PLL0AUDIO_MDIV register (UM10503 12.6.4.3)"""
    MDEC = bitrange(0,16)
    # bits 17-31 reserved


class PLLNPDiv(BitFlag):
    """Contents of the PLL0AUDIO_NP_DIV register (UM10503 12.6.4.4)"""
    PDEC = bitrange(0,6)
    # bits 7-11 reserved
    NDEC = (0xFF)<<12
    # bits 22-31 reserved


class PLLFrac(BitFlag):
    """Contents of the PLL0AUDIO_FRAC register (UM10503 12.6.4.5)"""
    PLLFRACT_CTRL = bitrange(0,21)
    # bits 22-31 reserved


class IDIVECtrl(BitFlag):
    """Contents of the IDIVE_CTRL register (UM10503 12.6.8)"""
    PD = onebit(0)
    # bit 1 reserved
    IDIV = bitrange(2,9)
    # bit 10 reserved
    AUTOBLOCK = onebit(11)
    # bits 12-23 reserved
    CLK_SEL = bitrange(24,28)
    # bits 29-31 reserved


class FIFOCfg(BitFlag):
    """Contents of the ADCHS FIFO_CFG register (UM10503 47.6.4)"""
    PACKED_READ = onebit(0)
    FIFO_LEVEL = bitrange(1,4)
    # bits 5-31 reserved


class Config(BitFlag):
    """Contents of the ADCHS CONFIG register (UM10503 47.6.8)"""
    TRIGGER_MASK = bitrange(0,1)
    TRIGGER_MODE = bitrange(2,3)
    TRIGGER_SYNC = onebit(4)
    CHANNEL_ID_EN = onebit(5)
    RECOVERY_TIME = bitrange(6,13)
    # bits 14-31 reserved


class ADCSpeed(BitFlag):
    """Contents of the ADCHS ADC_SPEED register (UM10503 47.6.11)"""
    DGEC0 = bitrange(0,3)
    DGEC1 = bitrange(4,7)
    DGEC2 = bitrange(8,11)
    DGEC3 = bitrange(12,15)
    DGEC4 = bitrange(16,19)
    DGEC5 = bitrange(20,23)
    # bits 24-31 reserved


class PowerControl(BitFlag):
    """Contents of the ADCHS POWER_CONTROL register (UM10503 47.6.12)"""
    CRS = bitrange(0,3)
    DCINNEG = bitrange(4,9)
    DCINPOS = bitrange(10,15)
    TWOS = onebit(16)
    POWER_SWITCH = onebit(17)
    BGAP_SWITCH = onebit(18)
    # bits 19-31 reserved


class Int0Status(BitFlag):
    """Contents of the ADCHS STATUS0 register (UM10503 47.6.19)"""
    FIFO_LEVEL_TRIG = onebit(0)
    FIFO_EMPTY = onebit(1)
    FIFO_OVERFLOW = onebit(2)
    DSCR_DONE = onebit(3)
    DSCR_ERROR = onebit(4)
    ADC_OVF = onebit(5)
    ADC_UNF = onebit(6)
    # bits 7-31 reserved


class FIFOSts(BitFlag):
    """Contents of the ADCHS FIFO_STS register (UM10503 47.6.3)"""
    LEVEL = bitrange(0,3)
    # bits 4-31 reserved


class DscrSts(BitFlag):
    """Contents of the ADCHS DSCR_STS register (UM10503 47.6.6)"""    
    ACT_TABLE = onebit(0)
    ACT_DESCRIPTOR = bitrange(1,3)
    # bits 4-31 reserved


def print_hsadc_status(status, file):
    if StatusFlags.HSADC_RUN not in status.flags:
        return

    fifo_cfg = FIFOCfg(status.adchs_fifo_cfg)
    config = Config(status.adchs_config) 
    adc_speed = ADCSpeed(status.adchs_adc_speed) 
    power_control = PowerControl(status.adchs_power_control)
    int0_status = Int0Status(status.adchs_int0_status)
    fifo_sts = FIFOSts(status.adchs_fifo_sts)
    dscr_sts = DscrSts(status.adchs_dscr_sts)
    
    print(f'HSADC:', file=file)
    print(f'  FIFO_CFG:       {fifo_cfg:08X}  {flag_string(fifo_cfg,True)}', file=file)
    print(f'  CONFIG:         {config:08X}  {flag_string(config,True)}', file=file)
    print(f'  ADC_SPEED:      {adc_speed:08X}  {flag_string(adc_speed,True)}', file=file)
    print(f'  POWER_CONTROL:  {power_control:08X}  {flag_string(power_control,True)}', file=file)
    print(f'  INTS[0].STATUS: {int0_status:08X}  {flag_string(int0_status,False)}', file=file)
    print(f'  FIFO_STS:       {fifo_sts:08X}  {flag_string(fifo_sts,True)}', file=file)
    print(f'  DSCR_STS:       {dscr_sts:08X}  {flag_string(dscr_sts,True)}', file=file)
    print(f'', file=file)

    
def print_adc_clock_status(status, file):
    print(f'Target fADC: {format_frequency(status.hsadc_frequency)}', file=file)
    print(f'', file=file)
    if not status.hsadc_frequency:
        return

    pll_stat = PLLStat(status.pll_stat)
    pll_ctrl = PLLCtrl(status.pll_ctrl)

    pll_mdiv = PLLMDiv(status.pll_mdiv)
    msel = mdec_lut.get(pll_mdiv.extract(PLLMDiv.MDEC), None)
    
    pll_npdiv = PLLNPDiv(status.pll_np_div)
    psel = pdec_lut.get(pll_npdiv.extract(PLLNPDiv.PDEC), None)
    nsel = ndec_lut.get(pll_npdiv.extract(PLLNPDiv.NDEC), None)

    pll_frac = PLLFrac(status.pll_frac)    
    fractional_m = pll_frac.extract(PLLFrac.PLLFRACT_CTRL) / (1<<15)

    print(f'PLL0AUDIO:', file=file)
    print(f'  STAT:   {pll_stat:08X}  {flag_string(pll_stat)}', file=file)
    print(f'  CTRL:   {pll_ctrl:08X}  {flag_string(pll_ctrl)}', file=file)
    print(f'  MDIV:   {pll_mdiv:08X}  {flag_string(pll_mdiv)}  MSEL={msel}', file=file)
    print(f'  NPDIV:  {pll_npdiv:08x}  {flag_string(pll_npdiv)}  PSEL={psel} NSEL={nsel}', file=file)
    print(f'  FRAC:   {pll_frac:08x}  {flag_string(pll_frac)}  FRACTIONAL_M={fractional_m:.5f}', file=file)
    print(f'', file=file)

    idiv_e_ctrl = IDIVECtrl(status.idiv_e_ctrl)    
    print(f'IDIV_E:', file=file)
    print(f'  CTRL:   {idiv_e_ctrl:08X}  {flag_string(idiv_e_ctrl)}', file=file)
    print(f'', file=file)

    if pll_ctrl & PLLCtrl.DIRECTI:
        fRef = 12e6
        n = 'bypassed'
    elif nsel is None:
        fRef = 0
        n = 'invalid'
    else:
        fRef = 12e6 / nsel
        n = nsel

    if not (pll_ctrl & PLLCtrl.SEL_EXT):
        m = fractional_m
    elif msel is None:
        m = 'invalid'
    else:
        m = msel
    fCCO = 2 * m * fRef

    if pll_ctrl & PLLCtrl.DIRECTO:
        fPLL = fCCO
        p = 'bypassed'
    elif psel is None:
        fPLL = 0
        p = 'invalid'
    else:
        fPLL = fCCO / 2 / psel
        p = psel

    if idiv_e_ctrl & IDIVECtrl.PD:
        fADC = fPLL
        i = 'bypassed'
        fadc_source = 'PLL0AUDIO'
    else:
        idiv_divisor = 1 + idiv_e_ctrl.extract(IDIVECtrl.IDIV)
        fADC = fPLL / idiv_divisor
        i = idiv_divisor
        fadc_source = 'IDIV_E'

    print(f'Expected clocks with: N={n} M={m:.5f} P={p} I={i}', file=file)
    print(f'  fRef: {format_frequency(fRef)}', file=file)
    print(f'  fCCO: {format_frequency(fCCO)}', file=file)
    print(f'  fPLL: {format_frequency(fPLL)}', file=file)
    print(f'  fADC: {format_frequency(fADC)} (from {fadc_source})', file=file)
    print(f'', file=file)


def print_status(status, file):
    print_hsadc_status(status, file)
    print_adc_clock_status(status, file)
