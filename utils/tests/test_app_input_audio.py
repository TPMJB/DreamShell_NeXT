"""Regressions for mixed ISO controls and selection-dependent Games audio."""
import unittest
from test_iso_loader_next import ROOT, compile_run


class AppInputAudioTests(unittest.TestCase):
    def test_iso_open_mouse_and_controller_dispatch(self):
        ui = (ROOT/'applications/iso_loader/modules/next_ui.h').read_text()
        code = ui[ui.index('static void next_input('):ui.index('void isoLoader_Close(')]
        support = r'''
#include <assert.h>
#include <stddef.h>
enum {SDL_NOEVENT,SDL_MOUSEMOTION,SDL_MOUSEBUTTONDOWN,SDL_MOUSEBUTTONUP,SDL_KEYDOWN,SDL_KEYUP,
      SDL_JOYBUTTONDOWN,SDL_JOYBUTTONUP,SDL_JOYHATMOTION,SDL_JOYAXISMOTION};
enum {SDLK_UNKNOWN,SDLK_F1,SDLK_PRINT,SDLK_ESCAPE,SDLK_BACKSPACE,SDLK_RETURN,SDLK_KP_ENTER,
      SDLK_LEFT,SDLK_RIGHT,SDLK_UP,SDLK_DOWN,SDLK_SPACE};
enum {SDL_DC_A,SDL_DC_X,SDL_DC_Y,SDL_DC_START};
enum {SDL_BUTTON_LEFT=1,SDL_BUTTON_RIGHT=3,KMOD_CTRL=4,KMOD_ALT=8,
      SDL_HAT_LEFT=1,SDL_HAT_RIGHT=2,SDL_HAT_UP=4,SDL_HAT_DOWN=8,
      APP_STATE_OPENED=1,EVENT_ACTION_UPDATE=1,WIDGET_HIDDEN=1,SDL_TRUE=1};
typedef struct {int state; } App_t;
typedef struct {int type;struct {int button,x,y;} button;struct {int button;} jbutton;
 struct {struct {int sym,mod;} keysym;} key;struct {int hat,value;} jhat;
} SDL_Event;
static App_t app={APP_STATE_OPENED};
static struct {App_t *app;int controller_mouse_event;void *message,*pages,*games,*input_event;} self;
static int emulate,active,default_input=1,motions,clicks,downs,navigations,pending,applied,physical;
static int ConsoleIsVisible(void) {return 0;}
#define GUI_GetScreen() NULL
#define GUI_ScreenGetFocusWidget(s) NULL
#define GUI_WidgetGetFlags(w) WIDGET_HIDDEN
#define GUI_CardStackGetIndex(w) 0
#define GUI_ScreenSetJoySelectState(s,v) ((void)0)
static void GUI_ScreenEvent(void *s,SDL_Event *e,int x,int y) {
 (void)s;(void)x;(void)y;
 if(e->type==SDL_MOUSEMOTION) {assert(emulate);++motions;}
 if(e->type==SDL_MOUSEBUTTONUP) ++physical;
 if(e->type==SDL_NOEVENT) {applied+=pending;pending=0;}
}
static void GUI_DisableInput(void) {default_input=0;emulate=0;}
static void GUI_EnableInput(void) {default_input=1;emulate=1;}
static void SDL_DC_EmulateMouse(int v) {emulate=v;}
static void SetEventActive(void *e,int v) {(void)e;active=v;}
static void next_controller_click(int pressed) {if(pressed)++downs;else ++clicks;}
static void next_cancel_click(void) {}
static void next_navigate(int dx,int dy,int r) {(void)dx;(void)dy;(void)r;++navigations;}
static void isoLoader_Dismiss(void *w) {(void)w;}
static void isoLoader_ShowGames(void *w) {(void)w;}
static void isoLoader_Up(void *w) {(void)w;pending=1;}
static void isoLoader_Run(void *w) {(void)w;}
static void next_refresh(void) {}
static void next_status(const char *s) {(void)s;}
'''
        cases = r'''
static void send(SDL_Event e) {next_input(NULL,&e,EVENT_ACTION_UPDATE);assert(e.type==SDL_NOEVENT);}
int main(void) {
 self.app=&app;self.input_event=&app;isoLoader_Open(&app);
 assert(emulate && active && !default_input);
 send((SDL_Event){.type=SDL_MOUSEMOTION});
 send((SDL_Event){.type=SDL_JOYHATMOTION,.jhat={0,SDL_HAT_DOWN}});
 send((SDL_Event){.type=SDL_JOYBUTTONDOWN,.jbutton={SDL_DC_A}});
 send((SDL_Event){.type=SDL_MOUSEBUTTONDOWN,.button={SDL_BUTTON_LEFT}});
 send((SDL_Event){.type=SDL_JOYBUTTONUP,.jbutton={SDL_DC_A}});
 send((SDL_Event){.type=SDL_MOUSEBUTTONUP,.button={SDL_BUTTON_LEFT}});
 assert(downs==1 && clicks==1 && physical==0 && navigations==1);
 send((SDL_Event){.type=SDL_MOUSEMOTION});
 send((SDL_Event){.type=SDL_MOUSEBUTTONDOWN,.button={SDL_BUTTON_LEFT}});
 send((SDL_Event){.type=SDL_MOUSEBUTTONUP,.button={SDL_BUTTON_LEFT}});
 assert(motions==2 && physical==1 && emulate);
 send((SDL_Event){.type=SDL_MOUSEBUTTONUP,.button={SDL_BUTTON_RIGHT}});
 assert(!pending && applied==1); /* B/up also applies its directory scan */
 self.input_event=NULL;isoLoader_Open(&app);assert(default_input && emulate);
 return 0;
}
'''
        compile_run(support+code+cases)

    def test_preview_tracks_from_descriptor(self):
        code = (ROOT/'applications/games_menu/modules/next_audio.h').read_text()
        support = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
static int FileSize(const char *p) {struct stat st;return stat(p,&st)?-1:st.st_size;}
static void write_file(const char *p,const char *s) {FILE *f=fopen(p,"wb");assert(f);fputs(s,f);fclose(f);}
'''
        cases = r'''
int main(void) {
 char path[256];mkdir("game",0700);
 write_file("game/track02.raw","warning");
 write_file("game/music short.raw","audio");
 write_file("game/data.raw","data");
 write_file("game/disc.gdi","5\n1 0 4 2352 track01.bin 0\n2 2000 0 2352 track02.raw 0\n3 45000 4 2352 track03.bin 0\n4 60000 4 2352 data.raw 0\n5 70000 0 2352 \"music short.raw\" 0\n");
 assert(NextFindPreviewTrack("game/disc.gdi",path,sizeof(path)));
 assert(!strcmp(path,"game/music short.raw")); /* short, named track 5 */
 remove("game/music short.raw");write_file("game/music short.wav","audio");
 assert(NextFindPreviewTrack("game/disc.gdi",path,sizeof(path)));
 assert(!strcmp(path,"game/music short.wav"));
 remove("game/music short.wav");
 assert(!NextFindPreviewTrack("game/disc.gdi",path,sizeof(path)) && !*path);
 /* The supplied Evolution 2 descriptor has only warning audio, so is silent. */
 write_file("game/disc.gdi","3\n1 0 4 2352 track01.bin 0\n2 2293 0 2352 track02.raw 0\n3 45000 4 2352 track03.bin 0\n");
 assert(!NextFindPreviewTrack("game/disc.gdi",path,sizeof(path)));
 assert(!NextFindPreviewTrack("game/missing.gdi",path,sizeof(path)));
 write_file("game/disc.gdi","99999999999999999\n");
 assert(!NextFindPreviewTrack("game/disc.gdi",path,sizeof(path)));
 write_file("game/track99.wav","audio");
 assert(NextFindPreviewTrack("game/disc.iso",path,sizeof(path)));
 assert(!strcmp(path,"game/track99.wav"));
 assert(!NextFindPreviewTrack("game/disc.iso",path,8) && !*path);
 remove("game/track99.wav");
 assert(!NextFindPreviewTrack("game/disc.iso",path,sizeof(path))); /* finite missing-track search */
 return 0;
}
'''
        compile_run(support+code+cases, ['-fsanitize=address'])

    def test_preview_worker_cancel_and_cache_retry(self):
        menu = (ROOT/'applications/games_menu/modules/menu.c').read_text()
        code = menu[menu.index('void StopCDDA()'):menu.index('ImageDimensionStruct *GetImageDimension(')]
        support = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {APP_DEVICE_SD=1,APP_DEVICE_IDE,APP_DEVICE_CD,CCGE_NOT_CDDA=3};
static struct {bool cdda_game_changed;void *play_cdda_thread;int games_array_count;
 struct {int device,is_cdda;} games_array[3];
 void *ffplay;int (*ffplay_is_playing)(void);void (*ffplay_shutdown)(void);
} menu_data;
static int joined,stopped,created,last_index,playing;
static void *PlayCDDAThread(void *p) {return p;}
static void thd_join(void *t,void *r) {
 (void)r;assert(t && menu_data.cdda_game_changed);++joined;playing=1; /* startup already in flight */
}
static void StopCDDATrack(void) {assert(menu_data.cdda_game_changed);++stopped;playing=0;}
static void *thd_create(int flags,void *(*fn)(void *),void *p) {
 assert(!flags && fn==PlayCDDAThread && !menu_data.cdda_game_changed && !playing);
 ++created;last_index=(intptr_t)p;return &created;
}
'''
        cases = r'''
int main(void) {
 menu_data.games_array_count=3;
 menu_data.games_array[0].device=APP_DEVICE_SD;
 menu_data.games_array[1].device=APP_DEVICE_IDE;
 menu_data.games_array[2].device=APP_DEVICE_CD;
 menu_data.games_array[0].is_cdda=menu_data.games_array[1].is_cdda=CCGE_NOT_CDDA;
 PlayCDDA(0);assert(created==1 && last_index==0);
 PlayCDDA(1);assert(created==2 && last_index==1 && joined==1 && !playing);
 PlayCDDA(0);assert(created==3 && last_index==0 && joined==2);
 StopCDDA();assert(!menu_data.play_cdda_thread && !playing && !menu_data.cdda_game_changed);
 PlayCDDA(-1);PlayCDDA(3);PlayCDDA(2);assert(created==3);
 return 0;
}
'''
        compile_run(support+code+cases, ['-Wno-int-to-pointer-cast'])
