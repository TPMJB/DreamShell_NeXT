#ifndef GD_DESTINATION_H
#define GD_DESTINATION_H
#include "../../game_paths.h"

static const char *gd_default_destination(void) {
    if(DirExists("/ide")) return NEXT_IDE_GAMES_PATH;
    if(DirExists("/sd")) return NEXT_SD_GAMES_PATH;
    if(DirExists("/pc")) return NEXT_PC_GAMES_PATH;
    /* Keep the intended destination visible; never silently rip into RAM. */
    return NEXT_SD_GAMES_PATH;
}
static int gd_prepare_destination(const char *path) {
    if(DirExists(path)) return 0;
    const char *root = !strcmp(path, NEXT_SD_GAMES_PATH) ? "/sd" :
        !strcmp(path, NEXT_IDE_GAMES_PATH) ? "/ide" :
        !strcmp(path, NEXT_PC_GAMES_PATH) ? "/pc" : NULL;
    if(!root || !DirExists(root)) { errno = ENOENT; return -1; }
    if(fs_mkdir(path) == 0 || DirExists(path)) return 0;
    return -1; /* A file named Games or an IO failure must not change the target. */
}
#endif
