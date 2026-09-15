#ifndef __MAIN_H
#define __MAIN_H

/* KOS */
#include <kos.h>
#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include <kmg/kmg.h>
#include <zlib/zlib.h>
#include "boot.h"

/* spiral.c */
int spiral_init();
void spiral_frame();

/* menu.c */
int menu_init();
void menu_graphics_init();
void menu_update();
void menu_frame();
void menu_autoboot();
uint32 boot_detect_devices(bool rescan);
int show_message(const char *fmt, ...);

int FileSize(const char *fn);
int FileExists(const char *fn);
int DirExists(const char *dir);

int flashrom_get_region_only();

extern const char	title[];
extern uint32 spiral_color;
extern volatile int start_pressed;
extern uint32 boot_detect_ms;

#define RES_PATH "/rd"

#endif	/* __MAIN_H */
