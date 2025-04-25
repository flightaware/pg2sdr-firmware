# Testing via the Python scripts

Here's some notes on how to test device stuff using the Python scripts

# Initial setup

Install the udev rules to automatically give the LPCSDR USB device the right permissions:

 * Copy udev/99-lpcsdr.rules into /etc/udev/rules.d/
 * Reload udev: `sudo systemctl reload udev`

Ensure that you've got pyusb installed

Decide if you will either:

 a) build the firmware using mcuxpresso, producing Debug/lpcsdr.bin; or
 b) use the prebuilt firmware in images/lpcsdr.bin

The python scripts will look for images in that order, so if you choose (b), make sure that
you do not have an old mcuxpresso-built image in Debug/lpcsdr.bin

Set the boot switch on the LPCSDR to the "boot from USB" position, this is the position
towards the USB connector.

# LED meanings

There are three LEDs:

D2, a single-color yellow LED near the boot-mode switch
DS1 and DS2, two multi-color red/yellow/green LEDs near the tactile switch

## After power-on reset

On power-on reset, when the DFU bootloader is running:

 * D2 is on
 * DS1/DS2 are "mostly" off, but there's just enough current that they'll show dimly red

## After loading the firmware

After loading the LPCSDR firmware:

On startup, the firmware cycles through all 3 LEDs, once, to verify that they are working. After that:

D2 indicates the state of RF power.

DS1 shows the current ADC/DMA/USB state:
 * DS1 off: ADC is not running
 * DS1 green: ADC is running, and the host is consuming data over USB
 * DS1 yellow: ADC is running, but some data is being dropped
 * DS1 red: ADC is running, but a lot of data is being dropped (e.g. the host is not reading data at all)

DS2 shows the current R860T tuner state:
 * DS2 off: RF power off, or tuner not yet configured
 * DS2 green: tuner configured and PLL has successfully locked
 * DS2 yellow: tuner configured, but PLL does not have lock
 * DS2 red: I2C communication error with the tuner

## Error states

If the LPC bootloader tries to boot from flash (not USB), but there's a problem with the image, then it will
blink D2 at 1Hz for 60 seconds, then reset. This usually means you have the boot-mode switch in the wrong
position.

If the LPCSDR firmware panics, it will blink all of D2, DS1, DS2 in a morse code pattern for a few seconds,
then reset. The morse code letters/digits give some information about the source of the panic.

If the LPCSDR firmware encounters a watchdog reset, then it will just reset immediately back into the DFU
bootloader (D2 on, DS1/DS2 weakly red)

# Scripts

## Loading the firmware

You don't have to do anything special here, if there is no firmware loaded then the python
scripts will automatically download the firmware to the device via DFU.

## comms-check.py

This script does the most basic communication checks:

```
$ python/comms-check.py 
Found a LPC DFU device at <DEVICE ID 1fc9:000c on Bus 001 Address 064>
Downloading LPCSDR firmware from /home/parallels/git/lpcsdr/images/lpcsdr.bin
Waiting for LPCSDR to re-enumerate
New LPCSDR device appeared at <DEVICE ID dead:beef on Bus 001 Address 065>
comms check was okay
```

## reset.py

This script tells the LPCSDR to reset itself. After reset, the device will be back in
DFU mode, ready to accept a new firmware upload.

```
$ python/reset.py 
sent device reset request
```

## status.py

This script asks the LPCSDR for a variety of board status info. If you manage to get
the board to do something unexpected, then running status.py and saving the output
will help with debugging later.

```
$ python/status.py
Flags: SW1_USBBOOT

Target fADC: 0.000000 MHz

Measured clock source frequencies (MHz):
  IRC:        12.05
  PLL0USB:   479.42
  PLL0AUDIO:   0.00
  PLL1:       48.02
  IDIV_A:     12.02
  IDIV_B:     12.05
  IDIV_C:     16.01
  IDIV_D:      0.75
  IDIV_E:     12.02

```

(extra info will appear depending on what parts of the LPCSDR are currently active)

## set-rf-power.py

This script lets you control the state of RF power:

```
$ python/set-rf-power.py --on       # turns RF power on

$ python/set-rf-power.py --off      # turns RF power off

$ python/set-rf-power.py --reset    # turns RF power off, waits a bit, turns it back on
```

## start-transfer.py

This script programs the ADC clock for a given frequency, sets the CPU to high-speed mode,
and starts the ADC and DMA transfer process. Give it one argument, the ADC frequency in
MHz:

```
$ ./python/start-transfer.py 9.6
Calculated settings: INTEGER     N=  0 M=       12 P=15 I=  0  fCCO=288.000000MHz fOut= 9.600000MHz error=0.0Hz

Starting ADC/DMA ..
.. done.
```

The ADC clock can be monitored on pins CLK0/CLK2 (J2/J4)

## stop-transfer.py

This script reverses the effect of start-transfer.py, stopping ADC & DMA and setting the CPU
to a slower speed:

```
$ ./python/stop-transfer.py 
stopped ADC/DMA
```

## tuner.py

This script has various options to let you configure the R860T tuner.

It will turn on RF power, and initialize the tuner, if this has not already been done.

Provide one or more options to say what to do:

 * `--reset`: First toggle RF power to reset the tuner, then re-initialize the tuner. This
   will lose any previous changes you've made to the tuner settings.
 * `--powerdown`: Power down most of the tuner. This does not turn off RF power and doesn't
   _fully_ turn off the tuner
 * `--lna-gain`, `--vga-gain`, `--mix-gain`: Set the gains of the three gain stages. Each
   option takes a gain setting (an integer, 0-15, with no particular meaning in dB, just
   larger values = more gain)
 * `--pll`: Configure and start the tuner's LO PLL. Specify the LO frequency in MHz.

```
$ ./python/tuner.py --reset
resetting tuner (RF power cycle)
initializing tuner

$ ./python/tuner.py --lna-gain 7 --vga-gain 10 --mix-gain 15
setting LNA gain to 7
setting mixer gain to 15
setting VGA gain to 10

$ ./python/tuner.py --pll 1091.2
tuning PLL to 1091.200 MHz
programming PLL with settings: PLLParameters(refdiv=True, seldiv=2, feedback_n=75, feedback_sdm=50972, freq=1091199957.2753906)
got PLL lock with vco_current=4
```


