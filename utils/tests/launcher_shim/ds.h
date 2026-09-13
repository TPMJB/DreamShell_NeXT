#ifndef LAUNCHER_HOST_DS_H
#define LAUNCHER_HOST_DS_H
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#define _SDL_config_h
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define SDL_HAS_64BIT_TYPE 1
#include <SDL.h>
#include <SDL_dreamcast.h>
#include <mxml.h>
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#define DEFAULT_MODULE_EXPORTS(x)
#define FILEHND_INVALID -1
#define O_DIR 0x10000000
#define APP_STATE_OPENED 1
#define PVR_LIST_TR_POLY 2
#define EVENT_TYPE_INPUT 0
#define EVENT_PRIO_DEFAULT 0
#define EVENT_ACTION_UPDATE 1
#define DS_SFX_CLICK 1
#define DS_SFX_CLICK2 2
#define LUA_DO_FILE 1
#define LIST_ITEM_APP 1
#define LIST_ITEM_TSU_DRAWABLE 2
#define LIST_ITEM_TSU_FONT 3
#define APP_GET_TSU_DRAWABLE(n) host_drawable(n)
#define APP_GET_TSU_FONT(n) host_font(n)
typedef int file_t;
typedef struct { char name[256]; int attr; } dirent_t;
typedef struct { int unused; } kthread_t;
typedef struct { float x,y,z,w; } Vector;
typedef struct { float a,r,g,b; } Color;
typedef struct { int w,h; char path[1024]; } Texture;
typedef struct { float left[256],right[256],up[256],down[256]; int exists[256]; } Font;
typedef struct drawable {
 int kind,size,focus,visible; float w,h,alpha,radius; Vector pos; Color color;
 char text[384], name[64]; Texture *texture;
} Drawable;
typedef Drawable Label;
typedef Drawable Banner;
typedef Drawable Rectangle;
typedef Drawable Dialog;
typedef struct { int unused; } DSApp;
typedef struct { int id,state; char fn[NAME_MAX],icon[NAME_MAX],name[64],ver[32]; DSApp *tsunami; kthread_t *thd; } App_t;
typedef struct item { void *data; struct item *next; } Item_t;
typedef struct { Item_t *first; } Item_list_t;
typedef void Event_func(void *,void *,int);
typedef struct { Event_func *fn; } Event_t;
void *host_drawable(const char *name);
Font *host_font(const char *name);
file_t fs_open(const char *, int);
int fs_close(file_t);
ssize_t fs_read(file_t, void *, size_t);
const dirent_t *fs_readdir(file_t);
int fs_unlink(const char *);
int FileExists(const char *);
int DirExists(const char *);
int RemoveDirectory(const char *,int);
int RemoveApp(App_t *);
void GetAppPath(char *,size_t,const char *);
void relativeFilePath_wb(char *,const char *,const char *);
Item_list_t *GetAppList(void);
Item_t *listGetItemFirst(Item_list_t *);
Item_t *listGetItemNext(Item_t *);
App_t *GetAppById(int);
App_t *GetAppByName(const char *);
int OpenApp(App_t *,const char *);
int LuaDo(int,const char *,void *);
void *GetLuaState(void);
int dsystem_script(const char *);
void ds_printf(const char *,...);
void ds_sfx_play(int);
void LockVideo(void);
void UnlockVideo(void);
int pvr_wait_ready(void);
int pvr_wait_render_done(void);
void SDL_DS_Blit_Cursor(void);
uint64_t timer_ms_gettime64(void);
time_t rtc_unix_secs(void);
kthread_t *thd_create(int,void *(*)(void *),void *);
int thd_join(kthread_t *,void **);
void thd_sleep(int);
Event_t *AddEvent(const char *,int,int,Event_func *,void *);
int RemoveEvent(Event_t *);
#endif
