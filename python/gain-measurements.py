#!/usr/bin/env python3

import sys
import argparse
import struct
import math
import time
import serial

import numpy as np
import scipy.fft
import scipy.signal

import lpcsdr.device
import lpcsdr.adc
import lpcsdr.bulk
import lpcsdr.tuner
from lpcsdr.util import *

import erasynth

def get_power_spectrum(lpcsdr_dev, args, quick=False):
    # For some details on the scaling etc that goes on here, see
    #  https://kluedo.ub.rptu.de/frontdoor/deliver/index/docId/4293/file/exact_fft_measurements.pdf
    #  "Exact Signal Measurements using FFT Analysis"
    #    Stefan Scholl
    #    Microelectronic Systems Design Research Group
    #    TU Kaiserslautern, Germany
    
    spb = lpcsdr_dev.last_board_status.usb_samples_per_block    

    samples = 2 * args.bins                  # Samples per FFT
    binsize = args.sample_rate / samples     # Bin size of each FFT

    ffts_per_block = spb // samples
    if quick:
        scale = min(128, args.scale)
    else:
        scale = args.scale
    total_samples = spb * math.ceil(scale / ffts_per_block)

    window = scipy.signal.windows.blackman(samples)                   # Window we will apply before FFT
    cpg_gain = 1.0 / (np.sum(window) / len(window))                   # Coherent power gain correction (Blackman window = 7.54dB)
    enbw_gain = len(window) * np.sum(window**2) / np.sum(window)**2   # Equivalent noise bandwidth correction for a Blackman window = 2.38dB

    freqs = scipy.fft.rfftfreq(samples) * args.sample_rate  # Center frequencies of each FFT bin

    # fetch all the blocks we need; we don't care about discontinuities, because we
    # just do however many FFTs fit in each block independently. Discard the first 8
    # blocks so we're sure we have fresh data.
    discard_blocks = 8
    raw_blocks = lpcsdr.bulk.read_blocks(lpcsdr_dev, total_samples + discard_blocks * spb)

    # count ADC samples with an (absolute) value larger than this
    threshold = 0.5
    threshold_count = 0
    
    # Unpack the raw data into a single big numpy float32 array
    read_samples = 0
    adc_min = adc_max = 0
    work = np.empty(total_samples + spb, np.float32)
    for index, adc_block in enumerate(lpcsdr.bulk.unpack_blocks(raw_blocks)):
        if index < discard_blocks:
            continue
        assert len(adc_block.samples) == spb
        adc_min = min(adc_min, min(adc_block.samples))    # record ADC min/max so we can detect overflow
        adc_max = max(adc_max, max(adc_block.samples))
        scaled = adc_block.samples / 2048.0
        for sample in scaled:
            if abs(sample) > threshold:
                threshold_count += 1
        work[read_samples:read_samples+spb] = scaled
        read_samples += spb

    assert read_samples >= total_samples
    threshold_percent = 100 * threshold_count / read_samples
    
    # Compute N individual non-overlapping FFTs, each
    # from a block of "samples"-length input samples.
    # Because of how we set up "total_samples", there
    # should be exactly args.scale of these FFts.
    #
    # Then do incoherent averaging of the individual
    # FFTs to produce our final result.

    summed = np.zeros(len(freqs), dtype=np.float32)
    fft_count = 0
    for k in range(0, total_samples, spb):
        for j in range(0, spb, samples):
            if j+k+samples > read_samples:
                break
            block = work[j+k:j+k+samples]
            this_fft = scipy.fft.rfft(block * window, norm="forward")   # note: norm="forward" scales by 1/N
            this_fft[1:] *= 2                                           # compensate for discarding the mirrored bins
            summed += np.abs(this_fft)
            fft_count += 1

    # average, convert to power spectrum, handle CPG
    summed = (summed / fft_count)**2 * cpg_gain    

    # build a list of (bin center freq, power) tuples
    power_spectrum = list(zip(freqs, summed))

    return (power_spectrum, adc_min, adc_max, threshold_percent, enbw_gain)


def measure(lpcsdr_dev, args, quick=False):
    power_spectrum, adc_min, adc_max, threshold_percent, enbw_gain = get_power_spectrum(lpcsdr_dev, args, quick)
    binsize = power_spectrum[1][0] - power_spectrum[0][0]

    # measure the bin closest to rate/4, which is where we're going to put our test signal
    for freq, power in power_spectrum:
        if abs(freq - args.sample_rate / 4) <= binsize/2:
            signal_power = power
            break
    else:
        raise AssertionError("no test signal frequency found??")

    # find a noise floor estimate: sum all the bins we consider to be
    # noise, excluding:
    #
    #   frequencies close to 0Hz or rate/2, as we know these are
    #   somewhat unreliable (e.g. DC spur)
    # 
    #   frequencies close to our test signal (because of FFT leakage)

    noise_power = 0
    for freq, power in power_spectrum:
        if abs(freq) <= 500e3:                               # near 0
            continue
        if abs(freq - args.sample_rate / 2) <= 500e3:        # near Fs/2
            continue
        if abs(freq - args.sample_rate / 4) <= binsize*2.5: # near test signal
            continue
        noise_power += power

    # compensate for ENBW
    # (no need to compensate for processing gain, since we're summing the noise bins directly)
    noise_power = noise_power / enbw_gain

    # adjust to a dBm value:
    #  - the ADC maps +/- 400mV to +/- 2048.0, which we then scale to +/- 1.0 before doing the FFT
    #  - a full range sine input to the ADC (i.e. V(t) = 400mV * sin(f*t)) has
    #      RMS voltage = 400mV/sqrt(2)
    #  - assuming 50 ohm impedance:
    #      ADC input power = (400mV / sqrt(2))**2 / 50ohm = 1.6mW
    #  - a full-range sine input will appear as a FFT power of 0dB
    #  - therefore, we should multiply by 1.6 (+2.04dB) to convert from FFT values to ADC input in dBm
    fft_to_dbm = 1.6
    signal_power *= fft_to_dbm
    noise_power *= fft_to_dbm

    return (signal_power, noise_power, adc_min, adc_max, threshold_percent)

def setup(lpcsdr_dev, synth_dev, args):
    # Tuner on, tune LO to freq + Fs/4 (lower sideband), tuner filter as wide as possible without exceeding the Nyquist frequency
    lpcsdr.tuner.init_tuner(lpcsdr_dev)
    lpcsdr.tuner.set_if_lpf(lpcsdr_dev, 20e6, args.sample_rate / 2.0)
    lpcsdr.tuner.set_if_hpf(lpcsdr_dev, 0)
    pll_config = lpcsdr.tuner.find_parameters(args.frequency + args.sample_rate / 4.0, lpcsdr_dev.last_board_status.tuner_xtal)
    lpcsdr.tuner.configure_pll(lpcsdr_dev, pll_config)

    # Start ADC transfers immediately. We're going to be doing
    # a lot of short captures, not much point in continuously
    # stopping/starting the ADC.
    #
    # This will immediately fill the USB buffers (and start
    # dropping data) while the python script is doing other work,
    # but on each capture we discard the initial blocks to clear
    # out any stale data and get a good capture
    lpcsdr.adc.start_transfer(lpcsdr_dev, args.sample_rate)

    # Tune signal generator, set to -50dBm
    synth_dev.dbm = -50
    synth_dev.frequency = args.frequency
    synth_dev.rf_on = True
    synth_dev.update_lcd()

def teardown(lpcsdr_dev, synth_dev):
    lpcsdr_dev.stop_transfer()
    lpcsdr_dev.set_rf_power(lpcsdr.device.RFPowerMode.OFF)
    synth_dev.rf_on = False
    synth_dev.update_lcd()
        
    
def find_min_power_setting(lpcsdr_dev, synth_dev, args):
    for dbm in range(-50, 16, 3):
        synth_dev.dbm = dbm
        synth_dev.update_lcd()
        time.sleep(0.05)
        signal_power, noise_power, adc_min, adc_max, threshold_percent = measure(lpcsdr_dev, args, quick=True)
        if 10*math.log10(signal_power / noise_power) > args.min_signal_db:
            # Input signal is far enough above noise, use this
            return dbm

    # no signal seen
    print(f"warning: no input signal seen, dbm={dbm} signal={10*math.log10(signal_power):.1f}dB noise={10*math.log10(noise_power):.1f}dB ADC-min={adc_min:.0f} ADC-max={adc_max:.0f} >threshold={threshold_percent:.1f}%", file=sys.stderr)
    return dbm

def scan_gain(lpcsdr_dev, synth_dev, set_gain_fn, result_fn, args):
    set_gain_fn(lpcsdr_dev, 0)
    min_dbm = find_min_power_setting(lpcsdr_dev, synth_dev, args)
    # no need to set this, find_min_power_setting has already done it
    #synth_dev.dbm = min_dbm
    #synth_dev.update_lcd()
    time.sleep(0.05)
    for gain in range(0, 16, 1):
        set_gain_fn(lpcsdr_dev, gain)
        signal_power, noise_power, adc_min, adc_max, threshold_percent = measure(lpcsdr_dev, args)
        result_fn(dbm=min_dbm + args.attenuator, gain=gain, signal_power=signal_power, noise_power=noise_power, adc_min=adc_min, adc_max=adc_max, threshold_percent=threshold_percent)

def write_result_line(*, dbm, lna, mix, vga, signal_power, noise_power, adc_min, adc_max, threshold_percent, f):
    dbm_str = f"{dbm:.0f}" if dbm is not None else ""
    line = f"{dbm_str},{lna},{mix},{vga},{10*math.log10(signal_power):.2f},{10*math.log10(noise_power):.2f},{adc_min:.0f},{adc_max:.0f},{threshold_percent:.2f}"
    print(line + "     \r", end="", file=sys.stdout, flush=True)
    print(line, file=f, flush=True)


def run_lna_scan(lpcsdr_dev, synth_dev, outfile, args):
    lpcsdr.tuner.set_mix_gain(lpcsdr_dev, args.mix)
    lpcsdr.tuner.set_vga_gain(lpcsdr_dev, args.vga)
    def write_results_lna(*, gain, **kwargs):
        write_result_line(lna=gain, mix=args.mix, vga=args.vga, f=outfile, **kwargs)
    print(f"LNA scan with MIX={args.mix} VGA={args.vga}", file=sys.stderr)
    scan_gain(lpcsdr_dev, synth_dev, lpcsdr.tuner.set_lna_gain, write_results_lna, args)
    print("", file=sys.stderr)

def run_mix_scan(lpcsdr_dev, synth_dev, outfile, args):
    lpcsdr.tuner.set_lna_gain(lpcsdr_dev, args.lna)
    lpcsdr.tuner.set_vga_gain(lpcsdr_dev, args.vga)        
    def write_results_mix(*, gain, **kwargs):
        write_result_line(lna=args.lna, mix=gain, vga=args.vga, f=outfile, **kwargs)
    print(f"MIX scan with LNA={args.lna} VGA={args.vga}", file=sys.stderr)
    scan_gain(lpcsdr_dev, synth_dev, lpcsdr.tuner.set_mix_gain, write_results_mix, args)
    print("", file=sys.stderr)

def run_vga_scan(lpcsdr_dev, synth_dev, outfile, args):
    lpcsdr.tuner.set_lna_gain(lpcsdr_dev, args.lna)
    lpcsdr.tuner.set_mix_gain(lpcsdr_dev, args.mix)
    def write_results_vga(*, gain, **kwargs):
        write_result_line(lna=args.lna, mix=args.mix, vga=gain, f=outfile, **kwargs)
    print(f"VGA scan with LNA={args.lna} MIX={args.mix}", file=sys.stderr)
    scan_gain(lpcsdr_dev, synth_dev, lpcsdr.tuner.set_vga_gain, write_results_vga, args)
    print("", file=sys.stderr)

def run_dbm_scan(lpcsdr_dev, synth_dev, outfile, args):
    lpcsdr.tuner.set_lna_gain(lpcsdr_dev, args.lna)
    lpcsdr.tuner.set_mix_gain(lpcsdr_dev, args.mix)
    lpcsdr.tuner.set_vga_gain(lpcsdr_dev, args.vga)

    print(f"dBm scan with LNA={args.lna} MIX={args.mix} VGA={args.vga}", file=sys.stderr)

    # get a no-signal sample first
    synth_dev.rf_on = False
    synth_dev.update_lcd()
    time.sleep(0.05)
    signal_power, noise_power, adc_min, adc_max, threshold_percent = measure(lpcsdr_dev, args)
    write_result_line(dbm=None, lna=args.lna, mix=args.mix, vga=args.vga, signal_power=signal_power, noise_power=noise_power, adc_min=adc_min, adc_max=adc_max, threshold_percent=threshold_percent, f=outfile)

    synth_dev.rf_on = True    
    for dbm in range(-50, 16, 1):
        synth_dev.dbm = dbm
        synth_dev.update_lcd()
        time.sleep(0.05)
        signal_power, noise_power, adc_min, adc_max, threshold_percent = measure(lpcsdr_dev, args)
        write_result_line(dbm=dbm + args.attenuator, lna=args.lna, mix=args.mix, vga=args.vga, signal_power=signal_power, noise_power=noise_power, adc_min=adc_min, adc_max=adc_max, threshold_percent=threshold_percent, f=outfile)
    print("", file=sys.stderr)

def run_spectrum(lpcsdr_dev, synth_dev, outfile, args):
    lpcsdr.tuner.set_lna_gain(lpcsdr_dev, args.lna)
    lpcsdr.tuner.set_mix_gain(lpcsdr_dev, args.mix)
    lpcsdr.tuner.set_vga_gain(lpcsdr_dev, args.vga)

    synth_dev.dbm = args.spectrum
    synth_dev.update_lcd()
    time.sleep(0.05)

    print(f"capturing spectrum with dBm={args.spectrum:.1f}{args.attenuator:+.1f}dBm LNA={args.lna} MIX={args.mix} VGA={args.vga} to file {args.output}", file=sys.stderr)
    power_spectrum, adc_min, adc_max, threshold_percent, enbw_gain = get_power_spectrum(lpcsdr_dev, args)
    print("frequency,dB", file=outfile)
    for freq, power in power_spectrum:
        print(f"{freq:.2f},{10*math.log10(power):.2f}", file=outfile)
    print(f"# ADC min={adc_min}, ADC max={adc_max}, above_threshold={threshold_percent:.2f}, ENBW={10*math.log10(enbw_gain):.2f}dB", file=outfile)

def main():
    parser = argparse.ArgumentParser(description='Measures signal & noise at different tuner gain settings')

    parser.add_argument('--frequency', help="Set test frequency", type=frequency_value, default="1090M")
    parser.add_argument('--sample-rate', help="Set sample rate", type=frequency_value, default="6M")
    parser.add_argument('--bins', help="Set number of FFT bins", type=int, default=256)
    parser.add_argument('--scale', help="Set number of FFT captures to average over", type=int, default=1024)
    parser.add_argument('--adc-threshold-percent', help="Set threshold percentage of ADC samples above half range that is considered 'too loud'", type=float, default=0.1)
    parser.add_argument('--min-signal-db', help="Set minimum acceptable test signal level above noise floor, in dB", type=float, default=6.0)
    parser.add_argument('--attenuator', help="Set assumed amount of extra attenuation between the erasynth & lpcsdr, in dB", type=float, default=0.0)

    parser.add_argument('--erasynth-path', help="Set path to erasynth serial port", type=str, default="/dev/ttyACM0")
    
    parser.add_argument('--output', help="Set path to write results to", type=str, required=True)

    parser.add_argument('--spectrum', help="Oneshot run at given power, write spectrum to output file", type=float)
    
    parser.add_argument('--scan-dbm', help="Scan input power, keeping LNA/MIX/VGA constant", action="store_true")
    
    parser.add_argument('--lna', help="Set base LNA gain step", type=int, default=8)
    parser.add_argument('--mix', help="Set base MIX gain step", type=int, default=8)
    parser.add_argument('--vga', help="Set base VGA gain step", type=int, default=8)

    parser.add_argument('--scan-lna', help="Scan LNA gain steps, keeping MIX/VGA constant", action="store_true")
    parser.add_argument('--scan-mix', help="Scan MIX gain steps, keeping LNA/VGA constant", action="store_true")
    parser.add_argument('--scan-vga', help="Scan VGA gain steps, keeping LNA/MIX constant", action="store_true")

    parser.add_argument('--scan-lna-mix', help="Scan LNA and MIX gain step combinations, keeping VGA constant", action="store_true")
    
    args = parser.parse_args()

    lpcsdr_dev = lpcsdr.device.find()
    if lpcsdr_dev is None:
        print('no lpcsdr device found')
        return 1

    # FFT must fit in a single sample block
    if args.bins * 2 > lpcsdr_dev.last_board_status.usb_samples_per_block:
        print("cannot set bins > {spb//2}", file=sys.stderr)
        return 1

    # ensure the bin size is a multiple of 2, so we get good alignment for
    # the Fs/4 bin
    adjusted_bins = args.bins - (args.bins % 2)
    if args.bins != adjusted_bins:
        print(f"adjusting number of FFT bins from {args.bins} -> {adjusted_bins}")
        args.bins = adjusted_bins

    synth_dev = erasynth.ErasynthMicroControl(path=args.erasynth_path)

    setup(lpcsdr_dev, synth_dev, args)
    try:
        if args.spectrum:
            with open(args.output, 'w') as outfile:
                run_spectrum(lpcsdr_dev, synth_dev, outfile, args)                
        else:        
            with open(args.output, 'a') as outfile:
                print(f"dBm,LNA,MIX,VGA,signal,noise,adc_min,adc_max,above_threshold", file=outfile)
                if args.scan_lna:
                    run_lna_scan(lpcsdr_dev, synth_dev, outfile, args)
                if args.scan_mix:
                    run_mix_scan(lpcsdr_dev, synth_dev, outfile, args)
                if args.scan_vga:
                    run_vga_scan(lpcsdr_dev, synth_dev, outfile, args)
                if args.scan_lna_mix:
                    for lna in range(0, 16):
                        args.lna = lna
                        run_mix_scan(lpcsdr_dev, synth_dev, outfile, args)
                if args.scan_dbm:
                    run_dbm_scan(lpcsdr_dev, synth_dev, outfile, args)
    finally:
        teardown(lpcsdr_dev, synth_dev)


if __name__ == '__main__':
    sys.exit(main())
