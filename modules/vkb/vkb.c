/* DreamShell virtual keyboard.
 * Original keyboard (C) 2007-2023 SWAT.
 * QWERTY editor / input isolation: DreamShell NeXT, TPMJB, 2026.
 */
#include "ds.h"

#define VKB_TEXT_MAX 1024
#define VKB_KEYS 35
#define VKB_WIDTH 608
#define VKB_HEIGHT 296

enum { KEY_SHIFT = 256, KEY_ERASE, KEY_PAGE, KEY_LEFT, KEY_RIGHT, KEY_DONE };
typedef struct { SDL_Rect rect; int row, value; const char *label; } Key;
static struct {
    int initialized, visible, dirty, selected, shift, symbols, console;
    int direction, stick_x, stick_y, mouse_key;
    uint32_t repeat_at;
    size_t cursor;
    char text[VKB_TEXT_MAX], original[VKB_TEXT_MAX];
    GUI_Widget *target;
    SDL_Surface *surface;
    TTF_Font *font, *small;
    Event_t *input, *video;
    Key keys[VKB_KEYS];
} vkb;

static void keyboard_input(void *, void *, int);
static void keyboard_draw(void *, void *, int);

static void layout(void) {
    const char *letters[] = { "qwertyuiop", "asdfghjkl", "zxcvbnm" };
    const char *symbols[2][3] = {
        { "1234567890", "@#$%&*()-", ".,:;!?=" },
        { "!@#$%^&*()", "[]{}<>\\|~", "'\"`:+-=" }
    };
    int k = 0;
    for (int row = 0; row < 3; ++row) {
        if (row == 2) vkb.keys[k++] = (Key){{12,172,72,38},2,KEY_SHIFT,"Shift"};
        const char *p = vkb.symbols ? symbols[vkb.shift][row] : letters[row];
        for (int col = 0; p[col]; ++col) {
            int ch = p[col];
            if (!vkb.symbols && vkb.shift && ch >= 'a' && ch <= 'z') ch -= 'a'-'A';
            vkb.keys[k++] = (Key){{(row == 0 ? 12 : row == 1 ? 41 : 88) + col*58,
                84+row*44,54,38},row,ch,NULL};
        }
        if (row == 2) vkb.keys[k++] = (Key){{494,172,98,38},2,KEY_ERASE,"Backspace"};
    }
    vkb.keys[k++] = (Key){{12,216,80,38},3,KEY_PAGE,vkb.symbols ? "ABC" : "123 #+="};
    vkb.keys[k++] = (Key){{96,216,48,38},3,'_',NULL};
    vkb.keys[k++] = (Key){{148,216,48,38},3,'/',NULL};
    vkb.keys[k++] = (Key){{200,216,52,38},3,KEY_LEFT,"<"};
    vkb.keys[k++] = (Key){{256,216,168,38},3,' ',"Space"};
    vkb.keys[k++] = (Key){{428,216,52,38},3,KEY_RIGHT,">"};
    vkb.keys[k++] = (Key){{484,216,108,38},3,KEY_DONE,"Done"};
}

static void send_console(int ch) {
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_KEYDOWN;
    event.key.state = SDL_PRESSED;
    event.key.keysym.sym = ch;
    event.key.keysym.unicode = ch >= 32 && ch <= 126 ? ch : 0;
    CON_Events(&event);
}

/* Directly edit the focused field once. No queued synthetic key can be
 * replayed by both the app's input handler and GUI_Input. */
static int apply_text(const char *next, size_t cursor) {
    if (vkb.target) {
        if (GUI_ScreenGetFocusWidget(GUI_GetScreen()) != vkb.target) return 0;
        GUI_TextEntrySetText(vkb.target, next);
        if (strcmp(GUI_TextEntryGetText(vkb.target), next)) return 0;
    }
    strcpy(vkb.text, next);
    vkb.cursor = cursor;
    vkb.dirty = 1;
    return 1;
}

static void insert_char(int ch) {
    char next[VKB_TEXT_MAX];
    size_t len = strlen(vkb.text);
    if (ch < 32 || ch > 126 || len + 1 >= sizeof(next)) return;
    memcpy(next, vkb.text, vkb.cursor);
    next[vkb.cursor] = ch;
    memcpy(next+vkb.cursor+1, vkb.text+vkb.cursor, len-vkb.cursor+1);
    apply_text(next, vkb.cursor+1);
}

static void erase_char(int forward) {
    char next[VKB_TEXT_MAX];
    size_t at = vkb.cursor, len = strlen(vkb.text);
    if (forward ? at == len : !at) return;
    if (!forward) --at;
    memcpy(next, vkb.text, at);
    memcpy(next+at, vkb.text+at+1, len-at);
    apply_text(next, at);
}

static void finish(int cancel) {
    GUI_Widget *target = vkb.target;
    if (target && GUI_ScreenGetFocusWidget(GUI_GetScreen()) == target) {
        if (cancel) GUI_TextEntrySetText(target, vkb.original);
        /* Clicked performs the normal blur callback, including folder validation. */
        GUI_WidgetClicked(target, 0, 0);
    } else if (vkb.console && !cancel) {
        for (const char *p = vkb.text; *p; ++p) send_console(*p);
        send_console(SDLK_RETURN);
    }
    VirtKeyboardHide();
}

static void activate(int key) {
    switch (key) {
        case KEY_SHIFT: vkb.shift = !vkb.shift; layout(); break;
        case KEY_PAGE: vkb.symbols = !vkb.symbols; layout(); break;
        case KEY_ERASE: erase_char(0); break;
        case KEY_LEFT: if (vkb.cursor) --vkb.cursor; break;
        case KEY_RIGHT: if (vkb.text[vkb.cursor]) ++vkb.cursor; break;
        case KEY_DONE: finish(0); break;
        default: insert_char(key); break;
    }
    vkb.dirty = 1;
}

/* Up/down follow the nearest key center; left/right wrap within a row. */
static void navigate(int direction) {
    Key *from = &vkb.keys[vkb.selected];
    int best = vkb.selected, distance = 10000;
    if (direction == 1 || direction == 2) {
        int step = direction == 1 ? -1 : 1;
        int i = (vkb.selected + step + VKB_KEYS) % VKB_KEYS;
        if (vkb.keys[i].row == from->row) best = i;
        else {
            for (i = 0; i < VKB_KEYS; ++i) if (vkb.keys[i].row == from->row) {
                best = i;
                if (step == 1) break;
            }
        }
    } else {
        int row = (from->row + (direction == 3 ? 3 : 1)) % 4;
        int center = from->rect.x*2 + from->rect.w;
        for (int i = 0; i < VKB_KEYS; ++i) if (vkb.keys[i].row == row) {
            int d = abs(vkb.keys[i].rect.x*2 + vkb.keys[i].rect.w - center);
            if (d < distance) { best = i; distance = d; }
        }
    }
    vkb.selected = best;
    vkb.dirty = 1;
}

static void hold_direction(int direction) {
    if (direction != vkb.direction) {
        vkb.direction = direction;
        if (direction) navigate(direction);
        vkb.repeat_at = timer_ms_gettime64() + 350;
    }
}

static int load_graphics(void) {
    if (vkb.surface) return 1;
    char path[NAME_MAX];
    const char *root = getenv("PATH");
    if (!root || snprintf(path, sizeof(path), "%s/fonts/ttf/arial_lite.ttf", root) >= (int)sizeof(path)) return 0;
    vkb.font = TTF_OpenFont(path, 18);
    vkb.small = TTF_OpenFont(path, 14);
    vkb.surface = SDL_CreateRGBSurface(SDL_SWSURFACE, VKB_WIDTH, VKB_HEIGHT, 16, 0xf800, 0x07e0, 0x001f, 0);
    if (vkb.font && vkb.small && vkb.surface) return 1;
    if (vkb.font) TTF_CloseFont(vkb.font);
    if (vkb.small) TTF_CloseFont(vkb.small);
    if (vkb.surface) SDL_FreeSurface(vkb.surface);
    vkb.font = vkb.small = NULL; vkb.surface = NULL;
    ds_printf("DS_ERROR: Unable to load keyboard font or surface.\n");
    return 0;
}

int VirtKeyboardInit(void) {
    if (vkb.initialized) return 0;
    memset(&vkb, 0, sizeof(vkb));
    vkb.input = AddEvent("VirtKeyboardInput", EVENT_TYPE_INPUT, EVENT_PRIO_OVERLAY, keyboard_input, NULL);
    vkb.video = AddEvent("VirtKeyboardVideo", EVENT_TYPE_VIDEO, EVENT_PRIO_OVERLAY, keyboard_draw, NULL);
    if (!vkb.input || !vkb.video) { VirtKeyboardShutdown(); return -1; }
    SetEventActive(vkb.video, 0);
    vkb.initialized = 1;
    return 0;
}

void VirtKeyboardShow(void) {
    if (!vkb.initialized || vkb.visible) return;
    LockVideo();
    GUI_Widget *target = GUI_ScreenGetFocusWidget(GUI_GetScreen());
    if (target && GUI_WidgetGetType(target) != WIDGET_TYPE_TEXTENTRY) target = NULL;
    vkb.console = ConsoleIsVisible();
    if ((!target && !vkb.console) || !load_graphics()) { UnlockVideo(); return; }
    const char *text = target ? GUI_TextEntryGetText(target) : "";
    if (strlen(text) >= sizeof(vkb.text)) { UnlockVideo(); return; }
    strcpy(vkb.text, text); strcpy(vkb.original, text);
    vkb.cursor = strlen(text);
    vkb.target = target;
    if (target) GUI_ObjectIncRef((GUI_Object *)target);
    vkb.selected = 0; vkb.shift = vkb.symbols = vkb.direction = 0;
    vkb.stick_x = vkb.stick_y = 0; vkb.mouse_key = -1;
    layout();
    vkb.visible = vkb.dirty = 1;
    SDL_DC_EmulateMouse(SDL_FALSE);
    SetEventActive(vkb.video, 1);
    UnlockVideo();
}

void VirtKeyboardHide(void) {
    if (!vkb.visible) return;
    LockVideo();
    vkb.visible = 0; vkb.direction = 0;
    SetEventActive(vkb.video, 0);
    if (vkb.target) GUI_ObjectDecRef((GUI_Object *)vkb.target);
    vkb.target = NULL;
    VideoEventUpdate_t area = {0,0,640,480};
    ProcessVideoEventsUpdate(&area);
    /* Restore controller mouse emulation only for apps that use it. */
    if (!ConsoleIsVisible()) SDL_DC_EmulateMouse(GUI_ScreenGetJoySelectState(GUI_GetScreen()) ? SDL_TRUE : SDL_FALSE);
    UnlockVideo();
}

void VirtKeyboardShutdown(void) {
    /* Video traversal also visits input nodes in the shared event list. */
    LockVideo();
    /* Core shutdown tears down the event list before unloading modules. */
    if (GetEventList()) {
        VirtKeyboardHide();
        if (vkb.input) RemoveEvent(vkb.input);
        if (vkb.video) RemoveEvent(vkb.video);
    } else if (vkb.target) GUI_ObjectDecRef((GUI_Object *)vkb.target);
    if (vkb.surface) SDL_FreeSurface(vkb.surface);
    if (vkb.font) TTF_CloseFont(vkb.font);
    if (vkb.small) TTF_CloseFont(vkb.small);
    memset(&vkb, 0, sizeof(vkb));
    UnlockVideo();
}

int VirtKeyboardIsVisible(void) { return vkb.visible; }
void VirtKeyboardToggle(void) { if (vkb.visible) finish(0); else VirtKeyboardShow(); }
void VirtKeyboardReDraw(void) { if (vkb.visible) vkb.dirty = 1; }

static int hit_key(int x, int y) {
    SDL_Surface *screen = GetScreen();
    x -= (screen->w - VKB_WIDTH)/2; y -= screen->h - VKB_HEIGHT - 16;
    for (int i = 0; i < VKB_KEYS; ++i) {
        SDL_Rect *r = &vkb.keys[i].rect;
        if (x >= r->x && x < r->x+r->w && y >= r->y && y < r->y+r->h) return i;
    }
    return -1;
}

static void keyboard_input(void *ds_event, void *param, int action) {
    SDL_Event *e = param;
    (void)ds_event;
    if (action != EVENT_ACTION_UPDATE || !e) return;
    LockVideo();
    if (e->type == DS_SHOW_VKB_EVENT) { VirtKeyboardShow(); e->type = 0; }
    else if (e->type == DS_HIDE_VKB_EVENT) { VirtKeyboardHide(); e->type = 0; }
    else if (!vkb.visible) {
        if (e->type == SDL_JOYBUTTONDOWN && e->jbutton.button == SDL_DC_START &&
                (ConsoleIsVisible() || GUI_ScreenGetFocusWidget(GUI_GetScreen()))) {
            VirtKeyboardShow(); if (vkb.visible) e->type = 0;
        }
    } else if (vkb.target && GUI_ScreenGetFocusWidget(GUI_GetScreen()) != vkb.target) {
        VirtKeyboardHide();
    } else {
        switch (e->type) {
            case SDL_KEYDOWN:
                switch (e->key.keysym.sym) {
                    case SDLK_ESCAPE: finish(1); break;
                    case SDLK_RETURN: finish(0); break;
                    case SDLK_BACKSPACE: erase_char(0); break;
                    case SDLK_DELETE: erase_char(1); break;
                    case SDLK_LEFT: activate(KEY_LEFT); break;
                    case SDLK_RIGHT: activate(KEY_RIGHT); break;
                    case SDLK_HOME: vkb.cursor = 0; break;
                    case SDLK_END: vkb.cursor = strlen(vkb.text); break;
                    default: insert_char(e->key.keysym.unicode); break;
                }
                vkb.dirty = 1;
                break;
            case SDL_JOYHATMOTION:
                if (!e->jhat.hat) hold_direction(e->jhat.value & SDL_HAT_LEFT ? 1 :
                    e->jhat.value & SDL_HAT_RIGHT ? 2 : e->jhat.value & SDL_HAT_UP ? 3 :
                    e->jhat.value & SDL_HAT_DOWN ? 4 : 0);
                break;
            case SDL_JOYAXISMOTION:
                if (e->jaxis.axis <= 1) {
                    int dir = e->jaxis.value < -48 ? -1 : e->jaxis.value > 48 ? 1 : 0;
                    if (!e->jaxis.axis) vkb.stick_x = dir; else vkb.stick_y = dir;
                    hold_direction(vkb.stick_y ? (vkb.stick_y < 0 ? 3 : 4) :
                        vkb.stick_x ? (vkb.stick_x < 0 ? 1 : 2) : 0);
                }
                break;
            case SDL_JOYBUTTONDOWN:
                if (e->jbutton.button == SDL_DC_A) activate(vkb.keys[vkb.selected].value);
                else if (e->jbutton.button == SDL_DC_B) finish(1);
                else if (e->jbutton.button == SDL_DC_X) activate(KEY_ERASE);
                else if (e->jbutton.button == SDL_DC_Y) activate(KEY_SHIFT);
                else if (e->jbutton.button == SDL_DC_START) finish(0);
                break;
            case SDL_MOUSEMOTION: {
                int key = hit_key(e->motion.x, e->motion.y);
                if (key >= 0) { vkb.selected = key; vkb.dirty = 1; }
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
                if (e->button.button == SDL_BUTTON_LEFT) vkb.mouse_key = hit_key(e->button.x, e->button.y);
                break;
            case SDL_MOUSEBUTTONUP: {
                int key = hit_key(e->button.x, e->button.y);
                if (e->button.button == SDL_BUTTON_LEFT && key >= 0 && key == vkb.mouse_key)
                    activate(vkb.keys[key].value);
                vkb.mouse_key = -1;
                break;
            }
            default: break;
        }
        /* Keep all physical input in the modal editor, including releases. */
        if (e->type >= SDL_KEYDOWN && e->type <= SDL_JOYBUTTONUP) e->type = 0;
    }
    UnlockVideo();
}

static void text_at(TTF_Font *font, const char *text, SDL_Rect box, int centered, SDL_Color color) {
    if (!*text) return;
    SDL_Surface *s = TTF_RenderText_Blended(font, text, color);
    if (!s) return;
    SDL_Rect clip;
    SDL_GetClipRect(vkb.surface, &clip);
    SDL_SetClipRect(vkb.surface, &box);
    SDL_Rect dst = {box.x + (centered ? (box.w-s->w)/2 : 0), box.y+(box.h-s->h)/2,0,0};
    SDL_BlitSurface(s, NULL, vkb.surface, &dst);
    SDL_SetClipRect(vkb.surface, &clip);
    SDL_FreeSurface(s);
}

static void paint(void) {
    SDL_Color ink = {235,242,248,255}, muted = {164,184,204,255};
    SDL_FillRect(vkb.surface, NULL, SDL_MapRGB(vkb.surface->format,20,29,41));
    text_at(vkb.font, vkb.console ? "Console command" : "Edit text", (SDL_Rect){12,8,300,24},0,ink);
    text_at(vkb.small, vkb.shift ? "SHIFT ON" : "", (SDL_Rect){480,8,112,24},1,muted);
    SDL_Rect preview = {12,40,580,32};
    SDL_FillRect(vkb.surface, &preview, SDL_MapRGB(vkb.surface->format,37,51,68));
    /* Scroll the preview to keep the insertion point visible. */
    char before[VKB_TEXT_MAX]; size_t start = 0; int width = 0, height;
    memcpy(before, vkb.text, vkb.cursor); before[vkb.cursor] = 0;
    TTF_SizeText(vkb.font, before, &width, &height);
    while (width > 548 && start < vkb.cursor) TTF_SizeText(vkb.font, before + ++start, &width, &height);
    text_at(vkb.font, vkb.text+start, (SDL_Rect){20,40,564,32},0,ink);
    SDL_Rect cursor = {20+width,46,2,20};
    SDL_FillRect(vkb.surface, &cursor, SDL_MapRGB(vkb.surface->format,83,225,227));
    for (int i = 0; i < VKB_KEYS; ++i) {
        Key *k = &vkb.keys[i]; SDL_Rect inner = k->rect;
        int selected = i == vkb.selected;
        SDL_FillRect(vkb.surface, &inner, SDL_MapRGB(vkb.surface->format,
            selected ? 83 : 50, selected ? 225 : 68, selected ? 227 : 88));
        inner.x += 2; inner.y += 2; inner.w -= 4; inner.h -= 4;
        SDL_FillRect(vkb.surface, &inner, SDL_MapRGB(vkb.surface->format,
            selected ? 25 : 35, selected ? 88 : 47, selected ? 106 : 62));
        char ch[] = {(char)k->value,0};
        text_at(k->label ? vkb.small : vkb.font, k->label ? k->label : ch, inner,1,ink);
    }
    text_at(vkb.small, "D-pad / stick: move   A: type   X: erase   Y: shift", (SDL_Rect){12,260,580,16},1,muted);
    text_at(vkb.small, "START: done   B: cancel   Mouse / keyboard supported", (SDL_Rect){12,277,580,16},1,muted);
    vkb.dirty = 0;
}

static void keyboard_draw(void *event, void *param, int action) {
    (void)event; (void)param;
    if (!vkb.visible) return;
    if (action == EVENT_ACTION_UPDATE) { vkb.dirty = 1; return; }
    if (action != EVENT_ACTION_RENDER) return;
    if (vkb.direction && (int32_t)((uint32_t)timer_ms_gettime64() - vkb.repeat_at) >= 0) {
        navigate(vkb.direction); vkb.repeat_at = timer_ms_gettime64()+100;
    }
    if (!vkb.dirty && ScreenUpdated() && !ConsoleIsVisible()) return;
    if (vkb.dirty) paint();
    SDL_Surface *dst = ConsoleIsVisible() ? GetConsole()->ConsoleSurface : GetScreen();
    SDL_Rect area = {(dst->w-VKB_WIDTH)/2,dst->h-VKB_HEIGHT-16,0,0};
    SDL_BlitSurface(vkb.surface, NULL, dst, &area);
    if (ConsoleIsVisible()) GetConsole()->WasUnicode = 1; else ScreenChanged();
}
