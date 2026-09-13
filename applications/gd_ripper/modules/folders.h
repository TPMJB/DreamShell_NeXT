/* Bounded-memory directory navigation. Reads metadata only, on any KOS mount. */
#ifndef GD_FOLDERS_H
#define GD_FOLDERS_H
#define FOLDER_ROWS 7
typedef struct {
    char path[NAME_MAX];
    char names[FOLDER_ROWS + 1][NAME_MAX];
    unsigned count, total, offset;
    int valid;
} folder_browser_t;

static int folder_root(const char *path) {
    const char *roots[] = {"/sd", "/ide", "/pc"};
    for (unsigned i = 0; i < sizeof(roots)/sizeof(roots[0]); ++i) {
        size_t n = strlen(roots[i]);
        if (!strncmp(path, roots[i], n) && (!path[n] || path[n] == '/')) return n;
    }
    return 0;
}

static int folder_parent(char *path) {
    int root = folder_root(path);
    char *slash = strrchr(path, '/');
    if (!root || !slash || slash < path + root) return 0;
    *slash = 0;
    return 1;
}

/* Keep only the nearest seven names on either side of the current page.
 * Even a directory with thousands of games needs less than 3 KB of RAM. */
static int folder_scan(folder_browser_t *b, int direction) {
    char bound[NAME_MAX] = "";
    unsigned before = 0;
    if (b->count && direction)
        strcpy(bound, b->names[direction < 0 ? 0 : b->count-1]);
    b->count = b->total = b->offset = 0; b->valid = 0;
    if (!folder_root(b->path)) return -1;
    file_t fd = fs_open(b->path, O_RDONLY | O_DIR);
    if (fd == FILEHND_INVALID) return -1;
    const dirent_t *ent;
    while ((ent = fs_readdir(fd)) != NULL) {
        if (!(ent->attr & O_DIR) && ent->size >= 0) continue;
        const char *name = ent->name;
        if (!name[0] || !strcmp(name,".") || !strcmp(name,"..") || strchr(name,'/')) continue;
        if (strlen(name) >= NAME_MAX) continue;
        ++b->total;
        int cmp = strcmp(name, bound);
        if (cmp < 0 || (cmp == 0 && direction > 0)) ++before;
        if (direction < 0 ? cmp >= 0 : direction > 0 && cmp <= 0) continue;
        unsigned pos = 0;
        while (pos < b->count && strcmp(b->names[pos], name) < 0) ++pos;
        memmove(b->names[pos+1], b->names[pos], (b->count-pos)*NAME_MAX);
        strcpy(b->names[pos], name);
        if (++b->count > FOLDER_ROWS) {
            --b->count;
            if (direction < 0) memmove(b->names[0], b->names[1], FOLDER_ROWS*NAME_MAX);
        }
    }
    int rv = fs_close(fd);
    if (rv) return -1;
    b->offset = direction < 0 ? before - b->count : direction > 0 ? before : 0;
    b->valid = 1;
    return 0;
}

static int folder_enter(folder_browser_t *b, unsigned row) {
    char path[NAME_MAX];
    if (!b->valid || row >= b->count ||
        snprintf(path, sizeof(path), "%s/%s", b->path, b->names[row]) >= (int)sizeof(path)) return -1;
    strcpy(b->path, path);
    return folder_scan(b, 0);
}
#endif
