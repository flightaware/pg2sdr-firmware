import sys
import math
import time
from typing import NamedTuple
import bisect
import operator

import lpcsdr.device
from lpcsdr.device import Device, Changeset
from lpcsdr.util import BitFlag, flag_string_parts, onebit, bitrange

#
# IF bandpass filter magic values, taken from pxsdr-firmware/pv2-firmware.c for now
# (we should revisit this)
#

class LPFSettings(NamedTuple):
    cutoff_khz: int
    lpf_coarse: int
    lpf_fine: int
    lpf_q: int
    lpf_narrow: int

lpf_calibration = (
    LPFSettings(cutoff_khz = 2027, lpf_coarse = 3, lpf_fine = 15, lpf_q = 0, lpf_narrow = 1), # target 1894; narrowest narrow-mode filter
    LPFSettings(cutoff_khz = 2093, lpf_coarse = 3, lpf_fine = 13, lpf_q = 0, lpf_narrow = 1), # target 2105
    LPFSettings(cutoff_khz = 2320, lpf_coarse = 1, lpf_fine = 15, lpf_q = 0, lpf_narrow = 1), # target 2338
    LPFSettings(cutoff_khz = 2601, lpf_coarse = 1, lpf_fine = 9, lpf_q = 0, lpf_narrow = 1),  # target 2598
    LPFSettings(cutoff_khz = 2891, lpf_coarse = 0, lpf_fine = 12, lpf_q = 0, lpf_narrow = 1), # target 2887
    LPFSettings(cutoff_khz = 3177, lpf_coarse = 0, lpf_fine = 8, lpf_q = 0, lpf_narrow = 1),  # target 3207
    LPFSettings(cutoff_khz = 3525, lpf_coarse = 0, lpf_fine = 4, lpf_q = 0, lpf_narrow = 1),  # target 3564
    LPFSettings(cutoff_khz = 3960, lpf_coarse = 0, lpf_fine = 0, lpf_q = 0, lpf_narrow = 1),  # widest narrow-mode filter
    LPFSettings(cutoff_khz = 5733, lpf_coarse = 3, lpf_fine = 15, lpf_q = 0, lpf_narrow = 0), # target 5355; narrowest wide-mode filter
    LPFSettings(cutoff_khz = 5920, lpf_coarse = 3, lpf_fine = 13, lpf_q = 0, lpf_narrow = 0), # target 5950
    LPFSettings(cutoff_khz = 6555, lpf_coarse = 1, lpf_fine = 15, lpf_q = 0, lpf_narrow = 0), # target 6611
    LPFSettings(cutoff_khz = 7345, lpf_coarse = 1, lpf_fine = 9, lpf_q = 0, lpf_narrow = 0),  # target 7346
    LPFSettings(cutoff_khz = 8168, lpf_coarse = 0, lpf_fine = 12, lpf_q = 0, lpf_narrow = 0), # target 8162
    LPFSettings(cutoff_khz = 8975, lpf_coarse = 0, lpf_fine = 8, lpf_q = 0, lpf_narrow = 0),  # target 9069
    LPFSettings(cutoff_khz = 9955, lpf_coarse = 0, lpf_fine = 4, lpf_q = 0, lpf_narrow = 0),  # target 10076
    LPFSettings(cutoff_khz = 11196, lpf_coarse = 0, lpf_fine = 0, lpf_q = 0, lpf_narrow = 0), # widest wide-mode filter
)

def lpf_settings_for(target):
    # find the lowest setting with cutoff >= target
    n = bisect.bisect_left(lpf_calibration, target / 1000, key=operator.itemgetter(0))
    return lpf_calibration[min(n, len(lpf_calibration)-1)]

class HPFSettings(NamedTuple):
    cutoff_khz: int
    hpf_corner: int

hpf_calibration = (
    HPFSettings(cutoff_khz = 527, hpf_corner = 15),
    HPFSettings(cutoff_khz = 659, hpf_corner = 14),
    HPFSettings(cutoff_khz = 774, hpf_corner = 13),
    HPFSettings(cutoff_khz = 863, hpf_corner = 12),
    HPFSettings(cutoff_khz = 1096, hpf_corner = 11),
    HPFSettings(cutoff_khz = 1374, hpf_corner = 10),
    HPFSettings(cutoff_khz = 1522, hpf_corner = 9),
    HPFSettings(cutoff_khz = 1665, hpf_corner = 8),
    HPFSettings(cutoff_khz = 1914, hpf_corner = 7),
    HPFSettings(cutoff_khz = 2138, hpf_corner = 6),
    HPFSettings(cutoff_khz = 2342, hpf_corner = 5),
    HPFSettings(cutoff_khz = 2458, hpf_corner = 4),
    HPFSettings(cutoff_khz = 2733, hpf_corner = 3),
    HPFSettings(cutoff_khz = 3005, hpf_corner = 2),
    HPFSettings(cutoff_khz = 3563, hpf_corner = 1),
    HPFSettings(cutoff_khz = 3724, hpf_corner = 0),
)

def hpf_settings_for(target):
    # find the highest setting with cutoff <= target
    n = bisect.bisect_right(hpf_calibration, target / 1000, key=operator.itemgetter(0))
    return hpf_calibration[max(0, n-1)]

#
# register definitions
#
# "datasheet" notes indicate where the datasheet disagrees
# (missing fields, different names, etc) with what we have

class TunerR0(BitFlag):
    TUNER_ID = bitrange(7,0)            # datasheet: b10010110

class TunerR1(BitFlag):
    DET3_ADC = bitrange(5,0)            # datasheet: -

class TunerR2(BitFlag):
    PLL_LOCK = onebit(6)                # datasheet: VCO_INDICATORS[6:0]
    VCO_ADC = bitrange(5,0)             # from librtlsdr (ADC reading VCO control voltage?). datasheet: VCO_INDICATORS[6:0]

class TunerR3(BitFlag):
    MIX_ADC = bitrange(7,4)             # from librtlsdr. ADC feeding mixer autogain? datasheet: RF_INDICATORS[7:0]
    LNA_ADC = bitrange(3,0)             # from librtlsdr. ADC feeding LNA autogain? datasheet: RF_INDICATORS[7:0]

class TunerR4(BitFlag):
    vco_fine_tune = bitrange(5,4)       # datasheet: -
    filt_cal_code = bitrange(3,0)       # datasheet: -

class TunerR5(BitFlag):
    PWD_LT = onebit(7)
    reserved_6_0 = onebit(6)
    PWD_LNA1 = onebit(5)
    LNA_GAIN_MODE = onebit(4)
    LNA_GAIN = bitrange(3,0)

class TunerR6(BitFlag):
    PWD_PDET1 = onebit(7)
    PWD_PDET2 = onebit(6)
    FILT_3DB = onebit(5)
    reserved_4_1 = onebit(4)
    reserved_3_0 = onebit(3)
    PW_LNA = bitrange(2,0)

class TunerR7(BitFlag):
    img_r = onebit(7)                   # datasheet: 0
    PW_MIX = onebit(6)                  # datasheet: PWD_MIX
    PW0_MIX = onebit(5)
    MIXGAIN_MODE = onebit(4)
    MIX_GAIN = bitrange(3,0)

class TunerR8(BitFlag):
    PW_AMP = onebit(7)                  # datasheet: PWD_AMP
    PW0_AMP = onebit(6)
    imr_g_path = onebit(5)              # datasheet: part of IMR_G
    IMR_G = bitrange(4,0)

class TunerR9(BitFlag):
    PWD_IFFILT = onebit(7)
    PW1_IFFILT = onebit(6)
    imr_p_path = onebit(5)              # datasheet: part of IMR_P
    IMR_P = bitrange(4,0)

class TunerR10(BitFlag):
    PW_FILT = onebit(7)                 # datasheet: PWD_FILT
    filter_cur = bitrange(6,5)          # datasheet: PW_FILT   -- maybe call this one PW0_FILT?
    iffilt_q = onebit(4)                # datasheet: 1
    iffilt_fine_lpf = bitrange(3,0)     # datasheet: FILT_CODE "filter bandwidth manual fine tune"

class TunerR11(BitFlag):
    iffilt_narrow = onebit(7)           # datasheet: 0
    iffilt_coarse_lpf = bitrange(6,5)   # datasheet: FILT_BW "filter bandwidth manual coarse tunnel"
    calibration_trigger = onebit(4)     # datasheet: 0
    iffilt_hpf_corner = bitrange(3,0)   # datasheet: HPF "high pass filter corner control"

class TunerR12(BitFlag):
    pwd_adc = onebit(7)                 # datasheet: 1
    PW_VGA = onebit(6)                  # datasheet: PWD_VGA
    reserved_5_1 = onebit(5)
    VGA_GAIN_MODE = onebit(4)           # datasheed: VGA_MODE
    VGA_GAIN = bitrange(3,0)            # datasheet: VGA_CODE "IF vga manual gain control"

class TunerR13(BitFlag):
    LNA_VTH_H = bitrange(7,4)
    LNA_VTH_L = bitrange(3,0)

class TunerR14(BitFlag):
    MIX_VTH_H = bitrange(7,4)
    MIX_VTH_L = bitrange(3,0)

class TunerR15(BitFlag):
    flt_ext_widest = onebit(7)          # datasheet: 0
    reserved_6_0 = onebit(6)
    reserved_5_1 = onebit(5)
    clk_out_dis = onebit(4)             # datasheet: CLK_OUT_ENB
    ring_disable = onebit(3)            # datasheet: 1
    reserved_2_0 = onebit(2)
    clk_agc_dis = onebit(1)             # datasheet: CLK_AGC_ENB
    reserved_0_0 = onebit(0)

class TunerR16(BitFlag):
    SEL_DIV = bitrange(7,5)
    REF_DIV2 = onebit(4)
    xtal_drive = onebit(3)              # datasheet: 0
    det1_cap = onebit(2)                # datasheet: 1
    CAPX = bitrange(1,0)

class TunerR17(BitFlag):
    PW_LDO_A = bitrange(7,6)
    cp_current = bitrange(5,3)          # datasheet: b000
    reserved_2_0 = onebit(2)
    reserved_1_0 = onebit(1)            # datasheet: 1   **** check me ****
    reserved_0_0 = onebit(0)            # datasheet: 1   **** check me ****

class TunerR18(BitFlag):
    vco_current = bitrange(7,5)         # datasheet: b100
    sdm_dither_dis = onebit(4)          # datasheet: 0
    PWD_SDM = onebit(3)                 # datasheet: PW_SDM, only in summary chart, actually seems to be a power-down bit
    reserved_2_0 = onebit(2)
    reserved_1_0 = onebit(1)
    reserved_0_0 = onebit(0)

class TunerR19(BitFlag):
    reserved_7_0 = onebit(7)
    vco_mode = onebit(6)                # datasheet: 0.    mode 0 = auto, mode 1 = use vco_dac to control VCO
    vco_dac = bitrange(5,0)             # datasheet: b000000

class TunerR20(BitFlag):
    S_I2C = bitrange(7,6)
    N_I2C = bitrange(5,0)

class TunerR21(BitFlag):
    SDM_IN_LSB = bitrange(7,0)          # datasheet: SDM_IN

class TunerR22(BitFlag):
    SDM_IN_MSB = bitrange(7,0)          # datasheet: SDM_IN

class TunerR23(BitFlag):
    PW_LDO_D = bitrange(7,6)
    div_buf_cur = bitrange(5,4)         # datasheet: b11
    OPEN_D = onebit(3)
    reserved_2_1 = onebit(2)
    reserved_1_0 = onebit(1)
    reserved_0_0 = onebit(0)

class TunerR24(BitFlag):
    reserved_7_0 = onebit(7)
    reserved_6_1 = onebit(6)
    ring_se23 = onebit(5)               # datasheet: -
    pw_ring = onebit(4)                 # datasheet: -
    ring_n = bitrange(3,0)              # datasheet: ----

class TunerR25(BitFlag):
    PW_RFFILT = onebit(7)               # datasheet: PWD_RFFILT
    rffilt_current = bitrange(6,5)      # datasheet: b00
    SW_AGC = onebit(4)
    reserved_3_1 = onebit(3)
    reserved_2_1 = onebit(2)
    ring_seldiv = bitrange(1,0)         # datasheet: --

class TunerR26(BitFlag):
    RFMUX = bitrange(7,6)
    agc_clock = bitrange(5,4)           # datasheet: b10
    PLL_AUTO_CLK = bitrange(3,2)
    RFFILT = bitrange(1,0)

class TunerR27(BitFlag):
    TF_NCH = bitrange(7,4)
    TF_LP = bitrange(3,0)

class TunerR28(BitFlag):
    PDET3_GAIN = bitrange(7,4)
    reserved_3_0 = onebit(3)
    discharge_mode = onebit(2)          # datasheet: 1
    rf_source = onebit(1)               # datasheet: -
    reserved_0_0 = onebit(0)

class TunerR29(BitFlag):
    detect_bw = bitrange(7,6)           # datasheet: b11
    PDET1_GAIN = bitrange(5,3)
    PDET2_GAIN = bitrange(2,0)

class TunerR30(BitFlag):
    sw_pdet = onebit(7)                 # datasheet: 0
    FILTER_EXT = onebit(6)
    PDET_CLK = bitrange(5,0)

class TunerR31(BitFlag):
    lt_att = onebit(7)                  # datasheet: 1
    reserved_6_1 = onebit(6)
    reserved_5_0 = onebit(5)
    reserved_4_0 = onebit(4)
    reserved_3_0 = onebit(3)
    reserved_2_0 = onebit(2)
    ring_att = bitrange(1,0)            # datasheet: --


# all tuner reg flag definitions, indexed by tuner register number
TunerRegs = (
    TunerR0, TunerR1, TunerR2, TunerR3,
    TunerR4, TunerR5, TunerR6, TunerR7,
    TunerR8, TunerR9, TunerR10, TunerR11,
    TunerR12, TunerR13, TunerR14, TunerR15,
    TunerR16, TunerR17, TunerR18, TunerR19,
    TunerR20, TunerR21, TunerR22, TunerR23,
    TunerR24, TunerR25, TunerR26, TunerR27,
    TunerR28, TunerR29, TunerR30, TunerR31
)


# build a class that has names pointing to (regnum, regfield) tuples
# for use with write_field
class TunerFields:
    pass

for regnum, regclass in enumerate(TunerRegs):
    for member in regclass:
        if member.name.startswith('reserved'):
            name = f'r{regnum}_{member.name}'
        else:
            name = member.name
        assert not hasattr(TunerFields, name)
        setattr(TunerFields, name, (regnum, member))


def read_reg(dev:Device, reg:int) -> BitFlag:
    """Read one tuner register from the R860"""
    return TunerRegs[reg](dev.tuner_read(reg, 1)[0])

def write_field(cs:Changeset, reg_and_flag:tuple[int,BitFlag], insert_value):
    reg, flag = reg_and_flag
    cs.write_bits(reg, flag, flag(insert_value))

def wrap_change(change_fn):
    """Given a function with args (Changeset, ...)
that populates the given Changeset with some changes,
return a wrapped function with args (Device, ...)
that applies those same changes to the given Device"""
    def _wrapper(dev, *args, **kwargs):
        cs = Changeset()
        r = change_fn(cs, *args, **kwargs)
        dev.tuner_update(cs)
        return r
    return _wrapper


initial_values = {
    5: (TunerR5.PWD_LT(1)                |   # PWD_LT = 1, loopthrough off
        TunerR5.reserved_6_0(0)          |   # fixed bit
        TunerR5.PWD_LNA1(0)              |   # PWD_LNA1=0, LNA power on
        TunerR5.LNA_GAIN_MODE(1)         |   # LNA_GAIN_MODE=1, LNA gain mode = manual
        TunerR5.LNA_GAIN(0)),                # LNA_GAIN = 0
    6: (TunerR6.PWD_PDET1(0)             |   # PWD_PDET1 = 1, power off ***
        TunerR6.PWD_PDET2(0)             |   # PWD_PDET2 = 1, power off ***
        TunerR6.FILT_3DB(1)              |   # FILT_3DB = 1, filter +3dB gain on
        TunerR6.reserved_4_1(1)          |   # fixed bit
        TunerR6.reserved_3_0(0)          |   # fixed bit
        TunerR6.PW_LNA(2)),                  # PW_LNA = b010, LNA power control
    7: (TunerR7.img_r(0)                 |   # img_r = 0, image negative
        TunerR7.PW_MIX(1)                |   # PW_MIX = 1, mixer power on
        TunerR7.PW0_MIX(1)               |   # PW0_MIX = 1, mixer normal current
        TunerR7.MIXGAIN_MODE(0)          |   # MIXGAIN_MODE = 1, mixer gain mode = manual
        TunerR7.MIX_GAIN(0)),                # MIX_GAIN = 0, mixer manual gain = 0
    8: (TunerR8.PW_AMP(1)                |   # PW_AMP = 1, mixer buffer power on
        TunerR8.PW0_AMP(1)               |   # PW0_AMP = 1, mixer buffer high current
        TunerR8.imr_g_path(0)            |   # imr_g_path = 0, image rejection gain adjust sign = 0 (Q)
        TunerR8.IMR_G(0)),                   # IMR_G = 0, image rejection gain adjust = 0
    9: (TunerR9.PWD_IFFILT(0)            |   # PWD_IFFILT = 0, IF filter power on
        TunerR9.PW1_IFFILT(1)            |   # PW1_IFFILT = 1, IF filter high current
        TunerR9.imr_p_path(0)            |   # imr_p_path = 0, image rejection phase adjust sign = 0 (Q)
        TunerR9.IMR_P(0)),                   # IMR_P = 0, image rejection phase adjust = 0
    10: (TunerR10.PW_FILT(1)             |   # PW_FILT = 1, channel filter power on
         TunerR10.filter_cur(3)          |   # filter_cur = b11, channel filter current = highest
         TunerR10.iffilt_q(0)            |   # iffilt_q = 0, IF filter low Q
         TunerR10.iffilt_fine_lpf(0)),       # iffilt_fine_lpf = 15, upper bandpass cutoff for IF filter, fine control = widest setting (~11MHz cutoff)
    11: (TunerR11.iffilt_narrow(0)       |   # iffilt_narrow = 0, wide IF filter mode
         TunerR11.iffilt_coarse_lpf(0)   |   # iffilt_coarse_lpf = 00, upper bandpass cutoff for IF filter, coarse control = widest setting (~11MHz cutoff)
         TunerR11.calibration_trigger(0) |   # calibration_trigger = 0, no trigger
         TunerR11.iffilt_hpf_corner(15)),    # iffilt_hpf_corner = 15, lower bandpass cutoff for IF filter, corner = highest (~500kHz cutoff)
    12: (TunerR12.pwd_adc(1)             |   # pwd_adc = 1, ADC power off
         TunerR12.PW_VGA(1)              |   # PW_VGA = 1, VGA power on
         TunerR12.reserved_5_1(1)        |   # fixed bit
         TunerR12.VGA_GAIN_MODE(0)       |   # VGA_GAIN_MODE = 0, VGA gain mode = manual
         TunerR12.VGA_GAIN(0)),              # VGA_GAIN = 0, VGA manual gain = 0
    13: (TunerR13.LNA_VTH_H(5)           |   # LNA_VTH_H = 5, LNA AGC high threshold
         TunerR13.LNA_VTH_L(3)),             # LNA_VTH_L = 3, LNA AGC low threshold
    14: (TunerR14.MIX_VTH_H(7)           |   # MIX_VTH_H = 7, mixer AGC high threshold
         TunerR14.MIX_VTH_L(5)),             # MIX_VTH_L = 5, mixer AGC low threshold
    15: (TunerR15.flt_ext_widest(0)      |   # flt_ext_widest = 0
         TunerR15.reserved_6_0(0)        |   # fixed bit
         TunerR15.reserved_5_1(1)        |   # fixed bit
         TunerR15.clk_out_dis(1)         |   # clk_out_dis = 1, CLK_OUT disabled
         TunerR15.ring_disable(1)        |   # ring_disable = 1, ring disabled
         TunerR15.reserved_2_0(0)        |   # fixed bit
         TunerR15.clk_agc_dis(0)         |   # clk_agc_dis = 0, internal agc clock on
         TunerR15.reserved_0_0(0)),          # fixed bit
    16: (TunerR16.SEL_DIV(0)             |   # SEL_DIV = 0, mixer in = VCO/2
         TunerR16.REF_DIV2(1)            |   # REF_DIV2 = 1, PLL reference = XTAL/2
         TunerR16.xtal_drive(0)          |   # xtal_drive = 0, xtal drive high
         TunerR16.det1_cap(1)            |   # det1_cap = 1, ???
         TunerR16.CAPX(0)),                  # CAPX = b00, xtal internal cap = no cap
    17: (TunerR17.PW_LDO_A(0)            |   # PW_LDO_A = b00, PLL analog LDO off
         TunerR17.cp_current(0)          |   # cp_current = b000, ??
         TunerR17.reserved_2_0(0)        |   # fixed bit
         TunerR17.reserved_1_0(0)        |   # fixed bit
         TunerR17.reserved_0_0(0)),          # fixed bit
    18: (TunerR18.vco_current(4)         |   # vco_current = b100
         TunerR18.sdm_dither_dis(0)      |   # SDM dithering enabled
         TunerR18.PWD_SDM(1)             |   # PWD_SDM = 1, SDM power off
         TunerR18.reserved_2_0(0)        |   # fixed bit
         TunerR18.reserved_1_0(0)        |   # fixed bit
         TunerR18.reserved_0_0(0)),          # fixed bit
    19: (TunerR19.reserved_7_0(0)        |   # fixed bit
         TunerR19.vco_mode(0)            |   # VCO mode = auto
         TunerR19.vco_dac(0)),               # VCO DAC = (don't care) 0
    20: (TunerR20.S_I2C(0)               |   # S_I2C, PLL integer divisor
         TunerR20.N_I2C(0)),                 # N_I2C, PLL integer divisor
    21: (TunerR21.SDM_IN_LSB(0)),            # SDM_IN_LSB, PLL fractional divisor
    22: (TunerR22.SDM_IN_MSB(0)),            # SDM_IN_MSB, PLL fractional divisor
    23: (TunerR23.PW_LDO_D(0)            |   # PW_LDO_D = b00, PLL digital LDO off
         TunerR23.div_buf_cur(3)         |   # div_buf_cur = 3, 150uA
         TunerR23.OPEN_D(0)              |   # OPEN_D = 0, high-z
         TunerR23.reserved_2_1(1)        |   # fixed bit
         TunerR23.reserved_1_0(0)        |   # fixed bit
         TunerR23.reserved_0_0(0)),          # fixed bit
    24: (TunerR24.reserved_7_0(0)        |   # fixed bit
         TunerR24.reserved_6_1(1)        |   # fixed bit
         TunerR24.ring_se23(0)           |   # ring_se23 = 0, no extra ring divisor
         TunerR24.pw_ring(0)             |   # pw_ring = 0, ring power off
         TunerR24.ring_n(0)),                # ring_n = 0, ring PLL divisor
    25: (TunerR25.PW_RFFILT(1)           |   # PW_RFFILT = 1, RF filter power on
         TunerR25.rffilt_current(2)      |   # rffilt_current = b10
         TunerR25.SW_AGC(0)              |   # SW_AGC = 0, AGC = agc_in
         TunerR25.reserved_3_1(1)        |   # fixed bit
         TunerR25.reserved_2_1(1)        |   # fixed bit
         TunerR25.ring_seldiv(0)),           # ring_seldiv = 0, ring divisor = 4
    26: (TunerR26.RFMUX(1)               |   # RFMUX = b01, TF bypass
         TunerR26.agc_clock(2)           |   # agc_clock = b10, 60Hz
         TunerR26.PLL_AUTO_CLK(0)        |   # PLL_AUTO_CLK = b00, 128kHz
         TunerR26.RFFILT(0)),                # RFFILT = b00, RF filter band = highest
    27: (TunerR27.TF_NCH(0)              |   # TF_NCH
         TunerR27.TF_LP(0)),                 # TF_LP
    28: (TunerR28.PDET3_GAIN(5)          |   # PDET3_GAIN = 5
         TunerR28.reserved_3_0(0)        |   # fixed bit
         TunerR28.discharge_mode(1)      |   # discharge mode = low discharge
         TunerR28.rf_source(0)           |   # RF source = RF in
         TunerR28.reserved_0_0(0)),          # fixed bit
    29: (TunerR29.detect_bw(2)           |   # detect_bw = b10
         TunerR29.PDET1_GAIN(4)          |   # PDET1_GAIN = b100
         TunerR29.PDET2_GAIN(6)),            # PDET2_GAIN = 110
    30: (TunerR30.sw_pdet(0)             |   # sw_pdet = 0
         TunerR30.FILTER_EXT(0)          |   # FILTER_EXT = 0, filter extension disable
         TunerR30.PDET_CLK(0x0A)),           # PDET_CLK = b001010
    31: (TunerR31.lt_att(1)              |   # lt_att = 1
         TunerR31.reserved_6_1(1)        |   # fixed bit
         TunerR31.reserved_5_0(0)        |   # fixed bit
         TunerR31.reserved_4_0(0)        |   # fixed bit
         TunerR31.reserved_3_0(0)        |   # fixed bit
         TunerR31.reserved_2_0(0)        |   # fixed bit
         TunerR31.ring_att(0)),              # ring_att = 0
}

def set_initial_values_cs(cs:Changeset):
    for reg, value in initial_values.items():
        cs.write(reg, value)
set_initial_values = wrap_change(set_initial_values_cs)


def init_tuner(dev:Device):
    dev.set_rf_power(mode=lpcsdr.device.RFPowerMode.RESET)

    ident = read_reg(dev, 0)
    if ident.extract(TunerR0.TUNER_ID) != 0x96:
        raise IOError(f'tuner detect failed, reg0 expected 0x96 but got 0x{ident:02X}')

    set_initial_values(dev)


class PLLParameters(NamedTuple):
    refdiv: bool
    seldiv: int
    feedback_n: int
    feedback_sdm: int
    vco: float
    freq: float


def find_parameters(requested:float, xtal:float = 28.8e6):
    if xtal > 24e6:
        # Turn on the /2 divider on the PLL reference input
        refdiv = True
        pll_ref = xtal / 2
    else:
        refdiv = False
        pll_ref = xtal

    # Find a suitable seldiv output divisor that would
    # put the VCO into its supported operating range
    VCO_MIN = 1750e6
    VCO_MAX = 3700e6
    seldiv = 2
    while requested * seldiv < VCO_MIN and seldiv < 64:
        seldiv *= 2

    required_vco = requested * seldiv
    if required_vco < VCO_MIN or required_vco > VCO_MAX:
        raise ValueError(f'required VCO {required_vco/1e6:.3f}MHz out of range')

    # Work out the feedback divider value needed to
    # generate required_vco from pll_ref
    pll_feedback = (required_vco / 2) / pll_ref
    if pll_feedback < 13 or pll_feedback >= 269:
        raise ValueError(f'pll feedback dividor {pll_feedback:.03f} out of range')

    f, i = math.modf(pll_feedback)     # fractional and integer parts of pll_feedback
    pll_feedback_int = int(i)          # integer part only
    sdm_numerator = round(f * 2**18)   # fractional part only, as 18-bit fixed-point

    # dithering effectively makes the low bits average 01
    # so force that so we report a more accurate output_freq
    sdm_numerator = (sdm_numerator & ~3) | 1

    # Avoid fractions that are close to 0.0 / 0.5 / 1.0
    # as they tend to generate more spurs
    if sdm_numerator < 32:
        # close to 0, switch to integer mode
        sdm_numerator = 0
    elif sdm_numerator > (2**18 - 32):
        # close to 1, switch to integer mode
        sdm_numerator = 0
        pll_feedback_int += 1
    elif sdm_numerator > (2**17 - 32) and sdm_numerator <= 2**17:
        # close to 0.5 and <=0.5, decrease it a little
        sdm_numerator = 2**17 - 32
    elif sdm_numerator > 2**17 and sdm_numerator < (2**17 + 32):
        # close to 0.5 and >0.5, increase it a little
        sdm_numerator = 2**17 + 32

    actual_vco = pll_ref * 2 * (pll_feedback_int + sdm_numerator / 2**18)
    actual_out = actual_vco / seldiv

    return PLLParameters(refdiv=refdiv,
                         seldiv=seldiv,
                         feedback_n=pll_feedback_int,
                         feedback_sdm=sdm_numerator >> 2,  # can only set the top 16 bits
                         vco=actual_vco,
                         freq=actual_out)


def has_pll_lock(dev:Device):
    return (TunerR2.PLL_LOCK in read_reg(dev, 2))


def configure_pll_cs(cs:Changeset, params:PLLParameters):
    sdm_disable = 1 if (params.feedback_sdm == 0) else 0
    sdm_lsb = params.feedback_sdm & 0xff
    sdm_msb = (params.feedback_sdm >> 8) & 0xff

    # integer N = 4*ni2c + si2c + 13
    ni2c = (params.feedback_n - 13) // 4
    si2c = (params.feedback_n - 13) & 3

    seldiv_lut = {
        2: 0,
        4: 1,
        8: 2,
        16: 3,
        32: 4,
        64: 5
    }
    seldiv = seldiv_lut[params.seldiv]
    refdiv = 1 if params.refdiv else 0

    # rough estimate for initial value of vco_dac (based on a quick line fit to data from --vco-scan)
    # this will get the VCO into about the right place, then the firmware sets
    # the mode back to auto and it should lock fast
    vco_dac = round(params.vco * 0.0318e-6 - 49.0)
    vco_dac = max(0, vco_dac)
    vco_dac = min(63, vco_dac)
    
    write_field(cs, TunerFields.PW_LDO_A, 1)            # PW_LDO_A = 01, analog LDO on
    write_field(cs, TunerFields.PW_LDO_D, 2)            # PW_LDO_D = 10, digital LDO on
    write_field(cs, TunerFields.PWD_SDM, sdm_disable)   # set PWD_SDM=0/1 for fractional/integer mode
    write_field(cs, TunerFields.SEL_DIV, seldiv)        # PLL post-divider
    write_field(cs, TunerFields.REF_DIV2, refdiv)       # reference clock pre-divider
    write_field(cs, TunerFields.S_I2C, si2c)            # integer part of divisor, S_I2C
    write_field(cs, TunerFields.N_I2C, ni2c)            # integer part of divisor, N_I2C
    write_field(cs, TunerFields.SDM_IN_LSB, sdm_lsb)    # fractional part of divisor, LSB
    write_field(cs, TunerFields.SDM_IN_MSB, sdm_msb)    # fractional part of divisor, MSB
    write_field(cs, TunerFields.PLL_AUTO_CLK, 0)        # set PLL_AUTO_CLK=00, 128kHz
    write_field(cs, TunerFields.vco_current, 4)         # set vco_current = 4
    write_field(cs, TunerFields.vco_mode, 1)            # set vco_mode = manual
    write_field(cs, TunerFields.vco_dac, vco_dac)       # set initial vco frequency
configure_pll = wrap_change(configure_pll_cs)


class TunerLockError(RuntimeError):
    pass

def start_pll(dev:Device, params:PLLParameters):
    # initialize PLL    
    configure_pll(dev, params)
    
    # wait for PLL lock
    for vco in (4, 3, 2, 1, 0):
        if dev.tuner_lock(vco, 50):
            break
    else:
        raise TunerLockError(f'tuner PLL did not lock for frequency {params.freq}')

    # clean up PLL_AUTO_CLK
    cs = Changeset()
    write_field(cs, TunerFields.PLL_AUTO_CLK, 2)  # set PLL_AUTO_CLK=10, 8kHz
    dev.tuner_update(cs)

def powerdown_cs(cs:Changeset):
    write_field(cs, TunerFields.PWD_LNA1, 1)
    write_field(cs, TunerFields.PWD_PDET1, 1)
    write_field(cs, TunerFields.PW_MIX, 0)
    write_field(cs, TunerFields.PW_AMP, 0)
    write_field(cs, TunerFields.PWD_IFFILT, 1)
    write_field(cs, TunerFields.PW_FILT, 0)
    write_field(cs, TunerFields.pwd_adc, 1)
    write_field(cs, TunerFields.PW_VGA, 0)
    write_field(cs, TunerFields.clk_out_dis, 1)
    write_field(cs, TunerFields.clk_agc_dis, 1)
    write_field(cs, TunerFields.PW_LDO_A, 0)
    write_field(cs, TunerFields.PWD_SDM, 1)
    write_field(cs, TunerFields.PW_LDO_D, 0)
    write_field(cs, TunerFields.pw_ring, 0)
    write_field(cs, TunerFields.PW_RFFILT, 0)
powerdown = wrap_change(powerdown_cs)


def set_lna_gain_cs(cs:Changeset, gain:int):
    write_field(cs, TunerFields.LNA_GAIN, gain)
set_lna_gain = wrap_change(set_lna_gain_cs)


def set_mix_gain_cs(cs:Changeset, gain:int):
    write_field(cs, TunerFields.MIX_GAIN, gain)
set_mix_gain = wrap_change(set_mix_gain_cs)


def set_vga_gain_cs(cs:Changeset, gain:int):
    write_field(cs, TunerFields.VGA_GAIN, gain)
set_vga_gain = wrap_change(set_vga_gain_cs)


def set_if_lpf_cs(cs:Changeset, cutoff:float):
    lpf = lpf_settings_for(cutoff)
    write_field(cs, TunerFields.iffilt_q, lpf.lpf_q)
    write_field(cs, TunerFields.iffilt_fine_lpf, lpf.lpf_fine)
    write_field(cs, TunerFields.iffilt_narrow, lpf.lpf_narrow)
    write_field(cs, TunerFields.iffilt_coarse_lpf, lpf.lpf_coarse)
    return lpf
set_if_lpf = wrap_change(set_if_lpf_cs)


def set_if_hpf_cs(cs:Changeset, cutoff:float):
    hpf = hpf_settings_for(cutoff)
    write_field(cs, TunerFields.iffilt_hpf_corner, hpf.hpf_corner)
    return hpf
set_if_hpf = wrap_change(set_if_hpf_cs)


def set_if_bandpass_cs(cs:Changeset, lo:float, hi:float):
    hpf = set_if_hpf_cs(cs, min(lo,hi))
    lpf = set_if_lpf_cs(cs, max(lo,hi))
    return (hpf.cutoff_khz*1e3, lpf.cutoff_khz*1e3)
set_if_bandpass = wrap_change(set_if_bandpass_cs)


def print_regs(regs: bytes, file=sys.stdout):
    print('Tuner:', file=file)
    for i, value in enumerate(regs):
        reg = TunerRegs[i](value)
        print(f'  R{i:<2d} (0x{value:02X})  ', end='', file=file)
        for part in flag_string_parts(reg,True):
            if not part.startswith('reserved'):
                print(f'{part.ljust(20)} ', end='', file=file)
        print(file=file)
    print(file=file)


def vco_scan(dev:Device):
    xtal = 28.8e6
    refdiv = 1
    pll_ref = xtal / 2
    seldiv = 2

    scan_start = 1650e6
    scan_end = 3800e6

    feedback_lo = max(13, math.floor(scan_start / pll_ref / 2))
    feedback_hi = min(268, math.ceil(scan_end / pll_ref / 2))

    for pll_feedback in range(feedback_lo, feedback_hi+1):
        actual_vco = pll_ref * 2 * pll_feedback
        actual_out = actual_vco / seldiv
        params = PLLParameters(refdiv=refdiv,
                               seldiv=seldiv,
                               feedback_n=pll_feedback,
                               feedback_sdm=0,                               
                               vco=actual_vco,
                               freq=actual_out)
        try:
            start_pll(dev, params)
            vco_adc = read_reg(dev, 2).extract(TunerR2.VCO_ADC)
            print(f'{actual_vco:.0f}\t{vco_adc}')
        except TunerLockError as e:
            pass
        
    

def main():
    import argparse

    parser = argparse.ArgumentParser(description='Control R860T tuner')

    parser.add_argument('--status', help="Print tuner regs", action='store_true')
    parser.add_argument('--reset', help="Reset and re-init tuner", action='store_true')
    parser.add_argument('--pll', help="Tune PLL to given frequency (specify as MHz)", type=float)
    parser.add_argument('--powerdown', help="Power down tuner (soft powerdown, don't turn off RF power)", action='store_true')
    parser.add_argument('--lna-gain', help="Set LNA gain (0..15)", type=int)
    parser.add_argument('--vga-gain', help="Set VGA gain (0..15)", type=int)
    parser.add_argument('--mix-gain', help="Set mixer gain (0..15)", type=int)
    parser.add_argument('--lpf', help="Set IF LPF cutoff frequency (high limit of bandpass filter, specify as kHz)", type=float)
    parser.add_argument('--hpf', help="Set IF HPF cutoff frequency (low limit of bandpass filter, specify as kHz)", type=float)
    parser.add_argument('--vco-scan', help="generate VCO ADC data", action='store_true')

    if len(sys.argv) < 2:
        parser.print_usage()
        return 2
    
    args = parser.parse_args()

    dev = lpcsdr.device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    if args.reset:
        print('resetting tuner (RF power cycle)')
        dev.set_rf_power(mode=lpcsdr.device.RFPowerMode.RESET)
        needs_init = True
    else:
        dev.set_rf_power(mode=lpcsdr.device.RFPowerMode.ON)
        regs = dev.tuner_read(5, 27, lpcsdr.device.TunerCacheMode.REFRESH_CACHE)
        needs_init = (regs == b'\x00' * 27)

    if needs_init:
        # tuner is not initialized
        print('initializing tuner')
        init_tuner(dev)

    if args.pll:
        print(f'tuning PLL to {args.pll:.3f} MHz')
        params = find_parameters(requested=args.pll * 1e6)
        print(f'programming PLL with settings: {params}')
        start_pll(dev, params)

    if args.lna_gain:
        print(f'setting LNA gain to {args.lna_gain}')
        set_lna_gain(dev, args.lna_gain)

    if args.mix_gain:
        print(f'setting mixer gain to {args.mix_gain}')
        set_mix_gain(dev, args.mix_gain)

    if args.vga_gain:
        print(f'setting VGA gain to {args.vga_gain}')
        set_vga_gain(dev, args.vga_gain)

    if args.lpf:
        print(f'setting IF bandpass filter high cutoff to {args.lpf} kHz')
        set_if_lpf(dev, args.lpf * 1000)
        
    if args.hpf:
        print(f'setting IF bandpass filter low cutoff to {args.hpf} kHz')
        set_if_hpf(dev, args.hpf * 1000)

    if args.vco_scan:
        vco_scan(dev)

    if args.powerdown:
        print('powering down tuner LDOs')
        powerdown(dev)

    if args.status:
        regs = dev.tuner_read(0, 32, lpcsdr.device.TunerCacheMode.REFRESH_CACHE)
        print_regs(regs, file=sys.stdout)

    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main())
