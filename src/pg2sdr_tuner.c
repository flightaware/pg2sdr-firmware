/*
 *  pg2sdr_tuner.c - PG2 firmware, I2C and R860T tuner access
 *
 *  Copyright (c) 2026 FlightAware All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are
 *  met:
 *
 *  1. Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *  notice, this list of conditions and the following disclaimer in the
 *  documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pg2sdr_tuner.h"
#include "pg2sdr_gpio.h"
#include "pg2sdr_uart.h"
#include "pg2sdr_hardware.h"

#include "chip.h"
#include "stopwatch.h"

#include <string.h>

#if defined(HW_USES_I2C0)
# define LPC_I2C LPC_I2C0
# define LPC_I2C_CLK CLK_APB1_I2C0
#elif defined(HW_USES_I2C1)
# define LPC_I2C LPC_I2C1
# define LPC_I2C_CLK CLK_APB3_I2C1
#else
# error missing HW_USES_I2Cn define, lpc_hardware.h is broken?
#endif

/* nb: I2C support library expects only 7-bit slave addresses, not including the trailing R/W bit */
#define R860T_I2C_ADDR 0x1A

/* R860 supports 400kHz, I2C fast mode */
#define I2C_SPEED 400000

/* I2C read timeout, milliseconds */
#define I2C_READ_TIMEOUT 50
/* I2C write timeout, milliseconds */
#define I2C_WRITE_TIMEOUT 50
/* I2C bus-hang timeout (time we'll wait to send a START before forcing it), milliseconds */
#define I2C_BUSHANG_TIMEOUT 5

/* if defined, emit extra debugging for I2C controller state machine transitions */
#undef I2C_DEBUG_STATE_MACHINE

/* The R860T sometimes misbehaves on the I2C bus, usually immediately after the PLL is retuned.
 * If an I2C read/write fails, rather than immediately bailing out, retry a few times (after
 * a small delay). Most of the time, the read/write succeeds on the 2nd or 3rd attempt.
 */

/* Maximum number of retries before giving up on an I2C read/write */
#define I2C_RETRIES 4
/* Delay before each I2C retry, microseconds */
#define I2C_RETRY_DELAY_US 250

static bool rf_power = false;            /* is RF power on? */
static int i2c_error = I2C_STATUS_DONE;  /* if not DONE, this was the last I2C error we saw */
static bool shadow_is_valid = false;     /* Have we actually updated the shadow regs at all yet? */
static uint8_t reg_shadow[32];           /* Shadow copy of expected tuner reg values */

/* a special status value to indicate timeouts */
#define I2C_STATUS_TIMEOUT (I2C_STATUS_SLAVENAK + 1)

/* update LED to reflect current tuner status */
static void update_tuner_led()
{
    if (!rf_power) {
        /* RF power is off */
        pg2sdr_led_set(2, C_OFF);
        return;
    }

    if (i2c_error) {
        /* Saw an I2C error */
        pg2sdr_led_set(2, C_RED);
        return;
    }

    if ((reg_shadow[17] & 0xC0) == 0 || (reg_shadow[23] & 0xC0) == 0) {
        /* LDO is off, PLL not running / tuner not configured */
        pg2sdr_led_set(2, C_OFF);
        return;
    }

    if ((reg_shadow[2] & 0x40) == 0) {
        /* PLL configured, but no PLL lock */
        pg2sdr_led_set(2, C_YELLOW);
        return;
    }

    /* PLL is running and has lock */
    pg2sdr_led_set(2, C_GREEN);
}

static bool handle_i2c_error(int status)
{
#ifdef DEBUG
    const char *err;
    switch (status) {
    case I2C_STATUS_DONE:     err = "DONE (?!)"; break;
    case I2C_STATUS_NAK:      err = "NAK"; break;
    case I2C_STATUS_ARBLOST:  err = "ARBLOST"; break;
    case I2C_STATUS_BUSERR:   err = "BUSERR"; break;
    case I2C_STATUS_BUSY:     err = "BUSY"; break;
    case I2C_STATUS_SLAVENAK: err = "SLAVENAK"; break;
    case I2C_STATUS_TIMEOUT:  err = "TIMEOUT"; break;
    default:                  err = "(unknown)"; break;
    }

    debug_printf("tuner: I2C error! status=%u (%s)\r\n", status, err);
#endif

    i2c_error = status;
    shadow_is_valid = false;
    update_tuner_led();
    return false;
}

void pg2sdr_tuner_init(void)
{
    /* configure I2C pins */

#ifdef HW_USES_I2C0
    LPC_SCU->SFSI2C0 = I2C0_STANDARD_FAST_MODE; /* SCL_EZI | SDA_EZI */
#endif

#ifdef HW_USES_I2C1
    Chip_SCU_PinMuxSet(2, 3, SCU_MODE_FUNC1 | SCU_MODE_INBUFF_EN | SCU_MODE_ZIF_DIS);
    Chip_SCU_PinMuxSet(2, 4, SCU_MODE_FUNC1 | SCU_MODE_INBUFF_EN | SCU_MODE_ZIF_DIS);
#endif

    /* enable internal I2C clock, reset all the control state */
    Chip_Clock_Enable(LPC_I2C_CLK);
    LPC_I2C->CONCLR = I2C_I2CONCLR_AAC | I2C_I2CONCLR_SIC | I2C_I2CONCLR_STAC | I2C_I2CONCLR_I2ENC;
    pg2sdr_tuner_clock_update();

    /* we're the only master on the I2C bus, might as well just enable SCL/SDA now */
    LPC_I2C->CONSET = I2C_I2CONSET_I2EN;
}

void pg2sdr_tuner_clock_update(void)
{
    /* The I2C0 internal clock uses the APB1 clock, which in turn is driven by PLL1.
     * We configure the timing of SCL in terms of the I2C0 internal clock.
     * So we need to reconfigure whenever the PLL1 frequency changes
     * (i.e. whenever we change CPU speed)
     */
    uint32_t scl_period = Chip_Clock_GetRate(LPC_I2C_CLK) / I2C_SPEED; /* number of I2C_CLK cycles per SCL cycle */

    /* use a 50% duty cycle on SCL */
    LPC_I2C->SCLH = scl_period / 2;              /* I2C_CLK clock cycles to keep SCL high */
    LPC_I2C->SCLL = scl_period - LPC_I2C->SCLH;  /* I2C_CLK clock cycles to keep SCL low; ensure SCLH+SCLL = scl_period */
}

/* bit-reverse a single byte */
__attribute__ ((always_inline)) static inline uint8_t bitreverse(uint8_t b)
{
#if 0
    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4); /* swap 7:4/3:0 */
    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2); /* swap 7:6/5:4, 3:2/1:0 */
    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1); /* swap 7/6, 5/4, 3/2, 1/0 */
    return b;
#else
    /* ARM has an instruction for almost exactly this (but for 32-bit words, not single bytes); might as well use it */
    return __RBIT(b) >> 24;
#endif
}

/* Low-level I2C write. Write `len` bytes from `buf` to slave `slaveAddr` */
static int i2c_write(uint8_t slaveAddr, const uint8_t *buf, uint8_t len)
{
    uint32_t start_ticks = StopWatch_Start();
    uint32_t timeout_ticks = StopWatch_MsToTicks(I2C_WRITE_TIMEOUT);
    uint32_t bushang_ticks = StopWatch_MsToTicks(I2C_BUSHANG_TIMEOUT);

    /* set initial state: STA set, everything else clear -- set START condition on the I2C bus */
    LPC_I2C->CONCLR = I2C_I2CONCLR_AAC | I2C_I2CONCLR_SIC;
    LPC_I2C->CONSET = I2C_I2CONSET_STA;

    /* monitor the I2C controller state machine, feeding it data as needed, until
     * we're finished or we hit the timeout
     */
    uint8_t i = 0;
    uint8_t last_state = 0xFF; /* 0xff: waiting to send START. 0xfe: trying to un-hang bus */
    int status = I2C_STATUS_BUSY;

#ifdef I2C_DEBUG_STATE_MACHINE
    debug_printf("  ->%02X%s%s%s\r\n", last_state,
                 LPC_I2C->CONSET & I2C_I2CONSET_STA ? " STA" : "",
                 LPC_I2C->CONSET & I2C_I2CONSET_STO ? " STO" : "",
                 LPC_I2C->CONSET & I2C_I2CONSET_AA ? " AA" : "");
#endif

    while (status == I2C_STATUS_BUSY) {
        uint32_t elapsed = StopWatch_Elapsed(start_ticks);
        if (elapsed > timeout_ticks) {
            debug_printf("i2c_write: timeout (i=%u, len=%u, last=%02X, current=%02X)\r\n", i, len, last_state, LPC_I2C->STAT);
            status = I2C_STATUS_TIMEOUT;
            break;
        }

        if (last_state == 0xFF && elapsed > bushang_ticks) {
            /* Waited too long to send START. Force it out. */
            debug_printf("i2c_write: bus hang, forcing STOP\r\n");
            LPC_I2C->CONSET = I2C_I2CONSET_STA | I2C_I2CONSET_STO;
            last_state = 0xFE;
            continue;
        }

        if (!(LPC_I2C->CONSET & I2C_I2CONSET_SI)) {
            /* busy-wait until SI indicates a change in state */
            continue;
        }

        uint32_t next_state = LPC_I2C->STAT;
#ifdef I2C_DEBUG_STATE_MACHINE
        debug_printf("%02X->%02X%s%s%s\r\n", last_state, next_state,
                     LPC_I2C->CONSET & I2C_I2CONSET_STA ? " STA" : "",
                     LPC_I2C->CONSET & I2C_I2CONSET_STO ? " STO" : "",
                     LPC_I2C->CONSET & I2C_I2CONSET_AA ? " AA" : "");
#endif

        switch (next_state) {
        case 0x08: /* START transmitted */
        case 0x10: /* Repeated START (sometimes this happens after retrying a BUSERR) */
            /* transmit slave address, wait for ACK/NAK */
            LPC_I2C->DAT = (slaveAddr << 1) | 0; /* R/W bit = 0 = write */
            LPC_I2C->CONCLR = I2C_I2CONCLR_STAC | I2C_I2CONCLR_SIC; /* clear START, tell the controller it can continue */
            break;

        case 0x18: /* SLA+W transmitted, ACK received */
        case 0x28: /* data byte transmitted, ACK received */
            if (i == len) {
                /* done transmitting */
                status = I2C_STATUS_DONE;
            } else {
                /* transmit next byte */
                LPC_I2C->DAT = buf[i++];
                LPC_I2C->CONCLR = I2C_I2CONCLR_SIC; /* tell the controller it can continue */
            }
            break;

        case 0x20: /* SLA+W transmited, NAK received (no such slave on the bus) */
            debug_printf("i2c_write: address NAK\r\n");
            status = I2C_STATUS_SLAVENAK;
            break;

        case 0x30: /* data byte transmited, NAK received (slave refused the data byte) */
            debug_printf("i2c_write: data NAK, last=%02X i=%u len=%u\r\n", last_state, i, len);
            status = I2C_STATUS_NAK;
            break;

        case 0x38: /* arbitration lost (should never happen) */
            debug_printf("i2c_write: arbitration lost, last=%02X i=%u len=%u\r\n", last_state, i, len);
            status = I2C_STATUS_ARBLOST;
            break;

        case 0x00: /* bus error */
            debug_printf("i2c_write: BUSERR, last=%02X i=%u len=%u\r\n", last_state, i, len);
            status = I2C_STATUS_BUSERR;
            break;

        default:   /* anything else */
            debug_printf("i2c_write: unexpected state %02X, last=%02X i=%u len=%u\r\n", next_state, last_state, i, len);
            status = I2C_STATUS_BUSERR;
            break;
        }

        last_state = next_state;
    }

    /* set STOP condition to release the bus */
    LPC_I2C->CONCLR = I2C_I2CONCLR_STAC | I2C_I2CONCLR_AAC;
    LPC_I2C->CONSET = I2C_I2CONSET_STO;
#ifdef I2C_DEBUG_STATE_MACHINE
    debug_printf("%02X->  %s%s%s\r\n", last_state,
                 LPC_I2C->CONSET & I2C_I2CONSET_STA ? " STA" : "",
                 LPC_I2C->CONSET & I2C_I2CONSET_STO ? " STO" : "",
                 LPC_I2C->CONSET & I2C_I2CONSET_AA ? " AA" : "");
    }
#endif
    LPC_I2C->CONCLR = I2C_I2CONCLR_SIC;

    return status;
}

/* Low-level I2C read. Read `len` bytes into `buf` from slave `slaveAddr` */
static int i2c_read(uint8_t slaveAddr, uint8_t *buf, uint8_t len)
{
    uint32_t start_ticks = StopWatch_Start();
    uint32_t timeout_ticks = StopWatch_MsToTicks(I2C_READ_TIMEOUT);
    uint32_t bushang_ticks = StopWatch_MsToTicks(I2C_BUSHANG_TIMEOUT);

    /* set initial state: STA set, everything else clear -- set START condition on the I2C bus */
    LPC_I2C->CONCLR = I2C_I2CONCLR_AAC | I2C_I2CONCLR_SIC;
    LPC_I2C->CONSET = I2C_I2CONSET_STA;

    /* monitor the I2C controller state machine, reading data as needed, until
     * we're finished or we hit the timeout
     */
    uint8_t i = 0;
    uint8_t last_state = 0xFF; /* 0xff: waiting to send START. 0xfe: trying to un-hang bus */
    int status = I2C_STATUS_BUSY;

#ifdef I2C_DEBUG_STATE_MACHINE
    debug_printf("  ->%02X%s%s%s\r\n", last_state,
                 LPC_I2C->CONSET & I2C_I2CONSET_STA ? " STA" : "",
                 LPC_I2C->CONSET & I2C_I2CONSET_STO ? " STO" : "",
                 LPC_I2C->CONSET & I2C_I2CONSET_AA ? " AA" : "");
#endif

    while (status == I2C_STATUS_BUSY) {
        uint32_t elapsed = StopWatch_Elapsed(start_ticks);
        if (elapsed > timeout_ticks) {
            debug_printf("i2c_read: timeout (i=%u, len=%u, last=%02X, current=%02X)\r\n", i, len, last_state, LPC_I2C->STAT);
            status = I2C_STATUS_TIMEOUT;
            break;
        }

        if (last_state == 0xFF && elapsed > bushang_ticks) {
            /* Waited too long to send START. Force it out. */
            debug_printf("i2c_read: bus hang, forcing STOP\r\n");
            LPC_I2C->CONSET = I2C_I2CONSET_STA | I2C_I2CONSET_STO;
            last_state = 0xFE;
            continue;
        }

        if (!(LPC_I2C->CONSET & I2C_I2CONSET_SI)) {
            /* busy-wait until SI indicates a change in state */
            continue;
        }

        uint32_t next_state = LPC_I2C->STAT;
#ifdef I2C_DEBUG_STATE_MACHINE
        debug_printf("%02X->%02X%s%s%s\r\n", last_state, next_state,
                     LPC_I2C->CONSET & I2C_I2CONSET_STA ? " STA" : "",
                     LPC_I2C->CONSET & I2C_I2CONSET_STO ? " STO" : "",
                     LPC_I2C->CONSET & I2C_I2CONSET_AA ? " AA" : "");
#endif

        switch (next_state) {
        case 0x08: /* START transmitted */
        case 0x10: /* Repeated START (sometimes this happens after retrying a BUSERR) */
            /* transmit slave address, wait for ACK/NAK */
            LPC_I2C->DAT = (slaveAddr << 1) | 1; /* R/W bit = 1 = read */
            LPC_I2C->CONCLR = I2C_I2CONCLR_STAC | I2C_I2CONCLR_SIC; /* clear START, tell the controller it can continue */
            break;

        case 0x40: /* SLA+R transmitted, ACK received */
            /* continue to receive first byte */
            if (len > 1) {
                LPC_I2C->CONSET = I2C_I2CONSET_AA; /* ACK next byte */
            } else {
                LPC_I2C->CONCLR = I2C_I2CONCLR_AAC; /* NAK next byte */
            }
            LPC_I2C->CONCLR = I2C_I2CONCLR_SIC; /* tell the controller it can continue */
            break;

        case 0x48: /* SLA+R transmitted, NAK returned (no such slave on the bus) */
            debug_printf("i2c_read: address NAK\r\n");
            status = I2C_STATUS_SLAVENAK;
            break;

        case 0x50:  /* data byte received, ACK returned */
            if (i >= len-1) {
                /* wat? */
                debug_printf("i2c_read: unexpected state 50, last=%02X i=%u len=%u\r\n", last_state, i, len);
                status = I2C_STATUS_BUSERR;
                break;
            }

            buf[i++] = LPC_I2C->DAT; /* receive pending byte */
            if (i < len-1) {
                LPC_I2C->CONSET = I2C_I2CONSET_AA; /* ACK next byte */
            } else {
                LPC_I2C->CONCLR = I2C_I2CONCLR_AAC; /* NAK next byte */
            }
            LPC_I2C->CONCLR = I2C_I2CONCLR_SIC; /* tell the controller we've read the data and it can continue */
            break;

        case 0x58: /* data byte received, NAK returned */
            /* this should be the final byte */
            if (i != len-1) {
                /* wat? */
                debug_printf("i2c_read: unexpected state 58, last=%02X i=%u len=%u\r\n", last_state, i, len);
                status = I2C_STATUS_BUSERR;
                break;
            }
            buf[i++] = LPC_I2C->DAT; /* receive final byte */
            status = I2C_STATUS_DONE;
            break;

        case 0x38: /* arbitration lost (should never happen) */
            debug_printf("i2c_write: arbitration lost, last=%02X i=%u len=%u\r\n", last_state, i, len);
            status = I2C_STATUS_ARBLOST;
            break;

        case 0x00: /* bus error */
            debug_printf("i2c_read: BUSERR, last=%02X i=%u len=%u\r\n", last_state, i, len);
            status = I2C_STATUS_BUSERR;
            break;

        default:   /* anything else */
            debug_printf("i2c_read: unexpected state %02X, last=%02X i=%u len=%u\r\n", next_state, last_state, i, len);
            status = I2C_STATUS_BUSERR;
            break;
        }

        last_state = next_state;
    }

    /* set STOP condition to release the bus */
    LPC_I2C->CONCLR = I2C_I2CONCLR_STAC | I2C_I2CONCLR_AAC;
    LPC_I2C->CONSET = I2C_I2CONSET_STO;
#ifdef I2C_DEBUG_STATE_MACHINE
    debug_printf("%02X->  %s%s%s\r\n", last_state,
                 LPC_I2C->CONSET & I2C_I2CONSET_STA ? " STA" : "",
                 LPC_I2C->CONSET & I2C_I2CONSET_STO ? " STO" : "",
                 LPC_I2C->CONSET & I2C_I2CONSET_AA ? " AA" : "");
    }
#endif
    LPC_I2C->CONCLR = I2C_I2CONCLR_SIC;
    return status;
}


// read registers 0 .. count-1 into regs[0] .. regs[count-1]
bool pg2sdr_tuner_read_regs_direct(uint8_t *regs, unsigned count)
{
    if (!rf_power) {
        return false;
    }

    if (!count) {
        return true;
    }

    int status;
    for (uint32_t retry = 0; retry < I2C_RETRIES; ++retry) {
        if (retry) {
            StopWatch_DelayUs(I2C_RETRY_DELAY_US);
            debug_printf("tuner: retry failed I2C read (#%u)\r\n", retry);
        }
        status = i2c_read(R860T_I2C_ADDR, regs, count);
        if (status != I2C_STATUS_DONE)
            continue;

        // The R860T returns register values bit-reversed (because what's one more weird thing), unreverse the values
        for (unsigned i = 0; i < count; ++i) {
            regs[i] = bitreverse(regs[i]);
        }

        return true;
    }

    return handle_i2c_error(status);
}

// write registers first .. first+count-1 using values from regs[0] .. regs[count-1]
bool pg2sdr_tuner_write_regs_direct(unsigned first, const uint8_t *regs, unsigned count)
{
    if (!rf_power) {
        return false;
    }

    if (!count) {
        return true;
    }

    if (first < 5 || count > 27) {
        return false;
    }

    // build the write command, first byte is the start reg
    // In theory we could do scatter-gather if we re-implemented the tx state machine,
    // but it doesn't seem worth doing that, so just use a temporary buffer
    // sized for the largest possible write (regs 5 .. 31 inclusive)
    uint8_t buf[28];
    buf[0] = first;
    memcpy(buf + 1, regs, count);

    int status;
    for (uint32_t retry = 0; retry < I2C_RETRIES; ++retry) {
        if (retry) {
            StopWatch_DelayUs(I2C_RETRY_DELAY_US);
            debug_printf("tuner: retry failed I2C write (#%u)\r\n", retry);
        }

        status = i2c_write(R860T_I2C_ADDR, buf, count+1);
        if (status != I2C_STATUS_DONE)
            continue;

        return true;
    }

    return handle_i2c_error(status);
}

// Read one register, directly for R0..R4 or from our shadow copy for others
bool pg2sdr_tuner_read_reg(unsigned index, uint8_t *value)
{
    if (index >= 32) {
        /* out of range */
        return false;
    }

    if (index < 5) {
        // Reading a volatile / read-only register, refresh from the chip every time
        if (!pg2sdr_tuner_read_regs_direct(reg_shadow, index + 1))
            return false;
        update_tuner_led();
    }

    *value = reg_shadow[index];
    return true;
}

// Read many registers, minimizing actual chip access
bool pg2sdr_tuner_read_regs(unsigned first, uint8_t *regs, unsigned count)
{
    if (count > 32 || first + count > 32) {
        /* out of range */
        return false;
    }

    if (!count) {
        /* no work to do */
        return true;
    }

    if (first + count > 5 && !shadow_is_valid) {
        // We want data from non-volatile regs, but the shadow cache isn't valid,
        // reload the entire shadow cache from the tuner
        if (!pg2sdr_tuner_shadow_from_chip())
            return false;
        update_tuner_led();
    } else if (first < 5) {
        // We want data from volatile regs, load only those from the tuner
        if (!pg2sdr_tuner_read_regs_direct(reg_shadow, (first + count < 5) ? (first + count) : 5))
            return false;
        update_tuner_led();
    }

    // at this point, reg_shadow is up to date, so just copy from there
    memcpy(regs, reg_shadow + first, count);
    return true;
}

// Force a refresh of our shadow registers, reading actual values from the chip.
bool pg2sdr_tuner_shadow_from_chip()
{
    if (!pg2sdr_tuner_read_regs_direct(reg_shadow, 32)) {
        shadow_is_valid = false;
        return false;
    }

    shadow_is_valid = true;
    i2c_error = I2C_STATUS_DONE; /* clear previous errors when we successfully refresh tuner state */
    update_tuner_led();
    return true;
}

// Write many registers to the chip, writing through the shadow regs
bool pg2sdr_tuner_write_regs(unsigned offset, const uint8_t *regs, unsigned count)
{
    if (offset < 5 || offset >= 32 || count > 27 || offset + count > 32) {
        /* out of range */
        return false;
    }

    if (!pg2sdr_tuner_write_regs_direct(offset, regs, count)) {
        return false;
    }
    memcpy(&reg_shadow[offset], regs, count);
    update_tuner_led();
    return true;
}

// Write many register bits, writing through the shadow regs
// Registers from `offset` .. `offset+count-1` are (possibly) updated.
// New bit values are taken from `bits[0]` .. `bits[count-1]`
// Only bits that have a corresponding bit set in `mask[0]` .. `mask[count-1]` are modified,
// other bits are left unchanged.
bool pg2sdr_tuner_update_regs(unsigned offset, const uint8_t *bits, const uint8_t *mask, unsigned count)
{
    if (offset >= 32 || offset + count > 32) {
        /* out of range */
        return false;
    }

    /* ensure reg_shadow is valid */
    if (!shadow_is_valid && !pg2sdr_tuner_shadow_from_chip()) {
        return false;
    }

    // Apply changes directly to reg_shadow and write through to the tuner
    const unsigned NO_UPDATE = 64;
    unsigned first_update = NO_UPDATE;
    unsigned last_update = NO_UPDATE;
    for (unsigned i = 0; i < count; ++i) {
        unsigned reg_index = offset + i;
        uint8_t updated = (reg_shadow[reg_index] & ~mask[i]) | (bits[i] & mask[i]);
        if (reg_shadow[reg_index] != updated) {
            if (first_update == NO_UPDATE)
                first_update = reg_index;
            last_update = reg_index;
            reg_shadow[reg_index] = updated;
        }

        if (last_update != NO_UPDATE && (reg_index - last_update) >= 3) {
            // Sufficiently large gap with no changed registers,
            // do an incremental write as two smaller writes will
            // be faster than a single large write
            if (!pg2sdr_tuner_write_regs_direct(first_update, &reg_shadow[first_update], last_update - first_update + 1)) {
                return false;
            }
            first_update = last_update = NO_UPDATE;
        }
    }

    if (first_update != NO_UPDATE) {
        // Do a final write
        if (!pg2sdr_tuner_write_regs_direct(first_update, &reg_shadow[first_update], last_update - first_update + 1))
            return false;
    }

    update_tuner_led();
    return true;
}

// We just turned off the RF power, do anything we need to do in response
void pg2sdr_tuner_handle_poweroff()
{
    i2c_error = false;
    shadow_is_valid = false;
    rf_power = false;
    update_tuner_led();
}

// We just turned on the RF power, do anything we need to do in response
void pg2sdr_tuner_handle_poweron()
{
    i2c_error = false;
    shadow_is_valid = false;
    rf_power = true;
    update_tuner_led();

    StopWatch_DelayMs(5); // Give the tuner a moment to reset
}

/* Fill in the tuner-related bits of *status */
void pg2sdr_tuner_status(ep0_in_board_status_t *status)
{
    if (i2c_error)
        status->flags |= STATUS_TUNER_I2C_ERROR;
    if (shadow_is_valid) {
        memcpy(status->tuner_regs, reg_shadow, 32);
        if (reg_shadow[2] & 0x40)
            status->flags |= STATUS_TUNER_PLL_LOCK;
    }
    status->tuner_xtal = HW_TUNER_XTAL;
}

/* Update vco_current and wait for PLL lock
 * Returns:
 *    1 - PLL now has lock
 *    0 - PLL did not lock within timeout
 *   <0 - error communicating with the tuner
 */
int pg2sdr_tuner_lock(uint8_t vco_current, uint32_t timeout)
{
#ifdef DEBUG
    uint32_t start_ticks = StopWatch_Start();
#endif

    /* update vco_current (reg 18, bits 7..5) to the requested vco_current
     *    and vco_mode (reg 19, bit 6) to auto-mode
     * (if necessary)
     */
    uint8_t bits[2] = { (vco_current << 5) & 0xE0, 0x00 };
    uint8_t mask[2] = { 0xE0, 0x40 };
    if (!pg2sdr_tuner_update_regs(18, bits, mask, 2))
        return -1;

    /* poll the tuner, waiting for lock */
    uint32_t timeout_ticks = StopWatch_MsToTicks(timeout);
    uint32_t post_setup_ticks = StopWatch_Start();
    uint32_t loops = 0;
    do {
        ++loops;
        /* read reg 2 for PLL lock status */
        if (!pg2sdr_tuner_read_regs_direct(reg_shadow, 3))
            return -1;
        if (reg_shadow[2] & 0x40) {
#ifdef DEBUG
            uint32_t post_lock_ticks = StopWatch_Start();
            debug_printf("tuner: PLL lock with vco_current=%u in %u+%u us (%u polls)\r\n",
                         vco_current,
                         StopWatch_TicksToUs(post_setup_ticks - start_ticks),
                         StopWatch_TicksToUs(post_lock_ticks - post_setup_ticks),
                         loops);
#endif
            return 1; /* PLL has lock */
        }
    } while (StopWatch_Elapsed(post_setup_ticks) < timeout_ticks);

    /* timeout */
    debug_printf("tuner: PLL lock with vco_current=%u timed out\r\n", vco_current);
    return 0;
}
