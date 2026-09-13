/* DreamShell - app discovery and launcher metadata.
 * Copyright (C) 2026 SWAT. NeXT enhancements by TPMJB and contributors.
 */
#include "app_internal.h"
#include <strings.h>

/* Reject oversized image headers before the decoder allocates RAM/VRAM. */
Texture *LoadSmallTexture(const char *path, unsigned int max_side) {
    unsigned char h[64];
    unsigned int w = 0, height = 0;
    file_t fd;
    ssize_t n;
    size_t i;
    Texture *texture;
    if(!path || !path[0]) return NULL;
    fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID) return NULL;
    n = fs_read(fd, h, sizeof(h));
    fs_close(fd);
    if(n >= 24 && !memcmp(h, "\x89PNG\r\n\x1a\n", 8) && !memcmp(h + 12, "IHDR", 4)) {
        w = ((unsigned int)h[16] << 24) | ((unsigned int)h[17] << 16) | (h[18] << 8) | h[19];
        height = ((unsigned int)h[20] << 24) | ((unsigned int)h[21] << 16) | (h[22] << 8) | h[23];
    } else if(n >= 16) {
        for(i = 0; i + 16 <= (size_t)n; i += 4) {
            if(!memcmp(h + i, "PVRT", 4)) {
                w = h[i + 12] | (h[i + 13] << 8);
                height = h[i + 14] | (h[i + 15] << 8);
                break;
            }
        }
    }
    if(!w || !height || w > max_side || height > max_side) return NULL;
    texture = TSU_TextureCreateFromFile(path, true, false, 0);
    if(texture && (TSU_TextureGetW(texture) > (int)max_side || TSU_TextureGetH(texture) > (int)max_side)) {
        TSU_TextureDestroy(&texture);
    }
    return texture;
}

static launch_item_t *NewItem(void) {
    launch_item_t *items;
    int capacity;
    if(self.item_count == self.item_capacity) {
        capacity = self.item_capacity + 16;
        items = realloc(self.items, (size_t)capacity * sizeof(*items));
        if(!items) {
            ds_printf("DS_ERROR: Launcher: insufficient memory to list another app\n");
            return NULL;
        }
        self.items = items;
        self.item_capacity = capacity;
    }
    items = &self.items[self.item_count++];
    memset(items, 0, sizeof(*items));
    items->order = 100;
    return items;
}

static void Metadata(launch_item_t *item, const char *key, mxml_node_t *catalog) {
    mxml_node_t *entry;
    const char *v;
    if(!catalog) return;
    entry = mxmlFindElement(catalog, catalog, "entry", "app", key, MXML_DESCEND);
    if(!entry) return;
    v = mxmlElementGetAttr(entry, "title");
    if(v && v[0]) snprintf(item->name, sizeof(item->name), "%s", v);
    v = mxmlElementGetAttr(entry, "description");
    if(v && v[0]) snprintf(item->description, sizeof(item->description), "%s", v);
    v = mxmlElementGetAttr(entry, "category");
    if(v && v[0]) snprintf(item->category, sizeof(item->category), "%s", v);
    v = mxmlElementGetAttr(entry, "order");
    if(v) item->order = atoi(v);
    v = mxmlElementGetAttr(entry, "preview");
    if(v && v[0]) relativeFilePath_wb(item->preview, self.app->fn, v);
}

static int CompareItems(const void *a, const void *b) {
    const launch_item_t *x = a, *y = b;
    if(x->order != y->order) return x->order < y->order ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

void BuildAppList(void) {
    Item_list_t *apps = GetAppList();
    Item_t *node;
    App_t *app;
    launch_item_t *item;
    mxml_node_t *catalog = NULL;
    file_t fd;
    const dirent_t *ent;
    char path[NAME_MAX], dir[NAME_MAX];
    const char *last = getenv("DS_LAUNCH_LAST");
    int i, selected = 0;

    snprintf(path, sizeof(path), "%s/catalog.xml", self.app_path);
    fd = fs_open(path, O_RDONLY);
    if(fd != FILEHND_INVALID) {
        catalog = mxmlLoadFd(NULL, fd, NULL);
        fs_close(fd);
    }
    for(node = apps ? listGetItemFirst(apps) : NULL; node; node = listGetItemNext(node)) {
        app = node->data;
        if(!app || app->id == self.app->id) continue;
        item = NewItem();
        if(!item) break;
        item->type = LAUNCH_ITEM_APP;
        item->app_id = app->id;
        snprintf(item->name, sizeof(item->name), "%s", app->name);
        snprintf(item->identity, sizeof(item->identity), "%s", app->fn);
        snprintf(item->category, sizeof(item->category), "Application");
        snprintf(item->description, sizeof(item->description), "Open this installed DreamShell application.");
        snprintf(item->version, sizeof(item->version), "%s", app->ver);
        GetAppPath(dir, sizeof(dir), app->fn);
        snprintf(item->preview, sizeof(item->preview), "%s/images/preview.png", dir);
        Metadata(item, app->name, catalog);
        item->icon = LoadSmallTexture(app->icon, 64);
    }
    if(catalog) mxmlDelete(catalog);

    snprintf(path, sizeof(path), "%s/scripts", self.app_path);
    fd = fs_open(path, O_RDONLY | O_DIR);
    if(fd != FILEHND_INVALID) {
        while((ent = fs_readdir(fd)) != NULL) {
            size_t len = strlen(ent->name);
            int lua;
            if(ent->name[0] == '.' || ent->attr || len <= 4) continue;
            lua = !strcasecmp(ent->name + len - 4, ".lua");
            if(!lua && strcasecmp(ent->name + len - 4, ".dsc")) continue;
            item = NewItem();
            if(!item) break;
            item->script = calloc(1, sizeof(*item->script));
            if(!item->script) { self.item_count--; break; }
            item->type = LAUNCH_ITEM_SCRIPT;
            item->order = 200;
            snprintf(item->script->name, sizeof(item->script->name), "%.*s", (int)(len - 4), ent->name);
            snprintf(item->script->file, sizeof(item->script->file), "%s/scripts/%s", self.app_path, ent->name);
            snprintf(item->identity, sizeof(item->identity), "%s", item->script->file);
            /* Underscore-prefixed icon-only shortcuts still need an accessible name. */
            snprintf(item->name, sizeof(item->name), "%s", item->script->name[0] == '_' && item->script->name[1] ? item->script->name + 1 : item->script->name);
            snprintf(item->category, sizeof(item->category), "Shortcut");
            snprintf(item->description, sizeof(item->description), "Launch this saved game or application shortcut with its existing settings.");
            snprintf(item->preview, sizeof(item->preview), "%s/images/%s.png", self.app_path, item->script->name);
            if(!FileExists(item->preview)) snprintf(item->preview, sizeof(item->preview), "%s/images/%s.pvr", self.app_path, item->script->name);
            snprintf(path, sizeof(path), "%s/gui/icons/normal/%s.png", getenv("PATH"), lua ? "lua" : "script");
            item->icon = LoadSmallTexture(path, 64);
        }
        fs_close(fd);
    }
    if(self.item_count > 1) qsort(self.items, self.item_count, sizeof(*self.items), CompareItems);
    for(i = 0; i < self.item_count; i++) {
        Texture *texture;
        item = &self.items[i];
        texture = item->icon ? item->icon : self.fallback_icon;
        if(texture) {
            item->banner = TSU_BannerCreate(PVR_LIST_TR_POLY, texture);
            if(item->banner) {
                TSU_BannerSetSize(item->banner, 24, 24);
                TSU_AppSubAddBanner(self.app->tsunami, item->banner);
            }
        }
        item->label = TSU_LabelCreate(self.caption_font, "", 17, false, false, false);
        if(item->label) {
            FitLabel(item->label, item->name, LIST_W - 56);
            TSU_AppSubAddLabel(self.app->tsunami, item->label);
        }
        if(last && !strcmp(last, item->identity)) selected = i;
    }
    self.focused_index = -1;
    self.first_visible = 0;
    SetFocusedIndex(self.item_count ? selected : -1, 0);
    RefreshList();
    RefreshDetails();
}

void RememberFocused(void) {
    if(self.focused_index >= 0 && self.focused_index < self.item_count)
        setenv("DS_LAUNCH_LAST", self.items[self.focused_index].identity, 1);
}

void ClearAllItems(void) {
    int i;
    /* Caller holds the video lock and has waited for the previous GPU frame. */
    if(self.preview_banner) {
        TSU_AppSubRemoveBanner(self.app->tsunami, self.preview_banner);
        TSU_BannerDestroy(&self.preview_banner);
    }
    for(i = 0; i < self.item_count; i++) {
        launch_item_t *item = &self.items[i];
        if(item->banner) {
            TSU_AppSubRemoveBanner(self.app->tsunami, item->banner);
            TSU_BannerDestroy(&item->banner);
        }
        if(item->label) {
            TSU_AppSubRemoveLabel(self.app->tsunami, item->label);
            TSU_LabelDestroy(&item->label);
        }
        if(item->icon) TSU_TextureDestroy(&item->icon);
        free(item->script);
    }
    self.item_count = 0;
    self.focused_index = -1;
    self.selection_generation++;
}

void RebuildAppList(void) {
    LockVideo();
    pvr_wait_ready();
    pvr_wait_render_done();
    ClearAllItems();
    BuildAppList();
    UnlockVideo();
}

void ActivateItem(int index) {
    App_t *app;
    char path[NAME_MAX];
    int is_script;
    if(index < 0 || index >= self.item_count || self.busy) return;
    RememberFocused();
    ds_sfx_play(DS_SFX_CLICK);
    is_script = self.items[index].type == LAUNCH_ITEM_SCRIPT;
    if(is_script && self.items[index].script) {
        snprintf(path, sizeof(path), "%s", self.items[index].script->file);
        if(!strcasecmp(path + strlen(path) - 4, ".lua")) LuaDo(LUA_DO_FILE, path, GetLuaState());
        else dsystem_script(path);
    } else {
        app = GetAppById(self.items[index].app_id);
        if(app) OpenApp(app, NULL);
    }
}
