# LPCSDR hardware overview

The LPCSDR is a software-defined radio that feeds radio data over
USB to a USB host; the host is responsible for further processing.

The LPCSDR is a bus-powered USB device; that is, it is powered by
the 5V line from the USB host, and is not powered if not connected
to USB.

The LPCSDR has two main components that the firmware interacts with:

 * A LPC4370 microcontroller
 * A R860T tuner

RF signals from an antenna at around 900 - 1100Hz are fed to the R860T.

The R860T shifts a programmable part of the received RF spectrum so that
it is centered around a lower intermediate frequency (IF), typically putting
the signal we care about around 0-20MHz.

The LPC4370 digitizes the IF signal using a built-in analog-to-digital
converter (ADC). The data this produces is sent over a USB connection
to the USB host.

The LPC4370 is also responsible for controlling the R860T (over an
I2C serial bus connection), and various misc management tasks e.g.
monitoring and responding to the state of switches/buttons on the SDR,
controlling LEDs, updating firmware stored on flash memory.

This firmware is the software that is loaded into the LPC4370 to run
these tasks.

# LPC4370 overview

The LPC4370 is a microcontroller with integrated peripherals,
including:

 * One Cortex-M4 ARM core (ARMv7-M architecture) running at up to
   204MHz. This runs our firmware.
 * Two Cortex-M0 ARM cores (unused in the current firmware; one M0
   core will probably be used in the future for R860T communication)
 * A little over 256kB of built-in RAM to store firmware code and data
 * A 12-bit ADC that can convert signals at up to 80MHz
 * A USB 2.0 device controller
 * A DMA controller that can offload memory-to-memory or peripheral-
   to-memory data transfers, reducing the work the CPU needs to do
 * A clock generation unit that can generate clock signals at
   configurable frequencies for the CPU cores, USB controller, ADC,
   etc.
 * I2C and SPI serial bus interfaces
 * GPIO pins for interacting with simple external components like
   LEDs & switches

The M4 core accesses the rest of the hardware by reading and writing
to memory addresses in a special I/O region where those reads/writes
directly interact with a peripheral, rather than being "real" memory.

For documentation, you will want a copy of NXP's "UM10503" user
manual, which goes into the hardware in detail.

# Firmware images

The firmware image that we load is a flat binary file with and
identifying header and trailer; the bulk of the file (without
header/footer) end up loaded into RAM at a fixed location and
executed by the M4 core.

## Building with MCUXpresso

Notes on how to build and upload firmware can be found on this [Confluence page](https://flightaware.atlassian.net/wiki/spaces/ADSB/pages/3611590682/LPC4370+firmware+sdk+notes)

## Image format

The resulting image has:

* a special NXP/LPC header that identifies it as a valid
  image for the LPC4370; see UM10503 5.3.4 "Boot image
  header format". We do not use the AES fields, so all
  that's relevant here is the correct "AES not active"
  value, and HASH_SIZE to indicate the total size of
  the image. This header is not copied to RAM when
  the firmware is loaded.

* a special DFU trailer (at the end of the image) that
  identifies the image as a valid DFU image and indicates
  the USB vendor/product IDs that a DFU uploader should
  look for when uploading this firmware over USB. This
  trailer is for the use of the DFU uploader and is not
  actually sent to the LPC4370 when uploaded.

# Initial firmware loading

On power-up, the LPC4370's M4 core executes code contained
in the boot ROM of the 4370. This boot code will examine
the state of the BOOTn pins to determine how it should
load firmware (see UM10503 Fig. 14 "Boot process for
parts without flash" -- here "without flash" means
specifically "without _onboard_ flash".

We configure the boot pins so that the LPC4370 will try
either:

 1. Load firmware over USB, ignoring external flash; or
 2. Try to load firmware over SPI from external flash;
    if there is no valid firmware in external flash,
    then load firmware over USB

A switch on the PCB controls which mode is used.

## Loading over USB

To load firmware over USB, the LPC4370 connects to the
USB bus, enumerates using a well-known vendor/product ID,
and waits for the host to upload firmware using the
DFU ("device firmware upgrade") USB protocol. `dfu-util`
under Linux can be used to do this upload.

Uploaded firmware is written to RAM at address 0x10000000,
then executed from there when the upload completes.

## Loading from SPI-connected flash memory

If the external switch is set to load from external flash,
then the LPC4370 will communicate with the external flash
using the SPI bus, and look for a firmware image written to
the start of external flash memory. This image should be in
the same format as the USB-uploaded image, i.e. should start
with the NXP header.

The presence of the NXP header causes the boot ROM to
copy the image from flash to RAM at address 0x10000000,
then execute from RAM. This means that the same image
can be used for both the USB and Flash paths, as both
cases end up getting loaded to the same RAM address.

If there's a communication error with the flash chip
or if there does not appear to be a valid image
present in flash, then the LPC4370 will fall back to
trying USB.

# Firmware structure

The firmware is written in C and, as you'd expect for
an embedded system, there is little-to-no support code
that is not part of the firmware itself -- there's no
real C library, etc.

## Early startup

The linker arranges for the very start of the firmware
image to contain the "vector table", defined in
`g_pfnVectors` in `cr_startup_lpc43xx.c`. This contains:

 * The initial top-of-stack address;
 * The "reset ISR" address;
 * A table of interrupt handler addresses that the M4
   core will use to dispatch interrupts when they occur

This imitates what the M4 core would expect to see
immediately after the CPU is reset. The CPU isn't
_actually_ reset, but the boot ROM imitates the reset
process when transferring control to the firmware:

 * the `sp` register is loaded with the initial top-of-
   stack address;
 * control is transferred to the code at the "reset ISR"
   address

The reset ISR does the initial very early hardware setup
necessary to get things to a predictable state:

 * Tell the LPC4370 to reset all the peripherals that
   it's safe to reset;
 * Ensure external interrupts are masked until we're
   ready to handle them;
 * Initialize the FPU;
 * Do any early initialization the C library needs;
 * Call `main()`

## Hardware initialization

Now we're in `main()`. The start of `main()` does
more hardware setup:

 * Configuring an initial low-speed CPU clock speed;
 * Configuring GPIO pins appropriately for the
   board layout we use;
 * Resetting and configuring the hardware for the
   peripherals we're going to use;
 * Initializing the USB controller and telling it to
   establish a connection to the USB host

## "Superloop" and IPC handlers

The main body of `main()` is a simple "loop forever, doing work"
loop (you may see this referred to as a "superloop" pattern). The
"work" that it does is entirely IPC-driven: it waits
for IPC messages to arrive on an IPC queue, and when a message
arrives, that triggers some work to be done in the main loop.
When the queues are empty, the main loop executes a `WFI`
("wait for interrupt") instruction which suspends execution in
a low-power mode until an interrupt occurs.

The IPC queues are simple fixed-size ring buffers that allow
messages to be passed from other code (e.g. interrupt handlers,
or even, eventually, code running on other cores) up to the main
loop for execution, without the calling code having to block to
wait for completion.

Essentially all work that is not timing-critical is dispatched
to the main loop via this mechanism. We try to keep interrupt
handlers as short as possible and delegate any work that takes
time to the main loop.

To simplify concurrency, we use one IPC queue per pair of
(sender, receiver) CPU cores, so that there is no more than
one core that can write to each of the head and tail pointers
of the queue and no cross-core memory coherency is needed,
beyond having shared RAM. (Which is lucky, because the LPC4370
doesn't give us any cross-core memory coherency!)

Currently, that means we really only use one
queue -- the M4-to-M4 queue -- but if we start using a M0
core then we would have 4 queues:

  1. M4-to-M4 (same core)
  2. M4-to-M0 (cross core)
  3. M0-to-M4 (cross core)
  4. M0-to-M0 (same core)

For cross-core queues, we raise an interrupt on the receiver
core whenever a new message is added to the queue, to ensure
that it's woken and sees the new message. This is done using
the `SEV` instruction, which will end up raising a specific
interrupt on other cores in the system (see UM10503 Fig. 4
"Multi-core connections")

For same-core queues, we don't need to raise a separate interrupt
as either:

  1. we're currently halfway through processing an IPC message
     anyway, and will see the new message when we hit the top
     of the loop; or
  2. the main loop is not currently processing an IPC message,
     so we must be in an interrupt handler, so we can just
     set a wakeup flag and the main loop will wake after
     the interrupt handler is complete and notice the flag

## Interrupt handlers

All firmware work is originally triggered by an interrupt --
generated when something external needs to notify the CPU
about something happening. For example, we will receive
interrupts when we receive USB communication, or when a
DMA transfer completes, and so on.

Because interrupt handlers often need to respond promptly
to changes in hardware state, and interrupt handlers do not
(usually) pre-empt other interrupt handlers, we need to
keep them as short as possible, to avoid delaying other
interrupts that need handling. The usual process is to:

 * Acknowledge/clear the interrupt source
 * Examine the hardware to work out what needs to be done
 * Do the minimum necessary to get the hardware going again
 * Pass off any other work to the main loop via an IPC
   message

# USB protocol

todo

## Control transfers for configuration and state inspection

todo

## Bulk endpoint for ADC data

todo

## ADC data frame structure

todo

# Flow of bulk data from ADC to USB

## ADC data capture to internal FIFO

The ADC captures 12-bit samples and records them to a 32-entry FIFO
(2 samples per word, 16 word FIFO). This capture is driven by a separate
clock; we configure the Clock Generation Unit (CGU) to generate a
suitable clock matching the sample rate we want.

The samples can then be read out of the FIFO by reading a specific
memory address implemented by the ADC.

nb: don't confuse the ADC sample clock with the ADC register
clock. The ADC register clock is a separate clock used when the rest
of the chip wants to access the ADC register addresses.

We have to make sure to empty that FIFO regularly or we will start
dropping data.

## DMA transfer from ADC FIFO to RAM

It would be hard to ensure that the CPU always managed to read from the
ADC FIFO frequently enough to avoid the FIFO overflowing. e.g. with a
20MHz ADC clock and a 200MHz CPU clock, you have to completely empty
the FIFO every 320 CPU cycles without fail, which is going to be very
hard to do while also servicing interrupts.

Instead, we use the LPC4370 DMA (Direct Memory Access) controller to transfer
data from the ADC FIFO to RAM in the background, while the CPU can be doing
other work.

The DMA controller has 8 channels. Each can be processing a separate
transfer in parallel. Each channel transfers _from_ either a peripheral or
memory, and _to_ either a peripheral or memroy. We only use one channel
currently, channel 0, configured to transfer from the ADC (peripheral)
to memory.

DMA channels have an associated linked-list of transfers that the channel
will process. Each entry has:

 * a source adddress
 * a destination address
 * length of transfer (how many bytes/words/etc to transfer)
 * some control information   
 * a pointer to the next entry of the list

The DMA controller does, esssentially, this:

 * Wait for the source to indicate it has data ready.
   In our case, we configure the ADC to tell the DMA controller
   it has data when the ADC FIFO is at least half full.
 * Read one burst-size worth of data from the source address.
   In our case, this will read half of the FIFO contents.
 * Wait for the destination to indicate it's ready for data.
   On our case, we're writing to memory, so it's always ready.
 * Write one burst-size worth of data to the destination address.
   In our case that writes the FIFO data to a memory buffer.
 * Repeat until the length-of-transfer has been reached.
 * Raise an interrupt to indicate that this transfer is complete.
 * Load the next linked-list item and continue with the next transfer

We set up the linked-"list" to write to a series of memory buffers
in order, with the last entry pointing back to the first entry to
form a loop. This means that the DMA controller will continuously
read data from the ADC as it becomes available, and write it to
a loop of memory buffers, generating an interrupt after each
buffer is filled. The CPU only needs to get involved to handle
the interrupts; most of the transfer work happens without
intervention.

## CPU packing of data into USB buffers

The ADC data that ends up in RAM stores two 12-bit samples per
32-bit word, i.e. we're wasting 25% of available space. This
becomes an issue at higher sampling rates, where the total
data rate that needs to be transferred approaches the maximum
that USB 2.0 can do (around 30MB/s)

To mitigate this, we pack the sample data before sending it
over USB, so that there are 8 12-bit samples stored in 3
32-bit words.

This packing is done by the CPU each time it is notified
(by an interrupt) that the DMA controller has finished filling
an ADC buffer. The CPU reads unpacked samples from the ADC
buffer and writes packed samples to a separate USB buffer.

Since this packing takes a relatively long time, we do that
work from the main loop, not directly from the interrupt
handler. The interrupt handler sends an IPC message with
the details of the buffer to pack, and the main loop then
packs it. Interrupts can continue to be serviced while
the packing happens.

### Detecting overruns

If the main loop can't pack data fast enough, then it's possible
that the ADC buffer we are reading unpacked samples from will
start to be re-used by the DMA controller because it's run all
the way around the looped list of ADC buffers. In this case
we run the risk of the packing process picking up the newer data,
not the older data.

We detect this by:

 * setting an in-use flag when we first hand the ADC buffer
   off to the main loop
 * clearing that flag when the main loop finishes with the
   buffer
 * when we receive an interrupt indicating that DMA for
   a ADC buffer is complete, look at the in-use flag on
   the _next_ buffer. If the next buffer is flagged as
   in-use, then we're clobbering data. Set a separate
   "has been clobbered" flag on that buffer.
 * when the main loop completes packing, it inspects
   the "has been clobbered" flag. If it got set, then
   we have to drop the buffer we just completed on the floor

Tracking overruns carefully in this way means that we can still
be sure of exactly how many samples have been converted at
a given point in time, so we can avoid unexpected timestamp
jumps that would affect multilateration timing. We still drop
data on the floor, but the host can detect that by looking at
the buffer sequence number (see below) and noticing that
one of the buffers got skipped.

## USB bulk transfer of packed data to the host

We maintain a separate set of buffers that will be used
for USB transfers. These are statically allocated during
USB initialization, and placed into a linked-list of free
buffers (the "freelist").

Buffers are removed from the freelist when the main loop needs
a new buffer to pack data into. If there are no free buffers,
then the host (or the USB bus) can't keep up with the
transfer rate and we have to drop an ADC buffer.

Once a USB buffer has been filled with packed data, we
add that buffer to a linked-list of data waiting to be
sent by the USB controller on the USB bulk IN endpoint that
we're using to transfer ADC data. There is a fiddly process
to append to this list correctly while avoiding race
conditions with the USB controller.

As the host reads data over USB, the USB controller will
send data from the linked-list of buffers. When each buffer
is consumed, the USB controller generates an interrupt.
Our interrupt handler responds to this by moving the completed
USB buffer back onto the freelist, making it available for
reuse by the main loop for later sample data.

# Using the ADC

## General configuration

There is some register setup needed, notably configuing the
ADC to:

 * store two samples per FIFO word;
 * store samples in two's complement form;
 * generate a DMA request when the FIFO is at least half full

There are also some frequency-dependent settings that need
to be configured (in short, higher frequency = more power)

There are also "descriptor tables" to populate, that tell the
ADC when and what to capture. The ADC can provide detailed control
over the timing of captures (it can, for example, interleave
several channels, or wait for trigger conditions, etc). We don't
use any of that functionality, so the descriptor tables
essentially just say "on every cycle, capture input 0,
and repeat".

## Using the CGU to generate the ADC clock

The ADC sample clock is generated by the Clock Generation Unit
(CGU). The CGU provides several phase locked loops (PLLs) and
clock dividers, and interconnections between them, the various
clock inputs, and clock outputs.

For the ADC, we use:

 * A 12MHz reference clock, from the oscillator driven by
   the 12MHz external crystal;
 * The PLL0AUDIO PLL, which scales the 12MHz reference by
   a programmable fraction;
 * Optionally, clock divider E, which divides the output
   of PLL0AUDIO by an integer divisor of up to 256.

PLL0AUDIO generates a clock signal with a frequency of
one of these forms:

```
   f = reference * 2 * M

   f = reference * 2 * M / (2 * P)
  
   f = reference / N * 2 * M

   f = reference / N * 2 * M / (2 * P)
```
  
where N, P are integers and M is either an integer, or
a fraction of the form A+(B/32768) -- we prefer the
integer case where possible.

Additionally, (reference / N * 2 * M) - the CCO frequency --
must be within a specific range, 275MHz - 550MHz.

The values for N, M, P are determined by the host, since
selecting suitable values can require some searching
through possible combinations and we'd rather not do that
on the constrained hardware of the LPCSDR itself.

For example, a configuration for a 20MHz ADC clock might be:

```
  don't use divider N, M=15, P=9, don't use divider E
  CCO frequency = 12MHz * 2 * 15 = 360MHz (in range)
  ADC frequency = PLL0AUDIO frequency = 12MHz * 2 * 15 / 18 = 20MHz
```

# Using the USB controller

todo

## USB ROM API

todo

## Interrupt-context callbacks

todo

## Responding to USB control transfers

todo


## Bulk transfer queue and dTDs

todo

# Memory map

todo


