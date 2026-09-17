from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as E

ROOT=Path(__file__).resolve().parents[2]

class VMUManagerTests(unittest.TestCase):
    def test_pointer_dispatch_preserves_native_actions_and_cancel(self):
        from utils.tests.test_kui_bug_batch import SDL, run_source
        source=(ROOT/'applications/vmu_manager/modules/ui.h').read_text()
        handler=source[source.index('static void ui_input('):source.index('static void ui_init(')]
        opened=source[source.index('void VMU_Manager_Open('):source.index('void VMU_Manager_Close(')]
        commands=source[source.index('enum { UI_UP'):source.index('typedef struct')]
        run_source(SDL + commands + r'''
enum {APP_STATE_OPENED=1,EVENT_ACTION_UPDATE=1,CMD_OK=0,CMD_ERROR=-1,CMD_NO_ARG=-2};
typedef struct {int state;} App_t;
static struct {App_t *m_App;void *pages;} self;
static struct {void *input,*worker;int repeat_dir,modal,armed,answer,allow_browse,bulk,cancel,busy;uint64_t repeat_at,poll_at;} ui;
static int pointer_enabled,native_jobs,last_job,mouse_clicks;
void SDL_DC_EmulateMouse(SDL_bool v) {pointer_enabled=v;}
#include "applications/pointer_input.h"
static int ConsoleIsVisible(void) {return 0;}
#define GUI_GetScreen() NULL
#define GUI_ScreenGetFocusWidget(s) NULL
#define GUI_CardStackGetIndex(p) 1
#define GUI_ScreenSetJoySelectState(s,v) ((void)0)
#define SetEventActive(e,v) ((void)0)
static void GUI_DisableInput(void) {pointer_enabled=0;}
static void GUI_ScreenEvent(void *s,SDL_Event *e,int x,int y) {
 (void)s;(void)x;(void)y;if(e->type==SDL_MOUSEBUTTONUP) mouse_clicks++;
}
static void VMU_Manager_Exit(void *w) {(void)w;}
static uint64_t timer_ms_gettime64(void) {return 1;}
static void ui_queue(int job,void *w,void *e) {(void)w;(void)e;last_job=job;native_jobs++;}
static void ui_status(const char *s) {(void)s;}
static void ui_refresh(void) {}
''' + handler + opened + r'''
static void send(SDL_Event e) {ui_input(NULL,&e,EVENT_ACTION_UPDATE);assert(e.type==SDL_NOEVENT);}
static void click(int button,int mouse) {
 send((SDL_Event){.jbutton={.type=SDL_JOYBUTTONDOWN,.button=button}});
 send((SDL_Event){.button={.type=SDL_MOUSEBUTTONDOWN,.button=mouse}});
 send((SDL_Event){.jbutton={.type=SDL_JOYBUTTONUP,.button=button}});
 send((SDL_Event){.button={.type=SDL_MOUSEBUTTONUP,.button=mouse}});
}
int main(void) {
 App_t app={APP_STATE_OPENED};self.m_App=&app;ui.input=ui.worker=&app;
 VMU_Manager_Open(&app);assert(pointer_enabled);
 click(SDL_DC_A,SDL_BUTTON_LEFT);assert(native_jobs==1 && last_job==UI_ACTIVATE && !mouse_clicks);
 send((SDL_Event){.jhat={.type=SDL_JOYHATMOTION,.value=SDL_HAT_DOWN}});
 assert(last_job==UI_DOWN && ui.repeat_dir==UI_DOWN);
 send((SDL_Event){.jaxis={.type=SDL_JOYAXISMOTION,.axis=0,.value=100}});
 assert(native_jobs==2 && !ui.repeat_dir);
 click(SDL_DC_A,SDL_BUTTON_LEFT);assert(native_jobs==2 && mouse_clicks==1);
 ui.modal=ui.armed=1;ui.answer=99;
 click(SDL_DC_B,SDL_BUTTON_RIGHT);assert(ui.answer==CMD_ERROR && mouse_clicks==1);
 ui.modal=0;ui.bulk=1;click(SDL_DC_B,SDL_BUTTON_RIGHT);assert(ui.cancel && mouse_clicks==1);
 ui.bulk=0;send((SDL_Event){.jhat={.type=SDL_JOYHATMOTION,.value=SDL_HAT_UP}});
 send((SDL_Event){.jaxis={.type=SDL_JOYAXISMOTION,.axis=2,.value=200}});
 click(SDL_DC_A,SDL_BUTTON_LEFT);assert(native_jobs==4 && mouse_clicks==1);
 return 0;
}
''')

    def test_transfer_failure_and_confirmation_gates(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/'vmu-transfer'
            subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-fno-pie','-no-pie','utils/tests/vmu_transfer_harness.c','-o',str(exe)],cwd=ROOT,check=True)
            self.assertIn('passed',subprocess.check_output([str(exe)],text=True))

    def test_layout_exports_and_resources(self):
        app=ROOT/'applications/vmu_manager'
        tree=E.parse(app/'app.xml');root=tree.getroot()
        exports=set((app/'modules/exports.txt').read_text().splitlines())
        names=[e.get('name') for e in root.iter() if e.get('name')]
        # Widget and resource names may overlap (e.g. progressbar).
        widget_names=[e.get('name') for e in root.find('body').iter() if e.get('name')]
        self.assertEqual(len(widget_names),len(set(widget_names)))
        for e in root.iter():
            for attr,value in e.attrib.items():
                if value.startswith('export:'):self.assertIn(value[7:].split('(')[0],exports)
        self.assertEqual(root.get('version'),'2.1.0')
        for slot in ['A1','A2','B1','B2','C1','C2','D1','D2']:self.assertIn(slot,names)
        for control in ['copy-button','copy-all-button','dump-button','delete-button','format-c','modal-cancel','modal-accept']:self.assertIn(control,names)
        manage=root.find(".//panel[@name='vmu_page']")
        actions=root.find(".//panel[@name='tools_page']")
        self.assertIsNotNone(manage.find("input[@name='copy-all-button']"))
        self.assertIsNotNone(actions.find("input[@name='dump-button']"))

    def test_bulk_copy_failure_and_existing_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe=Path(tmp)/'vmu-bulk'
            subprocess.run(['gcc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-fno-pie','-no-pie','utils/tests/vmu_bulk_harness.c','-o',str(exe)],cwd=ROOT,check=True)
            self.assertIn('passed',subprocess.check_output([str(exe)],text=True))

    def test_cancel_folder_does_not_call_mkdir(self):
        # Compile the production handler against a minimal UI/storage harness.
        source=(ROOT/'applications/vmu_manager/modules/module.c').read_text()
        body=source.split('void VMU_Manager_make_folder(GUI_Widget *widget) {',1)[1].split('\nvoid VMU_Manager_clr_name',1)[0]
        stub='''#include <stdio.h>
#include <string.h>
#include <assert.h>
#define NAME_MAX 256
typedef int GUI_Widget;
static struct { GUI_Widget *pages,*filebrowser2,*folder_name; } self;
static int made;
static const char *GUI_ObjectGetName(GUI_Widget *w) {(void)w;return "confirm-no";}
static void GUI_CardStackShowIndex(GUI_Widget *w,int p) {(void)w;(void)p;}
static const char *GUI_FileManagerGetPath(GUI_Widget *w) {(void)w;return "/sd";}
static const char *GUI_TextEntryGetText(GUI_Widget *w) {(void)w;return "new_folder";}
static void GUI_FileManagerScan(GUI_Widget *w) {(void)w;}
static void ui_status(const char *s) {(void)s;}
static int fs_mkdir(const char *s) {(void)s;++made;return 0;}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c=Path(tmp)/'cancel.c';exe=Path(tmp)/'cancel'
            c.write_text(stub+'\n#include "'+str(ROOT/'applications/vmu_manager/modules/ui_logic.h')+'"\nvoid VMU_Manager_make_folder(GUI_Widget *widget) {'+body+'\nint main(void) { VMU_Manager_make_folder(0); assert(!made); return 0;}\n')
            subprocess.run(['gcc','-std=c11',str(c),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
