#!/bin/sh

# Gain calibration / gain curve selection / etc
#
# This assumes you have:
#
#   an erasynth micro at the default serial port path, /dev/ttyACM0
#   connected to 60dB worth of attenuators
#   connected to a LPCSDR
#
# It will collect measurements (thiis takes about 30 minutes) and writes:
#
#   dbm.csv - measurements of the perceived signal for different erasynth input
#             power levels (used to get accurate relative power levels for
#             different input powers, rather than just trusting the erasynth
#             output level that we set)
#
#   gain.csv - signal and noise measurements for all possible combinations
#              of the tuner gain settings
#
# Using that data, it generates:
#
#   adjusted.csv -
#      all the data from gain.csv, but with the resulting input & noise levels
#      adjusted based on the data in dbm.csv, and with some additional data
#      computed (gain, MDS, maximum input level, dynamic range)
#
#   sensitivity-curve.csv -
#      a series of points from adjusted.csv that produce a gain curve that
#      tries to produce the best minimum-detectable-signal at each setting
#
#   rtlsdr-curve.csv -
#      points from adjusted.csv that correspond to the gain curve that
#      librtlsdr implements
#
# and then finally it also makes a gnuplot graph in `gain.png` with data from
# the last 3 csv files.

ATT=-60
ERASYNTH=/dev/ttyACM0

set -e

if [ ! -f dbm.csv ]
then
    rm -f dbm.csv.new
    ./gain-measurements.py --output dbm.csv.new --lna 4 --mix 4 --vga 4 --scan-dbm --attenuator $ATT --erasynth-path $ERASYNTH
    mv dbm.csv.new dbm.csv
else
    echo "dbm.csv already present, skipping" >&2
fi

if [ ! -f gain.csv ]
then
    rm -f gain.csv.new
    for vga in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
    do
        ./gain-measurements.py --output gain.csv.new --vga $vga --scan-lna-mix --attenuator $ATT --erasynth-path $ERASYNTH
    done
    mv gain.csv.new gain.csv
else
    echo "gain.csv already present, skipping" >&2
fi

./make-gain-curve.py \
    --dbm-map dbm.csv --measurements gain.csv \
    --adjusted adjusted.csv \
    --rtlsdr rtlsdr-curve.csv \
    --curve sensitivity-curve.csv \
    --lna-table lna.csv \
    --mix-table mix.csv \
    --vga-table vga.csv \
    --c-tables gain-tables.gen.c

gnuplot - <<"EOF"
set datafile separator ","
set xlabel "gain (dB)"
set ylabel "Minimum detectable signal, 3dB SNR (dBm)"
set ytics nomirror
set y2label "dynamic range (dB)"
set y2tics

set terminal pngcairo size 1200,800
set output "gain.png"

plot "adjusted.csv" using 1:($12<2048*.8?$14:1/0) lt 5 ps 0.4                  \
         title "possible gain settings (input within ADC range)",              \
     "adjusted.csv" using 1:($12>=2048*.8?$14:1/0) lt 4 ps 0.4                 \
         title "possible gain settings (input exceeds ADC range)",             \
     "sensitivity-curve.csv" using 1:14 lt 1 with linespoints                  \
         title "sensitivity curve",                                            \
     "rtlsdr-curve.csv" using 1:14 lt 1 lc 2 with linespoints                  \
         title "librtlsdr gain curve",                                         \
     "sensitivity-curve.csv" using 1:($15-$14) lt 6 axes x1y2 with linespoints \
         title "sensitivity curve, dynamic range (RHS)",                       \
     "rtlsdr-curve.csv" using 1:($15-$14) lt 7 axes x1y2 with linespoints      \
         title "rtlsdr curve, dynamic range (RHS)"
EOF
