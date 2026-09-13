/* DreamShell - launcher lifecycle and direct controller navigation.
 * Copyright (C) 2026 SWAT. NeXT enhancements by TPMJB and contributors.
 */
#include "app_internal.h"
DEFAULT_MODULE_EXPORTS(app_launch_app);
LaunchAppContext_t self;

static void ResetHeld(void) {
    self.hat_dir = self.axis_dir = self.key_dir = self.repeat_dir = 0;
    self.repeat_at = 0;
}

void LaunchAppDeleteConfirm(Drawable *drawable) {
    (void)drawable;
    /* Execute filesystem changes after releasing the input/scene lock. */
    self.delete_requested = self.pending_delete_type != LAUNCH_DELETE_NONE;
    ResetHeld();
    TSU_DialogHide(self.delete_dialog);
}

void LaunchAppDeleteCancel(Drawable *drawable) {
    (void)drawable;
    ClearPendingDelete();
    ResetHeld();
    TSU_DialogHide(self.delete_dialog);
}

static void UpdateHeld(void) {
    int dir = self.hat_dir ? self.hat_dir : self.key_dir ? self.key_dir : self.axis_dir;
    if(dir != self.repeat_dir) {
        self.repeat_dir = dir;
        if(dir) {
            MoveFocus(dir);
            self.repeat_at = timer_ms_gettime64() + REPEAT_DELAY_MS;
        }
    }
}

static int InOpenButton(int x, int y) {
    return x >= DETAIL_X && x < DETAIL_X + DETAIL_W && y >= OPEN_Y && y < OPEN_Y + OPEN_H;
}

static void HandleDialog(SDL_Event *event) {
    if(event->type == SDL_JOYBUTTONDOWN) {
        if(event->jbutton.button == SDL_DC_B) LaunchAppDeleteCancel(NULL);
        else if(event->jbutton.button == SDL_DC_A) TSU_DialogActivateFocused(self.delete_dialog);
    } else if(event->type == SDL_JOYHATMOTION && !event->jhat.hat) {
        if(event->jhat.value & SDL_HAT_LEFT) TSU_DialogMoveFocus(self.delete_dialog, -1);
        else if(event->jhat.value & SDL_HAT_RIGHT) TSU_DialogMoveFocus(self.delete_dialog, 1);
    } else if(event->type == SDL_KEYDOWN) {
        switch(event->key.keysym.sym) {
            case SDLK_ESCAPE: case SDLK_BACKSPACE: LaunchAppDeleteCancel(NULL); break;
            case SDLK_RETURN: case SDLK_KP_ENTER: TSU_DialogActivateFocused(self.delete_dialog); break;
            case SDLK_LEFT: TSU_DialogMoveFocus(self.delete_dialog, -1); break;
            case SDLK_RIGHT: TSU_DialogMoveFocus(self.delete_dialog, 1); break;
            default: break;
        }
    } else if(event->type == SDL_MOUSEBUTTONDOWN) {
        if(event->button.button == SDL_BUTTON_LEFT) TSU_DialogHandleClick(self.delete_dialog, event->button.x, event->button.y);
        else if(event->button.button == SDL_BUTTON_RIGHT) LaunchAppDeleteCancel(NULL);
    }
}

static void InputHandler(void *ds_event, void *param, int action) {
    SDL_Event *event = param;
    int index, activate = -1, settings = 0, remove = 0;
    (void)ds_event;
    if(action != EVENT_ACTION_UPDATE || !event || !self.app || !(self.app->state & APP_STATE_OPENED)) return;
    LockVideo();
    if(self.busy) { UnlockVideo(); return; }
    if(TSU_DialogIsVisible(self.delete_dialog)) {
        HandleDialog(event);
    } else switch(event->type) {
        case SDL_MOUSEMOTION:
            /* A real mouse selects rows; controller input never warps a cursor. */
            self.mouse_visible = 1;
            index = HitTestItem(event->motion.x, event->motion.y);
            if(index >= 0) SetFocusedIndex(index, 0);
            break;
        case SDL_MOUSEBUTTONDOWN:
            self.mouse_visible = 1;
            ResetHeld();
            if(event->button.button == SDL_BUTTON_LEFT) {
                index = HitTestItem(event->button.x, event->button.y);
                if(index >= 0) SetFocusedIndex(index, 0);
                else if(InOpenButton(event->button.x, event->button.y)) index = self.focused_index;
                self.pending_activate_index = index;
            } else if(event->button.button == SDL_BUTTON_RIGHT) {
                /* B/right click backs out; X/Delete explicitly requests deletion. */
                self.pending_activate_index = -1;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if(event->button.button == SDL_BUTTON_LEFT) {
                index = HitTestItem(event->button.x, event->button.y);
                if(InOpenButton(event->button.x, event->button.y)) index = self.focused_index;
                if(index == self.pending_activate_index) activate = index;
                self.pending_activate_index = -1;
            }
            break;
        case SDL_JOYBUTTONDOWN:
            self.mouse_visible = 0;
            switch(event->jbutton.button) {
                case SDL_DC_A: activate = self.focused_index; break;
                case SDL_DC_START: settings = 1; break;
                case SDL_DC_X: ResetHeld(); RequestItemDelete(0, 0, 1); break;
                case SDL_DC_B: self.pending_activate_index = -1; ResetHeld(); break;
                case SDL_DC_L: MoveFocus(-LIST_ROWS); break;
                case SDL_DC_R: MoveFocus(LIST_ROWS); break;
                default: break;
            }
            break;
        case SDL_JOYHATMOTION:
            if(event->jhat.hat) break;
            self.hat_dir = (event->jhat.value & SDL_HAT_UP) ? -1 : (event->jhat.value & SDL_HAT_DOWN) ? 1 : 0;
            UpdateHeld();
            if(!self.hat_dir) {
                if(event->jhat.value & SDL_HAT_LEFT) MoveFocus(-LIST_ROWS);
                else if(event->jhat.value & SDL_HAT_RIGHT) MoveFocus(LIST_ROWS);
            }
            break;
        case SDL_JOYAXISMOTION:
            if(event->jaxis.axis == 1) {
                self.axis_dir = event->jaxis.value < -ANALOG_THRESHOLD ? -1 : event->jaxis.value > ANALOG_THRESHOLD ? 1 : 0;
                UpdateHeld();
            }
            break;
        case SDL_KEYDOWN:
            switch(event->key.keysym.sym) {
                case SDLK_UP: self.key_dir = -1; UpdateHeld(); break;
                case SDLK_DOWN: self.key_dir = 1; UpdateHeld(); break;
                case SDLK_LEFT: case SDLK_PAGEUP: MoveFocus(-LIST_ROWS); break;
                case SDLK_RIGHT: case SDLK_PAGEDOWN: MoveFocus(LIST_ROWS); break;
                case SDLK_RETURN: case SDLK_KP_ENTER: activate = self.focused_index; break;
                case SDLK_ESCAPE: case SDLK_BACKSPACE: ResetHeld(); break;
                case SDLK_DELETE: ResetHeld(); RequestItemDelete(0, 0, 1); break;
                case SDLK_F1: settings = 1; break;
                default: break;
            }
            break;
        case SDL_KEYUP:
            if(event->key.keysym.sym == SDLK_UP || event->key.keysym.sym == SDLK_DOWN) { self.key_dir = 0; UpdateHeld(); }
            break;
        default: break;
    }
    if(self.delete_requested) {
        self.delete_requested = 0;
        self.busy = 1;
        self.selection_generation++;
        remove = 1;
    }
    UnlockVideo();
    if(remove) {
        /* Refresh even after partial deletion so stale application IDs disappear. */
        DeletePendingItem();
        ClearPendingDelete();
        RebuildAppList();
        LockVideo(); self.busy = 0; UnlockVideo();
    } else if(settings) {
        App_t *app = GetAppByName("Settings");
        RememberFocused();
        if(app) OpenApp(app, NULL);
    } else if(activate >= 0) ActivateItem(activate);
}

void *LaunchAppWorker(void *arg) {
    uint64_t last_clock = 0;
    (void)arg;
    while(self.app && (self.app->state & APP_STATE_OPENED)) {
        uint64_t now = timer_ms_gettime64();
        LockVideo();
        if(!self.busy && !TSU_DialogIsVisible(self.delete_dialog) && self.repeat_dir && now >= self.repeat_at) {
            MoveFocus(self.repeat_dir);
            self.repeat_at = now + REPEAT_INTERVAL_MS;
        }
        if(now - last_clock >= 1000) {
            char text[16];
            time_t seconds = rtc_unix_secs();
            struct tm *tm = localtime(&seconds);
            if(tm && self.clock_label) {
                snprintf(text, sizeof(text), "%02d:%02d", tm->tm_hour, tm->tm_min);
                TSU_LabelSetText(self.clock_label, text);
            }
            last_clock = now;
        }
        UnlockVideo();
        ServicePreview(now);
        thd_sleep(25);
    }
    return NULL;
}

void LaunchApp_Init(App_t *app) {
    memset(&self, 0, sizeof(self));
    self.app = app;
    self.focused_index = self.pending_activate_index = -1;
    if(!app || !app->tsunami) return;
    GetAppPath(self.app_path, sizeof(self.app_path), app->fn);
    self.caption_font = APP_GET_TSU_FONT("caption_font");
    if(!self.caption_font) { ds_printf("DS_ERROR: Launcher font is missing\n"); return; }
    InitScene();
    BuildAppList();
}

void LaunchApp_Open(App_t *app) {
    (void)app;
    if(!self.app || !self.app->tsunami || !self.caption_font) return;
    SDL_DC_EmulateMouse(SDL_FALSE);
    self.mouse_visible = 0;
    self.pending_activate_index = -1;
    ResetHeld();
    if(self.focused_index < 0 && self.item_count) SetFocusedIndex(0, 0);
    self.input_event = AddEvent("LaunchAppInput", EVENT_TYPE_INPUT, EVENT_PRIO_DEFAULT, InputHandler, NULL);
    self.app->thd = thd_create(0, LaunchAppWorker, NULL);
}

void LaunchApp_Close(App_t *app) {
    (void)app;
    RememberFocused();
    if(self.input_event) { RemoveEvent(self.input_event); self.input_event = NULL; }
    /* The core normally joins this worker before calling onclose. */
    if(self.app && self.app->thd) { thd_join(self.app->thd, NULL); self.app->thd = NULL; }
    TSU_DialogHide(self.delete_dialog);
    ResetHeld();
    SDL_DC_EmulateMouse(SDL_TRUE);
}

void LaunchApp_Shutdown(App_t *app) {
    (void)app;
    if(self.input_event) { RemoveEvent(self.input_event); self.input_event = NULL; }
    LockVideo();
    pvr_wait_ready();
    pvr_wait_render_done();
    /* Destroy the preview before the item textures it may reference. */
    DestroyScene();
    ClearAllItems();
    free(self.items);
    self.items = NULL;
    self.app = NULL;
    UnlockVideo();
}
