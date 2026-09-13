/* DreamShell removable-media discovery. The filesystem engine validates VBRs. */
#ifndef DS_FAT_PARTITION_H
#define DS_FAT_PARTITION_H
#include <stdint.h>
#include <string.h>

static inline uint32_t ds_part_u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
        (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Return number of candidates; -2 is GPT, -1 is invalid/no supported volume.
 * Indices 0..3 are MBR primary partitions, -1 is an unpartitioned volume.
 * 0x07 is only a candidate: NTFS must still fail FatFs's filesystem validation. */
static inline int ds_fat_partitions(const uint8_t mbr[512], uint64_t sectors,
                                  int indices[4]) {
    int count = 0;
    if (mbr[510] != 0x55 || mbr[511] != 0xaa) return -1;
    if (!memcmp(mbr + 3, "EXFAT   ", 8) ||
        ((mbr[0] == 0xeb || mbr[0] == 0xe9) &&
         mbr[11] == 0 && mbr[12] == 2 && mbr[13] &&
         (mbr[14] || mbr[15]) && mbr[16])) {
        indices[0] = -1;
        return 1;
    }
    for (int i = 0; i < 4; ++i) {
        const uint8_t *entry = mbr + 446 + i * 16;
        uint32_t start = ds_part_u32(entry + 8);
        uint32_t size = ds_part_u32(entry + 12);
        if (entry[4] == 0xee) return -2;
        if (!start || !size || (uint64_t)start + size > sectors) continue;
        switch (entry[4]) {
            case 0x01: case 0x04: case 0x06: case 0x07:
            case 0x0b: case 0x0c: case 0x0e:
                indices[count++] = i;
        }
    }
    return count ? count : -1;
}
#endif
