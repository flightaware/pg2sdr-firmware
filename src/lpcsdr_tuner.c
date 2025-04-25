#include "lpcsdr_tuner.h"
#include "lpcsdr_gpio.h"

#include "chip.h"
#include "stopwatch.h"

#include <string.h>

/* nb: I2C support library expects only 7-bit slave addresses, not including the trailing R/W bit */
#define R860T_I2C_ADDR 0x1A

static bool rf_power = false;            /* is RF power on? */
static int i2c_error = I2C_STATUS_DONE;  /* if not DONE, this was the last I2C error we saw */
static bool shadow_is_valid = false;     /* Have we actually updated the shadow regs at all yet? */
static uint8_t reg_shadow[32];           /* Shadow copy of expected tuner reg values */

static bool handle_i2c_error(int status)
{
    i2c_error = status;
    shadow_is_valid = false;
    return false;
}

void lpcsdr_tuner_init(void)
{
    Chip_SCU_I2C0PinConfig(I2C0_STANDARD_FAST_MODE);
    Chip_I2C_Init(I2C0);
    Chip_I2C_SetMasterEventHandler(I2C0, Chip_I2C_EventHandlerPolling); // Use polling mode. todo: look into interrupt-driven mode
    lpcsdr_i2c_clock_update();
}

void lpcsdr_i2c_clock_update(void)
{
    Chip_I2C_SetClockRate(I2C0, 100000);
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

// read registers 0 .. count-1 into regs[0] .. regs[count-1]
bool lpcsdr_tuner_read_regs_direct(uint8_t *regs, unsigned count)
{
    if (!rf_power) {
        return false;
    }

    if (!count) {
        return true;
    }

    I2C_XFER_T xfer = {0};
    xfer.slaveAddr = R860T_I2C_ADDR;
    xfer.rxBuff = regs;
    xfer.rxSz = count;

    int status = Chip_I2C_MasterTransfer(I2C0, &xfer);
    if (status != I2C_STATUS_DONE) {
        return handle_i2c_error(status);
    }

    if (xfer.rxSz) {
        /* short read */
        return handle_i2c_error(I2C_STATUS_NAK);
    }

    // The R860T returns register values bit-reversed (because what's one more weird thing), unreverse the values
    for (unsigned i = 0; i < count; ++i) {
        regs[i] = bitreverse(regs[i]);
    }

    return true;
}

// write registers first .. first+count-1 using values from regs[0] .. regs[count-1]
bool lpcsdr_tuner_write_regs_direct(unsigned first, const uint8_t *regs, unsigned count)
{
    if (!rf_power) {
        return false;
    }

    if (!count) {
        return true;
    }

    // build the write command, first byte is the start reg
    // In theory we could do scatter-gather if we re-implemented the tx state machine,
    // but it doesn't seem worth doing that, so just use a temporary buffer
    // sized for the largest possible write (regs 5 .. 31 inclusive)
    if (first < 5 || count > 27) {
        return false;
    }

    uint8_t buf[28];
    buf[0] = first;
    memcpy(buf + 1, regs, count);

    I2C_XFER_T xfer = {0};
    xfer.slaveAddr = R860T_I2C_ADDR;
    xfer.txBuff = buf;
    xfer.txSz = count + 1;

    int status = Chip_I2C_MasterTransfer(I2C0, &xfer);
    if (status != I2C_STATUS_DONE) {
        return handle_i2c_error(status);
    }

    if (xfer.txSz) {
        /* short write */
        return handle_i2c_error(I2C_STATUS_NAK);
    }

    return true;
}

// Read one register, directly for R0..R4 or from our shadow copy for others
bool lpcsdr_tuner_read_reg(unsigned index, uint8_t *value)
{
    if (index >= 32) {
        /* out of range */
        return false;
    }

    if (index < 5) {
        // Reading a volatile / read-only register, refresh from the chip every time
        if (!lpcsdr_tuner_read_regs_direct(reg_shadow, index + 1))
            return false;
    }

    *value = reg_shadow[index];
    return true;
}

// Read many registers, minimizing actual chip access
bool lpcsdr_tuner_read_regs(unsigned first, uint8_t *regs, unsigned count)
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
        if (!lpcsdr_tuner_shadow_from_chip())
            return false;
    } else if (first < 5) {
        // We want data from volatile regs, load only those from the tuner
        if (!lpcsdr_tuner_read_regs_direct(reg_shadow, (first + count < 5) ? (first + count) : 5))
            return false;
    }

    // at this point, reg_shadow is up to date, so just copy from there
    memcpy(regs, reg_shadow + first, count);
    return true;
}

// Force a refresh of our shadow registers, reading actual values from the chip.
bool lpcsdr_tuner_shadow_from_chip()
{
    if (!lpcsdr_tuner_read_regs_direct(reg_shadow, 32)) {
        shadow_is_valid = false;
        return false;
    }

    shadow_is_valid = true;
    i2c_error = I2C_STATUS_DONE; /* clear previous errors when we successfully refresh tuner state */
    return true;
}

// Write many registers to the chip, writing through the shadow regs
bool lpcsdr_tuner_write_regs(unsigned offset, const uint8_t *regs, unsigned count)
{
    if (offset < 5 || offset >= 32 || count > 27 || offset + count > 32) {
        /* out of range */
        return false;
    }

    if (!lpcsdr_tuner_write_regs_direct(offset, regs, count)) {
        return false;
    }
    memcpy(&reg_shadow[offset], regs, count);
    return true;
}

// Write many register bits, writing through the shadow regs
// Registers from `offset` .. `offset+count-1` are (possibly) updated.
// New bit values are taken from `bits[0]` .. `bits[count-1]`
// Only bits that have a corresponding bit set in `mask[0]` .. `mask[count-1]` are modified,
// other bits are left unchanged.
bool lpcsdr_tuner_update_regs(unsigned offset, const uint8_t *bits, const uint8_t *mask, unsigned count)
{
    if (offset >= 32 || offset + count > 32) {
        /* out of range */
        return false;
    }

    /* ensure reg_shadow is valid */
    if (!shadow_is_valid && !lpcsdr_tuner_shadow_from_chip()) {
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
            if (!lpcsdr_tuner_write_regs_direct(first_update, &reg_shadow[first_update], last_update - first_update + 1)) {
                return false;
            }
            first_update = last_update = NO_UPDATE;
        }
    }

    if (first_update != NO_UPDATE) {
        // Do a final write
        if (!lpcsdr_tuner_write_regs_direct(first_update, &reg_shadow[first_update], last_update - first_update + 1))
            return false;
    }

    return true;
}

// We just turned off the RF power, do anything we need to do in response
void lpcsdr_tuner_handle_poweroff()
{
    i2c_error = false;
    shadow_is_valid = false;
    rf_power = false;
}

// We just turned on the RF power, do anything we need to do in response
void lpcsdr_tuner_handle_poweron()
{
    i2c_error = false;
    shadow_is_valid = false;
    rf_power = true;

    StopWatch_DelayMs(5); // Give the tuner a moment to reset
}

/* Fill in the tuner-related bits of *status */
void lpcsdr_tuner_status(ep0_in_board_status_t *status)
{
    if (shadow_is_valid)
        memcpy(status->tuner_regs, reg_shadow, 32);
}
