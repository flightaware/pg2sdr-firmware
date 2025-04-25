# Things to test

Some notes for Tren, tests that would be useful feedback for me:

See SCRIPTS.md for the mechanics of how to use the scripts I refer to here.

## Power/heat

You can control power to different bits of the LPCSDR via a few of the scripts:

 * set-rf-power.py for coarse-grained control over the RF power LDO
 * start-transfer.py / stop-transfer.py will let you control whether the ADC
   is running (and the CPU is in high-speed mode)
 * tuner.py will let you initialize the tuner, and control the PLL & gain

Cases that would be useful to look at:

 * Firmware loaded, nothing else turned on
 * RF power off, ADC/DMA running at 9.6MHz and 20MHz
 * RF power on, tuner not initialized
 * RF power on, tuner configured with a ~ 1090MHz LO and max gain settings
 * RF power on, tuner configured as above, ADC/DMA running at 9.6MHz at 20MHz

How hot are the hotspots?

(on my board, with everything on, both the LPC chip and the SMA connector
get too hot to touch)


## ADC clock

Configure the ADC clock with different frequencies and observe the output on
CLK0/CLK2 to see if it's reasonable.

```
 ./python/start-transfer.py 9.6
Calculated settings: INTEGER     N=  0 M=       12 P=15 I=  0  fCCO=288.000000MHz fOut= 9.600000MHz error=0.0Hz

Starting ADC/DMA ..
.. done.
```

These frequencies would be useful to test:

 * 9.6MHz        (2.4MHz * 4, probably what we will start with for dump1090)
 * 19.2MHz       (2.4MHz * 8)
 * 8.333333MHz   (UAT bitrate * 8)
 * 16.666667MHz  (UAT bitrate * 16)
 * 6, 12, 20MHz  (airspy mini supported sampling rates, is there something special that made them choose only these?)

and look at:

 1) is the output frequency correct?
 2) how does the frequency domain look? how bad is the phase noise around the target frequency? any spurs?


# Tuner PLL

Configure the tuner LO somewhere near 1090MHz

```
./python/tuner.py --pll 1092.4
```

Feed in a signal at 1090MHz from a signal generator

Put a probe on the connection from the R860T to the LPC ADC inputs and see if you can find the appropriate IF signal.
In this case it is _probably_ at 2.4MHz, but I'm not sure of which sideband the tuner is set up to suppress. You might
need to tune the tuner LO below 1090MHz (i.e. 1090 - 2.4 = 1087.6MHz) to get a useful IF signal.

You will probably also need to play with the tuner gain settings (use `tuner.py --lna-gain` etc) to get a useful result here.

If you can't find an IF signal, maybe try scanning around with the frequency generator and see if you can work out where the tuner LO has actually been set to? It's quite possible that my code is doing the wrong thing and misconfiguring it, it's just hard to tell without being able to look at the IF signal.
