/* DreamShell NeXT Settings. Original app (C) 2016-2025 SWAT.
 * NeXT UI and transactional editing (C) 2026 TPMJB and contributors. */
#include "ds.h"
#include "../../utility_ui.h"
#include "settings_model.h"
#include "settings_nav.h"

DEFAULT_MODULE_EXPORTS(app_settings);

static struct {
    App_t *app;
    Event_t *input;
    Settings_t draft, saved;
    struct tm clock;
    GUI_Widget *rows[7], *tabs[5], *back, *save, *status, *help, *dialog, *heading;
    int page, focus, action, clock_dirty;
    char apps[32][64];
    int app_count;
} self;

enum { ASK_NONE, ASK_DISCARD, ASK_DEFAULTS, ASK_REBOOT };
static const int row_counts[] = {3,5,4,7,7};
static const char *page_names[] = {"Display","Sound","Startup","Clock","System"};
static const char *native_names[] = {"Auto (detect cable)","PAL 480i","NTSC 480i","VGA 480p"};
static const char *shape_names[] = {"4:3 (640 x 480)","3:2 (720 x 480)","16:10 (768 x 480)","16:9 (854 x 480)"};
static const int widths[] = {640,720,768,854};
static const char *roots[] = {"","/sd/DS","/ide/DS","/pc","/cd"};
static const char *scripts[] = {"/lua/startup.lua","/lua/custom.lua"};
static const char *homes[] = {"Launch App","Main","Games Menu"};
static const char *onoff(int value) { return value ? "On" : "Off"; }
static int dirty(void) { return memcmp(&self.draft, &self.saved, sizeof(Settings_t)) != 0; }
static void status(const char *message) { GUI_LabelSetText(self.status, message); }
static int row_count(void) { return row_counts[self.page]; }
static int native_index(void) {
    const int modes[] = {-1,DM_640x480_PAL_IL,DM_640x480_NTSC_IL,DM_640x480_VGA};
    for(int i=1; i<4; i++)
        if(!memcmp(&self.draft.video.mode,&vid_builtin[modes[i]],sizeof(vid_mode_t))) return i;
    return 0;
}
static int width_index(void) {
    for(int i=0;i<4;i++) if(self.draft.video.virt_width == widths[i]) return i;
    return 0;
}
static void cycle_string(char *dst, size_t size, const char *const *choices, int n, int step) {
    int i;
    for(i=0;i<n;i++) if(!strcmp(dst, choices[i])) break;
    i = i == n ? 0 : (i + step + n) % n;
    snprintf(dst,size,"%s",choices[i]);
}
static void set_row(int row, const char *name, const char *value) {
    char text[160];
    snprintf(text,sizeof(text),"%s%s%s",name,*value ? ":  " : "",value);
    GUI_LabelSetText(GUI_ButtonGetCaption(self.rows[row]),text);
}
static GUI_Widget *focused(void) {
    if(self.focus < 5) return self.tabs[self.focus];
    if(self.focus < 5 + row_count()) return self.rows[self.focus-5];
    if(self.focus == 5 + row_count()) return self.save;
    return self.back;
}
static void mark_focus(void) {
    LockVideo();
    for(int i=0;i<5;i++) GUI_WidgetClearFlags(self.tabs[i],WIDGET_INSIDE);
    for(int i=0;i<7;i++) GUI_WidgetClearFlags(self.rows[i],WIDGET_INSIDE);
    GUI_WidgetClearFlags(self.save,WIDGET_INSIDE);
    GUI_WidgetClearFlags(self.back,WIDGET_INSIDE);
    GUI_WidgetSetFlags(focused(),WIDGET_INSIDE);
    for(int i=0;i<5;i++) {
        int active=i==self.page;
        GUI_ButtonSetNormalImage(self.tabs[i],APP_GET_SURFACE(active?"tab-active-normal":"b108x30-normal"));
        GUI_ButtonSetHighlightImage(self.tabs[i],APP_GET_SURFACE(active?"tab-active-highlight":"b108x30-highlight"));
        GUI_ButtonSetPressedImage(self.tabs[i],APP_GET_SURFACE(active?"tab-active-pressed":"b108x30-pressed"));
        GUI_LabelSetTextColor(GUI_ButtonGetCaption(self.tabs[i]),
            active ? 16 : 239,active ? 25 : 245,active ? 35 : 252);
    }
    UnlockVideo();
}
static void refresh(void) {
    char text[160],tz[24];
    snprintf(text,sizeof(text),"Settings / %s",page_names[self.page]);
    GUI_LabelSetText(self.heading,text);
    for(int i=0;i<7;i++) {
        GUI_WidgetSetEnabled(self.rows[i],1);
        if(i < row_count()) GUI_WidgetClearFlags(self.rows[i],WIDGET_HIDDEN);
        else GUI_WidgetSetFlags(self.rows[i],WIDGET_HIDDEN);
    }
    switch(self.page) {
    case 0:
        set_row(0,"Native output",native_names[native_index()]);
        set_row(1,"Screen shape",shape_names[width_index()]);
        set_row(2,"Scaling filter",self.draft.video.tex_filter < 0 ? "Auto" :
            self.draft.video.tex_filter == PVR_FILTER_NEAREST ? "Sharp (nearest)" : "Smooth (bilinear)");
        GUI_LabelSetText(self.help,"Native output applies after restart. Auto detects your video cable.");
        break;
    case 1:
        snprintf(text,sizeof(text),"%d%%",(self.draft.audio.volume*100+127)/255);
        set_row(0,"Volume",text);
        set_row(1,"Menu sounds",onoff(self.draft.audio.sfx_enabled));
        set_row(2,"Button click",onoff(self.draft.audio.click_enabled));
        set_row(3,"Selection sound",onoff(self.draft.audio.hover_enabled));
        set_row(4,"Startup chime",onoff(self.draft.audio.startup_enabled));
        GUI_LabelSetText(self.help,"Muting preserves your individual sound choices. Save to apply.");
        break;
    case 2:
        set_row(0,"Start in",self.draft.startup_app);
        set_row(1,"Return to",self.draft.main_app);
        set_row(2,"Resources",*self.draft.root ? self.draft.root : "Auto (recommended)");
        set_row(3,"Startup script",self.draft.startup);
        GUI_LabelSetText(self.help,"Startup changes apply after restart. Resources must be mounted.");
        break;
    case 3:
        snprintf(text,sizeof(text),"%04d",self.clock.tm_year+1900); set_row(0,"Year",text);
        snprintf(text,sizeof(text),"%02d",self.clock.tm_mon+1); set_row(1,"Month",text);
        snprintf(text,sizeof(text),"%02d",self.clock.tm_mday); set_row(2,"Day",text);
        snprintf(text,sizeof(text),"%02d",self.clock.tm_hour); set_row(3,"Hour (local)",text);
        snprintf(text,sizeof(text),"%02d",self.clock.tm_min); set_row(4,"Minute",text);
        settings_format_timezone(tz,sizeof(tz),self.draft.time_zone); set_row(5,"Network sync time zone",tz);
        set_row(6,"Set console clock",self.clock_dirty ? "Apply edited time" : "No clock changes");
        GUI_LabelSetText(self.help,"Clock fields use local time. Time zone applies to network clock sync.");
        break;
    case 4:
        set_row(0,"Connect at startup",self.draft.network.startup_connect_eth ? "Ethernet" : self.draft.network.startup_connect_ppp ? "Dial-up" : "Off");
        set_row(1,"Sync clock at startup",onoff(self.draft.network.startup_ntp));
        set_row(2,"Network configuration","Open Network app");
        snprintf(text,sizeof(text),"SD %s / IDE %s / VMU %s",DirExists("/sd")?"yes":"no",DirExists("/ide")?"yes":"no",maple_enum_type(0,MAPLE_FUNC_MEMCARD)?"yes":"no");
        set_row(3,"Devices",text);
        snprintf(text,sizeof(text),"%.64s",getenv("PATH")?getenv("PATH"):"Unknown"); set_row(4,"Running from",text);
        set_row(5,"Restore defaults","Review before saving");
        set_row(6,"Restart K-UI","");
        GUI_LabelSetText(self.help,"Clock sync needs a connection. Select Devices to refresh detection.");
        break;
    }
    if(self.focus >= 7+row_count()) self.focus=5;
    mark_focus();
}
static void pending(void) { status(dirty() ? "Unsaved changes  /  START or Save settings to keep them" : "No unsaved changes"); }
static void ask(int action, const char *title, const char *body) {
    self.action=action;
    GUI_DialogShow(self.dialog,DIALOG_MODE_CONFIRM,title,body);
}
void SettingsApp_Cancel(GUI_Widget *widget) { (void)widget; self.action=ASK_NONE; GUI_DialogHide(self.dialog); }
void SettingsApp_Back(GUI_Widget *widget) {
    (void)widget;
    if(dirty() || self.clock_dirty) ask(ASK_DISCARD,"Discard changes?","Leave Settings without saving pending changes?");
    else OpenMainApp();
}
static void defaults(void) {
    /* Reset only this draft. ResetSettings() deletes saved files on every VMU. */
    self.draft=self.saved;
    memset(&self.draft.video.mode,0,sizeof(vid_mode_t));
    self.draft.video.virt_width=640; self.draft.video.virt_height=480;
    self.draft.video.tex_filter=-1;
    self.draft.audio.volume=230;
    self.draft.audio.sfx_enabled=self.draft.audio.click_enabled=1;
    self.draft.audio.hover_enabled=self.draft.audio.startup_enabled=1;
    memset(&self.draft.network,0,sizeof(self.draft.network));
    self.draft.root[0]=0; self.draft.time_zone=0;
    snprintf(self.draft.startup,sizeof(self.draft.startup),"/lua/startup.lua");
    snprintf(self.draft.startup_app,sizeof(self.draft.startup_app),"Launch App");
    snprintf(self.draft.main_app,sizeof(self.draft.main_app),"Launch App");
}
void SettingsApp_Confirm(GUI_Widget *widget) {
    (void)widget;
    int action=self.action;
    self.action=ASK_NONE; GUI_DialogHide(self.dialog);
    if(action==ASK_DISCARD) { self.draft=self.saved; self.clock_dirty=0; OpenMainApp(); }
    else if(action==ASK_DEFAULTS) { defaults(); refresh(); pending(); }
    else if(action==ASK_REBOOT) {
        if(dirty() || self.clock_dirty) { status("Save or discard pending changes before restarting."); return; }
        char path[NAME_MAX];
        snprintf(path,sizeof(path),"%s/DS_CORE.BIN",getenv("PATH"));
        if(!FileExists(path)) { status("DS_CORE.BIN is missing. Restart cancelled."); return; }
        dsystemf("exec -b -f %s",path);
        status("Restart returned unexpectedly. Check the console.");
    }
}
/* Read in the same priority order as LoadSettings: first valid VMU, then root.
 * A save to a later device must not claim success if an older VMU wins at boot. */
static int saved_file(const char *path, int exact_size) {
    Settings_t value;
    file_t fd=fs_open(path,O_RDONLY);
    if(fd<0) return -1;
    int valid=(!exact_size || fs_total(fd)==sizeof(value)) &&
        fs_read(fd,&value,sizeof(value))==sizeof(value) && value.version==DS_SETTIGS_VERSION;
    fs_close(fd);
    return valid ? !memcmp(&value,&self.draft,sizeof(value)) : -1;
}
static int saved_matches(void) {
    char path[NAME_MAX];
    for(int i=0;i<8;i++) {
        maple_device_t *vmu=maple_enum_type(i,MAPLE_FUNC_MEMCARD);
        if(!vmu) break;
        snprintf(path,sizeof(path),"/vmu/%c%c/DSCONFIG.CFG",vmu->port+'A',vmu->unit+'0');
        int match=saved_file(path,0);
        if(match>=0) return match;
    }
    snprintf(path,sizeof(path),"%s/DSCONFIG.CFG",getenv("PATH"));
    return saved_file(path,1)==1;
}
void SettingsApp_Save(GUI_Widget *widget) {
    (void)widget;
    if(*self.draft.root && !DirExists(self.draft.root)) { status("Resources folder is not mounted. Select Auto or a valid folder."); return; }
    if(strcmp(self.draft.startup,self.saved.startup)) {
        char path[NAME_MAX];
        snprintf(path,sizeof(path),"%s%s",getenv("PATH"),self.draft.startup);
        if(!FileExists(path)) { status("Startup script was not found. Choose the standard script."); return; }
    }
    Settings_t old=*GetSettings();
    SetSettings(&self.draft);
    if(!SaveSettings() || !saved_matches()) {
        SetSettings(&old);
        status("Save could not be verified. Check VMU space and writable storage.");
        return;
    }
    self.saved=self.draft;
    SetScreenFilter(self.draft.video.tex_filter);
    SetScreenMode(self.draft.video.virt_width,self.draft.video.virt_height,0,0,1);
    status("Saved to VMU or resources folder. Startup/output apply after restart.");
}
static void change(int row,int step) {
    char *dst;
    if(self.page==0) {
        if(row==0) {
            int i=(native_index()+step+4)%4;
            const int modes[]={-1,DM_640x480_PAL_IL,DM_640x480_NTSC_IL,DM_640x480_VGA};
            if(!i) memset(&self.draft.video.mode,0,sizeof(vid_mode_t));
            else memcpy(&self.draft.video.mode,&vid_builtin[modes[i]],sizeof(vid_mode_t));
        } else if(row==1) { self.draft.video.virt_width=widths[(width_index()+step+4)%4]; self.draft.video.virt_height=480; }
        else {
            int values[]={-1,PVR_FILTER_NEAREST,PVR_FILTER_BILINEAR},i=0;
            while(i<2 && values[i]!=self.draft.video.tex_filter) i++;
            self.draft.video.tex_filter=values[(i+step+3)%3];
        }
    } else if(self.page==1) {
        if(row==0) self.draft.audio.volume=settings_clamp(self.draft.audio.volume+step*13,0,255);
        else if(row==1) self.draft.audio.sfx_enabled=!self.draft.audio.sfx_enabled;
        else if(row==2) self.draft.audio.click_enabled=!self.draft.audio.click_enabled;
        else if(row==3) self.draft.audio.hover_enabled=!self.draft.audio.hover_enabled;
        else self.draft.audio.startup_enabled=!self.draft.audio.startup_enabled;
    } else if(self.page==2) {
        if(row==0) {
            const char *names[32];
            for(int i=0;i<self.app_count;i++) names[i]=self.apps[i];
            if(self.app_count) cycle_string(self.draft.startup_app,sizeof(self.draft.startup_app),names,self.app_count,step);
        } else if(row==1) {
            dst=self.draft.main_app;
            for(int i=0;i<3;i++) { cycle_string(dst,sizeof(self.draft.main_app),homes,3,step); if(GetAppByName(dst)) break; }
        } else if(row==2) cycle_string(self.draft.root,sizeof(self.draft.root),roots,5,step);
        else cycle_string(self.draft.startup,sizeof(self.draft.startup),scripts,2,step);
    } else if(self.page==3) {
        if(row<5) { settings_adjust_clock(&self.clock,row,step); self.clock_dirty=1; }
        else if(row==5) self.draft.time_zone=settings_clamp(self.draft.time_zone+step*15,-12*60,14*60);
        else {
            struct tm value=self.clock;
            time_t seconds=mktime(&value);
            if(seconds==(time_t)-1) { status("Clock value is outside the supported range."); return; }
            if(rtc_set_unix_secs(seconds)<0) {
                status("Console clock could not be set. Check the date and retry.");
                return;
            }
            self.clock_dirty=0;
            status("Console clock set. Save settings to keep the time zone.");
            refresh(); return;
        }
    } else if(self.page==4) {
        if(row==0) {
            int mode=self.draft.network.startup_connect_eth ? 1 : self.draft.network.startup_connect_ppp ? 2 : 0;
            mode=(mode+step+3)%3;
            self.draft.network.startup_connect_eth=mode==1; self.draft.network.startup_connect_ppp=mode==2;
        } else if(row==1) self.draft.network.startup_ntp=!self.draft.network.startup_ntp;
        else if(row==2) {
            if(dirty() || self.clock_dirty) { status("Save or discard pending changes before opening Network."); return; }
            App_t *network=GetAppByName("Network");
            if(network) OpenApp(network,NULL); else status("Network app is not installed.");
            return;
        } else if(row==5) { ask(ASK_DEFAULTS,"Restore defaults?","Defaults will remain unsaved until you choose Save settings."); return; }
        else if(row==6) { ask(ASK_REBOOT,"Restart K-UI?","Reload the core from the current resources folder?"); return; }
    }
    refresh(); pending();
}
void SettingsApp_Change(GUI_Widget *widget) {
    const char *name=GUI_ObjectGetName((GUI_Object *)widget);
    int i=name ? atoi(name+4) : -1;
    if(i<0 || i>=row_count()) return;
    self.focus=5+i; change(i,1);
}
static void page(int index,int enter_rows) {
    self.page=(index+5)%5;
    self.focus=enter_rows ? 5 : self.page;
    refresh();
}
void SettingsApp_Tab(GUI_Widget *widget) {
    const char *name=GUI_ObjectGetName((GUI_Object *)widget);
    if(name && strlen(name)>4) page(atoi(name+4),1);
}
static void input(void *event,void *param,int action) {
    (void)event;
    SDL_Event *e=param;
    if(action!=EVENT_ACTION_UPDATE || !e || !self.app || !(self.app->state&APP_STATE_OPENED) || utility_global_input(e)) return;
    int key=utility_key(e);
    if(!(GUI_WidgetGetFlags(self.dialog)&WIDGET_HIDDEN)) {
        if(key==UI_OK) SettingsApp_Confirm(NULL);
        else if(key==UI_BACK) SettingsApp_Cancel(NULL);
        else if(e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP) utility_forward(e);
        e->type=SDL_NOEVENT; return;
    }
    if(key==UI_UP || key==UI_DOWN) {
        self.focus=settings_vertical_focus(self.page,self.focus,row_count(),key==UI_UP?-1:1);
        mark_focus();
    } else if(key==UI_LEFT || key==UI_RIGHT || key==UI_X) {
        int step=key==UI_RIGHT?1:-1;
        if(self.focus>=5 && self.focus<5+row_count()) change(self.focus-5,step);
        else if(self.focus<5 && key!=UI_X) page(self.page+step,0);
    } else if(key==UI_Y) page(self.page+1,self.focus>=5);
    else if(key==UI_BACK) SettingsApp_Back(NULL);
    else if(key==UI_START) SettingsApp_Save(NULL);
    else if(key==UI_OK) GUI_WidgetClicked(focused(),0,0);
    else utility_forward(e);
    e->type=SDL_NOEVENT;
}
void SettingsApp_Init(App_t *app) {
    memset(&self,0,sizeof(self)); self.app=app;
    for(int i=0;i<7;i++) { char name[16]; snprintf(name,sizeof(name),"row-%d",i); self.rows[i]=APP_GET_WIDGET(name); }
    for(int i=0;i<5;i++) { char name[16]; snprintf(name,sizeof(name),"tab-%d",i); self.tabs[i]=APP_GET_WIDGET(name); }
    self.back=APP_GET_WIDGET("back-btn"); self.save=APP_GET_WIDGET("save-btn");
    self.status=APP_GET_WIDGET("save-status"); self.help=APP_GET_WIDGET("help"); self.dialog=APP_GET_WIDGET("dialog");
    self.heading=APP_GET_WIDGET("heading");
    Item_t *item=listGetItemFirst(GetAppList());
    while(item && self.app_count<32) {
        App_t *a=item->data;
        if(strcmp(a->name,"Settings")) snprintf(self.apps[self.app_count++],64,"%s",a->name);
        item=listGetItemNext(item);
    }
    self.input=AddEvent("NextSettingsInput",EVENT_TYPE_INPUT,EVENT_PRIO_DEFAULT,input,NULL);
    if(self.input) SetEventActive(self.input,0);
}
void SettingsApp_Open(App_t *app) {
    (void)app;
    self.saved=self.draft=*GetSettings(); self.clock_dirty=0;
    time_t now=rtc_unix_secs(); struct tm *value=gmtime(&now);
    if(value) self.clock=*value; else memset(&self.clock,0,sizeof(self.clock));
    self.clock.tm_isdst=0;
    utility_open(self.input); page(0,1); pending();
}
void SettingsApp_Close(App_t *app) { (void)app; utility_close(self.input); }
void SettingsApp_Shutdown(App_t *app) { (void)app; utility_remove(&self.input); self.app=NULL; }
