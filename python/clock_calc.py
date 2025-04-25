#!/usr/bin/env python3

import math
import typing

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


class Settings(typing.NamedTuple):
    error: float
    n: int
    m: float
    p: int
    i: int
    actual_fcco: float
    actual_frequency: float
    fractional: bool

    def __repr__(self):
        if self.fractional:
            return f'FRACTIONAL  N={self.n:3d} M={self.m:9.5f} P={self.p:2d} I={self.i:3d}  fCCO={self.actual_fcco/1e6:10.6f}MHz fOut={self.actual_frequency/1e6:9.6f}MHz error={self.error:.1f}Hz'
        else:
            return f'INTEGER     N={self.n:3d} M={self.m:9.0f} P={self.p:2d} I={self.i:3d}  fCCO={self.actual_fcco/1e6:10.6f}MHz fOut={self.actual_frequency/1e6:9.6f}MHz error={self.error:.1f}Hz'


def settings_for(target_frequency, epsilon=1e-6):
    min_fcco = 275e6
    max_fcco = 550e6
    mid_fcco = (min_fcco + max_fcco)/2
    ref_frequency = 12e6
    error_threshold = target_frequency * epsilon

    def improves(current, candidate):
        if current is None:
            return True
        if current.error > error_threshold:
            return (candidate.error < current.error)
        return (candidate.m < current.m)

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
        error = abs(actual_frequency - target_frequency)

        candidate = Settings(error, 0, fractional_m, p, i, actual_fcco, actual_frequency, True)
        if improves(best_frac, candidate):
            best_frac = candidate

        for n in n_dividers:
            n_ref = ref_frequency / n_value(n)
            integer_m = int( round( wanted_fcco / n_ref / 2 ) )
            actual_fcco = 2 * integer_m * n_ref
            actual_frequency = actual_fcco / p_i
            error = round(abs(actual_frequency - target_frequency))

            candidate = Settings(error, n, integer_m, p, i, actual_fcco, actual_frequency, False)
            if improves(best_int, candidate):
                best_int = candidate

    return best_int, best_frac

def show(f, x):
    print(f'{f/1e6:9.6f}MHz => {x!r}')


if __name__ == '__main__':
    for f in (2.4e6, 4.8e6, 6e6, 10e6, 12e6, 18e6, 20e6, 24e6, 1.041667e6*2*2, 1234567, 24.576e6):
        for x in settings_for(f):
            show(f, x)
