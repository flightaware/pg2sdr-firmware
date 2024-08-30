#include "lpcsdr_tuner.h"
#include "lpcsdr_gpio.h"

#include "chip.h"

/* nb: I2C support library expects only 7-bit slave addresses, not including the trailing R/W bit */
#define R860T_I2C_ADDR 0x1A

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

bool lpcsdr_tuner_read_regs(uint8_t *regs, unsigned count, int *status)
{
    I2C_XFER_T xfer = {0};
    xfer.slaveAddr = R860T_I2C_ADDR;
    xfer.rxBuff = regs;
    xfer.rxSz = count;

    *status = Chip_I2C_MasterTransfer(I2C0, &xfer);
    if (*status != I2C_STATUS_DONE)
        return false;

    if (xfer.rxSz) {
        *status = I2C_STATUS_NAK;
        return false; // short read
    }

    // The R860T returns register values bit-reversed (because what's one more weird thing), unreverse the values
    for (unsigned i = 0; i < count; ++i) {
        regs[i] = bitreverse(regs[i]);
    }

    return true;
}
