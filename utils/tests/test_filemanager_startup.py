"""Exercise File Manager startup with the bundled Lua and tolua runtimes."""
from pathlib import Path
import json
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as E

ROOT = Path(__file__).resolve().parents[2]

class FileManagerStartupTests(unittest.TestCase):
    def test_real_lua_rect_binding_and_missing_startup_resource(self):
        resources = list(E.parse(ROOT / 'applications/filemanager/app.xml').getroot().find('resources'))
        script = next(i for i, e in enumerate(resources) if e.tag == 'script')
        has_sdl = any(e.tag == 'module' and e.get('src', '').endswith('/luaSDL.klf')
                      for e in resources[:script])
        setup = r"""
local names={'title','filemgr-top','filemgr-bottom','modal-dialog','toolbar-panel','path-top','path-bottom'}
app={resources={},elements={}}
for _,n in ipairs(names) do app.elements[n]={data={path='/',caption={}}} end
for _,n in ipairs({'white-bg','blue-bg','body'}) do app.resources[n]={data={}} end
DS={LIST_ITEM_GUI_FONT=1,LIST_ITEM_GUI_SURFACE=2,
    listGetItemByName=function(t,n) return t[n] end, GetAppById=function() return app end}
GUI={AnyToFont=function(x) return x end,AnyToSurface=function(x) return x end,
    AnyToWidget=function(x) return x end, PanelSetBackground=function(w,s) assert(w and s) end,
    FileManagerGetPath=function(w) return w.path end, FontGetTextSize=text_size,
    ButtonGetCaption=function(w) return w.caption end,
    LabelSetText=function(w,s) assert(w); w.text=s end,
    DialogShow=function() error('startup must not display an uninitialized dialog') end}
local ok,msg=pcall(function() FileManager:Initialize() end)
collectgarbage('collect') -- Default collector must never call a NULL function.
assert(not ok and string.find(msg,'nil'), tostring(msg))
FileManager.app=nil
load_sdl_binding()
assert(FileManager:Initialize())
assert(app.elements['path-top'].data.caption.text=='> /')
assert(app.elements['path-bottom'].data.caption.text=='  /')
FileManager.mgr.top.widget.path='/sd/'..string.rep('long-folder/',20)
FileManager:updatePaths()
assert(#app.elements['path-top'].data.caption.text * 8 <= 260)
for i=1,20 do FileManager:updatePaths(); collectgarbage('collect') end
FileManager.app=nil; FileManager.modal.widget=nil
app.elements['filemgr-top']=nil
assert(FileManager:Initialize()==false)
"""
        source = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "tolua.h"
typedef struct {int16_t x,y;uint16_t w,h;} SDL_Rect;
static int custom_collected;
static int custom_collect(lua_State *L) {
    free(tolua_tousertype(L,1,NULL));custom_collected++;return 0;
}
static int text_size(lua_State *L) {
    const char *s=luaL_checkstring(L,2);
    SDL_Rect r={0,0,(uint16_t)(strlen(s)*8),16};
    void *copy=tolua_copy(L,&r,sizeof(r));
    tolua_pushusertype(L,tolua_clone(L,copy,NULL),"SDL_Rect");return 1;
}
static int rect_w(lua_State *L) {
    SDL_Rect *r=tolua_tousertype(L,1,NULL);assert(r);lua_pushnumber(L,r->w);return 1;
}
static int load_sdl_binding(lua_State *L) {
    if(HAS_SDL) {
        tolua_module(L,NULL,0);tolua_beginmodule(L,NULL);
        tolua_cclass(L,"SDL_Rect","SDL_Rect","",NULL);tolua_beginmodule(L,"SDL_Rect");
        tolua_variable(L,"w",rect_w,NULL);tolua_endmodule(L);tolua_endmodule(L);
    }
    return 0;
}
int main(void) {
    lua_State *L=luaL_newstate();assert(L);
    luaopen_base(L);luaopen_table(L);luaopen_string(L);lua_settop(L,0);
    tolua_open(L);tolua_usertype(L,"SDL_Rect");
    lua_register(L,"text_size",text_size);lua_register(L,"load_sdl_binding",load_sdl_binding);
    int rv=luaL_loadfile(L,SCRIPT);if(!rv)rv=lua_pcall(L,0,0,0);assert(!rv);
    rv=luaL_loadstring(L,SETUP);if(!rv)rv=lua_pcall(L,0,0,0);
    if(rv)fprintf(stderr,"%s\n",lua_tostring(L,-1));
    assert(!rv);lua_settop(L,0);
    void *owned=malloc(sizeof(SDL_Rect));assert(owned);
    tolua_pushusertype(L,tolua_clone(L,owned,custom_collect),"SDL_Rect");lua_pop(L,1);
    lua_gc(L,LUA_GCCOLLECT,0);lua_gc(L,LUA_GCCOLLECT,0);
    assert(custom_collected==1);
    lua_pushstring(L,"tolua_gc");lua_rawget(L,LUA_REGISTRYINDEX);
    lua_pushnil(L);assert(lua_next(L,-2)==0); /* no retained default/custom clones */
    lua_close(L);return 0;
}
"""
        core = 'lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lstrlib ltablib'.split()
        tolua = ROOT / 'modules/tolua/tolua'
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            program = path / 'startup.c'
            program.write_text(f'#define HAS_SDL {int(has_sdl)}\n'
                + '#define SCRIPT ' + json.dumps(str(ROOT / 'applications/filemanager/lua/main.lua')) + '\n'
                + '#define SETUP ' + json.dumps(setup) + '\n' + source)
            command = ['gcc','-std=gnu11','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie',
                '-I'+str(ROOT/'lib/lua/src'),'-I'+str(tolua/'include'),str(program),
                *[str(ROOT/f'lib/lua/src/{n}.c') for n in core],
                *map(str,(tolua/'src/lib').glob('*.c')),'-lm','-o',str(path/'startup')]
            compiled=subprocess.run(command,capture_output=True,text=True)
            self.assertEqual(compiled.returncode,0,compiled.stderr)
            checked=subprocess.run([str(path/'startup')],capture_output=True,text=True)
            self.assertEqual(checked.returncode,0,checked.stdout+checked.stderr)

    def test_core_rejects_failed_native_or_lua_onload(self):
        from utils.tests.test_kui_bug_batch import SDL, run_source
        source=(ROOT/'src/app/load.c').read_text()
        body=source[source.index('int BuildAppBody('):source.index('int UnLoadApp(')]
        run_source(SDL + r'''
enum {APP_STATE_PROCESS=1,APP_STATE_READY=2,MXML_DESCEND=0,MXML_ELEMENT=1,LIST_ITEM_GUI_WIDGET=0,LUA_DO_STRING=1};
typedef int GUI_Widget,GUI_Surface,GUI_Object;
typedef struct node {struct node *child,*next;int type;union {struct {char *name;} element;} value;} mxml_node_t;
typedef struct {int state;GUI_Widget *body;void *xml,*elements,*lua;} App_t;
static mxml_node_t whitespace,root_node={.child=&whitespace};
static GUI_Widget widget;
static const char *event;
static int native_ok=1,reject_init,lua_error,no_lua;
#define mxmlFindElement(...) (&root_node)
#define isTsunamiBody(n) 0
#define appTsunamiBuildBody(a,n) 1
#define parseNodeSize(n,p,w,h) (*(w)=640,*(h)=480)
#define parseNodePosition(n,p,x,y) (*(x)=0,*(y)=0)
#define GUI_PanelCreate(...) (&widget)
#define GUI_PanelSetBackgroundColor(...) ((void)0)
#define GUI_PanelSetBackground(...) ((void)0)
#define GUI_WidgetGetArea(w) ((SDL_Rect){0,0,640,480})
#define GUI_ContainerAdd(...) ((void)0)
#define GUI_ObjectGetName(o) "widget"
#define GUI_ObjectDecRef(o) ((void)0)
#define listAddItem(...) ((void)0)
#define ds_printf(...) ((void)0)
#define dsystem_buff(s) ((void)0)
#define getElementSurface(a,b) NULL
#define parseAppElement(...) NULL
static char *FindXmlAttr(const char *name,mxml_node_t *n,char *fallback) {(void)n;return !strcmp(name,"onload")?(char *)event:fallback;}
static void TraceAppLoad(App_t *a,const char *phase,const char *detail) {(void)a;(void)phase;(void)detail;}
static int CallAppExportFunc(App_t *a,char *s) {(void)s;if(reject_init)a->state&=~APP_STATE_READY;return native_ok;}
static void SetupAppLua(App_t *a) {a->lua=no_lua?NULL:a;}
static int LuaDo(int type,const char *code,void *L) {(void)type;(void)code;assert(L);return lua_error;}
''' + body + r'''
int main(void) {
 App_t app={.state=APP_STATE_PROCESS};
 assert(BuildAppBody(&app)==1 && (app.state&APP_STATE_READY));
 event="export:FileManagerApp_Init()";
 assert(BuildAppBody(&app)==1);
 reject_init=1;assert(!BuildAppBody(&app) && !(app.state&APP_STATE_READY));
 reject_init=0;native_ok=0;assert(!BuildAppBody(&app));
 event="Initialize()";assert(BuildAppBody(&app));
 lua_error=2;assert(!BuildAppBody(&app));
 lua_error=0;no_lua=1;assert(!BuildAppBody(&app));
 return 0;
}
''',extra=['-Wno-unused-variable','-Wno-unused-but-set-variable'])
