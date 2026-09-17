#ifndef KUI_MEMORY_MOCK_H
#define KUI_MEMORY_MOCK_H
#include <stdint.h>
#include <malloc.h>
#include <errno.h>
#include <unistd.h>
uint32_t _arch_mem_top = 0x8d000000u;
static uintptr_t memory_break = 0x8c500000u;
static int memory_growing, memory_reads;
static struct mallinfo memory_info = {
    .arena = 2*1024*1024, .uordblks = 1536*1024, .fordblks = 512*1024
};
static inline void *MemoryMockBreak(int increment) {
    (void)increment;
    errno = EAGAIN;
    return (void *)memory_break;
}
static inline struct mallinfo MemoryMockInfo(void) {
    ++memory_reads;
    if (memory_growing) { memory_break += 4096; --memory_growing; }
    return memory_info;
}
#define sbrk(increment) MemoryMockBreak(increment)
#define mallinfo() MemoryMockInfo()
#endif
