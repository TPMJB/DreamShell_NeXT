/* Host MMIO checks, not a model of GD-ROM bus timing. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

void run_early_init(void);

int main(void) {
    const size_t size = 4096;
    void *address = (void *)(uintptr_t)0xA05F7000;
    uint8_t *registers = mmap(address, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(registers == address);

    const uint8_t selected[] = {0x00, 0x10, 0xB0, 0xE0, 0xFF};
    for(unsigned hardware = 0; hardware < 16; ++hardware) {
        for(unsigned i = 0; i < sizeof(selected); ++i) {
            /* Poison unrelated registers and vary reserved system-mode bits. */
            memset(registers, 0xA5 ^ hardware ^ i, size);
            *(uint32_t *)(registers + 0x4B0) =
                ((0xDEADBE0F ^ (i << 16)) & ~0xF0U) | (hardware << 4);
            registers[0x98] = selected[i];
            uint8_t expected[4096];
            memcpy(expected, registers, size);
            if(hardware == 0) expected[0x98] = 0x00;

            run_early_init();

            /* Retail selects GD-ROM with one byte; devkit/NAOMI stay untouched. */
            assert(memcmp(registers, expected, size) == 0);
        }
    }
    assert(munmap(registers, size) == 0);
    return 0;
}
