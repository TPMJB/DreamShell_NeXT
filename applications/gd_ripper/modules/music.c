/* The launcher and ripper compile the same player, with separate ownership.
 * Ripping may replace the disc: never load a music file from /cd. */
#include "../../launch_app/modules/music.c"

void RipperMusicOpen(const char *root) {
    char path[NAME_MAX];
    int local = root && (!strcmp(root,"/sd") || !strncmp(root,"/sd/",4) ||
                         !strcmp(root,"/ide") || !strncmp(root,"/ide/",5) ||
                         !strcmp(root,"/pc") || !strncmp(root,"/pc/",4));
    if(local && snprintf(path,sizeof(path),"%s/apps/launch_app",root) < (int)sizeof(path))
        MenuMusicOpen(path);
    else MenuMusicOpen(NULL);
}
