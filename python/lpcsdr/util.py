from enum import IntFlag

class BitFlag(IntFlag):
    def __init__(self, value):
        rightmost = (value & ~(value-1))
        self.pos = rightmost.bit_length() - 1
        rmask = (value >> self.pos)
        self.width = rmask.bit_length()

    def __call__(self, x:int) -> int:
        """flags = SomeFlag.WELL_KNOWN_FIELD(42)"""
        if x < 0 or x.bit_length() > self.width:
            raise ValueError(f'{x} is out of range for a bitfield of width {self.width}')
        return x << self.pos

    def extract(self, x:'BitFlag') -> int:
        """SomeFlag(42).extract(SomeFlag.WELL_KNOWN_FIELD)"""
        if type(self) != type(x):
            raise ValueError('incompatible BitFlag subclasses')
        return (self.value & x.value) >> x.pos


def onebit(x:int) -> int:
    """onebit(x) -> value with only bit 2**x set"""
    return (1<<x)

def bitrange(x:int, y:int) -> int:
    """bitrange(x,y) -> value with bits 2**x .. 2**y (inclusive) set"""
    if x > y:
        y,x = x,y
    return ((2<<y)-1) ^ ((1<<x)-1)

def decompose_flags(flags: BitFlag, all_members:bool=True) -> dict[BitFlag,BitFlag]:
    """Given a BitFlag instance where the integer values of
the flag members do not overlap, bitwise, decompose it
into a mapping where:

 * the key is a member of the BitFlag class, or None;
 * the value is an instance of the BitFlag class (but not
   necessarily a well-known member)

and:

 * a key of None means that the corresponding value is any
   "residual" integer value that does not correspond to the bits
   of any particular member;
 * other keys mean that the corresponding value is the value of
   the bits corresponding to the integer value of the key, even
   if not all those bits are set.

That is, given an IntFlag definition of:

class TestFlag(BitFlag):
    A = onebit(0)
    B = onebit(1)
    C = bitrange(2,3)
    D = bitrange(12,14)

then TestFlag(1 | (3 << 2) | (5 << 12) | (1<<16)) will decompose into
this mapping:

    {
        TestFlag.A: TestFlag.A,
        TestFlag.B: TestFlag(0),
        TestFlag.C: TestFlag.C,
        TestFlag.D: TestFlag(5 << 12),
        None: TestFlag(1<<16)
    }

If `all_members` is False, then zero-valued single-bit flags are
omitted (i.e. TestFlag.B would be omiited in the example above)
"""
    
    # we could just use Flag.__iter__ here ..
    # .. except it's only in Python 3.11 and later
    # and the Ubuntu VMs have 3.10
    result = {}
    remainder = flags
    for flag in type(flags):  # make sure to use the metaclass __iter__
        if all_members or (flag & remainder) or flag.width > 1:
            bits = flag & remainder
            result[flag] = bits
            remainder = remainder ^ bits
    if remainder:
        result[None] = remainder
    return result


def flag_string_parts(flags: BitFlag, all_members:bool = False) -> list[str]:
    """Given an IntFlag instance, decompose it (see
decompose_flags) and return a list of strings making up the Flag"""
    def format_one(k,v):
        if k is None:
            return f'unassigned=0x{v.value:02X}'
        elif (not all_members) and k.width == 1:
            return k.name
        else:
            return f'{k.name}={v.extract(k)}'

    return list( format_one(k,v) for k,v in decompose_flags(flags, all_members).items() )


def flag_string(flags: IntFlag, all_members:bool=False) -> str:
    return ' '.join(flag_string_parts(flags,all_members))


def frequency_value(s: str) -> float:
    s = s.strip().lower()
    if s.endswith('hz'):
        s = s[:-2]

    multiplier: float
    if s.endswith('g'):
        multiplier = 1e9
        s = s[:-1]
    elif s.endswith('m'):
        multiplier = 1e6
        s = s[:-1]
    elif s.endswith('k'):
        multiplier = 1e3
        s = s[:-1]
    else:
        multiplier = 1.0

    return multiplier * float(s)


def format_frequency(f: float, precision:int=-3) -> str:
    # nb: precision=-3 means "show kHz-level precision"
    #     precision=-6 means "show MHz-level precision"
    #     etc
    if abs(f) >= 2.0e9:
        return f'{f/1e9:.{max(0,precision+9)}f)} GHz'
    if abs(f) >= 2.0e6:
        return f'{f/1e6:.{max(0,precision+6)}f} MHz'
    if abs(f) >= 2.0e3:
        return f'{f/1e3:.{max(0,precision+3)}f} kHz'
    return f'{f:.{max(0,precision)}f} Hz'
