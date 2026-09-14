#ifndef VMU_UI_LOGIC_H
#define VMU_UI_LOGIC_H
#include <stdbool.h>
#include <string.h>
#include <strings.h>

/* Paths and confirmation gates are shared with the host regression tests. */
static bool vmu_ui_read_only(const char *path) {
    return !path || !*path || !strcmp(path, "/") ||
        (!strncmp(path, "/cd", 3) && (path[3] == 0 || path[3] == '/')) ||
        (!strncmp(path, "/vmd", 4) && (path[4] == 0 || path[4] == '/'));
}
static bool vmu_ui_folder_name(const char *name) {
    if (!name || !*name || !strcmp(name, ".") || !strcmp(name, "..")) return false;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        if (*p < 32 || strchr("/\\:*?\"<>|", *p)) return false;
    size_t n = strlen(name);
    return n <= 80 && name[n-1] != ' ' && name[n-1] != '.';
}
static bool vmu_ui_confirm_ready(bool armed, unsigned buttons, bool keyboard, bool mouse) {
    return armed || (!buttons && !keyboard && !mouse);
}
/* Storage exports append .vms; remove it when restoring shorter VMU names.
 * Native VMU/image filenames are already exact and may themselves end in .vms. */
static inline void vmu_ui_import_name(char out[13], const char *name, bool native) {
    size_t n=strlen(name);
    if(!native && n>=4 && !strcasecmp(name+n-4,".vms")) n-=4;
    if(n>12) n=12;
    memcpy(out,name,n); out[n]=0;
}
#endif
