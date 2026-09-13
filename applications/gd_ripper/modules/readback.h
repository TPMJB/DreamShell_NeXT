/* Bounded, read-only diagnostics for inconsistent saved-track reads. */
#ifndef GD_RIPPER_READBACK_H
#define GD_RIPPER_READBACK_H

#include "verify.h"

typedef struct {
    file_t log, samples;
    uint8_t *storage, *saved, *retry;
    uint32_t sample_bytes, buffer_events;
    int failed;
} gd_readback_t;

int gd_readback_begin(gd_readback_t *diag, const char *folder);
int gd_readback_end(gd_readback_t *diag, bool sync);
int gd_readback_guard(const char *folder, bool unstable, bool sync);
bool gd_readback_blocked(const char *folder);
int gd_readback_sector(gd_readback_t *diag, const char *path, uint32_t track,
    uint32_t sector, uint32_t fad, const uint8_t *source, unsigned flags,
    volatile int *active, gd_verify_summary_t *summary);
void gd_readback_buffer(gd_readback_t *diag, uint32_t track, uint64_t offset,
    const char *phase, uint32_t before, uint32_t after, gd_verify_summary_t *summary);

#endif
