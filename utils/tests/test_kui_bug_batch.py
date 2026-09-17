"""Host regressions for mixed input, dialog choice and app memory lifetime."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SDL = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _SDL_config_h
#define STDC_HEADERS 1
#define HAVE_STDINT_H 1
#define SDL_HAS_64BIT_TYPE 1
#include <SDL.h>
#include <SDL_dreamcast.h>
'''


def run_source(source, cpp=False, extra=()):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        src = path / ('test.cc' if cpp else 'test.c')
        src.write_text(source)
        subprocess.run(['g++' if cpp else 'gcc', '-std=c++17' if cpp else '-std=gnu11',
                        '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                        '-I'+str(ROOT), '-I'+str(ROOT/'include/SDL'),
                        str(src), *extra, '-o', str(path/'test')], cwd=ROOT, check=True)
        subprocess.run([str(path/'test')], check=True,
                       env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))


class KUIBugBatchTests(unittest.TestCase):
    def test_mie_aux_stick_navigation_preserves_button_and_keyboard_input(self):
        source = (ROOT/'applications/mie_jvs/modules/module.c').read_text()
        code = source[source.index('static uint32_t poll_aux_buttons('):source.index('static void poll_input(')]
        run_source(r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
enum {CONT_A=1,CONT_B=2,CONT_X=4,CONT_Y=8,CONT_START=16,
 CONT_DPAD_LEFT=32,CONT_DPAD_RIGHT=64,CONT_DPAD_UP=128,CONT_DPAD_DOWN=256};
enum {KBD_KEY_A,KBD_KEY_B,KBD_KEY_X,KBD_KEY_Y,KBD_KEY_ESCAPE,
 KBD_KEY_LEFT,KBD_KEY_RIGHT,KBD_KEY_UP,KBD_KEY_DOWN};
typedef struct {uint32_t buttons;int joyx,joyy;} cont_state_t;
typedef struct {struct {int is_down;} key_states[9];} kbd_state_t;
static cont_state_t controller;
static kbd_state_t keyboard;
#define MAPLE_FUNC_CONTROLLER (&controller)
#define MAPLE_FUNC_KEYBOARD (&keyboard)
#define MAPLE_FOREACH_BEGIN(kind,type,name) {type *name=(kind);
#define MAPLE_FOREACH_END() }
''' + code + r'''
int main(void) {
 assert(!poll_aux_buttons());
 controller.joyx=-100;assert(poll_aux_buttons()==CONT_DPAD_LEFT);
 controller.joyx=100;assert(poll_aux_buttons()==CONT_DPAD_RIGHT);
 controller.joyx=0;controller.joyy=-100;assert(poll_aux_buttons()==CONT_DPAD_UP);
 controller.joyy=100;assert(poll_aux_buttons()==CONT_DPAD_DOWN);
 controller.joyx=64;controller.joyy=-64;assert(!poll_aux_buttons());
 controller.buttons=CONT_X;controller.joyx=100;
 keyboard.key_states[KBD_KEY_B].is_down=1;
 assert(poll_aux_buttons()==(CONT_X|CONT_B|CONT_DPAD_RIGHT));return 0;
}
''')

    def test_speedtest_checks_all_chunks_and_reports_first_corrupt_byte(self):
        source = (ROOT/'applications/speedtest/modules/module.c').read_text()
        code = source[source.index('static int file_pass('):source.index('static int raw_pass(')]
        run_source(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
typedef int file_t;
#define FILEHND_INVALID (-1)
#define TEST_BUFFER (256*1024)
static unsigned char disk[8*1048576];
static size_t cursor,length;
static long corrupt=-1;
static int removed;
static char status[160];
static int FileExists(const char *p) {(void)p;return 0;}
static int DirExists(const char *p) {return !strcmp(p,"/sd");}
static file_t fs_open(const char *p,int mode) {
 (void)p;cursor=0;if(mode&O_WRONLY)length=0;return 1;
}
static int fs_close(file_t f) {(void)f;return 0;}
static size_t fs_total(file_t f) {(void)f;return length;}
static int fs_unlink(const char *p) {(void)p;removed++;return 0;}
static ssize_t fs_write(file_t f,const void *p,size_t n) {
 (void)f;if(n>8191)n=8191;assert(cursor+n<=sizeof(disk));
 memcpy(disk+cursor,p,n);cursor+=n;length=cursor;return n;
}
static ssize_t fs_read(file_t f,void *p,size_t n) {
 (void)f;if(n>8191)n=8191;if(n>length-cursor)n=length-cursor;
 memcpy(p,disk+cursor,n);
 if(corrupt>=0 && (size_t)corrupt>=cursor && (size_t)corrupt<cursor+n)
  ((uint8_t*)p)[corrupt-cursor]^=1;
 cursor+=n;return n;
}
#include "applications/maintenance_io.h"
#include "applications/maintenance_model.h"
static const unsigned sizes[]={2,8,32};
static struct {int mode,size;char file[MA_PATH],folder[MA_PATH],report[12288],mismatch[96];} self;
static void ma_status(const char *s,int error) {(void)error;snprintf(status,sizeof(status),"%s",s);}
static void ma_progress(size_t done,size_t total) {assert(done<=total);}
static int ma_pump(void) {return 1;}
static uint64_t timer_ns_gettime64(void) {static uint64_t now;return ++now;}
''' + code + r'''
int main(void) {
 uint8_t *buffer=malloc(TEST_BUFFER);assert(buffer);
 uint64_t written=0,readbytes=0,wns=0,rns=0;char leftover[MA_PATH]="";
 strcpy(self.folder,"/sd");self.size=1;
 assert(file_pass(buffer,&written,&readbytes,&wns,&rns,leftover)==0);
 assert(written==sizeof(disk) && readbytes==sizeof(disk) && removed==1 && !*leftover);
 corrupt=TEST_BUFFER+13;written=readbytes=0;
 assert(file_pass(buffer,&written,&readbytes,&wns,&rns,leftover)<0);
 assert(strstr(status,"DATA MISMATCH") && strstr(self.mismatch,"Byte 262157:"));
 assert(strstr(self.report,self.mismatch) && readbytes==TEST_BUFFER && removed==2);
 free(buffer);return 0;
}
''')

    def test_stick_pointer_native_focus_and_single_click(self):
        run_source(SDL + r'''
typedef int Event_t;
static int pointer, normal_input, active, mouse_down, mouse_up, mouse_move, native, last_sym;
static int ConsoleIsVisible(void) { return 0; }
static void *GUI_GetScreen(void) { return NULL; }
static void GUI_DisableInput(void) { normal_input=pointer=0; }
static void GUI_EnableInput(void) { normal_input=pointer=1; }
void SDL_DC_EmulateMouse(SDL_bool v) { pointer=v; }
static void GUI_ScreenSetJoySelectState(void *s,int state) { (void)s;(void)state; }
static void SetEventActive(Event_t *e,int v) { (void)e;active=v; }
static void LockVideo(void) {}
static void UnlockVideo(void) {}
static void RemoveEvent(Event_t *e) { (void)e; }
static void GUI_ScreenEvent(void *s,SDL_Event *e,int x,int y) {
 (void)s;(void)x;(void)y;
 if(e->type==SDL_MOUSEBUTTONDOWN) mouse_down++;
 if(e->type==SDL_MOUSEBUTTONUP) mouse_up++;
 if(e->type==SDL_MOUSEMOTION) mouse_move++;
 if(e->type==SDL_KEYDOWN) last_sym=e->key.keysym.sym;
}
#include "applications/utility_ui.h"
static void send(SDL_Event e) {
 assert(!utility_global_input(&e));
 int key=utility_key(&e);
 if(key==UI_OK) ++native;
 else utility_forward(&e);
}
static void click(void) {
 send((SDL_Event){.jbutton={.type=SDL_JOYBUTTONDOWN,.button=SDL_DC_A}});
 send((SDL_Event){.button={.type=SDL_MOUSEBUTTONDOWN,.button=SDL_BUTTON_LEFT}});
 send((SDL_Event){.jbutton={.type=SDL_JOYBUTTONUP,.button=SDL_DC_A}});
 send((SDL_Event){.button={.type=SDL_MOUSEBUTTONUP,.button=SDL_BUTTON_LEFT}});
}
int main(void) {
 Event_t event=1,*p=&event;utility_open(p);
 assert(pointer && active && !normal_input);
 click();assert(native==1 && !mouse_down && !mouse_up);
 /* The Dreamcast SDL axes range from -128 to 127, not desktop's 32767. */
 send((SDL_Event){.jaxis={.type=SDL_JOYAXISMOTION,.axis=0,.value=80}});
 click();assert(native==1 && mouse_down==1 && mouse_up==1);
 send((SDL_Event){.jhat={.type=SDL_JOYHATMOTION,.hat=0,.value=SDL_HAT_DOWN}});
 /* Idle motion from SDL must not take focus away from D-pad selection. */
 send((SDL_Event){.motion={.type=SDL_MOUSEMOTION,.xrel=0,.yrel=0}});
 assert(!mouse_move);
 click();assert(native==2 && mouse_down==1 && mouse_up==1);
 send((SDL_Event){.jaxis={.type=SDL_JOYAXISMOTION,.axis=2,.value=127}});
 click();assert(native==3); /* trigger is not cursor movement */
 send((SDL_Event){.motion={.type=SDL_MOUSEMOTION,.xrel=2}});
 send((SDL_Event){.button={.type=SDL_MOUSEBUTTONDOWN,.button=SDL_BUTTON_LEFT}});
 send((SDL_Event){.button={.type=SDL_MOUSEBUTTONUP,.button=SDL_BUTTON_LEFT}});
 assert(mouse_down==2 && mouse_up==2);
 SDL_Event e={0};utility_dialog(&e,UI_LEFT);assert(last_sym==SDLK_LEFT);
 utility_dialog(&e,UI_RIGHT);assert(last_sym==SDLK_RIGHT);
 utility_dialog(&e,UI_OK);assert(last_sym==SDLK_RETURN);
 utility_dialog(&e,UI_BACK);assert(last_sym==SDLK_ESCAPE);
 utility_close(p);assert(!active && normal_input);
 utility_remove(&p);assert(!p);
 return 0;
}
''')

    def test_dialog_choice_default_cancel_and_single_button_alert(self):
        source = (ROOT/'lib/SDL_gui/Dialog.cc').read_text()
        focus = source[source.index('void GUI_Dialog::FocusButton('):source.index('void GUI_Dialog::Show(')]
        event = source[source.index('int GUI_Dialog::Event('):source.index('void GUI_Dialog::Hide(')]
        support = SDL + r'''
enum {WIDGET_HIDDEN=1,WIDGET_DISABLED=2,WIDGET_INSIDE=4,WIDGET_PRESSED=8,WIDGET_HAS_FOCUS=16};
struct GUI_Drawable { static int Event(const SDL_Event *,int,int) {return 0;} };
struct GUI_Button {
 int flags=0,events=0;
 int GetFlags() {return flags;}
 void ClearFlags(int f) {flags&=~f;}
 void SetFlags(int f) {flags|=f;}
 int Event(const SDL_Event *e,int,int) {if(e->type==SDL_MOUSEBUTTONUP || e->type==SDL_KEYDOWN) {events++;return 1;}return 0;}
};
struct GUI_Dialog : GUI_Drawable {
 enum {MODE_CONFIRM,MODE_ALERT,MODE_INFO,MODE_PROGRESS,MODE_PROMPT};
 GUI_Button yes,no,body,entry,*input=&entry,*confirm_button=&yes,*cancel_button=&no,*widgets[1]={&body};
 int flags=0,focused_button=1,mode=MODE_CONFIRM,accepted=0,cancelled=0;
 SDL_Rect area={0,0,300,200};
 void ButtonClick(GUI_Button *b) {if(b==confirm_button)accepted++;else cancelled++;}
 void FocusButton(int);
 int Event(const SDL_Event *,int,int);
 void key(SDLKey key) {SDL_Event e={};e.type=SDL_KEYDOWN;e.key.keysym.sym=key;Event(&e,0,0);}
};
'''
        run_source(support + focus + event + r'''
int main() {
 GUI_Dialog d;d.FocusButton(1);
 assert(d.no.flags&WIDGET_INSIDE);d.key(SDLK_RETURN);assert(d.cancelled==1 && !d.accepted);
 d.key(SDLK_LEFT);assert(d.yes.flags&WIDGET_INSIDE);assert(!(d.no.flags&WIDGET_INSIDE));
 d.key(SDLK_RETURN);assert(d.accepted==1);
 d.key(SDLK_RIGHT);d.key(SDLK_RETURN);assert(d.cancelled==2);
 d.key(SDLK_TAB);d.key(SDLK_RETURN);assert(d.accepted==2);
 d.key(SDLK_ESCAPE);assert(d.cancelled==3);
 d.mode=GUI_Dialog::MODE_ALERT;d.no.flags=WIDGET_DISABLED;d.FocusButton(1);
 assert(d.focused_button==0);d.key(SDLK_RIGHT);d.key(SDLK_RETURN);assert(d.accepted==3);
 d.key(SDLK_ESCAPE);assert(d.accepted==4);
 SDL_Event mouse={};mouse.type=SDL_MOUSEBUTTONUP;d.Event(&mouse,0,0);assert(d.body.events==1);
 d.mode=GUI_Dialog::MODE_PROMPT;d.entry.flags=WIDGET_HAS_FOCUS;
 d.key(SDLK_SPACE);d.key(SDLK_RETURN);d.key(SDLK_ESCAPE);d.key(SDLK_LEFT);
 assert(d.accepted==4 && d.cancelled==3 && d.body.events==5);
 d.flags=WIDGET_HIDDEN;d.key(SDLK_RETURN);assert(d.accepted==4);
 return 0;
}
''', cpp=True)

    def test_file_picker_uses_count_after_directory_read(self):
        source = (ROOT/'lib/SDL_gui/FileManager.cc').read_text()
        code = source[source.index('void GUI_FileManager::LoadScanEntries()'):source.index('void GUI_FileManager::ScanApply(')]
        run_source(r'''
#include <assert.h>
#include <string.h>
#define NAME_MAX 256
struct dirent_t { int id; } entries[3]={{1},{2},{3}};
static dirent_t *ReadDirEntries(const char *path,int *count) {
 assert(!strcmp(path,"/sd"));*count=3;return entries;
}
struct GUI_FileManager {
 char cur_path[NAME_MAX]="/sd";int count=0;dirent_t *data=nullptr;
 void LoadScanEntries();
 void SetPendingScan(dirent_t *p,int n) {data=p;count=n;}
};
''' + code + '''
int main() {GUI_FileManager fm;fm.LoadScanEntries();assert(fm.count==3 && fm.data==entries);}
''', cpp=True)

    def test_classic_cannot_ship_or_become_home(self):
        sys.path.insert(0, str(ROOT/'utils'))
        from package_release import release_app_xml, validate_app_set
        xmls = release_app_xml()
        self.assertNotIn('main', {p.parent.name for p in xmls})
        names = {f'DS/apps/{p.parent.name}/app.xml' for p in xmls}
        self.assertEqual(validate_app_set(names), len(xmls))
        with self.assertRaisesRegex(ValueError, 'Classic'):
            validate_app_set(names | {'DS/apps/main/app.xml'})
        code = (ROOT/'src/settings.c').read_text()
        code = code[code.index('Settings_t *GetSettings()'):code.index('void SetSettings(')]
        run_source(r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
typedef struct {char startup_app[64],main_app[64];} Settings_t;
static Settings_t current_set;
static int loaded=1;
#define DS_DEFAULT_APP_NAME "Launch App"
static void LoadSettings(void) {}
static void ResetSettings(void) {}
''' + code + r'''
int main(void) {
 strcpy(current_set.startup_app,"Main");strcpy(current_set.main_app,"main");
 assert(!strcmp(GetSettings()->startup_app,"Launch App"));
 assert(!strcmp(GetSettings()->main_app,"Launch App"));
 strcpy(current_set.startup_app,"ISO Loader");strcpy(current_set.main_app,"Games Menu");
 assert(!strcmp(GetSettings()->startup_app,"ISO Loader"));
 assert(!strcmp(GetSettings()->main_app,"Games Menu"));return 0;
}
''')

    def test_lua_coroutines_survive_non_lifo_close_and_parent_stack_clear(self):
        source = (ROOT/'src/lua/lua.c').read_text()
        code = source[source.index('lua_State *NewLuaThread()'):]
        core = 'lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib'.split()
        run_source(r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
static lua_State *DSLua;
static void log_message(const char *s) {fputs(s,stderr);}
void (*luaB_fputs)(const char *)=log_message;
''' + code + r'''
int main(void) {
 DSLua=luaL_newstate();assert(DSLua);
 lua_pushliteral(DSLua,"parent sentinel");
 lua_State *a=NewLuaThread(),*b=NewLuaThread();assert(a && b);
 assert(lua_gettop(DSLua)==1);
 ReleaseLuaThread(a);assert(lua_gettop(DSLua)==1);
 lua_settop(DSLua,0);lua_gc(DSLua,LUA_GCCOLLECT,0);
 assert(luaL_loadstring(b,"return 40+2")==0 && lua_pcall(b,0,1,0)==0);
 assert(lua_tointeger(b,-1)==42);ReleaseLuaThread(b);
 lua_gc(DSLua,LUA_GCCOLLECT,0);
 for(int i=0;i<100;i++) {
  a=NewLuaThread();b=NewLuaThread();ReleaseLuaThread(a);
  lua_gc(DSLua,LUA_GCCOLLECT,0);lua_pushliteral(b,"still alive");
  assert(!strcmp(lua_tostring(b,-1),"still alive"));ReleaseLuaThread(b);
 }
 lua_close(DSLua);DSLua=NULL;assert(!NewLuaThread());return 0;
}
        ''', extra=['-I'+str(ROOT/'lib/lua/src'), '-Wno-unused-parameter',
             '-Wno-implicit-fallthrough', '-Wno-misleading-indentation', '-Wno-address',
             *[str(ROOT/f'lib/lua/src/{n}.c') for n in core], '-lm'])
