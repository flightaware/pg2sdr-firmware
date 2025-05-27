"""Helpers to program and inspect the ADC clock"""

__all__ = ['Settings', 'settings_for', 'start_transfer']

import math
import typing
import bisect
import operator

def effective_n_divisor(n):
    # n=0 means "bypass N-divider" i.e. divide-by-1
    # n=1 is legal ("divide-by-1") but pointless, prefer n=0 instead
    # n=2..256 mean "divide-by-n"
    return n if n > 0 else 1

def effective_p_divisor(p):
    # p=0 means "bypass P-divider" i.e. divide-by-1
    # p=1..32 mean "divide-by-p", and then there is a hardwired additional divide-by-2
    #   stage when the P-divider is not bypassed, i.e. the net effect is divide-by-2p
    return p*2 if p > 0 else 1

def effective_i_divisor(i):
    # i=0 means "bypass IDIVE", i.e. divide-by-1
    # i=1 is invalid (use i=0; IDIVE's minimum setting when not bypassed is divide-by-2)
    # i=2..256 mean "divide-by-i"
    """For an I-divider value of `i`, return the effective divisor used"""
    return i if i > 0 else 1

def _prepare_tables():
    # There are only certain values of N, P, and I available.
    # We prepopulate them here. Also, since P and I act
    # together at the end of the PLL, we can combine them
    # into a single effective divider, and pick the
    # "best" combination for each possible divider combination
    # (where "best" means "lowest I value", minimizing the
    # frequency of the clock signal that needs to cross between
    # PLL0AUDIO and IDIV_E)

    global n_dividers
    global p_dividers
    global i_dividers

    n_dividers = [0] + list(range(2,257))
    p_dividers = [0] + list(range(1,33))
    i_dividers = [0] + list(range(2,257))

    p_i_dividers_map = {}
    for i in i_dividers:
        for p in p_dividers:
            d = effective_p_divisor(p) * effective_i_divisor(i)
            if d in p_i_dividers_map:
                old_p, old_i = p_i_dividers_map[d]
                if i < old_i:
                    p_i_dividers_map[d] = (p, i)
            else:
                p_i_dividers_map[d] = (p, i)

    # we actually store the resulting divider combinations in a sorted list
    # so we can bisect the list quickly to discover a range of dividers
    global p_i_dividers
    p_i_dividers = sorted(p_i_dividers_map.items(), key=operator.itemgetter(0))

_prepare_tables()

class Settings(typing.NamedTuple):
    error: float
    n: int
    m: float
    p: int
    i: int
    actual_fcco: float
    actual_frequency: float
    fractional: bool

    @property
    def fixedpoint_m(self):
        return int(round(self.m * 32768))

    def __repr__(self):
        if self.fractional:
            return f'FRACTIONAL  N={self.n:3d} M={self.m:9.5f} P={self.p:2d} I={self.i:3d}  fCCO={self.actual_fcco/1e6:10.6f}MHz fOut={self.actual_frequency/1e6:9.6f}MHz error={self.error:.1f}Hz'
        else:
            return f'INTEGER     N={self.n:3d} M={self.m:9.0f} P={self.p:2d} I={self.i:3d}  fCCO={self.actual_fcco/1e6:10.6f}MHz fOut={self.actual_frequency/1e6:9.6f}MHz error={self.error:.1f}Hz'


def settings_for(target_frequency, allow_fractional=True, epsilon=1e-6):
    min_fcco = 275e6
    max_fcco = 550e6
    ref_frequency = 12e6
    error_threshold = target_frequency * epsilon

    def improves(current, candidate):
        # reject any candidates that are out of bounds
        if candidate.actual_fcco < min_fcco or candidate.actual_fcco > max_fcco:
            return False
        if (not candidate.fractional) and (candidate.m < 1 or candidate.m > 1<<15):
            return False
        if candidate.fractional and (candidate.fixedpoint_m < 1 or candidate.fixedpoint_m >= 1<<22):
            return False

        # reject any candidates with too much error
        if candidate.error > error_threshold:
            return False

        # accept the first candidate that's otherwise okay
        if current is None:
            return True

        # when both solutions are the same sort of solution (integer/fractional), choose whichever has smaller M
        if current.fractional == candidate.fractional:
            return (candidate.m < current.m)

        # when one solution is fractional and the other is integer;
        # accept the integer solution if the M value is no more than 4x larger
        #   (todo: actually quantify the effect of a fractional divisor vs. large M)
        if current.fractional:
            # current is fractional, candidate is integer
            return (candidate.m <= current.m * 4)
        else:
            # current is integer, candidate is fractional
            return not (current.m <= candidate.m * 4)

    min_divider = int(math.floor(min_fcco / target_frequency))
    max_divider = int(math.ceil(max_fcco / target_frequency))

    # discover the range of items in p_i_dividers that contains dividers
    # satisfying min_divider <= x <= max_divider
    key_fn = operator.itemgetter(0)  # compare to first item in tuple
    left = bisect.bisect_left(p_i_dividers, min_divider, key=key_fn)
    right = bisect.bisect_right(p_i_dividers, max_divider+1, lo=left, key=key_fn)

    best = None
    for p_i, (p, i) in p_i_dividers[left:right]:
        wanted_fcco = target_frequency * p_i
        wanted_fcco = min(wanted_fcco, max_fcco)
        wanted_fcco = max(wanted_fcco, min_fcco)

        if allow_fractional:
            scaled_m = round( wanted_fcco / ref_frequency / 2 * (1<<15) )
            test_fcco = 2 * scaled_m / (1<<15) * ref_frequency
            # rounding may cause test_fcco to be out of range; if so,
            # round it in the other direction to tweak it back into range
            if test_fcco < min_fcco:
                scaled_m += 1
            elif test_fcco > max_fcco:
                scaled_m -= 1            
            fractional_m = scaled_m / (1<<15)
            actual_fcco = 2 * fractional_m * ref_frequency
            actual_frequency = actual_fcco / p_i
            error = abs(actual_frequency - target_frequency)

            candidate = Settings(error, 0, fractional_m, p, i, actual_fcco, actual_frequency, True)
            if improves(best, candidate):
                best = candidate

        for n in n_dividers:
            n_ref = ref_frequency / effective_n_divisor(n)
            integer_m = int( round( wanted_fcco / n_ref / 2 ) )
            test_fcco = 2 * integer_m * n_ref
            # rounding may cause test_fcco to be out of range; if so,
            # round it in the other direction to tweak it back into range
            if test_fcco < min_fcco:
                integer_m += 1
            elif test_fcco > max_fcco:
                integer_m -= 1
            actual_fcco = 2 * integer_m * n_ref
            actual_frequency = actual_fcco / p_i
            error = round(abs(actual_frequency - target_frequency))

            candidate = Settings(error, n, integer_m, p, i, actual_fcco, actual_frequency, False)
            if improves(best, candidate):
                best = candidate

    return best


def start_transfer(dev, freq, allow_fractional=True, epsilon=1e-6):
    settings = settings_for(freq, allow_fractional, epsilon)
    if settings is None:
        raise ValueError(f'no suitable ADC settings found for sampling rate {freq/1e6:.1f}MHz')
    dev.start_transfer(n_div = settings.n,
                       m_div = settings.fixedpoint_m,
                       p_div = settings.p,
                       idiv_div = settings.i)
    

def main():
    import sys, argparse

    parser = argparse.ArgumentParser(description='Control HSADC')

    parser.add_argument('--calc', help="Find ADC clock settings for given frequency (specify as MHz)", type=float)
    parser.add_argument('--start', help="Program ADC clock for given frequency (specify as MHz) and start USB transfers", type=float)    
    parser.add_argument('--stop', help="Stop ADC clock and USB transfers", action='store_true')    
    parser.add_argument('--status', help="Print ADC status", action='store_true')

    if len(sys.argv) < 2:
        parser.print_usage()
        return 2
    
    args = parser.parse_args()

    if args.calc:
        print(f'{args.calc:9.6f}MHz => {settings_for(args.calc*1e6)!r}')

    if args.start or args.stop or args.status:
        import lpcsdr.device

        dev = lpcsdr.device.find()
        if dev is None:
            print('no lpcsdr device found')
            return 1

        if args.start:
            print(f'Starting ADC transfers at {args.start:.1f}MHz')
            start_transfer(dev, args.start*1e6)

        if args.stop:
            print('Stopping ADC')
            dev.stop_transfer()

        if args.status:
            from . import adc_status

            status = dev.board_status()            
            adc_status.print_status(status, file=sys.stdout)

    return 0

if __name__ == '__main__':
    import sys
    sys.exit(main())
