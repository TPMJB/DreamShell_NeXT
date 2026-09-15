/* Bounds shared by the ELF reader and host regression tests. */
#ifndef DS_ISOLDR_ELF_CHECK_H
#define DS_ISOLDR_ELF_CHECK_H
#include <stdint.h>
#include <stddef.h>
static inline int isoldr_elf_span(size_t total, uint32_t off, uint32_t count, uint32_t width) {
    return width != 0 && (uint64_t)off + (uint64_t)count * width <= total;
}
#endif
