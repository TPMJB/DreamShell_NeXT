/* DreamShell NeXT: shared CRC and checked executable layout helpers. */
#ifndef DS_ISOLDR_CHECK_H
#define DS_ISOLDR_CHECK_H
#include <stdint.h>
#include <stddef.h>

/* CRC-32/ISO-HDLC; caller starts with ~0U and complements the result. */
static inline uint32_t isoldr_crc32_update(uint32_t crc, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    while (len--) {
        crc ^= *p++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return crc;
}

/* Both intervals are half-open physical RAM ranges. No wrapping arithmetic. */
static inline int isoldr_ranges_overlap(uint32_t a, uint32_t an,
                                        uint32_t b, uint32_t bn) {
    return an && bn && (uint64_t)a < (uint64_t)b + bn &&
           (uint64_t)b < (uint64_t)a + an;
}

static inline int isoldr_boot_extent(uint32_t addr, uint32_t size,
                                     uint32_t sector, uint32_t *rounded) {
    uint64_t n;
    addr &= 0x1fffffffU;
    if (!size || sector != 2048 || addr < 0x0c010000U)
        return 0;
    n = ((uint64_t)size + sector - 1) / sector * sector;
    /* Keep the loader's temporary stack at the top of retail RAM. */
    if ((uint64_t)addr + n > 0x0cfff000U)
        return 0;
    *rounded = (uint32_t)n;
    return 1;
}
#endif
