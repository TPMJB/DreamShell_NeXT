#ifndef INPUT_SHIM_H
#define INPUT_SHIM_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define _SDL_config_h
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define SDL_HAS_64BIT_TYPE 1
#include <SDL.h>
#include <SDL_dreamcast.h>
#include <SDL_ttf.h>
#define NAME_MAX 255
#define EVENT_TYPE_INPUT 0
#define EVENT_TYPE_VIDEO 1
#define EVENT_PRIO_DEFAULT 0
#define EVENT_PRIO_OVERLAY 1
#define EVENT_ACTION_RENDER 0
#define EVENT_ACTION_UPDATE 1
#define EVENT_ACTION_RENDER_POST 2
#define DS_SHOW_VKB_EVENT (SDL_USEREVENT + 10)
#define DS_HIDE_VKB_EVENT (SDL_USEREVENT + 11)
#define WIDGET_TYPE_TEXTENTRY 7
#define LIST_ITEM_EVENT 1
typedef uint32_t uint32;
typedef struct Event Event_t;
typedef void Event_func(void *, void *, int);
struct Event { const char *name; uint32_t id; Event_func *event; void *param; int active,type,prio; };
typedef struct item { uint32_t id; void *data; const char *name; struct item *next; } Item_t;
typedef struct { Item_t *first; } Item_list_t;
typedef void listFreeItemFunc(void *);
Item_list_t *listMake(void);
void listDestroy(Item_list_t *, listFreeItemFunc *);
Item_t *listGetItemFirst(Item_list_t *);
Item_t *listGetItemNext(Item_t *);
Item_t *listGetItemById(Item_list_t *,uint32_t);
Item_t *listGetItemByName(Item_list_t *,const char *);
Item_t *listAddItem(Item_list_t *,int,const char *,void *,size_t);
void listRemoveItem(Item_list_t *,Item_t *,listFreeItemFunc *);
Event_t *AddEvent(const char *,int,int,Event_func *,void *);
int SetEventActive(Event_t *,int);
int RemoveEvent(Event_t *);
typedef SDL_Rect VideoEventUpdate_t;
void ProcessVideoEventsUpdate(VideoEventUpdate_t *);
void LockVideo(void);
void UnlockVideo(void);
void ScreenChanged(void);
int ScreenUpdated(void);
SDL_Surface *GetScreen(void);
uint64_t timer_ms_gettime64(void);
void ds_printf(const char *,...);
typedef struct { char text[1024]; size_t limit; int refs, type; } GUI_Widget;
typedef GUI_Widget GUI_Object;
typedef GUI_Widget GUI_Screen;
GUI_Screen *GUI_GetScreen(void);
GUI_Widget *GUI_ScreenGetFocusWidget(GUI_Screen *);
int GUI_ScreenGetJoySelectState(GUI_Screen *);
int GUI_WidgetGetType(GUI_Widget *);
const char *GUI_TextEntryGetText(GUI_Widget *);
void GUI_TextEntrySetText(GUI_Widget *,const char *);
void GUI_WidgetClicked(GUI_Widget *,int,int);
void GUI_ObjectIncRef(GUI_Object *);
int GUI_ObjectDecRef(GUI_Object *);
typedef struct { SDL_Surface *ConsoleSurface; int WasUnicode; } ConsoleInformation;
ConsoleInformation *GetConsole(void);
int ConsoleIsVisible(void);
SDL_Event *CON_Events(SDL_Event *);
int VirtKeyboardInit(void);
void VirtKeyboardShutdown(void);
void VirtKeyboardShow(void);
void VirtKeyboardHide(void);
int VirtKeyboardIsVisible(void);
void VirtKeyboardReDraw(void);
void VirtKeyboardToggle(void);
#endif
