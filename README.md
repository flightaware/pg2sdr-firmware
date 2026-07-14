# PG2SDR firmware

&#x26A0; Work in progress, not finalized yet! &#x26A0; 

This repository contains the source code for the Prostick Gen 2
firmware.

Prebuilt firmware images are available in the `images/` subdirectory.

## Applying firmware updates

See the
[libpg2sdr documentation](https://github.com/flightaware/libpg2sdr/blob/master/doc/firmware-update.md)
for details on how to apply a new firmware image to a ProStick Gen 2 device
using `pg2-util`.

## Build prerequisites

To build the firmware, you need:

 * CMake
 * a suitable cross-compiling GCC that can build binaries for ARM Cortex-M4
 * a Python 3 interpreter (needed for the final step that creates a
   loadable firmware image)

On Debian or Ubuntu, this is as simple as:

```bash
  sudo apt install cmake gcc-arm-none-eabi python3-minimal
```

Other environments left as an exercise for the reader. If your
cross-compiler is called something other than "arm-none-eabi-gcc",
then you will need to edit `lpc4370.cmake` accordingly.

## Building it

`make all` to build everything. Built firmware images can be found in
`build/pg2sdr-{debug,release}/pg2sdr-firmware-*.bin`

`make update-images` will copy the built images out into the `images/`
subdir.

`make clean` removes the build dir entirely.

## Licenses

See [here](LICENSES.md) for license info. TL;DR: mostly BSD 2-clause
compatible, with some third-party code (LPCOpen) having an additional
requirement that it can only be used with NXP microcontrollers (the
ProStick Gen 2 uses a NXP microcontroller)

## Other documentation

There is some more internal documentation written during development
that is somewhat incomplete / out of date, but FWIW:

* [Analog receive path](doc/analog.md)
* [ADC resistor divisor simulation](doc/circuit-sim/README.md)
* [Firmware design notes](doc/design.md)

## Related repositories

* [libpg2sdr](https://github.com/flightaware/libpg2sdr) -- host
  library for talking to a PG2SDR
* Python scripts for firmware development (TBA)
* Python scripts for characterizing the hardware (TBA)
