/* NeXT controller navigation for the Lua File Manager. */
#include "ds.h"
#include "../../utility_ui.h"
DEFAULT_MODULE_EXPORTS(app_filemanager);
static struct { App_t *app; Event_t *input; GUI_Widget *panes[2],*dialog,*actions[13]; int toolbar,focus; } self;
static void run(const char *code) { LuaDo(LUA_DO_STRING,code,self.app->lua); }
static int pane(void) {
    lua_State *L=self.app->lua;
    int top=lua_gettop(L),value=0;
    lua_getglobal(L,"FileManager");
    if(lua_istable(L,-1)) {
        lua_getfield(L,-1,"mgr");
        lua_getfield(L,-1,"top");
        lua_getfield(L,-1,"focus");
        value=!lua_toboolean(L,-1);
    }
    lua_settop(L,top); return value;
}
/* A long copy stays on the main thread, but polls only cancel input between
 * bounded file chunks. Other application commands cannot run during a copy. */
static int pump(lua_State *L) {
    SDL_Event e;
    int keep=1;
    while(SDL_PollEvent(&e)) if(utility_key(&e)==UI_BACK) keep=0;
    thd_pass();
    lua_pushboolean(L,keep); return 1;
}
static void toolbar(int enabled) {
    self.toolbar=enabled;
    for(int i=0;i<13;i++) GUI_WidgetClearFlags(self.actions[i],WIDGET_INSIDE);
    if(enabled) GUI_WidgetSetFlags(self.actions[self.focus],WIDGET_INSIDE);
}
static void move(int direction) {
    GUI_Widget *fm=self.panes[pane()];
    SDL_Event e;
    memset(&e,0,sizeof(e)); e.type=SDL_JOYHATMOTION;
    e.jhat.value=direction<0?SDL_HAT_UP:SDL_HAT_DOWN;
    GUI_WidgetSetFlags(fm,WIDGET_INSIDE|WIDGET_PRESSED);
    GUI_FileManagerEvent(fm,&e,0,0);
    GUI_WidgetClearFlags(fm,WIDGET_PRESSED);
}
static void input(void *event,void *param,int action) {
    (void)event; SDL_Event *e=param;
    if(action!=EVENT_ACTION_UPDATE || !e || !self.app || !(self.app->state&APP_STATE_OPENED) || utility_global_input(e)) return;
    int key=utility_key(e);
    if(GUI_ScreenGetFocusWidget(GUI_GetScreen())) { utility_forward(e); return; }
    if(!(GUI_WidgetGetFlags(self.dialog)&WIDGET_HIDDEN)) {
        if(key==UI_OK) run("FileManager:ModalClick(true)");
        else if(key==UI_BACK) run("FileManager:ModalClick(false)");
        else if(key==UI_X) run("FileManager:editPrompt()");
        else utility_forward(e);
        e->type=SDL_NOEVENT; return;
    }
    if(key==UI_Y) toolbar(!self.toolbar);
    else if(key==UI_BACK) {
        if(self.toolbar) toolbar(0);
        else run("FileManager:up()");
    } else if(key==UI_START) OpenMainApp();
    else if(self.toolbar && (key==UI_UP || key==UI_DOWN || key==UI_LEFT || key==UI_RIGHT)) {
        self.focus=(self.focus+(key==UI_UP || key==UI_LEFT?-1:1)+13)%13; toolbar(1);
    } else if(self.toolbar && key==UI_OK) GUI_WidgetClicked(self.actions[self.focus],0,0);
    else if(key==UI_LEFT || key==UI_RIGHT) {
        run(key==UI_LEFT?"FileManager:choosePane(0)":"FileManager:choosePane(1)");
    } else if(key==UI_UP || key==UI_DOWN) move(key==UI_UP?-1:1);
    else if(key==UI_X) run("FileManager:toolbarCopy()");
    else if(key==UI_OK) {
        GUI_Widget *fm=self.panes[pane()];
        int selected=GUI_FileManagerGetSelectedItem(fm);
        if(selected<0) move(1);
        else {
            GUI_Widget *item=GUI_FileManagerGetItem(fm,selected);
            if(item) GUI_WidgetClicked(item,0,0);
        }
    } else utility_forward(e);
    e->type=SDL_NOEVENT;
}
void FileManagerApp_Init(App_t *app) {
    memset(&self,0,sizeof(self)); self.app=app;
    char code[100]; snprintf(code,sizeof(code),"THIS_APP_ID=%lu; FileManager:Initialize()",(unsigned long)app->id); run(code);
    lua_register(app->lua,"FileManagerPump",pump);
    self.panes[0]=APP_GET_WIDGET("filemgr-top"); self.panes[1]=APP_GET_WIDGET("filemgr-bottom");
    self.dialog=APP_GET_WIDGET("modal-dialog");
    const char *names[]={"copy-btn","rename-btn","mkdir-btn","delete-btn","archive-btn","mount-btn","up-top","device-top","refresh-top","up-bottom","device-bottom","refresh-bottom","exit-btn"};
    for(int i=0;i<13;i++) self.actions[i]=APP_GET_WIDGET(names[i]);
    self.input=AddEvent("NextFileManagerInput",EVENT_TYPE_INPUT,EVENT_PRIO_DEFAULT,input,NULL);
    if(self.input) SetEventActive(self.input,0);
}
void FileManagerApp_Open(App_t *app) { (void)app; utility_open(self.input); toolbar(0); run("FileManager:tooltip(nil)"); }
void FileManagerApp_Close(App_t *app) { (void)app; utility_close(self.input); }
void FileManagerApp_Shutdown(App_t *app) {
    (void)app; utility_remove(&self.input); run("FileManager:Shutdown()");
    lua_pushnil(self.app->lua); lua_setglobal(self.app->lua,"FileManagerPump"); self.app=NULL;
}
