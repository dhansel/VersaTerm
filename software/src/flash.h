#ifndef FLASH_H
#define FLASH_H

#include "pico/stdlib.h"

uint32_t flash_get_write_offset(uint8_t sector);
uint8_t *flash_get_read_ptr(uint8_t sector);
size_t flash_get_sector_size();
int flash_write(uint8_t sector, const void *data, size_t length);
int flash_write_partial(uint8_t sector, const void *data, size_t position, size_t size);
void flash_read(uint8_t sector, void *data, size_t length);

#endif
