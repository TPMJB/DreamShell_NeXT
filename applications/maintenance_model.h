/* NeXT maintenance operations. Portable, injectable, and independent of GUI/IO. */
#ifndef NEXT_MAINTENANCE_MODEL_H
#define NEXT_MAINTENANCE_MODEL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

enum { MF_READ = 1, MF_BACKUP, MF_CHANGED, MF_ERASE, MF_WRITE, MF_VERIFY, MF_DONE };
typedef struct {
    void *ctx;
    int (*read)(void *, void *, size_t);
    int (*backup)(void *, const void *, size_t); /* Must verify the saved file. */
    int (*unchanged)(void *, const void *, size_t);
    void (*critical)(void *, int);
    int (*erase)(void *);
    int (*write)(void *, size_t, const void *, size_t); /* NULL for erase only. */
    void (*progress)(void *, int, size_t, size_t);
} maintenance_flash_ops;

/* All buffers are owned RAM, allocated before the first destructive command.
 * A failed backup or a changed chip/bank can never reach erase. No file IO is
 * performed between critical(1) and critical(0): BIOS and IDE share G1. */
static inline int maintenance_flash_run(const maintenance_flash_ops *o,
        const void *image, size_t size, size_t chunk, void *old, void *check) {
    int phase = MF_READ;
    if(!size || !chunk || !image || !old || !check) return phase;
    if(o->read(o->ctx, old, size) < 0) return phase;
    phase = MF_BACKUP;
    o->progress(o->ctx, phase, 0, size);
    if(o->backup(o->ctx, old, size) < 0) return phase;
    if(o->unchanged(o->ctx, old, size) < 0) return MF_CHANGED;
    o->critical(o->ctx, 1);
    phase = MF_ERASE;
    o->progress(o->ctx, phase, 0, size);
    if(o->erase(o->ctx) < 0) goto out;
    if(o->write) {
        phase = MF_WRITE;
        for(size_t pos = 0; pos < size; pos += chunk) {
            size_t n = size - pos < chunk ? size - pos : chunk;
            o->progress(o->ctx, phase, pos, size);
            if(o->write(o->ctx, pos, (const uint8_t *)image + pos, n) < 0) goto out;
        }
    }
    phase = MF_VERIFY;
    o->progress(o->ctx, phase, size, size);
    if(o->read(o->ctx, check, size) < 0 || memcmp(image, check, size)) goto out;
    phase = MF_DONE;
out:
    o->critical(o->ctx, 0);
    return phase;
}

/* Only exact, complete visible banks. No sector may cross the bank boundary. */
static inline int maintenance_bank_layout(size_t chip, size_t bank,
        const uint32_t *sectors, size_t count, int whole_chip) {
    if(!chip || !bank || bank > chip || bank > 0x200000) return 0;
    if(whole_chip) return bank == chip;
    if(!sectors || !count || sectors[0]) return 0;
    for(size_t i = 0; i < count; i++) {
        size_t end = i + 1 < count ? sectors[i + 1] : chip;
        if(end <= sectors[i] || end > chip) return 0;
        if(sectors[i] < bank && end > bank) return 0;
    }
    return 1;
}

static inline int maintenance_factory_valid(const uint8_t *data, size_t size) {
    return size == 8192 && ((data[2] >= '0' && data[2] <= '2') ||
        (data[2] >= 'X' && data[2] <= 'Z')) &&
        data[3] >= '0' && data[3] <= '5' && data[4] >= '0' && data[4] <= '3';
}
static inline void maintenance_factory_edit(uint8_t *data, int region,
        int language, int broadcast, int black) {
    data[2] = (uint8_t)((black ? 'X' : '0') + region);
    data[3] = (uint8_t)('0' + language);
    data[4] = (uint8_t)('0' + broadcast);
}

/* The pattern varies by absolute offset (also across 256 KiB chunks). */
static inline uint8_t maintenance_pattern(uint64_t pos) {
    uint64_t x = pos + UINT64_C(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return (uint8_t)(x ^ (x >> 31));
}
static inline double maintenance_mibps(uint64_t bytes, uint64_t ns) {
    return ns ? (double)bytes * 1000000000.0 / (double)ns / 1048576.0 : 0.0;
}
#endif
