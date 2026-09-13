#include "checksum.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <zlib/zlib.h>

static uint32_t edc_table[256];
static uint8_t ecc_f[256], ecc_b[256];
static bool tables_ready;

static void make_tables(void) {
    if (tables_ready) return;
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t v = i;
        unsigned j = i << 1;
        for (unsigned bit = 0; bit < 8; ++bit)
            v = (v >> 1) ^ ((v & 1) ? 0xd8018001U : 0);
        edc_table[i] = v;
        if (j & 0x100) j ^= 0x11d;
        ecc_f[i] = j;
        ecc_b[i ^ j] = i;
    }
    tables_ready = true;
}

static bool parity_ok(const uint8_t *src, unsigned major_count,
        unsigned minor_count, unsigned major_mult, unsigned minor_inc,
        const uint8_t *parity) {
    unsigned size = major_count * minor_count;
    for (unsigned major = 0; major < major_count; ++major) {
        unsigned index = (major >> 1) * major_mult + (major & 1);
        uint8_t a = 0, b = 0;
        for (unsigned minor = 0; minor < minor_count; ++minor) {
            uint8_t v = src[index];
            index = (index + minor_inc) % size;
            a ^= v;
            b ^= v;
            a = ecc_f[a];
        }
        a = ecc_b[ecc_f[a] ^ b];
        if (parity[major] != a || parity[major + major_count] != (a ^ b))
            return false;
    }
    return true;
}

static uint8_t bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

unsigned gd_check_sector(const uint8_t *s, uint32_t fad) {
    static const uint8_t sync[12] = {0,255,255,255,255,255,255,255,255,255,255,0};
    uint32_t edc = 0, stored;
    unsigned result = 0;
    make_tables();
    if (memcmp(s, sync, sizeof(sync))) return GD_SECTOR_SYNC;
    /* Mode 2 and audio require a different validation scheme. */
    if (s[15] != 1) return GD_SECTOR_UNSUPPORTED;
    if (s[12] != bcd(fad / 4500) || s[13] != bcd((fad / 75) % 60) ||
            s[14] != bcd(fad % 75)) result |= GD_SECTOR_ADDRESS;
    for (unsigned i = 0; i < 2064; ++i)
        edc = (edc >> 8) ^ edc_table[(edc ^ s[i]) & 255];
    stored = (uint32_t)s[2064] | ((uint32_t)s[2065] << 8) |
        ((uint32_t)s[2066] << 16) | ((uint32_t)s[2067] << 24);
    if (edc != stored) result |= GD_SECTOR_EDC;
    if (!parity_ok(s + 12, 86, 24, 2, 86, s + 2076) ||
        !parity_ok(s + 12, 52, 43, 86, 88, s + 2248)) result |= GD_SECTOR_ECC;
    return result;
}

file_t gd_open_append(const char *path) {
    errno = 0;
    file_t fd = fs_open(path, O_WRONLY);
    if (fd == FILEHND_INVALID && errno == ENOENT) {
        /* The pinned FatFs maps plain O_CREAT to FA_OPEN_ALWAYS, but its
         * f_open() omits that flag from the creation branch. It can reopen
         * existing files, yet returns FR_NO_FILE for new logs/journals.
         * Create explicitly and exclusively; never truncate a saved record. */
        errno = 0;
        fd = fs_open(path, O_WRONLY | O_CREAT | O_EXCL);
    }
    if (fd == FILEHND_INVALID) return fd;
    off_t size = fs_total(fd);
    if (size < 0 || fs_seek(fd, size, SEEK_SET) != size) {
        int error = errno ? errno : EIO;
        fs_close(fd);
        errno = error;
        return FILEHND_INVALID;
    }
    return fd;
}

int gd_crc_checkpoint(const char *track_path, uint32_t tag, uint64_t bytes,
        uint32_t crc, bool sync) {
    char path[NAME_MAX], body[120], line[160];
    file_t fd;
    ssize_t completed = 0;
    int n, rv = CMD_OK;
    if (snprintf(path, sizeof(path), "%s.crc", track_path) >= (int)sizeof(path))
        return CMD_ERROR;
    n = snprintf(body, sizeof(body), "CRC1 %08lx %llu %08lx",
        (unsigned long)tag, (unsigned long long)bytes, (unsigned long)crc);
    if (n < 0 || n >= (int)sizeof(body)) return CMD_ERROR;
    /* Leading newline isolates a previous torn record on FAT. */
    n = snprintf(line, sizeof(line), "\n%s %08lx\n", body,
        (unsigned long)crc32(0, (const Bytef *)body, strlen(body)));
    fd = gd_open_append(path);
    if (fd == FILEHND_INVALID) return CMD_ERROR;
    if (fs_write(fd, line, n) != n || (sync && fs_complete(fd, &completed))) {
        int error = errno ? errno : EIO;
        fs_close(fd);
        errno = error;
        rv = CMD_ERROR;
    } else if (fs_close(fd)) rv = CMD_ERROR;
    return rv;
}

bool gd_crc_restore(const char *track_path, uint32_t tag, uint64_t file_bytes,
        uint32_t sector_size, uint64_t *bytes, uint32_t *crc) {
    char path[NAME_MAX], line[160];
    FILE *fp;
    bool found = false;
    *bytes = 0;
    *crc = 0;
    if (!sector_size || snprintf(path, sizeof(path), "%s.crc", track_path) >=
            (int)sizeof(path)) return false;
    fp = fopen(path, "r");
    if (!fp) return false;
    while (fgets(line, sizeof(line), fp)) {
        unsigned long saved_tag, saved_crc, check;
        unsigned long long saved_bytes;
        int end = 0, consumed = 0;
        if (sscanf(line, "CRC1 %lx %llu %lx%n %lx%n", &saved_tag,
                &saved_bytes, &saved_crc, &end, &check, &consumed) != 4 ||
                line[consumed] != '\n' || line[consumed + 1] != '\0' ||
                saved_tag != tag || saved_crc > UINT32_MAX || check > UINT32_MAX ||
                saved_bytes > file_bytes || saved_bytes % sector_size ||
                saved_bytes < *bytes) continue;
        if (crc32(0, (const Bytef *)line, end) != check) continue;
        *bytes = saved_bytes;
        *crc = saved_crc;
        found = true;
    }
    fclose(fp);
    return found;
}

uint32_t gd_crc_tag(uint32_t number, uint32_t start, uint32_t count, uint32_t size) {
    char text[96];
    int n = snprintf(text, sizeof(text), "%lu %lu %lu %lu",
        (unsigned long)number, (unsigned long)start, (unsigned long)count,
        (unsigned long)size);
    return (uint32_t)crc32(0, (const Bytef *)text, n);
}
