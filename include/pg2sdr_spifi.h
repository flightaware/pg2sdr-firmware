#ifndef PG2SDR_SPIFI_H
#define PG2SDR_SPIFI_H

#include "lpc_types.h"
#include "error.h"

ErrorCode_t pg2sdr_spifi_init(void);

uint16_t pg2sdr_spifi_read_manufacturer_device_id();
uint64_t pg2sdr_spifi_read_unique_id();
void pg2sdr_spifi_read_data(uint32_t address, uint8_t *buffer, uint32_t length);
void pg2sdr_spifi_fast_read_quad(uint32_t address, uint8_t *buffer, uint32_t length);

ErrorCode_t pg2sdr_spifi_page_program(uint32_t address, const uint8_t *buffer, uint32_t length);
ErrorCode_t pg2sdr_spifi_sector_erase(uint32_t address);

#endif /* PG2SDR_SPIFI_H */
