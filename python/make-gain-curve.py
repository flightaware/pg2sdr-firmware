#!/usr/bin/env python3

import math
import sys
import csv
import typing
import argparse
import numpy

from typing import NamedTuple, Optional

class Gains(NamedTuple):
    lna: int
    mix: int
    vga: int

class Measurement(NamedTuple):
    gains: Gains
    signal: Optional[float]
    noise: Optional[float]
    input_dbm: Optional[float]
    raw_signal: float
    raw_noise: float
    adc_min: int
    adc_max: int
    threshold_percent: float

class DbmEntry(typing.NamedTuple):
    noise: float
    actual_dbm: float
    
def read_measurements(f):
    for row in csv.reader(f):
        if len(row) < 9:
            continue

        try:
            if row[0] == "":
                input_dbm = None
            else:
                input_dbm = int(float(row[0]))

            m = Measurement(gains=Gains(lna = int(row[1]),
                                        mix = int(row[2]),
                                        vga = int(row[3])),
                            signal = None,
                            noise = None,
                            input_dbm = input_dbm,
                            raw_signal = float(row[4]),
                            raw_noise = float(row[5]),
                            adc_min = int(row[6]),
                            adc_max = int(row[7]),
                            threshold_percent = float(row[8]))
        except ValueError:
            continue

        yield m

def read_dbm_map(f):
    # Build a map of (erasynth dbm setting) -> (observed differences from the -50dBm setting)
    # (actually, it is whatever the smallest dbm setting is, but that's always -50dBm + attenuation)
    #
    # e.g. if we compare -50dBm and -45dBm, we would expect to get, in a perfect world:
    #   - the recieved signal level seen by the ADC at -45dBm is 5dB higher than at -50dBm
    #   - the noise level seen by the ADC is the same for both
    #
    # but in the real world, that's not actually true, so record the observed differences in
    # dbm_map so we can have a more accurate idea of what a given input setting actually
    # produces.
    
    measurements = [m for m in read_measurements(f) if m.input_dbm is not None]
    if not measurements:
        raise RuntimeError(f'dBm map has no valid measurements')

    base = min(measurements, key=lambda x: x.input_dbm)
    dbm_map = {}
    for m in measurements:
        if m.input_dbm in dbm_map:
            raise RuntimeError(f'dBm map has duplicate entries for {m.input_dbm}dBm')
        dbm_map[m.input_dbm] = DbmEntry(noise = m.raw_noise - base.raw_noise,
                                        actual_dbm = m.raw_signal - base.raw_signal + base.input_dbm)

    return dbm_map

def apply_dbm_map(measurements, dbm_map):
    # Use the observations in dbm_map to adjust all the signal/noise/dbm values
    # in "measurements" to what we think they would be, if the erasynth was
    # perfect.

    for m in measurements:    
        adjust = dbm_map.get(m.input_dbm, None)
        if adjust is None:
            raise RuntimeError(f'missing dBm map entry for {m.input_dbm}dBm')        
        yield m._replace(input_dbm=adjust.actual_dbm,
                         signal=m.raw_signal,
                         noise=m.raw_noise - adjust.noise)

def calc_gain(m):
    # simple gain => observed signal - input signal
    return m.signal - m.input_dbm

def calc_mds(m):
    # infer the Minimum Detectable Signal from a gain measurement:
    # this is the input level where we would see a signal level 3dB above
    # the noise floor.

    # here, we assume that changing input_dbm directly changes m.signal
    # and doesn't change m.noise (note that we have already tried to
    # compensate for any changes to the noise floor due to changing the
    # signal generator settings in apply_dbm_map)
    return m.input_dbm - (m.signal - m.noise) + 3.0

def compute_gain_curve(measurements):
    # compute a gain curve that has gain settings at regular
    # (~3dB) intervals, and which tries to minimize MDS
    # (i.e. prefer gain settings that let us see weaker signals)

    gains = [calc_gain(m) for m in measurements]
    db_step = 3.0
    for target_gain in numpy.arange(min(gains), max(gains) + db_step, db_step):
        best = None
        for candidate in measurements:
            if calc_gain(candidate) < target_gain - db_step/2:
                continue
            if calc_gain(candidate) > target_gain + db_step/2:
                continue
            if best is not None and calc_mds(candidate) > calc_mds(best):
                continue
            best = candidate

        if best:
            yield best

def compute_rtlsdr_curve(measurements):
    m_map = { m.gains: m for m in measurements }

    # this replicates the gain selection logic in librtlsdr:
    #  - always use VGA=8
    #  - increment LNA, then MIX, then LNA, then MIX, etc
    
    vga = 8
    lna = 0
    mix = 0

    for i in range(15):
        yield m_map[Gains(lna,mix,vga)]
        lna += 1
        yield m_map[Gains(lna,mix,vga)]
        mix += 1

    yield m_map[Gains(lna,mix,vga)]

    # add a final gain setting that reflects the "AGC on"
    # librtlsdr setting, which actually sets VGA=11,
    # assuming that there's actually not much continuous
    # power arriving and so AGC will set LNA/VGA to 15.
    yield m_map[Gains(15,15,11)]
        
def write_csv(path, rows):
    print(f"writing {path}", file=sys.stderr)
    with open(path, 'w') as out:
        writer = csv.writer(out)
        writer.writerow(("gain_db", "LNA", "MIX", "VGA", "signal_db", "noise_db", "snr_db", "input_dbm", "raw_signal_db", "raw_noise_db", "adc_min", "adc_max", "threshold_percent", "mds_db", "max_signal_db", "dynamic_range_db"))
        for m in rows:
            gain = m.signal - m.input_dbm
            snr = m.signal - m.noise
            mds = calc_mds(m)
            max_signal = m.input_dbm + (2.04 - m.signal)    # input level that would produce a +2.04dBm (ADC full range) signal
            dynamic_range = max_signal - mds
            writer.writerow(( f"{gain:.2f}",
                              m.gains.lna,
                              m.gains.mix,
                              m.gains.vga,
                              f"{m.signal:.2f}",
                              f"{m.noise:.2f}",
                              f"{snr:.2f}",
                              f"{m.input_dbm:.2f}",
                              m.raw_signal,
                              m.raw_noise,
                              m.adc_min,
                              m.adc_max,
                              m.threshold_percent,
                              f"{mds:.2f}",
                              f"{max_signal:.2f}",
                              f"{dynamic_range:.2f}" ))

def main():
    parser = argparse.ArgumentParser(description='Compute a sensitivity gain curve from gain measurements')
    
    parser.add_argument('--dbm-map', help="Input CSV file with dBm mapping", required=True)
    parser.add_argument('--measurements', help="Input CSV file with gain measurements", required=True)

    parser.add_argument('--curve', help="Write sensitivity gain curve to this CSV file")
    parser.add_argument('--rtlsdr', help="Write a rtlsdr-style gain curve to this CSV file")
    parser.add_argument('--adjusted', help="Write adjusted-for-dbm-map measurements to this CSV file")

    args = parser.parse_args()

    with open(args.dbm_map, 'r') as f:
        dbm_map = read_dbm_map(f)

    with open(args.measurements, 'r') as f:
        measurements = list(apply_dbm_map(read_measurements(f), dbm_map))

    if args.adjusted:
        write_csv(args.adjusted, sorted(measurements, key=lambda m: (m.signal - m.input_dbm)))

    if args.curve:
        write_csv(args.curve, compute_gain_curve(measurements))

    if args.rtlsdr:
        write_csv(args.rtlsdr, compute_rtlsdr_curve(measurements))

if __name__ == '__main__':
    main()
    
        
            
