/* DreamShell launcher. Copyright (C) 2026 SWAT.
 * NeXT list/detail interface and controller navigation by TPMJB and contributors.
 */
#ifndef __LAUNCH_APP_INTERNAL_H
#define __LAUNCH_APP_INTERNAL_H
#include <ds.h>
#include <sfx.h>
#include <tsunami/tsunami.h>
#include "app_module.h"
#include "layout.h"

typedef struct script_item { char name[64]; char file[NAME_MAX]; } script_item_t;
typedef enum { LAUNCH_ITEM_APP, LAUNCH_ITEM_SCRIPT } launch_item_type_t;
typedef enum { LAUNCH_DELETE_NONE, LAUNCH_DELETE_SCRIPT, LAUNCH_DELETE_APP } launch_delete_type_t;

typedef struct launch_item {
    launch_item_type_t type;
    int app_id, order;
    script_item_t *script;
    char name[64], identity[NAME_MAX], preview[NAME_MAX];
    char description[320], category[48], version[32];
    Texture *icon;
    Banner *banner;
    Label *label;
} launch_item_t;

typedef struct { char path[NAME_MAX]; Texture *texture; unsigned int age; } preview_cache_t;
typedef struct {
    App_t *app;
    char app_path[NAME_MAX];
    Font *caption_font;
    launch_item_t *items;
    int item_count, item_capacity, focused_index, first_visible;
    int mouse_visible, pending_activate_index, busy, delete_requested;
    int hat_dir, axis_dir, key_dir, repeat_dir;
    uint64_t repeat_at, selection_time;
    unsigned int selection_generation, preview_generation, cache_age;
    Event_t *input_event;
    Texture *fallback_icon;
    Banner *preview_banner;
    preview_cache_t preview_cache[2];
    Rectangle *selection, *selection_edge;
    Label *title[2], *description[4], *category_label, *counter_label, *clock_label;
    Dialog *delete_dialog;
    script_item_t pending_shortcut;
    launch_delete_type_t pending_delete_type;
    int pending_app_id;
    char pending_app_name[64];
} LaunchAppContext_t;

extern LaunchAppContext_t self;
void ActivateItem(int index);
void BuildAppList(void);
void ClearAllItems(void);
void RebuildAppList(void);
void RememberFocused(void);
void SetFocusedIndex(int index, int sound);
void MoveFocus(int delta);
void RefreshList(void);
void RefreshDetails(void);
void InitScene(void);
void DestroyScene(void);
void ServicePreview(uint64_t now);
void *LaunchAppWorker(void *arg);
int HitTestItem(int x, int y);
int ItemVisibleOnPage(int index);
Texture *LoadSmallTexture(const char *path, unsigned int max_side);
void FitLabel(Label *label, const char *text, float width);
void ClearPendingDelete(void);
int DeletePendingItem(void);
int ItemCanDelete(const launch_item_t *item);
void RequestItemDelete(int x, int y, int prefer_focus);
void ShowItemDelete(int index);
#endif
