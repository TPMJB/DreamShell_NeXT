/* Minimal ISO Loader libc helper used by FatFs R0.16's filename checks. */
#include <string.h>

char *strchr(const char *s, int c) {
    const unsigned char ch = (unsigned char)c;
    do {
        if ((unsigned char)*s == ch) return (char *)s;
    } while (*s++);
    return NULL;
}
