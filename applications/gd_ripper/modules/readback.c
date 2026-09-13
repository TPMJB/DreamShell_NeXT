#include "readback.h"
#include "checksum.h"
#include <zlib/zlib.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_BYTES 2352
#define ALIGNED_SECTOR_BYTES 2368
#define MAX_SAMPLES 32

bool gd_readback_blocked(const char *folder) {
    char path[NAME_MAX];
    if (snprintf(path, sizeof(path), "%s/readback.unstable", folder) >= (int)sizeof(path)) return true;
    return FileExists(path);
}

int gd_readback_guard(const char *folder, bool unstable, bool sync) {
    char path[NAME_MAX];
    if (snprintf(path, sizeof(path), "%s/readback.unstable", folder) >= (int)sizeof(path)) return CMD_ERROR;
    if (!unstable) return FileExists(path) && fs_unlink(path) != 0 ? CMD_ERROR : CMD_OK;
    const char text[] = "Saved-track reads disagreed. Complete a consistent saved-dump scan before disc repair.\n";
    file_t hnd = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (hnd == FILEHND_INVALID) return CMD_ERROR;
    int failed = fs_write(hnd, text, sizeof(text) - 1) != sizeof(text) - 1;
    ssize_t completed = 0;
    if (sync && fs_complete(hnd, &completed) != 0) failed = 1;
    if (fs_close(hnd) != 0) failed = 1;
    return failed ? CMD_ERROR : CMD_OK;
}

static void trace(gd_readback_t *d, const char *format, ...) {
    char line[384];
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(line, sizeof(line), format, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof(line) || d->log == FILEHND_INVALID ||
            fs_write(d->log, line, n) != n) d->failed = 1;
}

int gd_readback_end(gd_readback_t *d, bool sync) {
    file_t handles[] = {d->log, d->samples};
    for (unsigned i = 0; i < 2; ++i) {
        if (handles[i] != FILEHND_INVALID) {
            ssize_t completed = 0;
            if (sync && fs_complete(handles[i], &completed) != 0) d->failed = 1;
            if (fs_close(handles[i]) != 0) d->failed = 1;
        }
    }
    free(d->storage);
    d->storage = d->saved = d->retry = NULL;
    d->log = d->samples = FILEHND_INVALID;
    return d->failed ? CMD_ERROR : CMD_OK;
}

int gd_readback_begin(gd_readback_t *d, const char *folder) {
    char path[NAME_MAX];
    memset(d, 0, sizeof(*d));
    d->log = d->samples = FILEHND_INVALID;
    d->storage = memalign(32, 2 * ALIGNED_SECTOR_BYTES);
    if (!d->storage) goto error;
    d->saved = d->storage;
    d->retry = d->storage + ALIGNED_SECTOR_BYTES;
    if (snprintf(path, sizeof(path), "%s/readback.log", folder) >= (int)sizeof(path)) goto error;
    d->log = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (d->log == FILEHND_INVALID) goto error;
    if (snprintf(path, sizeof(path), "%s/readback.bin", folder) >= (int)sizeof(path)) goto error;
    d->samples = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (d->samples == FILEHND_INVALID) goto error;
    trace(d, "DreamShell saved-read diagnostic v1\n"
        "policy first observation retained; two reopen/rereads per suspect; no track writes\n"
        "limits %u sectors; raw samples in readback.bin (2352 bytes each)\n"
        "note Reopening bypasses the file handle's sector buffer, not every hardware cache.\n",
        MAX_SAMPLES);
    if (!d->failed) return CMD_OK;
error:
    d->failed = 1;
    gd_readback_end(d, false);
    return CMD_ERROR;
}

void gd_readback_buffer(gd_readback_t *d, uint32_t track, uint64_t offset,
        const char *phase, uint32_t before, uint32_t after, gd_verify_summary_t *summary) {
    if (before == after) return;
    ++summary->buffer_changes;
    if (d->buffer_events++ < MAX_SAMPLES)
        trace(d, "buffer_changed track %lu offset %llu phase %s before %08lx after %08lx\n",
            (unsigned long)track, (unsigned long long)offset, phase,
            (unsigned long)before, (unsigned long)after);
}

static void sample(gd_readback_t *d, unsigned pass, const uint8_t *bytes, unsigned flags,
        gd_verify_summary_t *summary) {
    uint32_t crc = crc32(0L, bytes, SECTOR_BYTES);
    trace(d, "sample pass %u file_offset %lu bytes %u crc %08lx flags %u\n",
        pass, (unsigned long)d->sample_bytes, SECTOR_BYTES, (unsigned long)crc, flags);
    if (fs_write(d->samples, bytes, SECTOR_BYTES) != SECTOR_BYTES) d->failed = 1;
    d->sample_bytes += SECTOR_BYTES;
    if (crc != crc32(0L, bytes, SECTOR_BYTES)) {
        trace(d, "sample_buffer_changed pass %u\n", pass);
        ++summary->buffer_changes;
        d->failed = 1;
    }
}

int gd_readback_sector(gd_readback_t *d, const char *path, uint32_t track,
        uint32_t sector, uint32_t fad, const uint8_t *source, unsigned flags,
        volatile int *active, gd_verify_summary_t *summary) {
    if (summary->diagnostic_sectors >= MAX_SAMPLES) {
        ++summary->diagnostic_skipped;
        return CMD_OK;
    }
    ++summary->diagnostic_sectors;
    uint32_t original_crc = crc32(0L, source, SECTOR_BYTES);
    /* Use byte loads here so the snapshot does not depend on the fast SH-4
     * memcpy path being investigated. Compare around the copy as well. */
    for (unsigned i = 0; i < SECTOR_BYTES; ++i)
        d->saved[i] = ((const volatile uint8_t *)source)[i];
    uint32_t saved_crc = crc32(0L, d->saved, SECTOR_BYTES);
    uint64_t offset = (uint64_t)sector * SECTOR_BYTES;
    gd_readback_buffer(d, track, offset, "snapshot", original_crc, saved_crc, summary);
    gd_readback_buffer(d, track, offset, "source-after-snapshot", original_crc,
        crc32(0L, source, SECTOR_BYTES), summary);
    trace(d, "sector track %lu index %lu fad %lu track_offset %llu source %p saved %p retry %p\n",
        (unsigned long)track, (unsigned long)sector, (unsigned long)fad,
        (unsigned long long)offset, (const void *)source, (void *)d->saved, (void *)d->retry);
    sample(d, 0, d->saved, flags, summary);
    bool differs = false;
    for (unsigned pass = 1; pass <= 2; ++pass) {
        if (!*active) return 1;
        file_t hnd = fs_open(path, O_RDONLY);
        ssize_t got = -1;
        int read_errno = errno;
        if (hnd != FILEHND_INVALID) {
            if (fs_seek(hnd, (off_t)offset, SEEK_SET) == (off_t)offset) {
                got = fs_read(hnd, d->retry, SECTOR_BYTES);
            }
            read_errno = errno;
            if (fs_close(hnd) != 0) { got = -1; read_errno = errno; }
        }
        if (got != SECTOR_BYTES) {
            trace(d, "reread_error pass %u got %ld errno %d\n", pass, (long)got, read_errno);
            return CMD_ERROR;
        }
        unsigned changed = 0, first = SECTOR_BYTES, last = 0;
        for (unsigned i = 0; i < SECTOR_BYTES; ++i) {
            if (d->saved[i] != d->retry[i]) {
                if (!changed) first = i;
                last = i;
                ++changed;
            }
        }
        if (changed && !differs) {
            differs = true;
            ++summary->readback_disagreements;
        }
        trace(d, "reread pass %u changed_bytes %u first %u last %u\n", pass, changed, first, last);
        unsigned emitted = 0;
        for (unsigned i = first; i < SECTOR_BYTES && emitted < 32; ++i) {
            if (d->saved[i] != d->retry[i]) {
                trace(d, "diff %u %02x %02x\n", i, d->saved[i], d->retry[i]);
                ++emitted;
            }
        }
        sample(d, pass, d->retry, gd_check_sector(d->retry, fad), summary);
        gd_readback_buffer(d, track, offset, "saved-during-reread", saved_crc,
            crc32(0L, d->saved, SECTOR_BYTES), summary);
        gd_readback_buffer(d, track, offset, "source-during-reread", original_crc,
            crc32(0L, source, SECTOR_BYTES), summary);
    }
    return d->failed ? CMD_ERROR : CMD_OK;
}
