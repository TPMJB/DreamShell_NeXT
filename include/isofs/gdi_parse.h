/* Bounded GDI descriptor parsing, shared by ISOFS and launch checks. */
#ifndef DS_GDI_PARSE_H
#define DS_GDI_PARSE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t number, lba, flags, sector_size, offset;
    char name[128];
} ds_gdi_track;
/* GDI uses ASCII separators and decimal digits. Avoid imported newlib ctype
 * tables here: this parser runs inside independently loaded DreamShell modules. */
static inline int ds_gdi_is_space(unsigned char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}
static inline int ds_gdi_is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}
static inline void ds_gdi_space(const char **p) {
    while(ds_gdi_is_space((unsigned char)**p)) ++*p;
}
static inline int ds_gdi_number(const char **p, uint32_t *value) {
    uint32_t n = 0;
    ds_gdi_space(p);
    if(!ds_gdi_is_digit((unsigned char)**p)) return 0;
    while(ds_gdi_is_digit((unsigned char)**p)) {
        uint32_t digit = *(*p)++ - '0';
        if(n > UINT32_MAX / 10 ||
           (n == UINT32_MAX / 10 && digit > UINT32_MAX % 10)) return 0;
        n = n * 10 + digit;
    }
    if(**p && !ds_gdi_is_space((unsigned char)**p)) return 0;
    *value = n; return 1;
}
static inline int ds_gdi_count(const char *p) {
    uint32_t n;
    if(!ds_gdi_number(&p, &n) || n < 1 || n > 99) return -1;
    ds_gdi_space(&p);
    return *p ? -1 : (int)n;
}
static inline int ds_gdi_parse(const char *p, ds_gdi_track *t) {
    if(!ds_gdi_number(&p, &t->number) || !ds_gdi_number(&p, &t->lba) ||
       !ds_gdi_number(&p, &t->flags) || !ds_gdi_number(&p, &t->sector_size)) return 0;
    ds_gdi_space(&p);
    int quoted = *p == '"';
    if(quoted) ++p;
    size_t n = 0;
    while(*p && (quoted ? *p != '"' : !ds_gdi_is_space((unsigned char)*p))) {
        if(n + 1 >= sizeof(t->name) || (unsigned char)*p < 32) return 0;
        t->name[n++] = *p++;
    }
    t->name[n] = 0;
    if(!n || (quoted && *p++ != '"') || (*p && !ds_gdi_is_space((unsigned char)*p))) return 0;
    if(!ds_gdi_number(&p, &t->offset)) return 0;
    ds_gdi_space(&p);
    return !*p && t->number >= 1 && t->number <= 99 && t->lba < 0x01000000 &&
        (t->flags == 0 || t->flags == 4) &&
        (t->sector_size == 2048 || t->sector_size == 2352);
}
#endif
