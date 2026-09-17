/* Controller-first presentation layer. The worker serializes VMU/storage IO;
 * the input thread remains available for modal confirmation and keyboard input. */
#include "ui_logic.h"
#include <dc/maple/keyboard.h>
#include "../../pointer_input.h"

enum { UI_UP=1, UI_DOWN, UI_LEFT, UI_RIGHT, UI_ACTIVATE, UI_BACK, UI_COPY,
       UI_TOOLS, UI_WIDGET, UI_ENTRY, UI_PREVIEW, UI_DELETE };
#define UI_EXIT_EVENT 0x564d5501
typedef struct { GUI_Widget *w; int x, y; } ui_node_t;
static struct {
    kthread_t *worker;
    Event_t *input;
    volatile int job, busy, stop, modal, answer, armed, bulk, cancel;
    GUI_Widget *pending_widget, *focus;
    dirent_fm_t pending_entry, selected_entry;
    bool selected, preview_only, allow_browse, exit_pending;
    int repeat_dir;
    uint64_t repeat_at, poll_at;
} ui;

static void ui_allow_image(bool enabled) { ui.allow_browse=enabled; }

static GUI_Widget *ui_widget(const char *name) {
    return GetElement(name, LIST_ITEM_GUI_WIDGET, 1);
}
static void ui_status(const char *text) {
    GUI_LabelSetText(ui_widget("status"), text);
}
static bool ui_other_open(void) {
    return GUI_ContainerContains(self.vmu_page, self.filebrowser2) != 0;
}
static void ui_short_label(const char *name, const char *text, unsigned max) {
    char line[128];
    if (strlen(text) <= max) snprintf(line, sizeof(line), "%s", text);
    else snprintf(line, sizeof(line), "...%s", text + strlen(text) - max + 3);
    GUI_LabelSetText(ui_widget(name), line);
}
static int ui_nodes(ui_node_t *out) {
    int n=0, page=GUI_CardStackGetIndex(self.pages);
#define NODE(name,xx,yy) do { GUI_Widget *w=ui_widget(name); \
    if (w && !(GUI_WidgetGetFlags(w)&(WIDGET_DISABLED|WIDGET_HIDDEN))) \
        out[n++]=(ui_node_t){w,xx,yy}; } while(0)
    if (page==0) {
        for (int p=0;p<4;++p) for(int s=0;s<2;++s) {
            char name[3]={(char)('A'+p),(char)('1'+s),0};
            if(GUI_ContainerContains(self.vmu_container,self.vmu[p][s])) NODE(name,94+p*150,164+s*76);
        }
    } else if (page==1) {
        NODE("file_browser",156,210);
        if(ui_other_open()) { NODE("file_browser2",484,210); }
        else {
            NODE("/sd",414,145); NODE("/ide",554,145);
            NODE("/pc",414,201); NODE("/cd",554,201); NODE("dst-vmu",414,257);
        }
        NODE("copy-button",94,326); NODE("copy-all-button",244,326);
        NODE("location-button",394,326); NODE("tools-button",544,326);
    } else if(page==2) {
        NODE("folder-name",320,216); NODE("confirm-yes",196,279); NODE("confirm-no",444,279);
    } else {
        NODE("dump-button",320,137); NODE("delete-button",320,185);
        NODE("new-folder",320,233); NODE("format-c",320,281); NODE("tools-back",320,341);
    }
    NODE("home_but",500,458); NODE("exit-button",580,458);
#undef NODE
    return n;
}
static void ui_highlight(void) {
    ui_node_t nodes[20]; int n=ui_nodes(nodes), found=0;
    for(int i=0;i<n;++i) {
        if(!utility_pointer) GUI_WidgetClearFlags(nodes[i].w,WIDGET_INSIDE);
        if(nodes[i].w==ui.focus) found=1;
    }
    if(!found) ui.focus=n?nodes[0].w:NULL;
    if(ui.focus && !utility_pointer) GUI_WidgetSetFlags(ui.focus,WIDGET_INSIDE);
    GUI_LabelSetTextColor(ui_widget("left-title"),ui.focus==self.filebrowser?83:231,ui.focus==self.filebrowser?225:238,ui.focus==self.filebrowser?227:244);
    GUI_LabelSetTextColor(ui_widget("right-title"),ui.focus==self.filebrowser2?83:231,ui.focus==self.filebrowser2?225:238,ui.focus==self.filebrowser2?227:244);
}
static void ui_refresh(void) {
    int page=GUI_CardStackGetIndex(self.pages);
    bool other=ui_other_open();
    const char *left=GUI_FileManagerGetPath(self.filebrowser);
    const char *right=GUI_FileManagerGetPath(self.filebrowser2);
    char line[100];
    bool from_vmu=ui.selected && ui.selected_entry.obj==(GUI_Object *)self.filebrowser;
    GUI_WidgetSetEnabled(ui_widget("copy-button"),ui.selected && self.m_SelectedFile && other &&
        (!from_vmu || !vmu_ui_read_only(right)));
    GUI_WidgetSetEnabled(ui_widget("copy-all-button"),other && vmu_dev(left) &&
        vmu_bulk_destination(right) && !vmu_ui_read_only(right) && strcmp(left,right));
    GUI_WidgetSetEnabled(self.button_dump,other && !vmu_ui_read_only(right) && strncmp(right,"/vmu/",5));
    GUI_WidgetSetEnabled(ui_widget("delete-button"),ui.selected && !vmu_ui_read_only(GUI_FileManagerGetPath((GUI_Widget *)ui.selected_entry.obj)) && strcmp(ui.selected_entry.ent.name,".."));
    GUI_WidgetSetEnabled(ui_widget("new-folder"),other && !vmu_ui_read_only(right) && strncmp(right,"/vmu/",5));
    GUI_WidgetSetEnabled(self.format_c,vmu_dev(left)!=NULL);
    GUI_WidgetSetEnabled(self.button_home,page!=0 || self.direction_flag!=0);
    if(other) GUI_WidgetSetFlags(ui_widget("location-help"),WIDGET_HIDDEN);
    else GUI_WidgetClearFlags(ui_widget("location-help"),WIDGET_HIDDEN);
    snprintf(line,sizeof(line),"VMU %s",strlen(left)>=7?left+5:"--");
    GUI_LabelSetText(ui_widget("left-title"),line);
    snprintf(line,sizeof(line),self.vmu_freeblock>=0?"%d blocks free":"Card unavailable",self.vmu_freeblock);
    GUI_LabelSetText(ui_widget("left-path"),line);
    GUI_LabelSetText(ui_widget("right-title"),other?"OTHER LOCATION":"CHOOSE A LOCATION");
    ui_short_label("right-path",other?right:"SD / IDE / PC / disc / VMU",34);
    const char *copy="Copy save";
    if(ui.selected && self.m_SelectedFile) {
        if(ui.selected_entry.obj==(GUI_Object *)self.filebrowser) copy="Copy to other >";
        else if(VMU_GetSaveType(self.m_SelectedFile)==DS_VMD) copy="Open VMU image";
        else copy="< Copy to VMU";
    }
    GUI_LabelSetText(GUI_ButtonGetCaption(ui_widget("copy-button")),copy);
    GUI_LabelSetText(ui_widget("subtitle"),page==0?(self.direction_flag?"Choose the destination card for your saves.":"Choose a memory card to manage its saves."):
        page==1?"Select a save. X / Copy sends it to the opposite location.":page==2?"Create a folder in the location you are browsing.":"Choose an action for the selected save or VMU.");
    GUI_LabelSetText(ui_widget("controls"),page==0?"D-pad: move   A: open VMU   B: back":page==1?
        "D-pad: move   A: select/open   B: back   X: copy   Y: actions":"D-pad: move   A: select   B: back");
    if(self.m_SelectedFile) ui_short_label("save-name",self.m_SelectedFile,60);
    ui_highlight();
}
static void ui_queue(int job,GUI_Widget *widget,const dirent_fm_t *entry) {
    if(ui.busy || ui.job || ui.modal || ui.stop) return;
    ui.busy=1;
    ui.pending_widget=widget;
    if(entry) ui.pending_entry=*entry;
    ui.job=job;
}
static void ui_browse(const dirent_fm_t *entry,bool preview) {
    dirent_fm_t copy=*entry;
    ui.focus=(GUI_Widget *)copy.obj;
    ui.selected_entry=copy; ui.selected=true;
    if(copy.ent.attr==O_DIR) {
        reset_selected(); clr_statusbar();
        if(!preview) { VMU_Manager_ItemClick(&copy); ui.selected=false; }
        else { GUI_LabelSetText(self.save_name,copy.ent.name); GUI_LabelSetText(self.save_size,"Folder"); }
    } else {
        VMU_Manager_ItemSelect(&copy);
        if(!self.m_SelectedFile) ui.selected=false;
    }
}
void VMU_Manager_BrowseClick(dirent_fm_t *entry) {
    if(!entry) return;
    if(thd_get_current()==ui.worker) ui_browse(entry,ui.preview_only);
    else ui_queue(UI_ENTRY,NULL,entry);
}
void VMU_Manager_BrowseSelect(dirent_fm_t *entry) {
    if(entry) ui_queue(UI_PREVIEW,NULL,entry);
}
static void ui_select_row(GUI_Widget *fm,int index,bool preview) {
    GUI_Widget *panel=GUI_FileManagerGetItemPanel(fm);
    int count=GUI_ContainerGetCount(panel);
    if(index<0 || index>=count) return;
    GUI_FileManagerSetSelectedItem(fm,index);
    SDL_Rect area=GUI_WidgetGetArea(panel);
    int offset=GUI_PanelGetYOffset(panel), top=index*26;
    if(top<offset) offset=top;
    else if(top+26>offset+area.h) offset=top+26-area.h;
    GUI_PanelSetYOffset(panel,offset);
    ui.preview_only=preview;
    GUI_WidgetClicked(GUI_FileManagerGetItem(fm,index),0,0);
    ui.preview_only=false;
}
static void ui_move(int direction) {
    if((ui.focus==self.filebrowser || ui.focus==self.filebrowser2) && (direction==UI_UP || direction==UI_DOWN)) {
        int index=GUI_FileManagerGetSelectedItem(ui.focus);
        int count=GUI_ContainerGetCount(GUI_FileManagerGetItemPanel(ui.focus));
        int next=index<0?0:index+(direction==UI_UP?-1:1);
        if(next>=0 && next<count) { ui_select_row(ui.focus,next,true); return; }
    }
    ui_node_t nodes[20]; int count=ui_nodes(nodes), current=-1, best=-1, score=0x7fffffff;
    for(int i=0;i<count;++i) if(nodes[i].w==ui.focus) current=i;
    if(current<0) { ui_highlight(); return; }
    for(int i=0;i<count;++i) {
        int dx=nodes[i].x-nodes[current].x,dy=nodes[i].y-nodes[current].y;
        int along=direction==UI_LEFT?-dx:direction==UI_RIGHT?dx:direction==UI_UP?-dy:dy;
        int across=direction==UI_LEFT || direction==UI_RIGHT?abs(dy):abs(dx);
        if(along>0 && along+across*3<score) { score=along+across*3; best=i; }
    }
    if(best>=0) {
        GUI_WidgetClearFlags(ui.focus,WIDGET_INSIDE); ui.focus=nodes[best].w;
        if(ui.focus==self.filebrowser || ui.focus==self.filebrowser2) {
            int index=GUI_FileManagerGetSelectedItem(ui.focus);
            ui_select_row(ui.focus,index<0?0:index,true);
        } else if(GUI_CardStackGetIndex(self.pages)==0 && ui.focus!=self.button_home && ui.focus!=ui_widget("exit-button"))
            VMU_Manager_info_bar(ui.focus);
    }
}
static void ui_back(void) {
    int page=GUI_CardStackGetIndex(self.pages);
    if(page==2 || page==3) { GUI_CardStackShowIndex(self.pages,1); ui.focus=ui.selected?(GUI_Widget *)ui.selected_entry.obj:self.filebrowser; }
    else if(page==0 && self.direction_flag) {
        GUI_CardStackShowIndex(self.pages,1); self.direction_flag=0; ui.focus=ui_widget("location-button");
    } else if(page==1 && ui.focus==self.filebrowser2 && ui_other_open()) {
        GUI_Widget *first=GUI_FileManagerGetItem(self.filebrowser2,0);
        if(first) { ui.preview_only=false; GUI_WidgetClicked(first,0,0); }
    } else if(page==1) { VMU_Manager_EnableMainPage(); ui.focus=NULL; }
    else { ui.exit_pending=true; }
}
static bool ui_bulk_progress(int done,int total,const char *name,void *arg) {
    (void)arg;
    char message[96];
    if(ui.cancel || ui.stop || !(self.m_App->state&APP_STATE_OPENED)) return false;
    snprintf(message,sizeof(message),"Copying %d / %d: %s",done<total?done+1:done,total,name);
    GUI_LabelSetText(ui_widget("progress-label"),message);
    GUI_ProgressBarSetPosition(self.progressbar,total?(double)done/total:0.0);
    return true;
}
static void ui_copy_all(void) {
    const char *left=GUI_FileManagerGetPath(self.filebrowser);
    const char *right=GUI_FileManagerGetPath(self.filebrowser2);
    maple_device_t *source=vmu_dev(left), *target=vmu_dev(right);
    char message[768];
    vmu_bulk_plan_t *plan=calloc(1,sizeof(*plan));
    if(!plan) { ui_status("Not enough memory to copy all saves."); return; }
    if(!ui_other_open() || !vmu_bulk_prepare(source,target,right,plan)) {
        ui_status(plan->error==VMU_BULK_SPACE?"Not enough free blocks. No saves were copied.":
                  plan->error==VMU_BULK_SOURCE?"Could not read the VMU directory. No saves were copied.":
                  "Cannot copy all here. Check the VMU and SD / IDE destination.");
        free(plan); return;
    }
    if(!plan->pending) {
        snprintf(message,sizeof(message),plan->count?"0 copied, %d skipped: all filenames already exist.":"No saves on this VMU.",plan->count);
        ui_status(message); free(plan); return;
    }
    snprintf(message,sizeof(message),"Copy %d saves from VMU %.2s to %s? %d existing filenames will be skipped.",
             plan->pending,left+5,right,plan->count-plan->pending);
    GUI_LabelSetText(self.confirm_text,message);
    if(ui_confirm()!=CMD_OK) { ui_status("Copy all cancelled. No saves were copied."); free(plan); return; }
    ui.cancel=0; ui.bulk=1;
    GUI_LabelSetText(ui_widget("controls"),"Copying saves... B: stop after the current save");
    GUI_ProgressBarSetPosition(self.progressbar,0.0);
    GUI_ContainerAdd(self.vmu_page,self.progressbar_container);
    vmu_bulk_copy(source,target,right,plan,ui_bulk_progress,NULL);
    ui.bulk=0;
    GUI_ContainerRemove(self.vmu_page,self.progressbar_container);
    GUI_LabelSetText(ui_widget("progress-label"),"Working... keep the VMU connected.");
    if(!plan->error) snprintf(message,sizeof(message),"Copy all complete: %d copied, %d skipped.",plan->copied,plan->skipped);
    else if(plan->error==VMU_BULK_CANCELLED) snprintf(message,sizeof(message),"Copy stopped: %d copied, %d skipped. Completed saves were kept.",plan->copied,plan->skipped);
    else snprintf(message,sizeof(message),"%s failed: %.12s. %d copied, %d skipped; stopped.",
                  plan->error==VMU_BULK_READ?"Read":"Write",plan->failed_name,plan->copied,plan->skipped);
    ui_status(message);
    reset_selected(); clr_statusbar(); ui.selected=false;
    GUI_FileManagerScan(self.filebrowser2);
    if(target) free_blocks(right,1);
    free(plan);
}
static void ui_do_widget(GUI_Widget *widget) {
    if(!widget || (GUI_WidgetGetFlags(widget)&WIDGET_DISABLED)) return;
    const char *name=GUI_ObjectGetName(widget);
    if(strlen(name)==2 && name[0]>='A' && name[0]<='D' && name[1]>='1' && name[1]<='2') {
        VMU_Manager_vmu(widget); ui.selected=false; ui.focus=self.direction_flag?self.filebrowser2:self.filebrowser;
    } else if(!strcmp(name,"home_but")) { VMU_Manager_EnableMainPage(); ui.selected=false; ui.focus=NULL; }
    else if(!strcmp(name,"exit-button")) ui.exit_pending=true;
    else if(name[0]=='/') { self.direction_flag=0; VMU_Manager_addfileman(widget); ui.selected=false; ui.focus=self.filebrowser2; }
    else if(!strcmp(name,"dst-vmu")) { VMU_Manager_sel_dst_vmu(widget); ui.focus=NULL; }
    else if(!strcmp(name,"location-button")) { addbutton(); ui.selected=false; ui.focus=self.sd_c; }
    else if(!strcmp(name,"tools-button")) { GUI_CardStackShowIndex(self.pages,3); ui.focus=NULL; }
    else if(!strcmp(name,"tools-back")) { GUI_CardStackShowIndex(self.pages,1); ui.focus=ui.selected?(GUI_Widget *)ui.selected_entry.obj:self.filebrowser; }
    else if(!strcmp(name,"new-folder")) { GUI_CardStackShowIndex(self.pages,2); ui.focus=self.folder_name; }
    else if(!strcmp(name,"confirm-yes") || !strcmp(name,"confirm-no")) { VMU_Manager_make_folder(widget); ui.focus=self.filebrowser2; }
    else if(!strcmp(name,"copy-button")) {
        if(ui.selected && self.m_SelectedFile && ui_other_open()) {
            GUI_CardStackShowIndex(self.pages,1);
            dirent_fm_t entry=ui.selected_entry;
            VMU_Manager_ItemClick(&entry); ui.selected=false;
        }
    } else if(!strcmp(name,"delete-button")) {
        if(ui.selected) {
            GUI_CardStackShowIndex(self.pages,1);
            dirent_fm_t entry=ui.selected_entry;
            VMU_Manager_ItemContextClick(&entry); ui.selected=false;
        }
    } else if(!strcmp(name,"copy-all-button")) { ui_copy_all(); }
    else if(!strcmp(name,"dump-button")) {
        GUI_CardStackShowIndex(self.pages,1); ui.focus=ui_widget("copy-all-button"); VMU_Manager_Dump(widget);
    }
    else if(!strcmp(name,"format-c")) { GUI_CardStackShowIndex(self.pages,1); VMU_Manager_format(widget); ui.selected=false; }
}
void VMU_Manager_Action(GUI_Widget *widget) {
    if(!widget || (GUI_WidgetGetFlags(widget)&WIDGET_DISABLED)) return;
    if(thd_get_current()==ui.worker) ui_do_widget(widget);
    else ui_queue(UI_WIDGET,widget,NULL);
}
void VMU_Manager_Confirm(GUI_Widget *widget) {
    if(!ui.modal || !ui.armed) return;
    const char *name=GUI_ObjectGetName(widget);
    if(!strcmp(name,"modal-cancel")) ui.answer=CMD_ERROR;
    else if(!strcmp(name,"modal-accept")) ui.answer=CMD_OK;
    else if(ui.allow_browse) ui.answer=CMD_NO_ARG;
}
static int ui_confirm(void) {
    char message[1024];
    snprintf(message,sizeof(message),"%s",GUI_LabelGetText(self.confirm_text));
    const char *p=message;
    for(int i=0;i<3;++i) {
        char part[66]; size_t n=strlen(p); if(n>60) n=60;
        if(strlen(p)>n) { size_t space=n; while(space && p[space]!=' ' && p[space]!='/') --space; if(space) n=space; }
        memcpy(part,p,n); part[n]=0; p+=n; while(*p==' ') ++p;
        GUI_LabelSetText(i==0?self.confirm_text:ui_widget(i==1?"confirm-line-1":"confirm-line-2"),part);
    }
    const char *accept=!strncmp(message,"Delete",6)?"A  Delete":!strncmp(message,"Format",6)?"A  Format":
        !strncmp(message,"Restore",7)?"A  Restore":!strncmp(message,"Overwrite",9)?"A  Overwrite":"A  Continue";
    GUI_LabelSetText(GUI_ButtonGetCaption(ui_widget("modal-accept")),accept);
    GUI_WidgetSetEnabled(ui_widget("modal-browse"),ui.allow_browse);
    if(ui.allow_browse) GUI_WidgetClearFlags(ui_widget("modal-browse"),WIDGET_HIDDEN);
    else GUI_WidgetSetFlags(ui_widget("modal-browse"),WIDGET_HIDDEN);
    ui.answer=-100; ui.armed=0; ui.modal=1;
    GUI_ContainerAdd(self.vmu_page,self.confirm);
    while(!ui.stop && (self.m_App->state&APP_STATE_OPENED) && ui.answer==-100) {
        /* Read physical state: a modal keyboard can consume release events
         * before this app sees them. Do not latch those events locally. */
        unsigned held=0, keys=0;
        for(int i=0;i<4;++i) {
            maple_device_t *dev=maple_enum_type(i,MAPLE_FUNC_CONTROLLER);
            cont_state_t *state=dev?maple_dev_status(dev):NULL;
            if(state) held|=state->buttons&(CONT_A|CONT_B|CONT_X|CONT_Y);
            dev=maple_enum_type(i,MAPLE_FUNC_KEYBOARD);
            kbd_state_t *kbd=dev?maple_dev_status(dev):NULL;
            if(kbd) keys|=kbd->key_states[KBD_KEY_ENTER].is_down ||
                kbd->key_states[KBD_KEY_SPACE].is_down || kbd->key_states[KBD_KEY_X].is_down;
        }
        ui.armed=vmu_ui_confirm_ready(ui.armed,held,keys,SDL_GetMouseState(NULL,NULL)!=0);
        thd_sleep(20);
    }
    GUI_ContainerRemove(self.vmu_page,self.confirm);
    ui.modal=0; ui.repeat_dir=0;
    return ui.answer==-100?CMD_ERROR:ui.answer;
}
static void ui_scan_slots(void) {
    GUI_Widget *old_focus=ui.focus;
    for(int p=0;p<4;++p) for(int s=0;s<2;++s) {
        maple_device_t *dev=maple_enum_dev(p,s+1);
        bool ready=dev && (dev->info.functions&MAPLE_FUNC_MEMCARD);
        const char *source=GUI_FileManagerGetPath(self.filebrowser);
        bool same=self.direction_flag && strlen(source)>=7 && source[5]=='A'+p && source[6]=='1'+s;
        if(same) ready=false;
        char caption[32]; snprintf(caption,sizeof(caption),"%c%d / %s",'A'+p,s+1,same?"Source":ready?"Ready":dev?"Not a VMU":"Empty");
        GUI_LabelSetText(GUI_ButtonGetCaption(self.vmu[p][s]),caption);
        GUI_WidgetSetEnabled(self.vmu[p][s],ready);
    }
    if(!ui.poll_at) ui.focus=NULL;
    ui_highlight();
    if(ui.focus && ui.focus!=old_focus && ui.focus!=self.button_home && ui.focus!=ui_widget("exit-button"))
        VMU_Manager_info_bar(ui.focus);
}
static void *ui_service(void *arg) {
    (void)arg;
    while(!ui.stop) {
        if(!(self.m_App->state&APP_STATE_OPENED)) { thd_sleep(30); continue; }
        if(ui.job) {
            int job=ui.job; ui.job=0;
            if(job>=UI_UP && job<=UI_RIGHT) ui_move(job);
            else if(job==UI_ACTIVATE) {
                if(ui.focus==self.filebrowser || ui.focus==self.filebrowser2) {
                    int index=GUI_FileManagerGetSelectedItem(ui.focus);
                    ui_select_row(ui.focus,index<0?0:index,false);
                } else if(ui.focus) GUI_WidgetClicked(ui.focus,0,0);
            } else if(job==UI_BACK) ui_back();
            else if(job==UI_COPY) ui_do_widget(ui_widget("copy-button"));
            else if(job==UI_TOOLS) ui_do_widget(ui_widget("tools-button"));
            else if(job==UI_WIDGET) ui_do_widget(ui.pending_widget);
            else if(job==UI_ENTRY || job==UI_PREVIEW) ui_browse(&ui.pending_entry,job==UI_PREVIEW);
            if(GUI_CardStackGetIndex(self.pages)==0) ui_scan_slots();
            ui_refresh(); ui.busy=0;
            if(ui.exit_pending) {
                SDL_Event e; memset(&e,0,sizeof(e)); e.type=SDL_USEREVENT; e.user.code=UI_EXIT_EVENT;
                SDL_PushEvent(&e); ui.exit_pending=false;
            }
        }
        uint64_t now=timer_ms_gettime64();
        if(GUI_ScreenGetFocusWidget(GUI_GetScreen())) {
            ui.repeat_dir=0;
        }
        if(!ui.busy && !ConsoleIsVisible() && !GUI_ScreenGetFocusWidget(GUI_GetScreen()) && ui.repeat_dir && now>=ui.repeat_at) {
            ui_queue(ui.repeat_dir,NULL,NULL); ui.repeat_at=now+150;
        }
        if(!ui.busy && now>=ui.poll_at && GUI_CardStackGetIndex(self.pages)==0) {
            ui.busy=1; ui_scan_slots(); ui.busy=0; ui.poll_at=now+1000;
        }
        thd_sleep(20);
    }
    return NULL;
}
static void ui_input(void *event,void *param,int action) {
    (void)event; SDL_Event *e=param;
    if(action!=EVENT_ACTION_UPDATE || !e || !(self.m_App->state&APP_STATE_OPENED)) return;
    if(e->type==SDL_USEREVENT && e->user.code==UI_EXIT_EVENT) { e->type=SDL_NOEVENT; VMU_Manager_Exit(NULL); return; }
    if(ConsoleIsVisible() || (e->type==SDL_KEYDOWN && (e->key.keysym.sym==SDLK_F1 || e->key.keysym.sym==SDLK_PRINT || (e->key.keysym.mod&(KMOD_CTRL|KMOD_ALT))))) return;
    utility_pointer_event(e);
    if(e->type==SDL_NOEVENT) return;
    if(utility_pointer) ui.repeat_dir=0;
    if(GUI_ScreenGetFocusWidget(GUI_GetScreen())) {
        ui.repeat_dir=0; GUI_ScreenEvent(GUI_GetScreen(),e,0,0); e->type=SDL_NOEVENT; return;
    }
    int job=0,button=-1;
    if(e->type==SDL_JOYBUTTONDOWN &&
       (e->jbutton.button!=SDL_DC_A || !utility_pointer)) button=e->jbutton.button;
    if(e->type==SDL_KEYDOWN) {
        switch(e->key.keysym.sym) {
            case SDLK_UP:job=UI_UP;break; case SDLK_DOWN:job=UI_DOWN;break;
            case SDLK_LEFT:job=UI_LEFT;break; case SDLK_RIGHT:case SDLK_TAB:job=UI_RIGHT;break;
            case SDLK_RETURN:case SDLK_SPACE:button=SDL_DC_A;break;
            case SDLK_ESCAPE:case SDLK_BACKSPACE:button=SDL_DC_B;break;
            case SDLK_x:button=SDL_DC_X;break; case SDLK_y:button=SDL_DC_Y;break;
            default:break;
        }
    }
    if(ui.modal) {
        if(ui.armed && button==SDL_DC_A) ui.answer=CMD_OK;
        else if(ui.armed && button==SDL_DC_B) ui.answer=CMD_ERROR;
        else if(ui.armed && button==SDL_DC_X && ui.allow_browse) ui.answer=CMD_NO_ARG;
        else if(e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP) GUI_ScreenEvent(GUI_GetScreen(),e,0,0);
        e->type=SDL_NOEVENT; return;
    }
    if(ui.bulk) {
        if(button==SDL_DC_B) ui.cancel=1;
        e->type=SDL_NOEVENT; return;
    }
    if(e->type==SDL_JOYHATMOTION && !e->jhat.hat) {
        job=e->jhat.value&SDL_HAT_UP?UI_UP:e->jhat.value&SDL_HAT_DOWN?UI_DOWN:e->jhat.value&SDL_HAT_LEFT?UI_LEFT:e->jhat.value&SDL_HAT_RIGHT?UI_RIGHT:0;
        ui.repeat_dir=job; ui.repeat_at=timer_ms_gettime64()+400;
    }
    if(button==SDL_DC_A) job=UI_ACTIVATE;
    else if(button==SDL_DC_B) job=UI_BACK;
    else if(button==SDL_DC_X && GUI_CardStackGetIndex(self.pages)==1) job=UI_COPY;
    else if(button==SDL_DC_Y && GUI_CardStackGetIndex(self.pages)==1) job=UI_TOOLS;
    if(job) {
        if(job>UI_RIGHT) ui.repeat_dir=0;
        ui_queue(job,NULL,NULL);
    }
    else if(!ui.busy && (e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP || e->type>=SDL_USEREVENT)) GUI_ScreenEvent(GUI_GetScreen(),e,0,0);
    e->type=SDL_NOEVENT;
}
static void ui_init(void) {
    memset(&ui,0,sizeof(ui));
    ui.input=AddEvent("VMUManagerInput",EVENT_TYPE_INPUT,EVENT_PRIO_DEFAULT,ui_input,NULL);
    if(ui.input) SetEventActive(ui.input,0);
    ui.worker=thd_create(0,ui_service,NULL);
    ui_refresh();
}
void VMU_Manager_Open(App_t *app) {
    (void)app;
    if(!ui.input || !ui.worker) { ui_status("Unable to start the VMU manager."); return; }
    GUI_DisableInput(); utility_pointer_open();
    GUI_ScreenSetJoySelectState(GUI_GetScreen(),0); SetEventActive(ui.input,1);
    ui.repeat_dir=0; ui.poll_at=0;
    ui_refresh();
}
void VMU_Manager_Close(void) {
    ui.answer=CMD_ERROR; ui.repeat_dir=0;
    if(ui.input) SetEventActive(ui.input,0);
    GUI_ScreenSetJoySelectState(GUI_GetScreen(),1); SDL_DC_EmulateMouse(SDL_TRUE); GUI_EnableInput();
}
void VMU_Manager_Shutdown(void) {
    VMU_Manager_Close(); ui.stop=1;
    if(ui.worker) { thd_join(ui.worker,NULL); ui.worker=NULL; }
    if(ui.input) { RemoveEvent(ui.input); ui.input=NULL; }
    reset_selected(); fs_vmd_shutdown();
}
