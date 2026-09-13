/* DreamShell - launcher app/shortcut management. Copyright (C) 2026 SWAT. */
#include "app_internal.h"

static int DeleteShortcutFiles(const char *file, const char *name) {
    static const char *icon_exts[] = {
        "png",
        "pvr",
        NULL
    };
    char icon_path[NAME_MAX];
    int i;

    if(!file || !name || !name[0]) {
        return 0;
    }

    if(fs_unlink(file) < 0) {
        ds_printf("DS_ERROR: Can't delete shortcut script: %s\n", file);
        return 0;
    }

    for(i = 0; icon_exts[i]; i++) {
        snprintf(icon_path, sizeof(icon_path), "%s/images/%s.%s", self.app_path, name, icon_exts[i]);
        fs_unlink(icon_path);
    }

    return 1;
}

void ClearPendingDelete(void) {
    self.pending_delete_type = LAUNCH_DELETE_NONE;
    self.pending_app_id = 0;
    self.pending_app_name[0] = '\0';
    memset(&self.pending_shortcut, 0, sizeof(self.pending_shortcut));
}

int ItemCanDelete(const launch_item_t *item) {
    if(item == NULL) {
        return 0;
    }

    if(item->type == LAUNCH_ITEM_SCRIPT) {
        return item->script != NULL;
    }

    if(item->type == LAUNCH_ITEM_APP) {
        if(self.app != NULL && item->app_id == self.app->id) {
            return 0;
        }

        return item->app_id > 0;
    }

    return 0;
}

static int DeleteAppItem(App_t *app) {
    char app_dir[NAME_MAX];
    char *slash;

    if(app == NULL || !app->fn[0]) {
        return 0;
    }

    strncpy(app_dir, app->fn, sizeof(app_dir) - 1);
    app_dir[sizeof(app_dir) - 1] = '\0';

    slash = strrchr(app_dir, '/');

    if(slash == NULL) {
        return 0;
    }

    *slash = '\0';

    if(self.app != NULL && app->id == self.app->id) {
        return 0;
    }

    if(!RemoveApp(app)) {
        return 0;
    }

    if(!DirExists(app_dir)) {
        return 1;
    }

    if(!RemoveDirectory(app_dir, 0)) {
        ds_printf("DS_ERROR: Launch app: can't remove app directory '%s'\n", app_dir);
        return 0;
    }

    return 1;
}

int DeletePendingItem(void) {
    App_t *app;

    if(self.pending_delete_type == LAUNCH_DELETE_SCRIPT) {
        if(!self.pending_shortcut.file[0]) {
            return 0;
        }

        return DeleteShortcutFiles(self.pending_shortcut.file, self.pending_shortcut.name);
    }

    if(self.pending_delete_type == LAUNCH_DELETE_APP) {
        app = GetAppById(self.pending_app_id);

        if(app == NULL) {
            return 0;
        }

        return DeleteAppItem(app);
    }

    return 0;
}

void ShowItemDelete(int item_index) {
    launch_item_t *item;
    char body[256];

    if(self.delete_dialog == NULL) {
        return;
    }

    if(item_index < 0 || item_index >= self.item_count) {
        return;
    }

    item = &self.items[item_index];

    if(!ItemCanDelete(item)) {
        return;
    }

    if(item->type == LAUNCH_ITEM_SCRIPT) {
        self.pending_delete_type = LAUNCH_DELETE_SCRIPT;
        self.pending_app_id = 0;
        self.pending_app_name[0] = '\0';
        memcpy(&self.pending_shortcut, item->script, sizeof(self.pending_shortcut));
        snprintf(body, sizeof(body), "Delete shortcut %s?", self.pending_shortcut.name);
    }
    else {
        self.pending_delete_type = LAUNCH_DELETE_APP;
        self.pending_app_id = item->app_id;
        memset(&self.pending_shortcut, 0, sizeof(self.pending_shortcut));
        strncpy(self.pending_app_name, item->name, sizeof(self.pending_app_name) - 1);
        self.pending_app_name[sizeof(self.pending_app_name) - 1] = '\0';
        snprintf(body, sizeof(body), "Delete app %s?", self.pending_app_name);
    }

    self.pending_activate_index = -1;
    TSU_DialogShow(self.delete_dialog, body);
    TSU_DialogSetFocus(self.delete_dialog, 1);
}

static int GetItemDeleteIndexAt(int mx, int my, int prefer_focus) {
    int idx;

    if(prefer_focus &&
        self.focused_index >= 0 &&
        ItemCanDelete(&self.items[self.focused_index]) &&
        ItemVisibleOnPage(self.focused_index)) {
        return self.focused_index;
    }

    idx = HitTestItem(mx, my);

    if(idx >= 0 && ItemCanDelete(&self.items[idx])) {
        return idx;
    }

    if(!prefer_focus &&
        self.focused_index >= 0 &&
        ItemCanDelete(&self.items[self.focused_index]) &&
        ItemVisibleOnPage(self.focused_index)) {
        return self.focused_index;
    }

    return -1;
}

void RequestItemDelete(int mx, int my, int prefer_focus) {
    int idx;

    if(self.delete_dialog == NULL) {
        return;
    }

    idx = GetItemDeleteIndexAt(mx, my, prefer_focus);

    if(idx >= 0) {
        ShowItemDelete(idx);
    }
}
