/*
 *  pg2sdr_spifi.c - PG2 firmware, flash EEPROM access via SPIFI
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

#include "pg2sdr_spifi.h"

#include "chip.h"
#include "pg2sdr_usb.h"
#include "stopwatch.h"

/* generic SPI read, no polling.
 *
 * `command` should contain opcode / frameform / fieldform / intermediate byte count
 * `address` is the address to send (if the frameform indicates an address is used)
 * `length` bytes will be read into `buffer`.
 */
static void spifi_generic_read(uint32_t command, uint32_t address, uint8_t *buffer, uint32_t length)
{
    LPC_SPIFI->ADDR = address;
    LPC_SPIFI->CMD = command |
        SPIFI_CMD_DOUT(0) |                                // read data from SPI
        SPIFI_CMD_POLLRS(0) |                              // don't poll
        SPIFI_CMD_DATALEN(length);                         // read 'length' bytes

    for (uint32_t i = 0; i < length; ++i) {
        *buffer++ = LPC_SPIFI->DAT8;
    }

    spifi_HW_WaitCMD(LPC_SPIFI);
}

/* generic SPI write, wait for SPIFI controller to flush data
 *
 * `command` should contain opcode / frameform / fieldform / intermediate byte count
 * `address` is the address to send (if the frameform indicates an address is used)
 * `length` bytes will be written from `buffer`.
 */
static void spifi_generic_write(uint32_t command, uint32_t address, const uint8_t *buffer, uint32_t length)
{
    LPC_SPIFI->ADDR = address;
    LPC_SPIFI->CMD = command |
        SPIFI_CMD_DOUT(1) |                                // write data to SPI
        SPIFI_CMD_POLLRS(0) |                              // don't poll
        SPIFI_CMD_DATALEN(length);                         // write 'length' bytes

    for (uint32_t i = 0; i < length; ++i) {
        LPC_SPIFI->DAT8 = *buffer++;
    }

    spifi_HW_WaitCMD(LPC_SPIFI);
}

/* Read manufacturer and device ID (2 bytes) */
uint16_t pg2sdr_spifi_read_manufacturer_device_id()
{
    uint16_t id;
    spifi_generic_read(SPIFI_CMD_OPCODE(0x90) |                           // Read Manufacturer / Device ID (90h)
                       SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP_3ADDRESS) | // opcode and 3 (zeroed) address bytes
                       SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // serial opcode, address, data
                       SPIFI_CMD_INTER(0),                                // no intermediate bytes
                       0,                                                 // zero address
                       (uint8_t*) &id, 2);
    return id;
}

/* Read unique chip ID (8 bytes) */
uint64_t pg2sdr_spifi_read_unique_id()
{
    uint64_t id;
    spifi_generic_read(SPIFI_CMD_OPCODE(0x4B) |                           // Read Unique ID Number (4Bh)
                       SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP_4ADDRESS) | // opcode and 4 (zeroed) address bytes
                       SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // serial opcode, address, data
                       SPIFI_CMD_INTER(0),                                // No intermediate bytes
                       0,                                                 // zero address
                       (uint8_t *)&id, 8);
    return id;
}

/* Read `length` bytes at `address` into `buffer` */
void pg2sdr_spifi_read_data(uint32_t address, uint8_t *buffer, uint32_t length)
{
    spifi_generic_read(SPIFI_CMD_OPCODE(0x03) |                           // Read Data (03h)
                       SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP_3ADDRESS) | // Opcode and 3 address bytes
                       SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // serial opcode, address, data
                       SPIFI_CMD_INTER(0),                                // no intermediate bytes
                       address,                                           // start address
                       buffer, length);
}

/* Read `length` bytes at `address` into `buffer`, using quad I/O */
void pg2sdr_spifi_fast_read_quad(uint32_t address, uint8_t *buffer, uint32_t length)
{
    spifi_generic_read(SPIFI_CMD_OPCODE(0xEB) |                              // Fast Read Quad I/O (EBh)
                       SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP_4ADDRESS) |    // Opcode, 3 address bytes, M7:0 = FF
                       SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_SERIAL_OPCODE) |  // serial opcode, quad data and address
                       SPIFI_CMD_INTER(2),                                   // 2 dummy bytes
                       (address << 8) | 0xFF,                                // insert M7:0 = FF into the LSB of address
                       buffer, length);
}

/* Busy-wait for up to `timeout_ms` waiting for the busy bit in status register-1 to clear */
static ErrorCode_t spifi_wait_for_completion(uint32_t timeout_ms)
{

    LPC_SPIFI->ADDR = 0;
    LPC_SPIFI->CMD = SPIFI_CMD_OPCODE(0x05) |                           // Read Status Register-1 (05h)
                     SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP) |          // Opcode only
                     SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // Serial opcode
                     SPIFI_CMD_INTER(0) |                               // No intermediate bytes
                     SPIFI_CMD_DOUT(0) |                                // read data from SPI
                     SPIFI_CMD_POLLRS(1) |                              // poll for given byte pattern
                     SPIFI_CMD_DATALEN(0);                              // wait for bit 0 = 0

    uint32_t start_ticks = StopWatch_Start();
    uint32_t timeout_ticks = StopWatch_MsToTicks(timeout_ms);
    while ((LPC_SPIFI->STAT & SPIFI_STAT_CMD) != 0 && StopWatch_Elapsed(start_ticks) <= timeout_ticks) {
        /* busy-wait */
        __NOP();
    }

    if ((LPC_SPIFI->STAT & SPIFI_STAT_CMD) != 0) {
        // Give up, cancel the polling.
        spifi_HW_ResetController(LPC_SPIFI);
        return ERR_TIME_OUT;
    }

    (void) LPC_SPIFI->DAT8; // consume the matching byte that was delivered to the FIFO
    return LPC_OK;
}

/* Program `length` bytes of data, from `buffer`, to previously erased flash starting at `address`.
 * All data is written to the same flash page - if data would cross a page boundary,
 * the write will wrap back to the start of the page. (So you probably want a page-aligned
 * address, and length <= 256).
 *
 * Programming a page can take up to 3ms.
 */
ErrorCode_t pg2sdr_spifi_page_program(uint32_t address, const uint8_t *buffer, uint32_t length)
{
    if (address > 0x00FFFFFF || (address + length) > 0x01000000)
        return ERR_FAILED;
    if (length > 256)
        return ERR_FAILED;

    spifi_generic_write(SPIFI_CMD_OPCODE(0x06) |                           // Write Enable (06h)
                        SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP) |          // Opcode only
                        SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // Serial opcode
                        SPIFI_CMD_INTER(0),                                // No intermediate bytes
                        0,                                                 // No address
                        NULL, 0);                                          // No additional data

    spifi_generic_write(SPIFI_CMD_OPCODE(0x02) |                           // Page Program (02h)
                        SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP_3ADDRESS) | // Opcode and 3 address bytes
                        SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // serial opcode, address, and data
                        SPIFI_CMD_INTER(0),                                // No intermediate bytes
                        address,                                           // Start address
                        buffer, length);                                   // Data to write

    return spifi_wait_for_completion(3); // datasheet max page programming time = 3ms
}

/* Erase a 4k flash sector starting at `address`, which must be 4k-aligned.
 *
 * Erased sectors are filled with 0xff.
 *
 * Erasing a sector can take up to 300ms.
 */
ErrorCode_t pg2sdr_spifi_sector_erase(uint32_t address)
{
    // sectors are 4k aligned
    if (address & 0x0FFF)
        return ERR_FAILED;

    spifi_generic_write(SPIFI_CMD_OPCODE(0x06) |                           // Write Enable (06h)
                        SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP) |          // Opcode only
                        SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // Serial opcode
                        SPIFI_CMD_INTER(0),                                // No intermediate bytes
                        0,                                                 // No address
                        NULL, 0);                                          // No additional data

    spifi_generic_write(SPIFI_CMD_OPCODE(0x20) |                           // Sector Erase (20h)
                        SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP_3ADDRESS) | // Opcode and 3 address bytes
                        SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // serial opcode and address
                        SPIFI_CMD_INTER(0),                                // No intermediate bytes
                        address,                                           // Sector to erase
                        NULL, 0);                                          // No additional data

    return spifi_wait_for_completion(300); // datasheet max sector erase time = 300ms
}

ErrorCode_t pg2sdr_spifi_init(void)
{
    // warning: this assumes we are _not_ executing code over SPIFI!

    // set up SPIFI pins
    static const PINMUX_GRP_T pinmux[] = {
            { .pingrp = 3, .pinnum=3, .modefunc = SCU_PINIO_FAST | SCU_MODE_FUNC3 }, // P3_3, SPIFI_CLK
            { .pingrp = 3, .pinnum=4, .modefunc = SCU_PINIO_FAST | SCU_MODE_FUNC3 }, // P3_4, SPIFI_SIO3
            { .pingrp = 3, .pinnum=5, .modefunc = SCU_PINIO_FAST | SCU_MODE_FUNC3 }, // P3_5, SPIFI_SIO2
            { .pingrp = 3, .pinnum=6, .modefunc = SCU_PINIO_FAST | SCU_MODE_FUNC3 }, // P3_6, SPIFI_MISO
            { .pingrp = 3, .pinnum=7, .modefunc = SCU_PINIO_FAST | SCU_MODE_FUNC3 }, // P3_7, SPIFI_MOSI
            { .pingrp = 3, .pinnum=8, .modefunc = SCU_PINIO_FAST | SCU_MODE_FUNC3 }, // P3_8, SPIFI_CS
    };
    Chip_SCU_SetPinMuxing(pinmux, sizeof(pinmux) / sizeof(pinmux[0]));

    /* For simplicity, just run the SPIFI interface directly off the crystal at 12MHz -- we
     * don't really need speed here.
     */
    Chip_Clock_SetBaseClock(CLK_BASE_SPIFI, CLKIN_CRYSTAL, true, false);

    /* reset and configure SPIFI controller */
    spifi_HW_ResetController(LPC_SPIFI); // cancel anything outstanding
    LPC_SPIFI->MEMCMD = 0;
    LPC_SPIFI->DATINTM = 0;
    LPC_SPIFI->CTRL = SPIFI_CTRL_TO(0xFFFF) | SPIFI_CTRL_CSHI(15) | SPIFI_CTRL_RFCLK(1) | SPIFI_CTRL_FBCLK(1);
    spifi_HW_ResetController(LPC_SPIFI);

    // reset flash chip
    spifi_generic_write(SPIFI_CMD_OPCODE(0x66) |                          // Enable reset (66h)
                        SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP) |         // Opcode only
                        SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) | // Serial opcode
                        SPIFI_CMD_INTER(0),                               // No intermediate bytes
                        0,                                                // No address
                        NULL, 0);                                         // No additional data
    spifi_generic_write(SPIFI_CMD_OPCODE(0x99) |                          // Reset (99h)
                        SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP) |         // Opcode only
                        SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) | // Serial opcode
                        SPIFI_CMD_INTER(0),                               // No intermediate bytes
                        0,                                                // No address
                        NULL, 0);                                         // No additional data

    // Wait 50us for reset
    StopWatch_DelayUs(50);

    // read status registers
    uint8_t status[2];
    spifi_generic_read(SPIFI_CMD_OPCODE(0x05) |                           // Read Status Register-1 (05h)
                       SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP) |          // Opcode only
                       SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // Serial opcode
                       SPIFI_CMD_INTER(0),                                // No intermediate bytes
                       0,                                                 // No address
                       &status[0], 1);                                    // Receive 1 byte
    spifi_generic_read(SPIFI_CMD_OPCODE(0x35) |                           // Read Status Register-2 (35h)
                       SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP) |          // Opcode only
                       SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) |  // Serial opcode
                       SPIFI_CMD_INTER(0),                                // No intermediate bytes
                       0,                                                 // No address
                       &status[1], 1);                                    // Receive 1 byte

    // set QE (quad-enable) bit in status register-2
    status[1] |= _BIT(1);

    // update status registers (volatile state only)
    spifi_generic_write(SPIFI_CMD_OPCODE(0x50) |                          // Write Enable for Volatile Status Register (50h)
                        SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP) |         // Opcode only
                        SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) | // Serial opcode
                        SPIFI_CMD_INTER(0),                               // No intermediate bytes
                        0,                                                // No address
                        NULL, 0);                                         // No additional data
    spifi_generic_write(SPIFI_CMD_OPCODE(0x01) |                          // Write Status Register (01h)
                        SPIFI_CMD_FRAMEFORM(SPIFI_FRAMEFORM_OP) |         // Opcode only
                        SPIFI_CMD_FIELDFORM(SPIFI_FIELDFORM_ALL_SERIAL) | // Serial opcode
                        SPIFI_CMD_INTER(0),                               // No intermediate bytes
                        0,                                                // No address
                        status, 2);                                       // Two bytes of data to write

    return LPC_OK;
}
