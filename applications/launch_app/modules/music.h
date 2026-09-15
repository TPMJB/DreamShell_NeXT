/* Launcher-owned, RAM-backed music. No global audio shutdown. */
#ifndef NEXT_LAUNCHER_MUSIC_H
#define NEXT_LAUNCHER_MUSIC_H
#include <stddef.h>
void MenuMusicOpen(const char *app_path);
void MenuMusicClose(void);
void MenuMusicPoll(void);
void MenuMusicCycle(void);
void MenuMusicSuspend(int suspend);
void MenuMusicLabel(char *text, size_t size);
#endif
