# PG2SDR firmware

This repository contains the source code for the Prostick Gen 2 firmware.

Prebuilt firmware images are available in the `images/` subdirectory.

## Prerequistes

To build the firmware, you need:

 * CMake
 * a suitable cross-compiling GCC that can build binaries for ARM Cortex-M4
 * a Python 3 interpreter (needed for the final step that creates a
   loadable firmware image)

On Debian or Ubuntu, this is as simple as:

```
  sudo apt install cmake gcc-arm-none-eabi python3-minimal
```

Other environments left as an exercise to the reader. If your cross-compiler
is called something other than "arm-none-eabi-gcc", then you will need to
edit `lpc4370.cmake` accordingly.

## Building it

`make all` to build everything. `make -j4 all` if you want to go fast.
Built firmware images can be found in `build/{debug,release}/src/*.bin`

`make update-images` will copy the built images out into the `images/` subdir.

`make clean` removes the build dir entirely.

## Licenses

See [here](LICENSES.md) for license info. tl;dr: mostly BSD 2-clause compatible,
with some code having an additional requirement that it can only be used with
NXP microcontrollers (the ProStick Gen 2 uses a NXP microcontroller)
