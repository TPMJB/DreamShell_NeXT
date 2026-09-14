"""Controller and Games regressions from real-console feedback."""
from test_iso_loader_next import ROOT, compile_run
import unittest

class ConsoleFeedbackChecks(unittest.TestCase):
    def test_baseline_redetects_wince_and_selects_its_loader_address(self):
        ui=(ROOT/"applications/iso_loader/modules/next_ui.h").read_text()
        baseline=ui[ui.index("void isoLoader_Baseline("):ui.index("void isoLoader_RestoreProfile(")]
        support=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NAME_MAX 256
#define ISOLDR_DEFAULT_ADDR 0x8ce00000U
#define ISOLDR_DEFAULT_ADDR_MIN 0x8c000100U
enum {BIN_TYPE_KATANA=1,BIN_TYPE_KOS,BIN_TYPE_WINCE,IMAGE_TYPE_ROM_NAOMI=99,
      BOOT_MODE_DIRECT=0,CDDA_MODE_DISABLED=0};
typedef uint32_t uint32;
typedef struct {int state;const char *name;char text[128];} GUI_Widget;
typedef struct {int image_type;struct {unsigned size;int type;} exec;} isoldr_info_t;
static struct {
    int loading,image_type,profile_mode;
    char filename[64];
    isoldr_info_t *isoldr;
    GUI_Widget *filebrowser,*btn_run,*btn_check,*preset,*dma,*alt_read,*irq,*low,*fastboot,
               *screenshot,*use_gpio,*alt_boot,*vmu_disabled,*device,*verify_boot,*memory_text;
    GUI_Widget *os_chk[4],*async[10],*heap[20],*boot_mode_chk[3],*memory_chk[4],*wpa[2],*wpv[2];
    unsigned pa[2],pv[2];
} self;
static int detected_type,inspect_count,inspect_failed,messages;
static char status[128];
static const char *GUI_FileManagerGetPath(GUI_Widget *w) {(void)w;return "/sd/games/BAM4";}
static isoldr_info_t *isoldr_get_info(const char *p,int test) {
    assert(!strcmp(p,"/sd/games/BAM4/game.gdi") && !test);++inspect_count;
    if(inspect_failed) return NULL;
    isoldr_info_t *i=calloc(1,sizeof(*i));assert(i);i->exec.type=detected_type;i->exec.size=4096;return i;
}
static const char *isoldr_get_last_error(void) {return "Cannot read executable";}
static void next_status(const char *s) {snprintf(status,sizeof(status),"%s",s);}
static void next_message(const char *s) {assert(!strcmp(s,"Cannot read executable"));++messages;}
static void next_refresh(void) {}
static void GUI_WidgetSetState(GUI_Widget *w,int s) {if(w) w->state=s;}
static void GUI_TextEntrySetText(GUI_Widget *w,const char *s) {if(w) snprintf(w->text,sizeof(w->text),"%s",s);}
static const char *GUI_ObjectGetName(GUI_Widget *w) {return w->name;}
#define GUI_WidgetSetEnabled(w,s) ((void)(w),(void)(s))
#define isoLoader_toggleOS(w) ((void)(w))
#define isoLoader_toggleAsync(w) ((void)(w))
#define isoLoader_toggleVMU(w) ((void)(w))
#define isoLoader_toggleHeap(w) ((void)(w))
#define isoLoader_toggleBootMode(w) ((void)(w))
#define setModeCDDA(s) ((void)(s))
static void isoLoader_toggleMemory(GUI_Widget *w) {
    for(int i=0;self.memory_chk[i];++i) self.memory_chk[i]->state=self.memory_chk[i]==w;
}
'''
        cases=r'''
int main(void) {
    GUI_Widget low={.name="0x8c000100"},high={.name="0x8ce00000"},custom={.name="0x8"},text={0};
    self.memory_chk[0]=&low;self.memory_chk[1]=&high;self.memory_chk[2]=&custom;
    self.memory_text=&text;strcpy(self.filename,"game.gdi");
    self.isoldr=calloc(1,sizeof(*self.isoldr));assert(self.isoldr);
    /* A stale preset forcing KATANA must not make the WinCE baseline use high RAM. */
    self.isoldr->exec.type=BIN_TYPE_KATANA;detected_type=BIN_TYPE_WINCE;
    isoLoader_Baseline(NULL);
    assert(inspect_count==1 && self.isoldr->exec.type==BIN_TYPE_WINCE);
    assert(low.state && !high.state && !custom.state && self.profile_mode);
    assert(strstr(status,"WinCE") && strstr(status,"8c000100"));
    /* Other games keep their existing baseline address. */
    for(detected_type=BIN_TYPE_KATANA;detected_type<=BIN_TYPE_KOS;++detected_type) {
        isoLoader_Baseline(NULL);assert(!low.state && high.state && !custom.state);
        assert(strstr(status,"8ce00000"));
    }
    /* Custom memory entry fallback produces the same complete address. */
    self.memory_chk[0]=&custom;self.memory_chk[1]=NULL;detected_type=BIN_TYPE_WINCE;
    isoLoader_Baseline(NULL);assert(custom.state && !strcmp(text.text,"c000100"));
    char memory[144];snprintf(memory,sizeof(memory),"%s%s",custom.name,text.text);
    assert(strtoul(memory,NULL,16)==ISOLDR_DEFAULT_ADDR_MIN);
    isoldr_info_t *previous=self.isoldr;inspect_failed=1;self.profile_mode=0;
    isoLoader_Baseline(NULL);assert(messages==1 && self.isoldr==previous && !self.profile_mode);
    int inspections=inspect_count;self.loading=1;isoLoader_Baseline(NULL);
    assert(inspect_count==inspections);
    free(self.isoldr);
    return 0;
}
'''
        compile_run(support+baseline+cases,["-fsanitize=address"])

    def test_dpad_geometry_and_long_lists(self):
        nav=(ROOT/"applications/iso_loader/modules/next_nav.h").read_text()
        support=r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int x,y,w,h; } SDL_Rect;
typedef struct { int height; } GUI_Surface;
typedef struct Widget {
    int type,flags,xoff,yoff,index,count,refs,clicks;
    SDL_Rect area;
    struct Widget *parent,*children[160],*panel;
    GUI_Surface knob;
} GUI_Widget;
typedef GUI_Widget GUI_Object;
enum { WIDGET_TYPE_OTHER, WIDGET_TYPE_BUTTON, WIDGET_TYPE_SCROLLBAR,
       WIDGET_TYPE_CONTAINER, WIDGET_TYPE_CARDSTACK, WIDGET_TYPE_TEXTENTRY };
enum { WIDGET_HIDDEN=1, WIDGET_DISABLED=2, WIDGET_PRESSED=4, SDL_NOEVENT=0, SDL_MOUSEMOTION=3 };
typedef struct { int type; struct { int x,y; } motion; } SDL_Event;
static struct { GUI_Widget *body; } app;
static struct { __typeof__(app) *app; GUI_Widget *filebrowser,*fw_browser,*controller_target; } self;
static int mouse_x,mouse_y,hover_count,pending_scan,applied_scan;
#define GUI_WidgetGetArea(w) ((w)->area)
#define GUI_WidgetGetParent(w) ((w)->parent)
#define GUI_WidgetGetType(w) ((w)->type)
#define GUI_WidgetGetFlags(w) ((w)->flags)
#define GUI_WidgetSetFlags(w,f) ((w)->flags|=(f))
#define GUI_WidgetClearFlags(w,f) ((w)->flags&=~(f))
#define GUI_ObjectIncRef(w) (++(w)->refs)
#define GUI_ObjectDecRef(w) (--(w)->refs)
static void GUI_WidgetClicked(GUI_Widget *w,int x,int y) {assert(x>=0 && y>=0);++w->clicks;pending_scan=1;}
#define GUI_PanelGetXOffset(w) ((w)->xoff)
#define GUI_PanelGetYOffset(w) ((w)->yoff)
#define GUI_PanelSetYOffset(w,n) ((w)->yoff=(n))
#define GUI_ContainerGetCount(w) ((w)->count)
#define GUI_ContainerGetChild(w,n) ((w)->children[n])
#define GUI_CardStackGetIndex(w) ((w)->index)
#define GUI_FileManagerGetItemPanel(w) ((w)->panel)
#define GUI_FileManagerGetItem(w,n) ((w)->panel->children[n])
#define GUI_ScrollBarGetKnobImage(w) (&(w)->knob)
#define GUI_SurfaceGetHeight(k) ((k)->height)
#define GUI_ScrollBarSetVerticalPosition(w,n) ((w)->index=(n))
#define GUI_WidgetMarkChanged(w) ((void)(w))
#define GUI_GetScreen() 0
static void SDL_GetMouseState(int *x,int *y) { *x=mouse_x; *y=mouse_y; }
static void SDL_WarpMouse(int x,int y) { mouse_x=x; mouse_y=y; }
static void GUI_ScreenEvent(int s,SDL_Event *e,int x,int y) {
    (void)s;(void)x;(void)y;
    if(e->type==SDL_NOEVENT) {applied_scan+=pending_scan;pending_scan=0;}
    else {assert(e->type==SDL_MOUSEMOTION);++hover_count;}
}
static void add(GUI_Widget *p,GUI_Widget *w,int type,int x,int y,int width,int height) {
    memset(w,0,sizeof(*w));w->type=type;w->area=(SDL_Rect){x,y,width,height};
    if(p) {w->parent=p;p->children[p->count++]=w;}
}
'''
        cases=r'''
int main(void) {
    GUI_Widget root,header,buttons[9],pages,page0,page1,list,panel,rows[100],bar;
    GUI_Widget actions,footer[6],nested,setting,hidden,offscreen;
    add(NULL,&root,WIDGET_TYPE_CONTAINER,0,0,640,480);
    app.body=&root;self.app=&app;
    add(&root,&header,WIDGET_TYPE_CONTAINER,0,0,640,100);
    for(int i=0;i<9;i++) add(&header,&buttons[i],WIDGET_TYPE_BUTTON,24+i*64,68,58,30);
    add(&root,&pages,WIDGET_TYPE_CARDSTACK,0,100,640,380);
    add(&pages,&page0,WIDGET_TYPE_CONTAINER,0,0,640,380);
    add(&pages,&page1,WIDGET_TYPE_CONTAINER,0,0,640,380);
    add(&page0,&list,WIDGET_TYPE_CONTAINER,23,27,308,240);self.filebrowser=&list;
    add(&list,&panel,WIDGET_TYPE_CONTAINER,0,0,286,240);list.panel=&panel;
    for(int i=0;i<100;i++) add(&panel,&rows[i],WIDGET_TYPE_BUTTON,0,i*30,286,30);
    add(&list,&bar,WIDGET_TYPE_SCROLLBAR,290,0,18,240);bar.knob.height=40;
    add(&root,&actions,WIDGET_TYPE_CONTAINER,24,443,592,30);
    for(int i=0;i<6;i++) add(&actions,&footer[i],WIDGET_TYPE_BUTTON,i*98,0,90,30);
    add(&page1,&nested,WIDGET_TYPE_CONTAINER,350,80,200,100);
    add(&nested,&setting,WIDGET_TYPE_BUTTON,10,20,100,25);
    add(&page1,&hidden,WIDGET_TYPE_BUTTON,20,0,100,25);hidden.flags=WIDGET_HIDDEN;
    add(&nested,&offscreen,WIDGET_TYPE_BUTTON,0,110,100,25);
    /* Every top and bottom control is reached in screen order. */
    next_pointer(next_area(&buttons[0]));
    for(int i=1;i<9;i++) {next_navigate(1,0,0);assert(next_contains(next_area(&buttons[i]),mouse_x,mouse_y));}
    for(int i=7;i>=0;i--) {next_navigate(-1,0,0);assert(next_contains(next_area(&buttons[i]),mouse_x,mouse_y));}
    next_navigate(0,0,2);
    assert(next_contains(next_area(&footer[0]),mouse_x,mouse_y));
    for(int i=1;i<6;i++) {next_navigate(1,0,0);assert(next_contains(next_area(&footer[i]),mouse_x,mouse_y));}
    /* Scroll past the old 64-control ceiling, without activating a row. */
    next_pointer(next_area(&rows[0]));
    for(int i=1;i<100;i++) {
        next_navigate(0,1,0);
        assert(next_contains(next_area(&rows[i]),mouse_x,mouse_y));
        assert(next_contains(next_area(&panel),mouse_x,mouse_y));
    }
    assert(panel.yoff==2760 && bar.index==200);
    for(int i=98;i>=0;i--) {next_navigate(0,-1,0);assert(next_contains(next_area(&rows[i]),mouse_x,mouse_y));}
    assert(panel.yoff==0);
    next_navigate(0,-1,0);assert(mouse_y<100);
    /* Analog movement followed by D-pad uses the actual pointer position. */
    SDL_WarpMouse(40,127+4*30+3);next_navigate(0,1,0);
    assert(next_contains(next_area(&rows[5]),mouse_x,mouse_y));
    /* A activates a directory row even without WIDGET_INSIDE/hover state. */
    next_controller_click(1);assert(rows[5].refs==1 && (rows[5].flags&WIDGET_PRESSED));
    next_controller_click(0);assert(rows[5].refs==0 && rows[5].clicks==1);
    assert(!pending_scan && applied_scan==1); /* no extra directional input */
    /* Moving to another row while held cancels the old click. */
    next_controller_click(1);next_navigate(0,1,0);next_controller_click(0);
    assert(rows[5].clicks==1 && rows[6].clicks==0 && !rows[5].refs);
    next_controller_click(1);next_cancel_click();next_controller_click(0);
    assert(rows[6].clicks==0 && !rows[6].refs);
    pages.index=1;
    next_target_t targets[128];int count=0;SDL_Rect clip={0,0,640,480};
    next_targets(&root,clip,targets,&count);
    int found=0;
    for(int i=0;i<count;i++) {
        assert(targets[i].widget!=&list && targets[i].widget!=&hidden && targets[i].widget!=&offscreen);
        if(targets[i].widget==&setting) {
            assert(targets[i].area.x==360 && targets[i].area.y==200);found=1;
        }
    }
    assert(found && hover_count>200);
    return 0;
}
'''
        compile_run(support+nav+cases)

    def test_games_configuration_bounds(self):
        utils=(ROOT/"applications/games_menu/modules/utils.c").read_text()
        trim=utils[utils.index("char *TrimSpaces2("):utils.index("char *FixSpaces(")]
        parse=utils[utils.index("int ConfigParse("):utils.index("bool IsGdiOptimized(")]
        support=r'''
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
typedef uint32_t uint32;
typedef int32_t int32_t;
typedef int file_t;
#define FILEHND_INVALID -1
#define fs_open open
#define fs_close close
#define fs_read read
#define ds_printf(...) ((void)0)
enum {CONF_END,CONF_INT,CONF_STR,CONF_ULONG};
typedef struct {const char *name;int conf_type;void *pointer;size_t capacity;} isoldr_conf;
static size_t fs_total(int fd) {struct stat s;assert(!fstat(fd,&s));return s.st_size;}
static void write_config(const char *s) {
    FILE *f=fopen("preset.cfg","wb");assert(f);assert(fwrite(s,1,strlen(s),f)==strlen(s));fclose(f);
}
'''
        cases=r'''
int main(void) {
    struct {char value[12];unsigned guard;} mem={{0},0xaabbccdd};
    char title[129];int dma=-1;
    isoldr_conf cfg[]={{"memory",CONF_STR,mem.value,sizeof(mem.value)},
                      {"title",CONF_STR,title,sizeof(title)},{"dma",CONF_INT,&dma,0},
                      {NULL,CONF_END,NULL,0}};
    write_config("memory = 8ce00000\ntitle = RESIDENT EVIL CODE VERONICA\ndma = 0");
    assert(!ConfigParse(cfg,"preset.cfg"));
    assert(!strcmp(mem.value,"8ce00000") && dma==0 && mem.guard==0xaabbccdd);
    write_config("memory = 0123456789abcdef\n");
    assert(ConfigParse(cfg,"preset.cfg")==-1 && mem.guard==0xaabbccdd);
    char huge[4097];memset(huge,'A',4096);huge[4096]=0;write_config(huge);
    assert(ConfigParse(cfg,"preset.cfg")==-1);
    assert(ConfigParse(cfg,NULL)==-1);
    return 0;
}
'''
        compile_run(support+trim+parse+cases)

    def test_games_defaults_and_patch_mapping(self):
        menu=(ROOT/"applications/games_menu/modules/menu.c").read_text()
        utils=(ROOT/"applications/games_menu/modules/utils.c").read_text()
        defs=(ROOT/"applications/games_menu/modules/app_definition.h").read_text()
        structs=defs[defs.index("typedef struct PresetStructure"):defs.index("typedef struct MenuOptionStructure")]
        trim=utils[utils.index("void TrimSpaces("):utils.index("char *Trim(")]
        default=menu[menu.index("PresetStruct *GetDefaultPresetGame("):menu.index("PresetStruct *LoadPresetGame(")]
        convert=menu[menu.index("isoldr_info_t *ParsePresetToIsoldr("):menu.index("bool SavePresetGame(")]
        support=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
typedef uint32_t uint32;
typedef uint8_t uint8;
#define NAME_MAX 256
#define FIRMWARE_SIZE 7
#define ISOLDR_DEFAULT_ADDR 0x8ce00000
#define BOOT_MODE_DIRECT 0
#define BIN_TYPE_AUTO 0
#define CDDA_MODE_DISABLED 0
#define ISOLDR_DEV_SDCARD "sd"
#define ALT_BOOT_FILE "2ND_READ.BIN"
typedef struct {char title[128];} ipbin_meta_t;
typedef struct {
    unsigned use_dma,alt_read,emu_async,emu_cdda,use_irq,scr_hotkey,heap,boot_mode,fast_boot,emu_vmu,syscalls;
    unsigned patch_addr[2],patch_value[2],fs_part;
    struct {unsigned type;} exec;
    char fs_dev[8];
} isoldr_info_t;
static int dma_allowed,alt_failed;
static char boot_path[256],boot_name[32];
static const char *GetFullGamePathByIndex(int i) {(void)i;return "/sd2/games/CV/game.gdi";}
static int GetDeviceType(const char *p) {(void)p;return 1;}
static int CanUseTrueAsyncDMA(int s,int d,int i) {(void)s;(void)d;(void)i;return dma_allowed;}
static int fs_iso_mount(const char *p,const char *f) {(void)p;(void)f;return -1;}
static void fs_iso_unmount(const char *p) {(void)p;}
static isoldr_info_t *isoldr_get_info(const char *p,int flag) {
    (void)p;(void)flag;isoldr_info_t *i=calloc(1,sizeof(*i));strcpy(i->fs_dev,"sd");i->fs_part=2;i->exec.type=2;return i;
}
static int isoldr_set_boot_file(isoldr_info_t *i,const char *p,const char *f) {
    (void)i;strcpy(boot_path,p);strcpy(boot_name,f);return alt_failed?-1:0;
}
static size_t GetCDDATrackFilename(int t,const char *p,char **f) {(void)t;(void)p;(void)f;return 0;}
'''
        mocks=r'''
static void GetMD5HashISO(const char *p,SectorDataStruct *s) {(void)p;(void)s;}
static void PatchParseText(PresetStruct *p) {(void)p;}
'''
        cases=r'''
int main(void) {
    SectorDataStruct sector={0};
    memset(sector.boot_sector,'X',128);
    assert(sizeof(((PresetStruct *)0)->title)>=129);
    for(dma_allowed=0;dma_allowed<=1;++dma_allowed) {
        PresetStruct *p=GetDefaultPresetGame("/sd2/games/CV/game.gdi",&sector);assert(p);
        assert(!strcmp(p->memory,"0x8ce00000"));
        assert(p->boot_mode==BOOT_MODE_DIRECT && p->emu_async==(dma_allowed?0:8));
        p->game_index=0;p->pa[0]=0x8c123456;p->pv[0]=0xdeadbeef;p->use_dma=1;p->alt_read=1;
        isoldr_info_t *i=ParsePresetToIsoldr(0,p);assert(i);
        assert(!strcmp(i->fs_dev,"sd") && i->fs_part==2);
        assert(i->exec.type==2 && !i->use_dma && !i->alt_read);
        assert(i->patch_addr[0]==0x8c123456 && i->patch_value[0]==0xdeadbeef);
        free(i);p->alt_boot=1;i=ParsePresetToIsoldr(0,p);assert(i);
        assert(!strcmp(boot_path,"/sd2/games/CV/game.gdi") && !strcmp(boot_name,ALT_BOOT_FILE));
        free(i);alt_failed=1;assert(!ParsePresetToIsoldr(0,p));alt_failed=0;
        free(p);
    }
    assert(!GetDefaultPresetGame("/missing.gdi",NULL));
    return 0;
}
'''
        compile_run(support+structs+trim+mocks+default+convert+cases,["-fsanitize=address"])

    def test_controller_events_do_not_disable_pointer_or_double_click(self):
        ui=(ROOT/"applications/iso_loader/modules/next_ui.h").read_text()
        code=ui[ui.index("static void next_input("):ui.index("void isoLoader_Open(")]
        support=r'''
#include <assert.h>
#include <stddef.h>
enum {SDL_NOEVENT,SDL_KEYDOWN,SDL_KEYUP,SDL_JOYBUTTONDOWN,SDL_JOYBUTTONUP,
      SDL_JOYHATMOTION,SDL_JOYAXISMOTION,SDL_MOUSEBUTTONDOWN,SDL_MOUSEBUTTONUP,
      SDLK_UNKNOWN,SDLK_F1,SDLK_PRINT,SDLK_ESCAPE,SDLK_BACKSPACE,SDLK_RETURN,
      SDLK_KP_ENTER,SDLK_SPACE,SDLK_LEFT,SDLK_RIGHT,SDLK_UP,SDLK_DOWN,
      SDL_DC_A,SDL_DC_B,SDL_DC_X,SDL_DC_Y,SDL_DC_START};
#define KMOD_CTRL 1
#define KMOD_ALT 2
#define SDL_HAT_UP 1
#define SDL_HAT_RIGHT 2
#define SDL_HAT_DOWN 4
#define SDL_HAT_LEFT 8
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_RIGHT 3
#define EVENT_ACTION_UPDATE 1
#define APP_STATE_OPENED 1
#define WIDGET_HIDDEN 1
typedef struct {
 int type;
 struct {struct {int sym,mod;} keysym;} key;
 struct {int button;} button,jbutton;
 struct {int value,hat;} jhat;
} SDL_Event;
static struct {int state;} app={APP_STATE_OPENED};
static struct {__typeof__(app)*app;void *message,*pages,*games;int controller_mouse_event;} self={.app=&app};
static int modal,nav,region,lastdy,forwarded,launches,dismissed,back,presses,clicks;
static void next_controller_click(int pressed) {if(pressed) ++presses; else ++clicks;}
static void next_cancel_click(void) {}
static int ConsoleIsVisible(void) {return 0;}
static void *GUI_GetScreen(void) {return NULL;}
static void *GUI_ScreenGetFocusWidget(void *s) {(void)s;return NULL;}
static int GUI_WidgetGetFlags(void *w) {(void)w;return modal?0:WIDGET_HIDDEN;}
static int GUI_CardStackGetIndex(void *w) {(void)w;return 0;}
static void GUI_ScreenEvent(void *s,SDL_Event *e,int x,int y) {
 (void)s;(void)x;(void)y;
 assert(e->type!=SDL_JOYBUTTONDOWN && e->type!=SDL_JOYBUTTONUP && e->type!=SDL_JOYHATMOTION);
 ++forwarded;
}
static void isoLoader_Dismiss(void *w) {(void)w;modal=0;++dismissed;}
static void isoLoader_ShowGames(void *w) {(void)w;}
static void isoLoader_Up(void *w) {(void)w;++back;}
static void isoLoader_Run(void *w) {(void)w;++launches;}
static void next_navigate(int dx,int dy,int r) {(void)dx;lastdy=dy;region=r;++nav;}
'''
        cases=r'''
static void send(SDL_Event *e) {next_input(NULL,e,EVENT_ACTION_UPDATE);assert(e->type==SDL_NOEVENT);}
int main(void) {
 SDL_Event e={0};e.type=SDL_JOYHATMOTION;e.jhat.value=SDL_HAT_DOWN;send(&e);
 assert(nav==1 && lastdy==1 && forwarded==0);
 e.type=SDL_JOYHATMOTION;e.jhat.value=0;send(&e);assert(nav==1);
 for(int b=SDL_DC_X;b<=SDL_DC_Y;b++) {
  e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=b;send(&e);
  assert(region==(b==SDL_DC_X?1:2));
  e.type=SDL_JOYBUTTONUP;send(&e);
 }
 assert(!forwarded);
 e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=SDL_DC_A;send(&e);
 assert(!forwarded && !launches);
 e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;send(&e);
 assert(presses==1 && !clicks && !forwarded);
 e.type=SDL_JOYBUTTONUP;e.jbutton.button=SDL_DC_A;send(&e);
 e.type=SDL_MOUSEBUTTONUP;send(&e);assert(clicks==1 && !forwarded);
 /* A works on a core with no emulated mouse events, too. */
 e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=SDL_DC_A;send(&e);
 e.type=SDL_JOYBUTTONUP;send(&e);assert(clicks==2);
 /* A later physical mouse still reaches the GUI. */
 e.type=SDL_JOYHATMOTION;e.jhat.value=0;send(&e);
 e.type=SDL_MOUSEBUTTONDOWN;send(&e);e.type=SDL_MOUSEBUTTONUP;send(&e);
 assert(forwarded==2);
 e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=SDL_DC_START;send(&e);assert(launches==1);
 modal=1;e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_LEFT;send(&e);assert(!dismissed);
 e.type=SDL_MOUSEBUTTONUP;send(&e);assert(dismissed==1 && forwarded==2);
 modal=1;e.type=SDL_JOYBUTTONDOWN;e.jbutton.button=SDL_DC_A;send(&e);
 e.type=SDL_MOUSEBUTTONDOWN;send(&e);assert(dismissed==1);
 e.type=SDL_JOYBUTTONUP;send(&e);e.type=SDL_MOUSEBUTTONUP;send(&e);
 assert(dismissed==2 && clicks==2 && forwarded==2);
 return 0;
}
'''
        compile_run(support+code+cases)

if __name__=="__main__":
    unittest.main()
