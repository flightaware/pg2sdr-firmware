#!/usr/bin/env python3

import math

n_dividers = [0] + list(range(2,257))
p_dividers = [0] + list(range(1,33))
i_dividers = [0] + list(range(2,257))

def n_value(n):
    return n if n > 0 else 1

def p_value(p):
    return p*2 if p > 0 else 1

def i_value(i):
    return i if i > 0 else 1

post_dividers = {}
for i in i_dividers:
    for p in p_dividers:
        d = p_value(p) * i_value(i)
        if d in post_dividers:
            old_p, old_i = post_dividers[d]
            if i < old_i:
                post_dividers[d] = (p, i)
        else:
            post_dividers[d] = (p, i)

def settings_for(target_frequency):
    min_fcco = 275e6
    max_fcco = 550e6
    mid_fcco = (min_fcco + max_fcco)/2
    ref_frequency = 12e6

    min_divider = int(math.ceil(min_fcco / target_frequency))
    max_divider = int(math.floor(max_fcco / target_frequency)) + 1

    best_frac = None
    best_int = None
    for p_i in range(min_divider, max_divider):
        if p_i not in post_dividers:
            continue

        p, i = post_dividers[p_i]

        wanted_fcco = target_frequency * p_i
        scaled_m = round( wanted_fcco / ref_frequency / 2 * (1<<15) )
        fractional_m = scaled_m / (1<<15)
        actual_fcco = 2 * fractional_m * ref_frequency
        actual_frequency = actual_fcco / p_i
        error = round(abs(actual_frequency - target_frequency))

        if best_frac is None or error < best_frac[0]:
            best_frac = (error, 0, fractional_m, p, i, actual_fcco, actual_frequency)

        for n in n_dividers:
            n_ref = ref_frequency / n_value(n)
            integer_m = int( round( wanted_fcco / n_ref / 2 ) )
            actual_fcco = 2 * integer_m * n_ref
            actual_frequency = actual_fcco / p_i
            error = round(abs(actual_frequency - target_frequency))

            if best_int is None or (error,n) < best_int[0:2]:
                best_int = (error, n, integer_m, p, i, actual_fcco, actual_frequency)

    return best_int, best_frac

def show(f, x):
    error, n, m, p, i, actual_fcco, actual_frequency = x

    if m == int(m):
        print(f'{f/1e6:6.3f}MHZ => INTEGER     N={n:3d} M={m:9.0f} P={p:2d} I={i:3d}  fCCO={actual_fcco/1e6:7.3f}MHz  fOut={actual_frequency/1e6:6.3f}MHz  error={error:.1f}Hz')
    else:
        print(f'{f/1e6:6.3f}MHZ => FRACTIONAL  N={n:3d} M={m:9.5f} P={p:2d} I={i:3d}  fCCO={actual_fcco/1e6:7.3f}MHz  fOut={actual_frequency/1e6:6.3f}MHz  error={error:.1f}Hz')

if __name__ == '__main__':
    for f in (2.4e6, 4.8e6, 6e6, 10e6, 12e6, 18e6, 20e6, 24e6, 1.041667e6*2*2, 1234567, 24.576e6):
        for x in settings_for(f):
            show(f, x)
