/* DreamShell NeXT: read-only desktop diagnostics for raw Mode 1 tracks.
 * Build: cc -O3 -std=c99 scan_gd_sectors.c -o scan_gd_sectors
 * Usage: ./scan_gd_sectors track03.bin 45150 [expected-CRC32]
 *
 * No libraries or Dreamcast headers are required. The sector checks follow
 * applications/gd_ripper/modules/checksum.c; tests use independently encoded
 * sectors to cover bad addresses, payload, parity, and unsupported modes.
 * A passing sector check is not a catalog match or proof of correct contents.
 */
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTOR_SIZE 2352
#define BLOCK_SECTORS 64
#define SYNC 1
#define ADDRESS 2
#define EDC 4
#define ECC 8
#define UNSUPPORTED 16

static uint32_t edc_table[256], crc_table[256];
static uint8_t ecc_f[256], ecc_b[256];

static void make_tables(void) {
    for (unsigned i = 0; i < 256; ++i) {
        uint32_t edc = i, crc = i;
        unsigned j = i << 1;
        for (unsigned bit = 0; bit < 8; ++bit) {
            edc = (edc >> 1) ^ ((edc & 1) ? 0xd8018001U : 0);
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320U : 0);
        }
        edc_table[i] = edc;
        crc_table[i] = crc;
        if (j & 0x100) j ^= 0x11d;
        ecc_f[i] = (uint8_t)j;
        ecc_b[i ^ j] = (uint8_t)i;
    }
}

static int parity_ok(const uint8_t *src, unsigned major_count,
        unsigned minor_count, unsigned major_mult, unsigned minor_inc,
        const uint8_t *parity) {
    unsigned size = major_count * minor_count;
    for (unsigned major = 0; major < major_count; ++major) {
        unsigned index = (major >> 1) * major_mult + (major & 1);
        uint8_t a = 0, b = 0;
        for (unsigned minor = 0; minor < minor_count; ++minor) {
            uint8_t v = src[index];
            index += minor_inc;
            if (index >= size) index -= size;
            a = ecc_f[a ^ v];
            b ^= v;
        }
        a = ecc_b[ecc_f[a] ^ b];
        if (parity[major] != a || parity[major + major_count] != (a ^ b))
            return 0;
    }
    return 1;
}

static uint8_t bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static unsigned check_sector(const uint8_t *s, uint32_t fad) {
    static const uint8_t sync[12] = {0,255,255,255,255,255,255,255,255,255,255,0};
    uint32_t edc = 0, stored;
    unsigned result = 0;
    if (memcmp(s, sync, sizeof(sync))) return SYNC;
    if (s[15] != 1) return UNSUPPORTED;
    if (s[12] != bcd(fad / 4500) || s[13] != bcd((fad / 75) % 60) ||
            s[14] != bcd(fad % 75)) result |= ADDRESS;
    for (unsigned i = 0; i < 2064; ++i)
        edc = (edc >> 8) ^ edc_table[(edc ^ s[i]) & 255];
    stored = (uint32_t)s[2064] | ((uint32_t)s[2065] << 8) |
        ((uint32_t)s[2066] << 16) | ((uint32_t)s[2067] << 24);
    if (edc != stored) result |= EDC;
    if (!parity_ok(s + 12, 86, 24, 2, 86, s + 2076) ||
        !parity_ok(s + 12, 52, 43, 86, 88, s + 2248)) result |= ECC;
    return result;
}

static int parse_number(const char *s, int base, uint32_t *value) {
    char *end;
    unsigned long long n;
    if (!s[0] || s[0] == '-' || s[0] == '+' || s[0] == ' ') return 0;
    errno = 0;
    n = strtoull(s, &end, base);
    if (errno || *end || n > UINT32_MAX) return 0;
    *value = (uint32_t)n;
    return 1;
}

static void print_range(uint64_t first, uint64_t last, uint32_t start_fad,
        unsigned flags) {
    printf("range sector=%" PRIu64 "..%" PRIu64 " fad=%" PRIu64 "..%" PRIu64
        " flags=%u\n", first, last, first + start_fad, last + start_fad, flags);
}

int main(int argc, char **argv) {
    static uint8_t buffer[SECTOR_SIZE * BLOCK_SECTORS];
    uint32_t start_fad, expected_crc = 0, crc = UINT32_MAX;
    uint64_t sectors = 0, suspects = 0, unsupported = 0, bytes = 0;
    uint64_t flag_counts[4] = {0}, range_first = 0, range_last = 0;
    unsigned range_flags = 0;
    FILE *fp;
    size_t got;
    if ((argc != 3 && argc != 4) || !parse_number(argv[2], 10, &start_fad) ||
            (argc == 4 && (strlen(argv[3]) != 8 ||
            !parse_number(argv[3], 16, &expected_crc)))) {
        fprintf(stderr, "Usage: %s TRACK FIRST_FAD [EXPECTED_CRC32]\n"
            "Example: %s track03.bin 45150 f92c1222\n", argv[0], argv[0]);
        return 2;
    }
    fp = fopen(argv[1], "rb");
    if (!fp) { perror(argv[1]); return 2; }
    make_tables();
    printf("DreamShell GD desktop sector scan v1\nfile %s\nstart_fad %" PRIu32 "\n",
        argv[1], start_fad);
    puts("checks raw Mode 1 sync/address/EDC/ECC; whole-file CRC32 from storage");
    puts("flags sync=1 address=2 EDC=4 ECC=8 unsupported=16");
    puts("range indices are zero-based and inclusive");
    fflush(stdout);
    while ((got = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        bytes += got;
        if (got % SECTOR_SIZE) {
            fprintf(stderr, "Incomplete sector at byte %" PRIu64 ". Scan incomplete.\n",
                bytes - got + (got / SECTOR_SIZE) * SECTOR_SIZE);
            fclose(fp);
            return 2;
        }
        for (size_t offset = 0; offset < got; offset += SECTOR_SIZE) {
            const uint8_t *s = buffer + offset;
            /* GD-ROM's packed decimal minute extends through 159 (0xf9). */
            if (sectors + start_fad >= 160U * 4500U) {
                fputs("Address exceeds the supported raw GD-ROM range.\n", stderr);
                fclose(fp);
                return 2;
            }
            unsigned flags = check_sector(s, start_fad + (uint32_t)sectors);
            if (flags == UNSUPPORTED) ++unsupported;
            else if (flags) ++suspects;
            for (unsigned i = 0; i < 4; ++i)
                if (flags & (1U << i)) ++flag_counts[i];
            if (range_flags && (flags != range_flags || sectors != range_last + 1)) {
                print_range(range_first, range_last, start_fad, range_flags);
                range_flags = 0;
            }
            if (flags) {
                if (!range_flags) { range_first = sectors; range_flags = flags; }
                range_last = sectors;
            }
            ++sectors;
        }
        for (size_t i = 0; i < got; ++i)
            crc = (crc >> 8) ^ crc_table[(crc ^ buffer[i]) & 255];
        if (sectors % 32768 == 0)
            fprintf(stderr, "Scanned %" PRIu64 " sectors (%" PRIu64 " MiB)\n",
                sectors, bytes / (1024 * 1024));
    }
    int read_error = ferror(fp);
    if (fclose(fp) || read_error || !sectors) {
        fputs("Empty track or input read/close error. Scan incomplete.\n", stderr);
        return 2;
    }
    if (range_flags) print_range(range_first, range_last, start_fad, range_flags);
    crc ^= UINT32_MAX;
    printf("bytes %" PRIu64 "\nsectors %" PRIu64 "\nsuspect_sectors %" PRIu64
        "\nunsupported_sectors %" PRIu64 "\nsync_errors %" PRIu64
        "\naddress_errors %" PRIu64 "\nedc_errors %" PRIu64 "\necc_errors %" PRIu64
        "\ncrc32 %08" PRIx32 "\n", bytes, sectors, suspects, unsupported,
        flag_counts[0], flag_counts[1], flag_counts[2], flag_counts[3], crc);
    if (argc == 4) {
        printf("expected_crc32 %08" PRIx32 "\ncatalog_crc %s\n", expected_crc,
            crc == expected_crc ? "MATCH" : "MISMATCH");
    } else puts("catalog_crc NOT_CHECKED");
    if (suspects || unsupported) puts("result SECTOR_CHECKS_FAILED_OR_UNSUPPORTED");
    else if (argc == 4 && crc != expected_crc)
        puts("result SECTOR_CHECKS_PASS_BUT_CATALOG_CRC_DIFFERS");
    else if (argc == 4) puts("result SECTOR_CHECKS_AND_CATALOG_CRC_MATCH");
    else puts("result SECTOR_CHECKS_PASS_CATALOG_NOT_CHECKED");
    puts("note Consistent sector checks cannot prove agreement with a known dump.");
    if (fflush(stdout) || ferror(stdout)) {
        fputs("Failed to write the complete scan report.\n", stderr);
        return 2;
    }
    return suspects || unsupported || (argc == 4 && crc != expected_crc) ? 1 : 0;
}
