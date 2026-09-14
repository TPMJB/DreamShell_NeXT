/* Checked, exclusive file IO for maintenance tools. Callers own all buffers. */
#ifndef NEXT_MAINTENANCE_IO_H
#define NEXT_MAINTENANCE_IO_H
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <time.h>

#define MA_PATH 512
static inline int ma_persistent(const char *path) {
    const char *p;
    if(!path) return 0;
    if(!strncmp(path, "/sd", 3)) p = path + 3;
    else if(!strncmp(path, "/ide", 4)) p = path + 4;
    else if(!strncmp(path, "/pc", 3)) p = path + 3;
    else return 0;
    while(*p >= '0' && *p <= '9') p++;
    return !*p || *p == '/';
}
static inline const char *ma_default_folder(void) {
    return DirExists("/sd") ? "/sd" : DirExists("/ide") ? "/ide" :
           DirExists("/pc") ? "/pc" : "";
}
static inline int ma_join(char *out, size_t cap, const char *dir, const char *name) {
    if(!*name || strchr(name, '/') || !strcmp(name, ".") || !strcmp(name, "..")) return -1;
    int n = snprintf(out, cap, "%s%s%s", dir, !strcmp(dir, "/") ? "" : "/", name);
    return n < 0 || (size_t)n >= cap ? -1 : 0;
}
static inline int ma_read_exact(file_t fd, void *data, size_t bytes) {
    size_t pos = 0;
    while(pos < bytes) {
        ssize_t n = fs_read(fd, (uint8_t *)data + pos, bytes - pos);
        if(n <= 0 || (size_t)n > bytes - pos) return -1;
        pos += (size_t)n;
    }
    return 0;
}
static inline int ma_write_exact(file_t fd, const void *data, size_t bytes) {
    size_t pos = 0;
    while(pos < bytes) {
        ssize_t n = fs_write(fd, (const uint8_t *)data + pos, bytes - pos);
        if(n <= 0 || (size_t)n > bytes - pos) return -1;
        pos += (size_t)n;
    }
    return 0;
}
static inline int ma_load(const char *path, void *data, size_t size) {
    file_t fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID) return -1;
    int rc = fs_total(fd) == size ? ma_read_exact(fd, data, size) : -1;
    if(fs_close(fd) < 0) rc = -1;
    return rc;
}
/* Timestamp plus collision suffix, with O_EXCL as the final authority. */
static inline file_t ma_create(const char *dir, const char *stem, const char *ext,
        char *path, size_t cap) {
    char name[100];
    unsigned long stamp = (unsigned long)time(NULL);
    if(!ma_persistent(dir) || !DirExists(dir)) return FILEHND_INVALID;
    for(unsigned i = 0; i < 1000; i++) {
        snprintf(name, sizeof(name), "%s-%08lx-%03u.%s", stem, stamp, i, ext);
        if(ma_join(path, cap, dir, name) < 0) break;
        if(FileExists(path) || DirExists(path)) continue;
        file_t fd = fs_open(path, O_WRONLY | O_CREAT | O_EXCL);
        if(fd != FILEHND_INVALID) return fd;
        if(errno != EEXIST) break;
    }
    return FILEHND_INVALID;
}
/* Reopen and byte-compare before a caller is allowed to erase firmware. */
static inline int ma_save_verified(const char *dir, const char *stem, const char *ext,
        const void *data, size_t size, char *path, size_t cap) {
    uint8_t check[4096];
    file_t fd = ma_create(dir, stem, ext, path, cap);
    if(fd == FILEHND_INVALID) return -1;
    int rc = ma_write_exact(fd, data, size);
    if(fs_close(fd) < 0) rc = -1;
    if(rc < 0) return -1; /* Retain partial files for diagnosis; never call it a backup. */
    fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID) return -1;
    if(fs_total(fd) != size) rc = -1;
    for(size_t pos = 0; rc == 0 && pos < size; pos += sizeof(check)) {
        size_t n = size - pos < sizeof(check) ? size - pos : sizeof(check);
        if(ma_read_exact(fd, check, n) < 0 || memcmp(check, (const uint8_t *)data + pos, n)) rc = -1;
    }
    if(fs_close(fd) < 0) rc = -1;
    return rc;
}
static inline void ma_append(char *buffer, size_t cap, const char *format, ...) {
    size_t used = strlen(buffer);
    if(used >= cap - 1) return;
    va_list args; va_start(args, format);
    vsnprintf(buffer + used, cap - used, format, args);
    va_end(args);
}
#endif
