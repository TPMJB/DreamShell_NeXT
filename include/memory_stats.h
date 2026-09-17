/* K-UI RAM diagnostics. Counters only: never probe by allocating free RAM. */
#ifndef KUI_MEMORY_STATS_H
#define KUI_MEMORY_STATS_H
#include <arch/arch.h>
#include <arch/stack.h>
#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    uint32_t main_bytes, heap_used, heap_free, unclaimed, available;
    uintptr_t program_break;
    int valid;
} memory_snapshot_t;

typedef struct {
    memory_snapshot_t current;
    uint32_t lowest_available, samples, rejected;
    uint64_t last_sample_ms;
} memory_meter_t;

static inline int MemoryStatsRead(memory_snapshot_t *out) {
    int saved_errno = errno;
    uintptr_t base = 0x8c000000u, top = _arch_mem_top;
    memset(out, 0, sizeof(*out));
    /* mallinfo has its own allocator lock. Do not disable interrupts around it.
     * Retry if another thread grew the heap while the counters were read. */
    for (int attempt = 0; attempt < 3; ++attempt) {
        uintptr_t before = (uintptr_t)sbrk(0);
        struct mallinfo mi = mallinfo();
        uintptr_t after = (uintptr_t)sbrk(0);
        if (before != after) continue;
        if (top <= base + THD_KERNEL_STACK_SIZE || top - base > 32u*1024u*1024u)
            break;
        uintptr_t limit = top - THD_KERNEL_STACK_SIZE;
        if (before < base || before >= limit || mi.arena < 0 ||
                mi.uordblks < 0 || mi.fordblks < 0 || mi.hblkhd || mi.hblks ||
                (uint64_t)mi.uordblks + (uint64_t)mi.fordblks != (uint64_t)mi.arena ||
                (uintptr_t)mi.arena > before - base) break;
        out->main_bytes = (uint32_t)(top - base);
        out->heap_used = (uint32_t)mi.uordblks;
        out->heap_free = (uint32_t)mi.fordblks;
        /* Pinned KOS rounds sbrk increments to four bytes and rejects >=limit. */
        out->unclaimed = (uint32_t)((limit - before - 1) & ~(uintptr_t)3);
        out->available = out->heap_free + out->unclaimed;
        out->program_break = before;
        out->valid = 1;
        break;
    }
    errno = saved_errno;
    return out->valid;
}

static inline void MemoryStatsReset(memory_meter_t *meter) {
    memset(meter, 0, sizeof(*meter));
}

/* The five-second sampler does no storage I/O. Phase boundaries force a sample.
 * Lowest sampled availability is an estimate, not a contiguous allocation size. */
static inline int MemoryStatsSample(memory_meter_t *meter, uint64_t now, int force) {
    if (!force && (meter->samples || meter->rejected) &&
            now - meter->last_sample_ms < 5000) return 0;
    meter->last_sample_ms = now;
    if (!MemoryStatsRead(&meter->current)) { ++meter->rejected; return 0; }
    if (!meter->samples || meter->current.available < meter->lowest_available)
        meter->lowest_available = meter->current.available;
    ++meter->samples;
    return 1;
}

void MemoryStatsAppEvent(const char *phase, const char *app);
#endif
