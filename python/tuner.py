#!/usr/bin/env python3

import sys
import math
import time
import argparse
from typing import NamedTuple

import lpcsdr_device

class Changeset:
    """A set of pending tuner register changes"""

    def __init__(self):
        self.pending = {}

    def write(self, reg, value):
        """Update the entire value of a single register"""
        self.write_bits(reg, 0xFF, value)

    def write_bits(self, reg, mask, value):
        """Update specific bits of a single register"""
        assert (value & mask) == value
        old_mask, old_value = self.pending.get(reg, (0,0))
        self.pending[reg] = (old_mask | mask, (old_value & ~mask) | value)

    def apply(self, dev):
        """Apply the changes contained in this Changeset to the R860"""
        if not self.pending:
            return

        first = sorted(self.pending.keys())[0]
        last = sorted(self.pending.keys())[-1]

        new_bits = bytearray()
        mask_bits = bytearray()
        for reg in range(first, last+1):
            mask, value = self.pending.get(reg, (0,0))
            print(f'> update reg {reg} mask {mask:02X} value {value:02X}')
            new_bits.append(value)
            mask_bits.append(mask)

        dev.tuner_update(first, bytes(new_bits), bytes(mask_bits))
        self.pending.clear()


def read_reg(dev, reg):
    """Read one tuner register from the R860"""
    return dev.tuner_read(reg, 1)[0]


def init_tuner(dev):
    dev.set_rf_power(mode=lpcsdr_device.RFPowerMode.RESET)

    ident = read_reg(dev, 0)
    if ident != 0x96:
        raise IOError(f'tuner detect failed, reg0 expected 0x96 but got 0x{ident:02X}')

    cs = Changeset()
    cs.write(5,
             (1 << 7) |   # PWD_LT = 1, loopthrough off
             (0 << 6) |   # fixed bit
             (0 << 5) |   # PWD_LNA = 0, LNA power on
             (1 << 4) |   # LNA_GAIN_MODE = 1, LNA gain mode = manual
             (0 << 0))    # LNA_GAIN = 0, LNA manual gain = 0
    cs.write(6,
             (1 << 7) |   # PWD_PDET1 = 1, power off
             (1 << 6) |   # PWD_PDET2 = 1, power off  **
             (1 << 5) |   # filter +3dB gain on
             (1 << 4) |   # fixed bit
             (0 << 3) |   # fixed bit
             (2 << 0))    # PW_LNA = 010, LNA power control
    cs.write(7,
             (0 << 7) |   # img_r = 0, image negative
             (1 << 6) |   # PW_MIX = 1, mixer power on
             (1 << 5) |   # PW0_MIX = 1, mixer normal current
             (0 << 4) |   # MIXGAIN_MODE = 1, mixer gain mode = manual
             (0 << 0))    # MIX_GAIN = 0, mixer manaul gaain = 0
    cs.write(8,
             (1 << 7) |   # PW_AMP = 1, mixer buffer power on
             (1 << 6) |   # PW0_AMP = 1, mixer buffer high current
             (0 << 5) |   # imr_g_path = 0, image rejection gain adjust sign = 0 (Q)
             (0 << 0))    # IMR_G = 0, image rejection gain adjust = 0
    cs.write(9,
             (0 << 7) |   # PWD_IFFILT = 0, IF filter power on
             (1 << 6) |   # PW1_IFFILT = 1, IF filter high current
             (0 << 5) |   # imr_p_path = 0, image rejection phase adjust sign = 0 (Q)
             (0 << 0))    # IMR_P = 0, image rejection phase adjust = 0
    cs.write(10,
             (1 << 7) |   # PW_FILT = 1, channel filter power on
             (3 << 5) |   # filter_cur = 11, channel filter current = highest
             (0 << 4) |   # iffilt_q = 0, IF filter low Q
             (0 << 0))    # iffilt_fine_lpf = 15, upper bandpass cutoff for IF filter, fine control = widest setting (~11MHz cutoff)
    cs.write(11,
             (0 << 7) |   # iffilt_narrow = 0, wide IF filter mode
             (0 << 5) |   # iffilt_coarse_lpf = 00, upper bandpass cutoff for IF filter, coarse control = widest setting (~11MHz cutoff)
             (0 << 4) |   # calibration_trigger = 0, no trigger
             (15 << 0))   # iffilt_hpf_corner = 15, lower bandpass cutoff for IF filter, corner = highest (~500kHz cutoff)
    cs.write(12,
             (1 << 7) |   # pwd_adc = 1, ADC power off
             (1 << 6) |   # PW_VGA = 1, VGA power on
             (1 << 5) |   # fixed bit
             (0 << 4) |   # VGA_GAIN_MODE = 0, VGA gain mode = manual
             (0 << 0))    # VGA_GAIN = 0, VGA manual gain = 0
    cs.write(13,
             (5 << 4) |   # LNA_VTH_H = 5, LNA AGC high threshold
             (3 << 0))    # LNA_VTH_L = 3, LNA AGC low threshold
    cs.write(14,
             (7 << 4) |   # MIX_VTH_H = 7, mixer AGC high threshold
             (5 << 0))    # MIX_VTH_L = 5, mixer AGC low threshold
    cs.write(15,
             (0 << 7) |   # flt_ext_widest = 0
             (0 << 6) |   # fixed bit
             (1 << 5) |   # fixed bit
             (1 << 4) |   # clk_out_disable = 1, CLK_OUT disabled
             (1 << 3) |   # ring_disable = 1, ring disabled
             (0 << 2) |   # fixed bit
             (0 << 1) |   # clk_pwd = 0, clock power on (internal agc clock?)
             (0 << 0))    # fixed bit
    cs.write(16,
             (0 << 5) |   # SEL_DIV = 0, mixer in = VCO/2
             (1 << 4) |   # REFDIV = 1, PLL reference = XTAL/2
             (0 << 3) |   # xtal_drive = 0, xtal drive high
             (1 << 2) |   # det1_cap = 1, ???
             (0 << 0))    # CAPX = 00, xtal internal cap = no cap
    cs.write(17,
             (0 << 6) |   # PW_LDO_A = 00, PLL analog LDO off
             (0 << 3) |   # cp_current = 000, ??
             (0 << 2) |   # fixed bit
             (0 << 1) |   # fixed bit
             (0 << 0))    # fixed bit
    cs.write(18,
             (4 << 5) |   # vco_current = 100
             (0 << 4) |   # fixed bit
             (1 << 3) |   # PWD_SDM = 1, SDM power off
             (0 << 2) |   # fixed bit
             (0 << 1) |   # fixed bit
             (0 << 0))    # fixed bit
    cs.write(19,
             (0 << 7) |   # fixed bit
             (0 << 6) |   # fixed bit
             (0 << 0))    # ver_num
    cs.write(20,
             (0 << 4) |   # SI2C, PLL integer divisor
             (0 << 0))    # NI2C, PLL integer divisor
    cs.write(21,
             (0 << 0))    # SDM_IN_LSB, PLL fractional divisor
    cs.write(22,
             (0 << 0))    # SDM_IN_MSB, PLL fractional divisor
    cs.write(23,
             (0 << 6) |   # PW_LDO_D = 00, PLL digital LDO off
             (3 << 4) |   # div_buf_cur = 3, 150uA
             (0 << 3) |   # OPEN_D = 0, high-z
             (1 << 2) |   # fixed bit
             (0 << 1) |   # fixed bit
             (0 << 0))    # fixed bit
    cs.write(24,
             (0 << 7) |   # fixed bit
             (1 << 6) |   # fixed bit
             (0 << 5) |   # ring_se23 = 0, no extra ring divisor
             (0 << 4) |   # pw_ring = 0, ring power off
             (0 << 0))    # ring_n = 0, ring PLL divisor

    cs.write(25,
             (1 << 7) |   # PW_RFFILT = 1, RF filter power on
             (2 << 5) |   # rffilt_current = 10
             (0 << 4) |   # SW_AGC = 0, AGC = agc_in
             (1 << 3) |   # fixed bit
             (1 << 2) |   # fixed bit
             (0 << 0))    # ring_seldiv = 0, ring divisor = 4
    cs.write(26,
             (1 << 6) |   # RFMUX = 01, TF bypass
             (2 << 4) |   # agc_clock = 10, 60Hz
             (0 << 2) |   # PLL autotune = 00, 128kHz
             (0 << 0))    # RFFILT = 00, RF filter band = highest
    cs.write(27,
             (0 << 4) |   # TF_NCH
             (0 << 0))    # TF_LP
    cs.write(28,
             (5 << 4) |   # PDET3_GAIN = 5
             (0 << 3) |   # fixed bit
             (1 << 2) |   # discharge mode = low discharge
             (0 << 1) |   # RF source = RF in
             (0 << 0))
    cs.write(29,
             (2 << 6) |   # detect_bw = 10
             (4 << 3) |   # pdet1_gain = 100
             (6 << 0))    # pdet2_gain = 110
    cs.write(30,
             (0 << 7) |   # sw_pdet = 0
             (0 << 6) |   # FILTER_EXT = 0, filter extension disable
             (0x0A << 0)) # PDET_CLK = 01010
    cs.write(31,
             (1 << 7) |   # lt_att = 1
             (1 << 6) |   # fixed bit
             (0 << 5) |   # fixed bit
             (0 << 4) |   # fixed bit
             (0 << 3) |   # fixed bit
             (0 << 2) |   # fixed bit
             (0 << 0))    # ring_att = 0

    cs.apply(dev)


class PLLParameters(NamedTuple):
    refdiv: bool
    seldiv: int
    feedback_n: int
    feedback_sdm: int
    freq: float


def find_parameters(xtal, requested):
    if xtal > 24e6:
        # Turn on the /2 divider on the PLL reference input
        refdiv = True
        pll_ref = xtal / 2
    else:
        refdiv = False
        pll_ref = xtal

    # Find a suitable seldiv output divisor that would
    # put the VCO into its supported operating range
    VCO_MIN = 1770e6
    VCO_MAX = VCO_MIN*2
    seldiv = 2
    while requested * seldiv < VCO_MIN and seldiv < 64:
        seldiv *= 2

    required_vco = requested * seldiv
    if required_vco < VCO_MIN or required_vco > VCO_MAX:
        raise RuntimeError(f'required VCO {required_vco/1e6:.3f}MHz out of range')

    # Work out the feedback divider value needed to
    # generate required_vco from pll_ref
    pll_feedback = (required_vco / 2) / pll_ref
    if pll_feedback < 13 or pll_feedback >= 269:
        raise RuntimeError(f'pll feedback dividor {pll_feedback:.03f} out of range')

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
                         freq=actual_out)


def has_pll_lock(dev):
    return (read_reg(dev, 2) & 0x40) != 0


def start_pll(dev, params):
    sdm_disable = 1 if (params.feedback_sdm == 0) else 0
    sdm_lsb = params.feedback_sdm & 0xff
    sdm_msb = (params.feedback_sdm >> 8) & 0xff
    n_msb = (params.feedback_n - 13) // 4
    n_lsb = (params.feedback_n - 13) & 3
    n_reg = (n_lsb << 6) | n_msb

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

    cs = Changeset()
    cs.write_bits(17, 0xC0, 0x40)   # PW_LDO_A = 01, analog LDO on
    cs.write_bits(23, 0xC0, 0x80)   # PW_LDO_D = 10, digital LDO on
    cs.write_bits(18, 0x08, (sdm_disable << 3)) # set pwd_sdm
    cs.write_bits(16, 0xE0, (seldiv << 5))      # set SELDIV
    cs.write_bits(16, 0x10, (refdiv << 4))      # set REFDIV
    cs.write(20, n_reg)                         # set NI2C, SI2C
    cs.write(21, sdm_lsb)                       # set SDB_IN_LSB
    cs.write(22, sdm_msb)                       # set SDB_IN_MSB
    cs.write_bits(8, 0x3F, 0)                   # set imr_g_path, imr_g
    cs.write_bits(9, 0x3F, 0)                   # set imr_p_path, imr_p
    cs.write_bits(26, 0x0C, 0)                  # set PLL_AUTO_CLK=00, 128kHz
    cs.write_bits(18, 0xE0, 4 << 5)             # set vco_current = 4

    # initialize PLL
    cs.apply(dev)
    time.sleep(0.1)

    # wait for PLL lock
    for vco in [4, 3, 2, 1, 0]:
        if has_pll_lock(dev):
            print('> PLL lock')
            break

        print(f'waiting for PLL lock with vco_current={vco}')
        cs.write_bits(18, 0xE0, vco << 5)       # set vco_current = vco
        cs.apply(dev)
        time.sleep(0.1)

        for retry in range(10):
            if has_pll_lock(dev):
                print('> PLL lock')
                break
            time.sleep(0.1)

    cs.write_bits(26, 0x0C, 0x08)               # set PLL_AUTO_CLK=10, 8kHz
    cs.apply(dev)

    if not has_pll_lock(dev):
        raise RuntimeError('no pll lock')

    vco_current = (read_reg(dev,18) & 0xE0) >> 5
    print(f'got PLL lock with vco_current={vco_current}')


def powerdown(dev):
    cs = Changeset()
    cs.write_bits(5, 1<<7, 1<<7)  # PWD_LT = 1
    cs.write_bits(5, 1<<5, 1<<5)  # PWD_LNA = 1
    cs.write_bits(6, 1<<7, 1<<7)  # PWD_PDET1 = 1
    cs.write_bits(7, 1<<6, 0)     # PW_MIX = 0
    cs.write_bits(8, 1<<7, 0)     # PW_AMP = 0
    cs.write_bits(9, 1<<7, 1<<7)  # PWD_IFFILT = 1
    cs.write_bits(10, 1<<7, 0)    # PW_FILT = 0
    cs.write_bits(12, 1<<7, 1<<7) # pwd_adc = 1
    cs.write_bits(12, 1<<6, 0)    # PW_VGA = 0
    cs.write_bits(15, 1<<0, 1<<0) # pwd_clk = 1
    cs.write_bits(17, 0xC0, 0)    # PW_LDO_A = 00, analog LDO off
    cs.write_bits(18, 1<<3, 1<<3) # PWD_SDM = 1
    cs.write_bits(23, 0xC0, 0)    # PW_LDO_D = 00, digital LDO off
    cs.write_bits(24, 1<<4, 0)    # pw_ring = 0
    cs.write_bits(25, 1<<7, 0)    # PW_RFFILT = 0
    cs.apply(dev)


def set_lna_gain(dev, gain):
    cs = Changeset()
    cs.write_bits(5, 0x0F, gain)
    cs.apply(dev)


def set_mix_gain(dev, gain):
    cs = Changeset()
    cs.write_bits(7, 0x0F, gain)
    cs.apply(dev)


def set_vga_gain(dev, gain):
    cs = Changeset()
    cs.write_bits(12, 0x0F, gain)
    cs.apply(dev)


def main():
    parser = argparse.ArgumentParser(description='Control R860T tuner')

    parser.add_argument('--reset', help="Reset and re-init tuner", action='store_true')
    parser.add_argument('--pll', help="Tune PLL to given frequency (specify as MHz)", type=float)
    parser.add_argument('--powerdown', help="Power down tuner (soft powerdown, don't turn off RF power)", action='store_true')
    parser.add_argument('--lna-gain', help="Set LNA gain (0..15)", type=int)
    parser.add_argument('--vga-gain', help="Set VGA gain (0..15)", type=int)
    parser.add_argument('--mix-gain', help="Set mixer gain (0..15)", type=int)
    args = parser.parse_args()

    dev = lpcsdr_device.find()
    if dev is None:
        print('no lpcsdr device found')
        return 1

    if args.reset:
        print('resetting tuner (RF power cycle)')
        dev.set_rf_power(mode=lpcsdr_device.RFPowerMode.RESET)
    else:
        dev.set_rf_power(mode=lpcsdr_device.RFPowerMode.ON)

    regs = dev.tuner_read(5, 27)
    if regs == (b'\x00' * 27):
        # tuner is not initialized
        print('initializing tuner')
        init_tuner(dev)

    if args.pll:
        print(f'tuning PLL to {args.pll:.3f} MHz')
        params = find_parameters(xtal=28.8e6, requested=args.pll * 1e6)
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

    if args.powerdown:
        print('powering down tuner LDOs')
        powerdown(dev)

    return 0


if __name__ == '__main__':
    sys.exit(main())
