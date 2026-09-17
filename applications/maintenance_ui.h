/* NeXT maintenance shell: native focus, a shared picker, and bounded operations.
 * All operation callbacks run on the input thread; the renderer owns no work. */
#ifndef NEXT_MAINTENANCE_UI_H
#define NEXT_MAINTENANCE_UI_H
#include "utility_ui.h"
#include "maintenance_io.h"

typedef struct {
    App_t *app;
    Event_t *input;
    GUI_Widget *rows[6], *actions[3], *menu, *main, *browser, *fm, *dialog;
    GUI_Widget *path, *status, *note, *detail[2], *progress;
    int focus, busy, cancel, asking, browsing, folder, purpose, progress_width;
    void (*row)(int, int);
    void (*action)(int);
    void (*confirm)(int);
    void (*picked)(int, const char *);
    void (*back)(void);
    void (*opened)(void);
    void (*closed)(void);
} maintenance_ui;
static maintenance_ui ui;

static GUI_Widget *ma_widget(const char *name) {
    return getAppElement(ui.app, name, LIST_ITEM_GUI_WIDGET);
}
static void ma_text(GUI_Widget *w, const char *format, ...) {
    char text[256]; va_list args; va_start(args, format);
    vsnprintf(text, sizeof(text), format, args); va_end(args);
    GUI_LabelSetText(w, text);
}
static void ma_row(int row, const char *format, ...) {
    char text[160]; va_list args; va_start(args, format);
    vsnprintf(text, sizeof(text), format, args); va_end(args);
    GUI_LabelSetText(GUI_ButtonGetCaption(ui.rows[row]), text);
}
/* Visible paths use the tail; the original path always stays intact for IO. */
static const char *ma_tail(const char *path, size_t count) {
    size_t n = strlen(path); return n > count ? path + n - count : path;
}
static void ma_status(const char *text, int error) {
    GUI_LabelSetText(ui.status, text);
    GUI_LabelSetTextColor(ui.status, error ? 255 : 83, error ? 185 : 225, error ? 114 : 227);
}
static void ma_note(const char *text) { GUI_LabelSetText(ui.note, ma_tail(text, 87)); }
static void ma_progress(size_t done, size_t total) {
    if(done > total) done = total;
    GUI_WidgetSetSize(ui.progress, total ? (int)((double)ui.progress_width * done / total) : 0, 4);
}
static GUI_Widget *ma_focused(void) {
    return ui.focus < 6 ? ui.rows[ui.focus] : ui.focus < 9 ? ui.actions[ui.focus - 6] : ui.menu;
}
static void ma_focus(void) {
    for(int i = 0; i < 6; i++) GUI_WidgetClearFlags(ui.rows[i], WIDGET_INSIDE);
    for(int i = 0; i < 3; i++) GUI_WidgetClearFlags(ui.actions[i], WIDGET_INSIDE);
    GUI_WidgetClearFlags(ui.menu, WIDGET_INSIDE);
    if(!ui.browsing) GUI_WidgetSetFlags(ma_focused(), WIDGET_INSIDE);
}
static int ma_pump(void) {
    SDL_Event e;
    while(SDL_PollEvent(&e)) if(utility_key(&e) == UI_BACK) ui.cancel = 1;
    thd_pass(); return !ui.cancel;
}
static void ma_busy(int busy) {
    ui.busy = busy;
    if(busy) { ui.cancel = 0; ma_progress(0, 1); }
    for(int i = 0; i < 6; i++) GUI_WidgetSetEnabled(ui.rows[i], !busy);
    for(int i = 0; i < 3; i++) GUI_WidgetSetEnabled(ui.actions[i], !busy);
    GUI_WidgetSetEnabled(ui.menu, !busy);
    if(!busy) {
        /* Discard held/queued commands from a long critical section. */
        SDL_Event e; while(SDL_PollEvent(&e)) {}
        ma_focus();
    }
}
static void ma_ask(int action, const char *title, const char *body) {
    if(ui.busy) return;
    ui.asking = action;
    GUI_DialogShow(ui.dialog, action == 99 ? DIALOG_MODE_ALERT : DIALOG_MODE_CONFIRM, title, body);
}
static void ma_answer(int yes) {
    int action = ui.asking; ui.asking = 0;
    GUI_DialogHide(ui.dialog);
    if(yes && action && ui.confirm) ui.confirm(action);
}
static void ma_browser_close(void) {
    ui.browsing = 0;
    GUI_WidgetSetFlags(ui.browser, WIDGET_HIDDEN);
    GUI_WidgetClearFlags(ui.main, WIDGET_HIDDEN);
    for(int i = 0; i < 3; i++) GUI_WidgetClearFlags(ui.actions[i], WIDGET_HIDDEN);
    ma_focus();
}
static void ma_browser_path(void) {
    ma_text(ui.path, "%s", ma_tail(GUI_FileManagerGetPath(ui.fm), 78));
}
static void ma_browse(int purpose, int folder, const char *path) {
    if(ui.busy) return;
    ui.browsing = 1; ui.folder = folder; ui.purpose = purpose;
    GUI_WidgetSetFlags(ui.main, WIDGET_HIDDEN);
    for(int i = 0; i < 3; i++) GUI_WidgetSetFlags(ui.actions[i], WIDGET_HIDDEN);
    GUI_WidgetClearFlags(ui.browser, WIDGET_HIDDEN);
    GUI_WidgetSetEnabled(ma_widget("choose-folder"), folder);
    GUI_FileManagerSetPath(ui.fm, *path && DirExists(path) ? path : "/");
    GUI_FileManagerScan(ui.fm); ma_browser_path();
    ma_status(folder ? "Choose a folder, then press X to use it." : "Choose a file and press A to select it.", 0);
    ma_note("B: parent folder / cancel at Devices. Y: Devices. START: cancel picker.");
}
static void ma_browser_up(void) {
    const char *path = GUI_FileManagerGetPath(ui.fm);
    if(!strcmp(path, "/")) { ma_browser_close(); return; }
    GUI_FileManagerChangeDir(ui.fm, "..", -2); ma_browser_path();
}
static void ma_pick_folder(void) {
    if(!ui.browsing || !ui.folder) return;
    char path[MA_PATH]; snprintf(path, sizeof(path), "%s", GUI_FileManagerGetPath(ui.fm));
    if(!ma_persistent(path)) { ma_status("Choose an SD, IDE or PC folder for saved files.", 1); return; }
    ma_browser_close(); ui.picked(ui.purpose, path);
}
static void ma_item(dirent_fm_t *entry) {
    if(!ui.browsing || ui.busy || !entry) return;
    if(entry->ent.attr == O_DIR || entry->ent.size < 0) {
        /* The legacy widget's internal path buffer is NAME_MAX bytes. */
        if(entry->ent.size != -2 && strlen(GUI_FileManagerGetPath(ui.fm)) +
                strlen(entry->ent.name) + 2 >= NAME_MAX) {
            ma_status("That folder path is too long for the file picker.", 1); return;
        }
        GUI_FileManagerChangeDir(ui.fm, entry->ent.name, entry->ent.size);
        ma_browser_path(); return;
    }
    if(ui.folder) return;
    char path[MA_PATH];
    if(ma_join(path, sizeof(path), GUI_FileManagerGetPath(ui.fm), entry->ent.name) < 0) {
        ma_status("That file path is too long.", 1); return;
    }
    ma_browser_close(); ui.picked(ui.purpose, path);
}
static void ma_go_back(void) {
    if(ui.busy) { ui.cancel = 1; return; }
    if(ui.asking) { ma_answer(0); return; }
    if(ui.browsing) { ma_browser_close(); return; }
    if(ui.back) ui.back(); else OpenMainApp();
}
static void ma_input(void *event, void *param, int action) {
    (void)event; SDL_Event *e = param;
    if(action != EVENT_ACTION_UPDATE || !e || !ui.app || !(ui.app->state & APP_STATE_OPENED)) return;
    int key = utility_key(e);
    if(ui.busy) { if(key == UI_BACK) ui.cancel = 1; e->type = SDL_NOEVENT; return; }
    if(utility_global_input(e)) return;
    if(ui.asking) {
        utility_dialog(e, key);
    } else if(ui.browsing) {
        if(key == UI_UP || key == UI_DOWN) {
            SDL_Event move; memset(&move, 0, sizeof(move)); move.type = SDL_JOYHATMOTION;
            move.jhat.value = key == UI_UP ? SDL_HAT_UP : SDL_HAT_DOWN;
            GUI_WidgetSetFlags(ui.fm, WIDGET_INSIDE | WIDGET_PRESSED);
            GUI_FileManagerEvent(ui.fm, &move, 0, 0);
            GUI_WidgetClearFlags(ui.fm, WIDGET_PRESSED);
        } else if(key == UI_OK) {
            int i = GUI_FileManagerGetSelectedItem(ui.fm);
            GUI_Widget *item = GUI_FileManagerGetItem(ui.fm, i < 0 ? 0 : i);
            if(item) GUI_WidgetClicked(item, 0, 0);
        } else if(key == UI_BACK) ma_browser_up();
        else if(key == UI_X) ma_pick_folder();
        else if(key == UI_Y) { GUI_FileManagerSetPath(ui.fm, "/"); GUI_FileManagerScan(ui.fm); ma_browser_path(); }
        else if(key == UI_START) ma_browser_close();
        else utility_forward(e);
    } else if(key == UI_UP || key == UI_DOWN) {
        ui.focus = (ui.focus + (key == UI_UP ? 9 : 1)) % 10; ma_focus();
    } else if((key == UI_LEFT || key == UI_RIGHT || key == UI_X) && ui.focus < 6) {
        ui.row(ui.focus, key == UI_RIGHT ? 1 : -1);
    } else if(key == UI_BACK || key == UI_START) ma_go_back();
    else if(key == UI_OK) GUI_WidgetClicked(ma_focused(), 0, 0);
    else utility_forward(e);
    e->type = SDL_NOEVENT;
}
static void ma_init(App_t *app, const char *name) {
    memset(&ui, 0, sizeof(ui)); ui.app = app;
    for(int i = 0; i < 6; i++) { char n[16]; snprintf(n, sizeof(n), "row-%d", i); ui.rows[i] = ma_widget(n); }
    for(int i = 0; i < 3; i++) { char n[16]; snprintf(n, sizeof(n), "action-%d", i); ui.actions[i] = ma_widget(n); }
    ui.menu = ma_widget("menu"); ui.main = ma_widget("main-panel");
    ui.browser = ma_widget("browser-panel"); ui.fm = ma_widget("file-picker");
    ui.path = ma_widget("picker-path"); ui.dialog = ma_widget("dialog");
    ui.status = ma_widget("status"); ui.note = ma_widget("note");
    ui.detail[0] = ma_widget("detail-0"); ui.detail[1] = ma_widget("detail-1");
    ui.progress = ma_widget("progress");
    ui.progress_width = GUI_WidgetGetArea(ui.progress).w;
    ui.input = AddEvent(name, EVENT_TYPE_INPUT, EVENT_PRIO_DEFAULT, ma_input, NULL);
    if(ui.input) SetEventActive(ui.input, 0);
    ma_browser_close(); ma_progress(0, 1);
}
/* Actual exported names are unique per app (modules may remain loaded). */
#define MAINTENANCE_CALLBACKS(Name) \
void Name##_Open(App_t *app) { (void)app; utility_open(ui.input); if(ui.opened) ui.opened(); ma_focus(); } \
void Name##_Close(App_t *app) { (void)app; if(ui.closed) ui.closed(); utility_close(ui.input); } \
void Name##_Shutdown(App_t *app) { (void)app; utility_remove(&ui.input); ui.app = NULL; } \
void Name##_Row(GUI_Widget *w) { if(ui.busy) return; for(int i=0;i<6;i++) if(w==ui.rows[i]) { ui.focus=i; ui.row(i,1); ma_focus(); break; } } \
void Name##_Action(GUI_Widget *w) { if(ui.busy) return; for(int i=0;i<3;i++) if(w==ui.actions[i]) { ui.focus=i+6; ui.action(i); break; } } \
void Name##_Back(GUI_Widget *w) { (void)w; ma_go_back(); } \
void Name##_Confirm(GUI_Widget *w) { (void)w; ma_answer(1); } \
void Name##_Cancel(GUI_Widget *w) { (void)w; ma_answer(0); } \
void Name##_Item(dirent_fm_t *entry) { ma_item(entry); } \
void Name##_Up(GUI_Widget *w) { (void)w; ma_browser_up(); } \
void Name##_Devices(GUI_Widget *w) { (void)w; GUI_FileManagerSetPath(ui.fm,"/"); GUI_FileManagerScan(ui.fm); ma_browser_path(); } \
void Name##_Folder(GUI_Widget *w) { (void)w; ma_pick_folder(); }
#endif
