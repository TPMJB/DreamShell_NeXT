/* Targeted, restartable recovery. A committed immutable baseline records the
 * CRC before any patch. Reconciliation reads only original target sectors,
 * so even a torn sector write can be retried without hashing the whole SD file.
 */
#include "recovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <zlib/zlib.h>

#define SECTOR_BYTES 2352
#define HEADER_BYTES 36

static bool marked(const uint8_t *bits, uint32_t n) {
    return (bits[n >> 3] & (1 << (n & 7))) != 0;
}
static void mark(uint8_t *bits, uint32_t n, bool value) {
    if (value) bits[n >> 3] |= 1 << (n & 7);
    else bits[n >> 3] &= ~(1 << (n & 7));
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t *p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) { p[i] = v & 255; v >>= 8; }
}
static int sidecar(char *out, const char *path, const char *suffix) {
    return snprintf(out, NAME_MAX, "%s%s", path, suffix) < NAME_MAX ? CMD_OK : CMD_ERROR;
}
static int flush(file_t fd, bool sync) {
    ssize_t completed = 0;
    return sync && fs_complete(fd, &completed) ? CMD_ERROR : CMD_OK;
}
static int read_at(file_t fd, uint32_t index, uint8_t *buffer) {
    off_t offset = (off_t)index * SECTOR_BYTES;
    return fs_seek(fd, offset, SEEK_SET) == offset &&
        fs_read(fd, buffer, SECTOR_BYTES) == SECTOR_BYTES ? CMD_OK : CMD_ERROR;
}

static int read_queue(const char *path, uint32_t track, uint32_t first,
        uint32_t count, uint32_t sector_size, uint8_t *bits, uint32_t *targets) {
    char name[NAME_MAX], line[180];
    FILE *fp;
    int rv = CMD_ERROR;
    *targets = 0;
    if (sidecar(name, path, ".bad") != CMD_OK) return CMD_ERROR;
    fp = fopen(name, "r");
    if (!fp) return errno == ENOENT ? CMD_OK : CMD_ERROR;
    if (!fgets(line, sizeof(line), fp) ||
            strcmp(line, "track,track_sector,disc_lba,disc_fad,file_offset\n")) goto out;
    while (fgets(line, sizeof(line), fp)) {
        unsigned long tn, index, lba, fad;
        unsigned long long offset;
        int end = 0;
        if (sscanf(line, "%lu,%lu,%lu,%lu,%llu%n", &tn, &index, &lba, &fad,
                &offset, &end) != 5 || strcmp(line + end, "\n") || tn != track ||
                index >= count || fad != (uint64_t)first + index || fad < 150 ||
                lba != fad - 150 || offset != (uint64_t)index * sector_size) goto out;
        if (!marked(bits, index)) { ++*targets; mark(bits, index, true); }
    }
    if (!ferror(fp)) rv = CMD_OK;
out:
    fclose(fp);
    return rv;
}

int gd_recovery_count(const char *path, uint32_t track, uint32_t first,
        uint32_t count, uint32_t *targets) {
    uint8_t *bits;
    int rv;
    if (!count || count > INT32_MAX / SECTOR_BYTES) return CMD_ERROR;
    bits = calloc((count + 7) / 8, 1);
    if (!bits) return CMD_ERROR;
    rv = read_queue(path, track, first, count, SECTOR_BYTES, bits, targets);
    free(bits);
    return rv;
}

/* Complete header + sorted (index, original CRC) records + checksum. Publish
 * with rename only after flush/close; an incomplete .tmp has never authorized
 * a patch. A damaged published baseline is fatal and is never regenerated. */
static int create_baseline(const char *path, const char *base, file_t track_fd,
        uint32_t tag, uint32_t first, uint32_t count, uint32_t type,
        const uint8_t *bits, uint32_t targets, bool sync, volatile int *active,
        uint8_t *buffer) {
    char temp[NAME_MAX];
    uint8_t header[HEADER_BYTES], record[8];
    uint64_t bytes = 0;
    uint32_t whole = 0, crc;
    file_t fd;
    int rv = CMD_ERROR;
    if (!gd_crc_restore(path, tag, (uint64_t)count * SECTOR_BYTES,
            SECTOR_BYTES, &bytes, &whole) || bytes != (uint64_t)count * SECTOR_BYTES ||
            sidecar(temp, path, ".recovery-base.tmp") != CMD_OK) return CMD_ERROR;
    memcpy(header, "GDRCV1\r\n", 8);
    put32(header+8, tag); put32(header+12, first); put32(header+16, count);
    put32(header+20, type); put32(header+24, SECTOR_BYTES);
    put32(header+28, targets); put32(header+32, whole);
    fd = fs_open(temp, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd == FILEHND_INVALID) return CMD_ERROR;
    if (fs_write(fd, header, sizeof(header)) != sizeof(header)) goto out;
    crc = crc32(0, header, sizeof(header));
    for (uint32_t i = 0; i < count; ++i) {
        if (!*active) goto out;
        if (!marked(bits, i)) continue;
        if (read_at(track_fd, i, buffer) != CMD_OK) goto out;
        put32(record, i); put32(record+4, crc32(0, buffer, SECTOR_BYTES));
        if (fs_write(fd, record, sizeof(record)) != sizeof(record)) goto out;
        crc = crc32(crc, record, sizeof(record));
        thd_pass();
    }
    put32(record, crc);
    if (fs_write(fd, record, 4) != 4 || flush(fd, sync) != CMD_OK) goto out;
    if (fs_close(fd)) { fd = FILEHND_INVALID; goto out; }
    fd = FILEHND_INVALID;
    if (rename(temp, base)) goto out;
    rv = CMD_OK;
out:
    if (fd != FILEHND_INVALID) fs_close(fd);
    return rv;
}

static int validate_baseline(file_t fd, uint32_t tag, uint32_t first,
        uint32_t count, uint32_t type, uint8_t *bits, uint32_t targets,
        uint32_t *whole, bool restore_targets) {
    uint8_t header[HEADER_BYTES], record[8];
    uint32_t crc, previous = 0;
    if (fs_total(fd) != HEADER_BYTES + (int64_t)targets * 8 + 4 ||
            fs_read(fd, header, sizeof(header)) != sizeof(header) ||
            memcmp(header, "GDRCV1\r\n", 8) || get32(header+8) != tag ||
            get32(header+12) != first || get32(header+16) != count ||
            get32(header+20) != type || get32(header+24) != SECTOR_BYTES ||
            get32(header+28) != targets) return CMD_ERROR;
    *whole = get32(header+32);
    crc = crc32(0, header, sizeof(header));
    for (uint32_t i = 0; i < targets; ++i) {
        uint32_t index;
        if (fs_read(fd, record, 8) != 8) return CMD_ERROR;
        index = get32(record);
        if (index >= count || (i && index <= previous) ||
                (!restore_targets && !marked(bits, index))) return CMD_ERROR;
        if (restore_targets) mark(bits, index, true);
        previous = index;
        crc = crc32(crc, record, 8);
    }
    if (fs_read(fd, record, 4) != 4 || get32(record) != crc) return CMD_ERROR;
    return fs_seek(fd, HEADER_BYTES, SEEK_SET) == HEADER_BYTES ? CMD_OK : CMD_ERROR;
}

static int save_audio_done(const char *path, uint32_t tag, uint32_t index,
        uint32_t crc, bool sync) {
    char name[NAME_MAX], body[100], line[140];
    file_t fd;
    int n, rv;
    if (sidecar(name, path, ".recovery-audio") != CMD_OK) return CMD_ERROR;
    snprintf(body, sizeof(body), "AUDIO1 %08lx %lu %08lx", (unsigned long)tag,
        (unsigned long)index, (unsigned long)crc);
    n = snprintf(line, sizeof(line), "\n%s %08lx\n", body,
        (unsigned long)crc32(0, (const Bytef *)body, strlen(body)));
    fd = gd_open_append(name);
    if (fd == FILEHND_INVALID) return CMD_ERROR;
    rv = fs_write(fd, line, n) == n && flush(fd, sync) == CMD_OK ? CMD_OK : CMD_ERROR;
    if (fs_close(fd)) rv = CMD_ERROR;
    return rv;
}

static int restore_audio(const char *path, uint32_t tag, uint32_t count,
        file_t fd, uint8_t *bits, uint32_t *remaining, uint8_t *buffer,
        volatile int *active) {
    char name[NAME_MAX], line[140];
    FILE *fp;
    int rv = CMD_OK;
    if (sidecar(name, path, ".recovery-audio") != CMD_OK) return CMD_ERROR;
    fp = fopen(name, "r");
    if (!fp) return errno == ENOENT ? CMD_OK : CMD_ERROR;
    while (fgets(line, sizeof(line), fp)) {
        unsigned long saved_tag, index, crc, check;
        int end = 0, consumed = 0;
        if (!*active) { rv = CMD_ERROR; break; }
        /* A torn record never establishes agreement. Re-read that sector. */
        if (sscanf(line, "AUDIO1 %lx %lu %lx%n %lx%n", &saved_tag, &index,
                &crc, &end, &check, &consumed) != 4 || strcmp(line+consumed, "\n") ||
                saved_tag != tag || index >= count || crc > UINT32_MAX ||
                crc32(0, (const Bytef *)line, end) != check || !marked(bits, index)) continue;
        if (read_at(fd, index, buffer) != CMD_OK) { rv = CMD_ERROR; break; }
        if (crc32(0, buffer, SECTOR_BYTES) == crc) { mark(bits, index, false); --*remaining; }
    }
    if (ferror(fp)) rv = CMD_ERROR;
    fclose(fp);
    return rv;
}

int gd_recovery_inspect(const char *path, uint32_t track, uint32_t first,
        uint32_t count, uint32_t type, uint32_t sector_size, volatile int *active,
        gd_recovery_status_t *status) {
    char name[NAME_MAX];
    uint8_t *bits = NULL, *buffer = NULL, header[HEADER_BYTES];
    file_t baseline = FILEHND_INVALID, fd = FILEHND_INVALID;
    uint32_t targets = 0, whole, tag;
    int rv = CMD_ERROR;
    memset(status, 0, sizeof(*status));
    if (!active || !*active || !count ||
            (sector_size != 2048 && sector_size != SECTOR_BYTES) ||
            count > INT32_MAX / sector_size) goto out;
    bits = calloc((count + 7) / 8, 1);
    if (!bits || read_queue(path, track, first, count, sector_size, bits, &targets) != CMD_OK)
        goto out;
    status->flagged = status->remaining = targets;
    status->pending = targets != 0;
    /* Legacy cooked tracks have no raw-sector recovery evidence. */
    if (sector_size != SECTOR_BYTES || (type != 0 && type != 4)) { rv = CMD_OK; goto out; }
    if (sidecar(name, path, ".recovery-base") != CMD_OK) goto out;
    baseline = fs_open(name, O_RDONLY);
    if (baseline == FILEHND_INVALID) {
        if (errno == ENOENT) rv = CMD_OK; /* No recovery has been committed yet. */
        goto out;
    }
    tag = gd_crc_tag(track, first, count, SECTOR_BYTES);
    /* A completed recovery removes .bad but retains its immutable baseline.
     * Recover the original target list for history and validate it as strictly
     * as the repair engine does. Reporting never edits either file. */
    if (!targets) {
        if (fs_read(baseline, header, sizeof(header)) != sizeof(header)) goto out;
        targets = get32(header + 28);
        if (!targets || targets > count || fs_seek(baseline, 0, SEEK_SET) != 0) goto out;
    }
    if (validate_baseline(baseline, tag, first, count, type, bits, targets,
            &whole, !status->pending) != CMD_OK) goto out;
    status->flagged = status->remaining = targets;
    fd = fs_open(path, O_RDONLY);
    buffer = memalign(32, SECTOR_BYTES);
    if (fd == FILEHND_INVALID || !buffer || fs_total(fd) != (int64_t)count * SECTOR_BYTES)
        goto out;
    if (type == 4) {
        for (uint32_t i = 0; i < count; ++i) {
            if (!*active) goto out;
            if (!marked(bits, i)) continue;
            if (read_at(fd, i, buffer) != CMD_OK) goto out;
            if (gd_check_sector(buffer, first + i) == 0) --status->remaining;
            thd_pass();
        }
    } else if (restore_audio(path, tag, count, fd, bits, &status->remaining,
            buffer, active) != CMD_OK) goto out;
    if (!*active) goto out;
    /* A cleared queue must agree with the retained repair evidence. */
    if (!status->pending && status->remaining) goto out;
    status->recovered = status->flagged - status->remaining;
    rv = CMD_OK;
out:
    if (baseline != FILEHND_INVALID && fs_close(baseline)) rv = CMD_ERROR;
    if (fd != FILEHND_INVALID && fs_close(fd)) rv = CMD_ERROR;
    free(bits); free(buffer);
    return rv;
}

static int backup_sector(const char *path, uint32_t fad, const uint8_t *buffer, bool sync) {
    char name[NAME_MAX], header[40];
    file_t fd;
    int n, rv;
    if (sidecar(name, path, ".repair-backup") != CMD_OK) return CMD_ERROR;
    fd = gd_open_append(name);
    if (fd == FILEHND_INVALID) return CMD_ERROR;
    n = snprintf(header, sizeof(header), "FAD %lu\n", (unsigned long)fad);
    rv = fs_write(fd, header, n) == n && fs_write(fd, buffer, SECTOR_BYTES) == SECTOR_BYTES &&
        flush(fd, sync) == CMD_OK ? CMD_OK : CMD_ERROR;
    if (fs_close(fd)) rv = CMD_ERROR;
    return rv;
}

int gd_recover_track(const char *path, uint32_t track, uint32_t first,
        uint32_t count, uint32_t type, bool sync, unsigned passes,
        volatile int *active, gd_recovery_read_t read_sector,
        gd_recovery_progress_t progress, void *data, gd_recovery_result_t *result) {
    char base[NAME_MAX], name[NAME_MAX];
    uint8_t *bits = NULL, *buffer = NULL, *candidate, *second;
    file_t fd = FILEHND_INVALID, baseline = FILEHND_INVALID;
    uint32_t tag = gd_crc_tag(track, first, count, SECTOR_BYTES), whole = 0;
    int rv = CMD_ERROR;
    memset(result, 0, sizeof(*result));
    result->error = "Recovery queue invalid";
    if (!count || count > INT32_MAX / SECTOR_BYTES || (type != 0 && type != 4) ||
            !active || !read_sector || !passes || passes > 50) goto out;
    bits = calloc((count+7)/8, 1);
    /* 2352 is not 32-byte aligned. Allocate individually aligned drive buffers. */
    buffer = memalign(32, 2368 * 3);
    if (!bits || !buffer) { result->error = "Recovery allocation failed"; goto out; }
    candidate = buffer + 2368; second = candidate + 2368;
    if (read_queue(path, track, first, count, SECTOR_BYTES, bits, &result->targets) != CMD_OK) goto out;
    if (!result->targets) { rv = CMD_OK; goto out; }
    result->remaining = result->targets;
    result->error = "Recovery track open/size failed";
    fd = fs_open(path, O_RDWR);
    if (fd == FILEHND_INVALID || fs_total(fd) != (int64_t)count * SECTOR_BYTES) goto out;
    result->error = "Recovery baseline invalid / unavailable";
    if (sidecar(base, path, ".recovery-base") != CMD_OK) goto out;
    baseline = fs_open(base, O_RDONLY);
    if (baseline == FILEHND_INVALID) {
        if (errno != ENOENT || create_baseline(path, base, fd, tag, first, count,
                type, bits, result->targets, sync, active, buffer) != CMD_OK) goto out;
        baseline = fs_open(base, O_RDONLY);
    }
    if (baseline == FILEHND_INVALID || validate_baseline(baseline, tag, first, count,
            type, bits, result->targets, &whole, false) != CMD_OK) goto out;

    /* Only targets can have changed since the immutable baseline was saved.
     * Rebuild the current CRC, including any interrupted/torn patch. */
    result->error = "Recovery reconciliation failed";
    for (uint32_t i = 0; i < result->targets; ++i) {
        uint8_t record[8];
        uint32_t index;
        if (!*active || fs_read(baseline, record, 8) != 8) goto out;
        index = get32(record);
        if (read_at(fd, index, buffer) != CMD_OK) goto out;
        whole = gd_crc_replace(whole, get32(record+4), crc32(0, buffer, SECTOR_BYTES),
            (uint64_t)(count-index-1) * SECTOR_BYTES);
        if (type == 4 && gd_check_sector(buffer, first+index) == 0) {
            mark(bits, index, false); --result->remaining;
        }
        if (progress) progress(data, 0, first+index, result->remaining, false);
        thd_pass();
    }
    fs_close(baseline); baseline = FILEHND_INVALID;
    if (type == 0 && restore_audio(path, tag, count, fd, bits, &result->remaining,
            buffer, active) != CMD_OK) goto out;
    result->error = "Recovery CRC invalidation failed";
    if (sidecar(name, path, ".crc") != CMD_OK || (fs_unlink(name) && errno != ENOENT)) goto out;

    for (unsigned pass = 1; pass <= passes && result->remaining; ++pass) {
        for (uint32_t step = 0; step < count; ++step) {
            /* Alternate direction to approach damaged regions from each side. */
            uint32_t index = pass & 1 ? step : count-step-1;
            uint32_t fad = first+index, old_crc, new_crc;
            int read_result;
            if (!*active) { result->error = "Recovery stopped"; goto out; }
            if (!marked(bits, index)) continue;
            if (progress) progress(data, pass, fad, result->remaining, false);
            read_result = read_sector(data, candidate, fad);
            result->error = "Recovery drive / disc error";
            if (read_result < 0) goto out;
            if (read_result != 0) continue;
            if (type == 4) {
                if (gd_check_sector(candidate, fad) != 0) continue;
            } else {
                read_result = read_sector(data, second, fad);
                if (read_result < 0) goto out;
                if (read_result != 0 || memcmp(candidate, second, SECTOR_BYTES)) continue;
            }
            new_crc = crc32(0, candidate, SECTOR_BYTES);
            result->error = "Recovery backup failed";
            if (read_at(fd, index, buffer) != CMD_OK) goto out;
            old_crc = crc32(0, buffer, SECTOR_BYTES);
            if (backup_sector(path, fad, buffer, sync) != CMD_OK ||
                    old_crc != crc32(0, buffer, SECTOR_BYTES)) goto out;
            result->error = "Recovery write/read-back failed";
            off_t offset = (off_t)index * SECTOR_BYTES;
            if (fs_seek(fd, offset, SEEK_SET) != offset ||
                    fs_write(fd, candidate, SECTOR_BYTES) != SECTOR_BYTES ||
                    flush(fd, sync) != CMD_OK || new_crc != crc32(0, candidate, SECTOR_BYTES) ||
                    read_at(fd, index, buffer) != CMD_OK || memcmp(candidate, buffer, SECTOR_BYTES) ||
                    new_crc != crc32(0, buffer, SECTOR_BYTES) ||
                    (type == 4 && gd_check_sector(buffer, fad) != 0)) goto out;
            if (type == 0 && save_audio_done(path, tag, index, new_crc, sync) != CMD_OK) {
                result->error = "Recovery audio checkpoint failed"; goto out;
            }
            whole = gd_crc_replace(whole, old_crc, new_crc,
                (uint64_t)(count-index-1) * SECTOR_BYTES);
            mark(bits, index, false); --result->remaining; ++result->recovered;
            if (progress) progress(data, pass, fad, result->remaining, true);
            thd_pass();
        }
    }
    result->error = "Recovery CRC checkpoint failed";
    if (flush(fd, sync) != CMD_OK) goto out;
    if (fs_close(fd)) {
        fd = FILEHND_INVALID;
        result->error = "Recovery track close failed"; goto out;
    }
    fd = FILEHND_INVALID;
    if (gd_crc_checkpoint(path, tag,
            (uint64_t)count * SECTOR_BYTES, whole, sync) != CMD_OK) goto out;
    if (!result->remaining) {
        /* The immutable baseline and backups retain the original target list.
         * Until this unlink, old console/host verifiers still reject the dump. */
        result->error = "Recovery queue cleanup failed";
        if (sidecar(name, path, ".bad") != CMD_OK || fs_unlink(name)) goto out;
    }
    result->crc = whole;
    result->error = NULL;
    rv = CMD_OK;
out:
    if (baseline != FILEHND_INVALID) fs_close(baseline);
    if (fd != FILEHND_INVALID && fs_close(fd)) {
        result->error = "Recovery track close failed"; rv = CMD_ERROR;
    }
    free(bits); free(buffer);
    return rv;
}
