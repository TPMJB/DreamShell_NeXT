/* DreamShell - NeXT list/detail rendering. Copyright (C) 2026 SWAT.
 * NeXT enhancements by TPMJB and contributors.
 */
#include "app_internal.h"
#include <ctype.h>

static const Color normal = {1.0f, 0.88f, 0.92f, 0.96f};
static const Color selected = {1.0f, 1.0f, 1.0f, 1.0f};

static void Place(Drawable *d, float x, float y, float z, int visible) {
    Vector v = {x, y, z, 1.0f};
    if(!d) return;
    TSU_DrawableSetTranslate(d, &v);
    TSU_DrawableSetAlpha(d, visible ? 1.0f : 0.0f);
}

/* Labels have measured widths; long names never draw into the adjacent pane. */
void FitLabel(Label *label, const char *text, float width) {
    char s[384];
    size_t n;
    float w, h;
    if(!label) return;
    snprintf(s, sizeof(s), "%s", text ? text : "");
    TSU_LabelSetText(label, s);
    TSU_LabelGetSize(label, &w, &h);
    if(w <= width) return;
    n = strlen(s);
    if(n > sizeof(s) - 4) n = sizeof(s) - 4;
    for(;;) {
        s[n] = '\0';
        snprintf(s + n, sizeof(s) - n, "...");
        TSU_LabelSetText(label, s);
        TSU_LabelGetSize(label, &w, &h);
        if(w <= width) break;
        if(!n) { TSU_LabelSetText(label, ""); break; }
        n--;
        while(n && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
    }
}

static void WrapLabels(Label **labels, int count, const char *text, float width) {
    int line;
    const char *p = text ? text : "";
    for(line = 0; line < count; line++) {
        char s[384];
        size_t n = 0, space = 0;
        float w = 0, h;
        if(!labels[line]) continue;
        while(*p && isspace((unsigned char)*p)) p++;
        if(line == count - 1) { FitLabel(labels[line], p, width); break; }
        while(p[n] && n + 1 < sizeof(s)) {
            s[n] = p[n]; n++; s[n] = '\0';
            TSU_LabelSetText(labels[line], s);
            TSU_LabelGetSize(labels[line], &w, &h);
            if(w > width || s[n - 1] == '\n') { n--; break; }
            if(isspace((unsigned char)s[n - 1])) space = n - 1;
        }
        if(p[n] && space && w > width) n = space;
        if(!n && *p) n = 1;
        s[n] = '\0';
        TSU_LabelSetText(labels[line], s);
        p += n;
    }
}

int ItemVisibleOnPage(int index) {
    return index >= self.first_visible && index < self.first_visible + LIST_ROWS && index < self.item_count;
}

int HitTestItem(int x, int y) {
    int i;
    if(x < LIST_X || x >= LIST_X + LIST_W || y < LIST_Y || y >= LIST_Y + LIST_ROWS * ROW_H) return -1;
    i = self.first_visible + (y - LIST_Y) / ROW_H;
    return i < self.item_count ? i : -1;
}

void RefreshList(void) {
    int i, visible;
    char count[48];
    for(i = 0; i < self.item_count; i++) {
        launch_item_t *item = &self.items[i];
        float y = LIST_Y + (i - self.first_visible) * ROW_H;
        visible = ItemVisibleOnPage(i);
        Place((Drawable *)item->banner, LIST_X + 23, y + 22, 74, visible);
        Place((Drawable *)item->label, LIST_X + 44, y + 29, 74, visible);
        if(item->label) TSU_LabelSetTint(item->label, i == self.focused_index ? &selected : &normal);
    }
    visible = self.focused_index >= 0 && ItemVisibleOnPage(self.focused_index);
    i = self.focused_index - self.first_visible;
    Place((Drawable *)self.selection, LIST_X, LIST_Y + i * ROW_H + ROW_H - 2, 70, visible);
    Place((Drawable *)self.selection_edge, LIST_X, LIST_Y + i * ROW_H + ROW_H - 6, 71, visible);
    snprintf(count, sizeof(count), "%d / %d apps", self.focused_index < 0 ? 0 : self.focused_index + 1, self.item_count);
    if(self.counter_label) TSU_LabelSetText(self.counter_label, count);
}

static void ShowPreview(Texture *texture) {
    float w, h, factor;
    if(!texture) {
        if(self.preview_banner) TSU_DrawableSetAlpha((Drawable *)self.preview_banner, 0);
        return;
    }
    if(!self.preview_banner) {
        self.preview_banner = TSU_BannerCreate(PVR_LIST_TR_POLY, texture);
        if(!self.preview_banner) return;
        TSU_AppSubAddBanner(self.app->tsunami, self.preview_banner);
    } else TSU_BannerSetTexture(self.preview_banner, texture);
    w = TSU_TextureGetW(texture); h = TSU_TextureGetH(texture);
    if(w < 1 || h < 1) return;
    factor = DETAIL_W / w;
    if(factor > 128.0f / h) factor = 128.0f / h;
    TSU_BannerSetSize(self.preview_banner, w * factor, h * factor);
    Place((Drawable *)self.preview_banner, DETAIL_X + DETAIL_W / 2, 240, 73, 1);
}

void RefreshDetails(void) {
    launch_item_t *item;
    char meta[96];
    if(self.focused_index < 0 || self.focused_index >= self.item_count) {
        WrapLabels(self.title, 2, "No apps found", DETAIL_W);
        WrapLabels(self.description, 4, "Install applications in DS/apps, then reopen the launcher.", DETAIL_W);
        if(self.category_label) TSU_LabelSetText(self.category_label, "Applications");
        ShowPreview(self.fallback_icon);
        return;
    }
    item = &self.items[self.focused_index];
    WrapLabels(self.title, 2, item->name, DETAIL_W);
    WrapLabels(self.description, 4, item->description, DETAIL_W);
    snprintf(meta, sizeof(meta), "%s%s%s", item->category, item->version[0] ? "  /  v" : "", item->version);
    FitLabel(self.category_label, meta, DETAIL_W);
    /* Immediate response; the worker fetches optional artwork after selection settles. */
    ShowPreview(item->icon ? item->icon : self.fallback_icon);
}

void SetFocusedIndex(int index, int sound) {
    if(index < -1 || index >= self.item_count || index == self.focused_index) return;
    self.focused_index = index;
    if(index >= 0) {
        if(index < self.first_visible) self.first_visible = index;
        if(index >= self.first_visible + LIST_ROWS) self.first_visible = index - LIST_ROWS + 1;
    }
    self.pending_activate_index = -1;
    self.selection_time = timer_ms_gettime64();
    self.selection_generation++;
    RefreshList();
    RefreshDetails();
    if(sound && index >= 0) ds_sfx_play(DS_SFX_CLICK2);
}

void MoveFocus(int delta) {
    int index;
    if(!self.item_count || self.busy) return;
    self.mouse_visible = 0;
    index = (self.focused_index < 0 ? 0 : self.focused_index) + delta;
    if(index < 0) index = 0;
    if(index >= self.item_count) index = self.item_count - 1;
    SetFocusedIndex(index, 1);
}

/* Run outside the render callback. Cache at most two <=512x512 textures.
 * The scene mutex protects references; wait for GPU completion before eviction.
 */
void ServicePreview(uint64_t now) {
    char path[NAME_MAX];
    unsigned int generation;
    int i, slot = 0;
    Texture *texture;
    LockVideo();
    if(self.busy || self.focused_index < 0 || self.preview_generation == self.selection_generation || now - self.selection_time < PREVIEW_DELAY_MS) {
        UnlockVideo(); return;
    }
    generation = self.selection_generation;
    snprintf(path, sizeof(path), "%s", self.items[self.focused_index].preview);
    for(i = 0; i < 2; i++) {
        if(self.preview_cache[i].texture && !strcmp(path, self.preview_cache[i].path)) {
            self.preview_cache[i].age = ++self.cache_age;
            ShowPreview(self.preview_cache[i].texture);
            self.preview_generation = generation;
            UnlockVideo(); return;
        }
    }
    UnlockVideo();
    texture = LoadSmallTexture(path, 512);
    LockVideo();
    if(self.busy || generation != self.selection_generation || !(self.app->state & APP_STATE_OPENED)) {
        if(texture) TSU_TextureDestroy(&texture);
        UnlockVideo(); return;
    }
    self.preview_generation = generation;
    if(texture) {
        if(self.preview_cache[0].texture && (!self.preview_cache[1].texture || self.preview_cache[1].age < self.preview_cache[0].age)) slot = 1;
        pvr_wait_ready();
        pvr_wait_render_done();
        ShowPreview(texture);
        if(self.preview_cache[slot].texture) TSU_TextureDestroy(&self.preview_cache[slot].texture);
        self.preview_cache[slot].texture = texture;
        self.preview_cache[slot].age = ++self.cache_age;
        snprintf(self.preview_cache[slot].path, sizeof(self.preview_cache[slot].path), "%s", path);
    }
    UnlockVideo();
}

static void DrawCursor(void) { if(self.mouse_visible) SDL_DS_Blit_Cursor(); }

void InitScene(void) {
    int i;
    char name[32];
    self.selection = (Rectangle *)APP_GET_TSU_DRAWABLE("selection");
    self.selection_edge = (Rectangle *)APP_GET_TSU_DRAWABLE("selection_edge");
    self.category_label = (Label *)APP_GET_TSU_DRAWABLE("detail-category");
    self.counter_label = (Label *)APP_GET_TSU_DRAWABLE("app-count");
    self.clock_label = (Label *)APP_GET_TSU_DRAWABLE("clock");
    self.delete_dialog = (Dialog *)APP_GET_TSU_DRAWABLE("delete_dialog");
    for(i = 0; i < 2; i++) {
        snprintf(name, sizeof(name), "detail-title-%d", i);
        self.title[i] = (Label *)APP_GET_TSU_DRAWABLE(name);
    }
    for(i = 0; i < 4; i++) {
        snprintf(name, sizeof(name), "detail-description-%d", i);
        self.description[i] = (Label *)APP_GET_TSU_DRAWABLE(name);
    }
    /* The shared SDL default_app.png is 48x48 and cannot be uploaded directly
     * to the PVR. Our bundled launcher icon is a native 64x64 texture. */
    self.fallback_icon = LoadSmallTexture(self.app->icon, 64);
    TSU_AppSetDrawTransparentPolyEvent(self.app->tsunami, DrawCursor);
}

void DestroyScene(void) {
    int i;
    if(self.preview_banner) {
        TSU_AppSubRemoveBanner(self.app->tsunami, self.preview_banner);
        TSU_BannerDestroy(&self.preview_banner);
    }
    for(i = 0; i < 2; i++) if(self.preview_cache[i].texture) TSU_TextureDestroy(&self.preview_cache[i].texture);
    if(self.fallback_icon) TSU_TextureDestroy(&self.fallback_icon);
}
