#ifndef LPCSDR_SPIFI_H
#define LPCSDR_SPIFI_H

#include "lpc_types.h"
#include "error.h"

ErrorCode_t lpcsdr_spifi_init(void);

void lpcsdr_spifi_read_manufacturer_device_id(uint8_t *buffer);
void lpcsdr_spifi_read_unique_id(uint8_t *buffer);
void lpcsdr_spifi_read_data(uint32_t address, uint8_t *buffer, uint32_t length);
void lpcsdr_spifi_fast_read_quad(uint32_t address, uint8_t *buffer, uint32_t length);

ErrorCode_t lpcsdr_spifi_page_program(uint32_t address, const uint8_t *buffer, uint32_t length);
ErrorCode_t lpcsdr_spifi_sector_erase(uint32_t address);

#endif /* LPCSDR_SPIFI_H */
