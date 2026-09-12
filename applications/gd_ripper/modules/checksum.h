#ifndef GD_RIPPER_CHECKSUM_H
#define GD_RIPPER_CHECKSUM_H

#include "ds.h"
#include <stdbool.h>
#include <stdint.h>

/* These checks validate raw Mode 1 sectors; they never synthesize disc data. */
#define GD_SECTOR_SYNC 1
#define GD_SECTOR_ADDRESS 2
#define GD_SECTOR_EDC 4
#define GD_SECTOR_ECC 8
#define GD_SECTOR_UNSUPPORTED 16
unsigned gd_check_sector(const uint8_t *sector, uint32_t fad);

/* A journal record is published only AFTER its track has been flushed. */
uint32_t gd_crc_tag(uint32_t number, uint32_t start, uint32_t count, uint32_t size);
int gd_crc_checkpoint(const char *track_path, uint32_t tag, uint64_t bytes,
    uint32_t crc, bool sync);
bool gd_crc_restore(const char *track_path, uint32_t tag, uint64_t file_bytes,
    uint32_t sector_size, uint64_t *bytes, uint32_t *crc);

#endif
