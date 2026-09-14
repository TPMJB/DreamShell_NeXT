/* GD Play: original (C) 2014 megavolt85, 2024 SWAT.
 * NeXT interface and bounded drive worker (C) 2026 TPMJB and contributors. */
#include "ds.h"
#include <dc/sound/sound.h>
#include "../../utility_ui.h"
#include "disc_metadata.h"
DEFAULT_MODULE_EXPORTS(app_gdplay);

#define DRIVE_TIMEOUT_MS 5000
#define PATCH_SIZE 0xff00 /* exec.s copies 0x3fc0 32-bit words. */
typedef struct {
    disc_metadata_t info;
    int ready;
    char type[32],state[32],message[160];
} disc_result_t;
static struct {
    App_t *app;
    Event_t *input,*video;
    kthread_t *worker;
    volatile int stop,request,pending;
    disc_result_t result;
    GUI_Widget *buttons[3],*title[3],*fields[6],*type,*state,*message;
    int focus,ready;
    void *bios_patch;
} self;
void gdplay_run_game(void *patch);
static mutex_t result_lock = MUTEX_INITIALIZER;

static void publish(const disc_result_t *result) {
    mutex_lock(&result_lock); self.result=*result; self.pending=1; mutex_unlock(&result_lock);
}
static void report(const char *type,const char *state,const char *message) {
    disc_result_t result={0};
    snprintf(result.type,sizeof(result.type),"%s",type);
    snprintf(result.state,sizeof(result.state),"%s",state);
    snprintf(result.message,sizeof(result.message),"%s",message);
    publish(&result);
}
static void scan_disc(void) {
    int status=-1,type=-1;
    if(self.stop) return;
    report("DISC DRIVE","Reading disc...","Reading disc details. B returns to the menu.");
    int rv=cdrom_exec_cmd_timed(CD_CMD_INIT,NULL,DRIVE_TIMEOUT_MS);
    if(self.stop) return;
    if(rv!=ERR_OK) { report("DISC DRIVE","Not ready","Close the lid, then select Read disc again."); return; }
    rv=cdrom_get_status(&status,&type);
    if(rv!=ERR_OK || status==CD_STATUS_NO_DISC || status==CD_STATUS_OPEN) {
        report("DISC DRIVE","No disc","Insert a disc and close the lid. B returns to the menu."); return;
    }
    if(type==CD_CDDA) { report("AUDIO CD","Audio disc","This app boots Dreamcast game discs. B returns to the menu."); return; }
    if(type!=CD_GDROM && type!=CD_CDROM_XA) { report("DATA CD","Not bootable","No supported Dreamcast disc found. Try another disc."); return; }
    if(self.stop) return;
    if(cdrom_change_datatype(CDROM_READ_DEFAULT,-1,2048)!=ERR_OK) {
        report("DISC DRIVE","Read error","Could not select the disc read mode. Try reading again."); return;
    }
    uint32_t fad=45150;
    if(type==CD_CDROM_XA) {
        cd_toc_t toc;
        cd_cmd_toc_params_t params={.area=CD_AREA_LOW,.buffer=&toc};
        rv=cdrom_exec_cmd_timed(CD_CMD_GETTOC2,&params,DRIVE_TIMEOUT_MS);
        if(self.stop) return;
        if(rv!=ERR_OK || !(fad=cdrom_locate_data_track(&toc))) {
            report("DATA CD","Read error","Could not read the disc table. Select Read disc again."); return;
        }
    }
    unsigned char *buffer=memalign(32,2048);
    if(!buffer) { report("DISC DRIVE","No memory","Could not allocate a disc buffer. Reopen GD Play."); return; }
    cd_read_params_t params={.start_sec=fad,.num_sec=1,.buffer=buffer,.is_test=0};
    rv=cdrom_exec_cmd_timed(CD_CMD_PIOREAD,&params,DRIVE_TIMEOUT_MS);
    disc_result_t result={0};
    if(!self.stop) {
        if(rv==ERR_OK && disc_metadata_read(&result.info,buffer,2048)) {
            result.ready=self.bios_patch!=NULL;
            snprintf(result.type,sizeof(result.type),"%s",type==CD_GDROM?"GD-ROM":"MIL-CD");
            snprintf(result.state,sizeof(result.state),"%s",result.ready?"Ready to play":"Boot file missing");
            snprintf(result.message,sizeof(result.message),"%s",result.ready?
                "A plays the disc. B returns to the menu. DreamShell closes when a game starts.":
                "firmware/rungd.bin is missing or incomplete. Menu remains available.");
            publish(&result);
        } else report("DISC DRIVE","Read error","Could not read a valid Dreamcast header. Clean the disc, then retry.");
    }
    free(buffer);
}
static void *worker(void *unused) {
    (void)unused;
    int old_status=-999,old_type=-999;
    while(!self.stop) {
        int status=-1,type=-1;
        int rv=cdrom_get_status(&status,&type);
        int request=self.request; self.request=0;
        if(self.stop) break;
        if(rv==ERR_DISC_CHG || request || (rv==ERR_OK && (status!=old_status || type!=old_type))) {
            if(rv==ERR_OK && (status==CD_STATUS_OPEN || status==CD_STATUS_NO_DISC))
                report("DISC DRIVE","No disc","Insert a disc and close the lid. B returns to the menu.");
            else scan_disc();
            /* Cache the post-read status: a non-game disc is not re-read every poll. */
            status=-1;type=-1; cdrom_get_status(&status,&type);
            old_status=status;old_type=type;
        }
        thd_sleep(150);
    }
    return NULL;
}
static void focus(int value) {
    self.focus=(value+3)%3;
    if(self.focus==0 && !self.ready) self.focus=1;
    for(int i=0;i<3;i++) GUI_WidgetClearFlags(self.buttons[i],WIDGET_INSIDE);
    GUI_WidgetSetFlags(self.buttons[self.focus],WIDGET_INSIDE);
}
static void title(const char *value) {
    const char *p=value;
    for(int line=0;line<3;line++) {
        char text[132]; size_t n=0,space=0;
        GUI_Font *font=APP_GET_FONT(line<2?"heading":"body");
        while(*p==' ') p++;
        while(p[n] && n<128) {
            text[n]=p[n]; n++; text[n]=0;
            if(GUI_FontGetTextSize(font,text).w>338) { n--; break; }
            if(text[n-1]==' ') space=n-1;
        }
        if(p[n] && space) n=space;
        if(!n && *p) n=1;
        memcpy(text,p,n); text[n]=0;
        if(line==2 && p[n]) {
            while(n && GUI_FontGetTextSize(font,text).w>308) text[--n]=0;
            strcat(text,"...");
        }
        GUI_LabelSetText(self.title[line],text); p+=n;
    }
}
static void render(void *event,void *param,int action) {
    (void)event;(void)param;
    if(action!=EVENT_ACTION_RENDER || !self.app || !(self.app->state&APP_STATE_OPENED) || !self.pending) return;
    mutex_lock(&result_lock);
    disc_result_t result=self.result; self.pending=0;
    mutex_unlock(&result_lock);
    self.ready=result.ready;
    GUI_WidgetSetEnabled(self.buttons[0],self.ready);
    title(*result.info.title?result.info.title:"Insert a Dreamcast disc");
    const char *fields[]={result.info.region,result.info.vga,result.info.date,result.info.number,result.info.version,result.info.product};
    for(int i=0;i<6;i++) GUI_LabelSetText(self.fields[i],*fields[i]?fields[i]:"--");
    GUI_LabelSetText(self.type,result.type); GUI_LabelSetText(self.state,result.state);
    GUI_LabelSetText(self.message,result.message);
    focus(self.ready?0:self.focus);
}
static void stop_worker(void) {
    self.stop=1;
    if(self.worker) { thd_join(self.worker,NULL); self.worker=NULL; }
}
void gdplay_Back(GUI_Widget *widget) { (void)widget; OpenMainApp(); }
void gdplay_Refresh(GUI_Widget *widget) {
    (void)widget;
    if(self.worker) { self.ready=0; GUI_WidgetSetEnabled(self.buttons[0],0); self.request=1; }
    else GUI_LabelSetText(self.message,"Drive worker is unavailable. Reopen GD Play.");
}
void gdplay_play(GUI_Widget *widget) {
    (void)widget;
    if(!self.ready || !self.bios_patch) return;
    stop_worker();
    int status=-1,type=-1;
    if(cdrom_get_status(&status,&type)!=ERR_OK || status==CD_STATUS_OPEN || status==CD_STATUS_NO_DISC) {
        self.ready=0; self.stop=0; self.request=1;
        self.worker=thd_create(0,worker,NULL); return;
    }
    utility_close(self.input);
    ShutdownDS(true);
    arch_shutdown();
    gdplay_run_game(self.bios_patch);
}
static void input(void *event,void *param,int action) {
    (void)event;
    SDL_Event *e=param;
    if(action!=EVENT_ACTION_UPDATE || !e || !self.app || !(self.app->state&APP_STATE_OPENED) || utility_global_input(e)) return;
    int key=utility_key(e);
    if(key==UI_BACK || key==UI_START) gdplay_Back(NULL);
    else if(key==UI_X) gdplay_Refresh(NULL);
    else if(key==UI_LEFT || key==UI_UP) focus(self.focus-1);
    else if(key==UI_RIGHT || key==UI_DOWN) focus(self.focus+1);
    else if(key==UI_OK) GUI_WidgetClicked(self.buttons[self.focus],0,0);
    else utility_forward(e);
    e->type=SDL_NOEVENT;
}
void gdplay_Init(App_t *app) {
    memset(&self,0,sizeof(self)); self.app=app;
    self.buttons[0]=APP_GET_WIDGET("play-btn");self.buttons[1]=APP_GET_WIDGET("refresh-btn");self.buttons[2]=APP_GET_WIDGET("exit-btn");
    const char *fields[]={"region-txt","vga-txt","date-txt","disk-num-txt","version-txt","product-txt"};
    for(int i=0;i<6;i++) self.fields[i]=APP_GET_WIDGET(fields[i]);
    for(int i=0;i<3;i++) { char name[24]; snprintf(name,sizeof(name),"title%d-txt",i+1); self.title[i]=APP_GET_WIDGET(name); }
    self.type=APP_GET_WIDGET("disc-type");self.state=APP_GET_WIDGET("disc-state");self.message=APP_GET_WIDGET("status");
    char path[NAME_MAX]; snprintf(path,sizeof(path),"%s/firmware/rungd.bin",getenv("PATH"));
    file_t fd=fs_open(path,O_RDONLY);
    if(fd>=0) {
        if(fs_total(fd)>=PATCH_SIZE) {
            self.bios_patch=memalign(32,PATCH_SIZE);
            if(self.bios_patch && fs_read(fd,self.bios_patch,PATCH_SIZE)!=PATCH_SIZE) { free(self.bios_patch); self.bios_patch=NULL; }
        }
        fs_close(fd);
    }
    self.input=AddEvent("NextGDPlayInput",EVENT_TYPE_INPUT,EVENT_PRIO_DEFAULT,input,NULL);
    self.video=AddEvent("NextGDPlayRender",EVENT_TYPE_VIDEO,EVENT_PRIO_DEFAULT,render,NULL);
    if(self.input) SetEventActive(self.input,0);
}
void gdplay_Open(App_t *app) {
    (void)app;
    self.stop=0; self.request=1;self.ready=0;
    GUI_WidgetSetEnabled(self.buttons[0],0);
    utility_open(self.input); focus(1);
    if(!self.worker) self.worker=thd_create(0,worker,NULL);
    if(!self.worker) report("DISC DRIVE","Unavailable","Drive worker could not start. B returns to the menu.");
}
void gdplay_Close(App_t *app) { (void)app; utility_close(self.input); stop_worker(); }
void gdplay_Shutdown(App_t *app) {
    (void)app; stop_worker(); utility_remove(&self.input); utility_remove(&self.video);
    free(self.bios_patch); self.bios_patch=NULL; self.app=NULL;
}
