import serial
import time
import sys

from typing import ClassVar, Optional, Type
from types import TracebackType
from typing_extensions import Literal

#
# API for talking to the Erasynth Micro's USB-serial control interface
# 

class ErasynthMicroControl(object):
    port: serial.Serial
    verbose: bool
    tuning_offset: float = 0
    _rf_on: bool
    _dbm: int
    _frequency: int
    next_command: float = 0
    
    def __init__(self, path: str = '/dev/ttyACM0', verbose: bool = False):
        self.port = serial.Serial(path, baudrate=9600, bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE, stopbits=serial.STOPBITS_ONE, rtscts=True)
        self.verbose = verbose

        self.command('>SM0')    # modulation off
        self.command('>SS6')    # sweep off
        self.command('>SR0')    # internal reference

        # these also send commands:
        self.rf_on = False
        self.dbm = -50
        self.frequency = 1000000000

    def __enter__(self) -> 'ErasynthMicroControl':
        return self

    def __exit__(self, exc_type: Optional[Type[BaseException]], exc_val: Optional[BaseException], exc_tb: Optional[TracebackType]) -> Literal[False]:
        self.close()
        return False

    def close(self):
        self.port.close()

    def command(self, cmd: str):
        # ratelimit, don't send serial data faster than it can be consumed (crtscts notwithstanding)
        now = time.time()
        while now < self.next_command:
            time.sleep(self.next_command - now)
            now = time.time()
        self.next_command = now + (len(cmd) + 1) / 960 + 0.05

        if self.verbose:
            print(cmd, file=sys.stderr, flush=True)
        self.port.write(bytes(cmd, 'ascii') + b'\r')


    @property
    def rf_on(self) -> int:
        return self._rf_on

    @rf_on.setter
    def rf_on(self, value: bool):
        value = bool(value)
        if not hasattr(self, '_rf_on') or self._rf_on != value:
            self._rf_on = bool(value)
            if self._rf_on:
                self.command(f'>SF1')
            else:
                self.command(f'>SF0')

    @property
    def dbm(self) -> int:
        return self._dbm

    @dbm.setter
    def dbm(self, value: int):
        value = int(value)
        if value < -50 or value > 15:
            raise ValueError(f'{value} dBm out of range, must be -50 .. +15 dBm')
        if not hasattr(self, '_dbm') or value != self._dbm:
            self.command(f'>SA{value}')
            self._dbm = value

    @property
    def frequency(self) -> int:
        return self._frequency

    @frequency.setter
    def frequency(self, value: float):
        value = int(value + self.tuning_offset)
        if value < 12500000 or value > 6400000000:
            raise ValueError(f'{value} Hz out of range, must be 12.5MHz .. 6.4GHz')
        if not hasattr(self, '_frequency') or value != self._frequency:
            self.command(f'>F{value}')
            self._frequency = value

    def update_lcd(self):
        self.command('>GH')
