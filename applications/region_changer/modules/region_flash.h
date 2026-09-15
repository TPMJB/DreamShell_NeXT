/* Physical Dreamcast settings flash. Kept independent of BIOS read syscalls:
 * DreamShell's replacement can return zero and substitute a boot region.
 * The partition table is also used by firmware/isoldr/syscalls/syscallsc.c.
 * Known addresses permit reads/backups; writes still require BIOS layout info.
 */
#ifndef NEXT_REGION_FLASH_H
#define NEXT_REGION_FLASH_H
#include <stdint.h>
#include <stddef.h>

enum { REGION_FLASH_SIZE = 0x20000, REGION_FACTORY_START = 0x1a000,
       REGION_FACTORY_SIZE = 0x2000 };

static inline int region_partition(int part, int *start, int *size) {
    static const int offsets[] = {0x1a000, 0x18000, 0x1c000, 0x10000, 0};
    static const int sizes[] = {0x2000, 0x2000, 0x4000, 0x8000, 0x10000};
    if(part < 0 || part >= 5 || !start || !size) return 0;
    *start = offsets[part]; *size = sizes[part]; return 1;
}
static inline int region_partition_matches(int part, int start, int size) {
    int expected_start, expected_size;
    return region_partition(part, &expected_start, &expected_size) &&
        start == expected_start && size == expected_size;
}
static inline int region_flash_range(int start, size_t size) {
    return start >= 0 && start < REGION_FLASH_SIZE && size &&
        size <= (size_t)(REGION_FLASH_SIZE - start);
}
/* Byte reads through the uncached P2 mapping, including after erase/write.
 * Do not use a cached alias or a region-overriding BIOS call for verification.
 */
static inline int region_flash_read(const volatile uint8_t *flash, int start,
        void *data, size_t size) {
    if(!flash || !data || !region_flash_range(start, size)) return -1;
    uint8_t *out = data;
    for(size_t i = 0; i < size; i++) out[i] = flash[start + i];
    return 0;
}
static inline int region_flash_written(const volatile uint8_t *flash, int start,
        const void *data, size_t size, int result) {
    if(!flash || !data || !region_flash_range(start, size) ||
            (result != 0 && result != (int)size)) return -1;
    const uint8_t *wanted = data;
    for(size_t i = 0; i < size; i++)
        if(flash[start + i] != wanted[i]) return -1;
    return 0;
}
#endif
