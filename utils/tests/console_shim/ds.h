#ifndef GD_HOST_SHIM_H
#define GD_HOST_SHIM_H
/* Host VFS/GUI shim. Drive wire types are checked against the pinned KOS headers
 * during development; the release gate compiles against real SH-4 headers. */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#define _SDL_config_h
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define SDL_HAS_64BIT_TYPE 1
#include <SDL.h>
#include <SDL_dreamcast.h>
#define NAME_MAX 255
#define CMD_OK 0
#define CMD_ERROR -1
#define FILEHND_INVALID -1
#define O_DIR 0x10000000
#define DEFAULT_MODULE_EXPORTS(x)
#define APP_STATE_OPENED 1
#define WIDGET_INSIDE 2
#define WIDGET_HIDDEN 4
#define WIDGET_DISABLED 0x1000
#define EVENT_TYPE_INPUT 0
#define EVENT_TYPE_VIDEO 1
#define EVENT_PRIO_DEFAULT 0
#define EVENT_ACTION_UPDATE 1
#define EVENT_ACTION_RENDER 0
#define APP_GET_WIDGET(n) host_widget(n)
#define APP_GET_FONT(n) NULL
typedef unsigned irq_mask_t;
static inline irq_mask_t irq_disable(void) { return 0; }
static inline void irq_restore(irq_mask_t m) { (void)m; }
typedef int file_t;
typedef struct { char name[256]; int size; int attr; } dirent_t;
typedef struct { int flags, state; char text[512]; } GUI_Widget;
typedef GUI_Widget GUI_Screen;
typedef struct { int unused; } kthread_t;
typedef struct { int state; kthread_t *thd; char *fn; } App_t;
typedef struct { int unused; } Event_t;
typedef void Event_func(void *,void *,int);
SDL_Rect GUI_FontGetTextSize(void *, const char *);
GUI_Widget *host_widget(const char *name);
GUI_Widget *GUI_ButtonGetCaption(GUI_Widget *w);
GUI_Screen *GUI_GetScreen(void);
GUI_Widget *GUI_ScreenGetFocusWidget(GUI_Screen *s);
void GUI_ScreenEvent(GUI_Screen *s, const SDL_Event *e,int x,int y);
void GUI_ScreenSetJoySelectState(GUI_Screen *s,int v);
void GUI_WidgetClicked(GUI_Widget *w,int x,int y);
char *GUI_LabelGetText(GUI_Widget *w);
void GUI_LabelSetText(GUI_Widget *w,const char *text);
void GUI_LabelSetTextColor(GUI_Widget *w,int r,int g,int b);
void GUI_WidgetSetEnabled(GUI_Widget *w,int v);
int GUI_WidgetGetState(GUI_Widget *w);
void GUI_WidgetSetState(GUI_Widget *w,int v);
int GUI_WidgetGetFlags(GUI_Widget *w);
void GUI_WidgetSetFlags(GUI_Widget *w,int v);
void GUI_WidgetClearFlags(GUI_Widget *w,int v);
void GUI_TextEntrySetText(GUI_Widget *w,const char *t);
const char *GUI_TextEntryGetText(GUI_Widget *w);
void GUI_ProgressBarSetPosition(GUI_Widget *w,double v);
void GUI_CardStackShowIndex(GUI_Widget *w,int i);
void GUI_EnableInput(void);
void GUI_DisableInput(void);
int OpenMainApp(void);
int ConsoleIsVisible(void);
Event_t *AddEvent(const char *,int,int,Event_func *,void *);
int RemoveEvent(Event_t *e);
int SetEventActive(Event_t *e,int active);
uint64_t timer_ms_gettime64(void);
kthread_t *thd_create(int detached,void *(*fn)(void*),void *arg);
int thd_join(kthread_t *t,void **rv);
void thd_sleep(unsigned ms);
void thd_pass(void);
void ds_printf(const char *, ...);
const char *lib_get_name(void);
void GetAppPath(char *,size_t,const char *);
int FileExists(const char *);
int DirExists(const char *);
file_t fs_open(const char *,int);
ssize_t fs_total(file_t);
ssize_t fs_read(file_t,void *,size_t);
ssize_t fs_write(file_t,const void *,size_t);
int fs_close(file_t);
off_t fs_seek(file_t,off_t,int);
int fs_complete(file_t,ssize_t *);
int fs_unlink(const char *);
int fs_mkdir(const char *);
const dirent_t *fs_readdir(file_t);
#endif
