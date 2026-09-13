#ifndef GD_RIPPER_RECOVERY_H
#define GD_RIPPER_RECOVERY_H

#include "checksum.h"

typedef struct {
    uint32_t targets, remaining, recovered, crc;
    const char *error;
} gd_recovery_result_t;

/* read_sector returns 0 for a read, 1 for an ordinary read failure, -1 for a
 * fatal drive/media failure. The engine itself checks Mode 1 EDC/ECC/address,
 * and requires two matching reads for audio. */
typedef int (*gd_recovery_read_t)(void *data, uint8_t *buffer, uint32_t fad);
typedef void (*gd_recovery_progress_t)(void *data, uint32_t pass,
    uint32_t fad, uint32_t remaining, bool recovered);

/* Strictly validate and count the persistent .bad queue. Missing = zero. */
int gd_recovery_count(const char *path, uint32_t track, uint32_t first,
    uint32_t count, uint32_t *targets);

/* Requires a full-sized raw track. Never publishes rip.complete. Unresolved
 * sectors remain in .bad; successful patches and their CRC survive restart.
 * Storage/memory faults are fatal, not additional disc retry candidates. */
int gd_recover_track(const char *path, uint32_t track, uint32_t first,
    uint32_t count, uint32_t type, bool sync, unsigned passes,
    volatile int *active, gd_recovery_read_t read_sector,
    gd_recovery_progress_t progress, void *data, gd_recovery_result_t *result);

#endif
