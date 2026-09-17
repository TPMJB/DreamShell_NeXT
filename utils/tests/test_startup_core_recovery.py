"""Exercise startup failures that previously left an empty GUI and cursor."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def compile_run(source, extra=()):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        (path / 'check.c').write_text(source)
        result = subprocess.run(
            ['gcc', '-std=gnu11', '-O1', '-Wall', '-Wextra', str(path / 'check.c'),
             *map(str, extra), '-lm', '-o', str(path / 'check')],
            capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(path / 'check')], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)


class StartupCoreRecoveryTests(unittest.TestCase):
    def test_lua_failures_reveal_console_after_splash(self):
        source = (ROOT / 'src/main.c').read_text()
        startup = source[source.index("\tif(settings->startup[0] == '/') {"):]
        startup = startup[:startup.index('#ifdef DS_PROF')]
        declaration = next(line for line in source.splitlines()
                           if 'int startup_result =' in line)
        compile_run(r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NAME_MAX 256
enum {LUA_DO_FILE, LUA_DO_STRING};
typedef struct {char startup[128];} Settings_t;
static int lua_status, lua_calls, command_calls, shown, splash_hidden, wanted_type;
static const char *wanted_code;
static int LuaDo(int type,const char *code,void *state) {
    assert(state && type==wanted_type && !strcmp(code,wanted_code));
    ++lua_calls;return lua_status;
}
static void *GetLuaState(void) {return &lua_calls;}
static void dsystem_buff(const char *s) {assert(!strcmp(s,"#custom"));++command_calls;}
static void HideLogo(void) {assert(!shown);splash_hidden=1;}
static void ShowConsole(void) {assert(splash_hidden);++shown;}
#define SetConsoleDebug(x) ((void)(x))
#define dbgio_set_dev_ds() ((void)0)
#define ds_printf(...) ((void)0)
static void startup_check(const char *script) {
    Settings_t value={0},*settings=&value;
    char fn[NAME_MAX];
    int emu=1;
    snprintf(value.startup,sizeof(value.startup),"%s",script);
''' + declaration + '\n' + startup + r'''
}
int main(void) {
    const char *scripts[]={"/lua/missing.lua","error('bad startup')",""};
    setenv("PATH","/sd/DS",1);
    for(int i=0;i<3;i++) for(lua_status=0;lua_status<=2;lua_status+=2) {
        lua_calls=command_calls=shown=splash_hidden=0;
        wanted_type=i==1?LUA_DO_STRING:LUA_DO_FILE;
        wanted_code=i==0?"/sd/DS/lua/missing.lua":i==1?scripts[i]:"/sd/DS/lua/startup.lua";
        startup_check(scripts[i]);
        assert(lua_calls==1 && !command_calls && splash_hidden);
        assert(shown==(lua_status!=0));
    }
    lua_calls=command_calls=shown=splash_hidden=0;lua_status=2;
    startup_check("#custom");
    assert(!lua_calls && command_calls==1 && !shown && splash_hidden);
    return 0;
}
''')

    def test_directory_open_failure_is_an_error_and_handles_close_once(self):
        source = (ROOT / 'src/lua/lua_ds.c').read_text()
        directory = source[source.index('#define DIR_METATABLE'):source.index('static int l_copy_file')]
        compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
typedef int file_t;
typedef uint32_t uint32;
typedef struct {const char *name;int size,attr,time;} dirent_t;
#define FILEHND_INVALID (-1)
#define O_RDONLY 0
#define O_DIR 1
static int closes,reads,opened;
static file_t fs_open(const char *path,int flags) {
    assert(flags==O_DIR);reads=0;
    return opened=!strcmp(path,"missing")?FILEHND_INVALID:!strcmp(path,"zero")?0:7;
}
static const dirent_t *fs_readdir(file_t fd) {
    static dirent_t ent={"launch_app",0,1,0};
    assert(fd==opened && fd!=FILEHND_INVALID);
    return reads++==0?&ent:NULL;
}
static int fs_close(file_t fd) {assert(fd==opened && fd!=FILEHND_INVALID);closes++;return 0;}
''' + directory + r'''
int main(void) {
    lua_State *L=luaL_newstate();assert(L);
    luaopen_base(L);luaopen_string(L);lua_settop(L,0);
    dir_create_meta(L);lua_pop(L,1);lua_register(L,"dir",dir_iter_factory);
    int result=luaL_dostring(L,
        "local ok,msg=pcall(dir,'missing');assert(not ok and string.find(msg,'cannot open missing'));"
        "collectgarbage('collect')");
    if(result) fprintf(stderr,"%s\n",lua_tostring(L,-1));
    assert(!result && !closes);
    result=luaL_dostring(L,"local it=dir('zero');assert(it().name=='launch_app');it=nil;collectgarbage('collect')");
    if(result) fprintf(stderr,"%s\n",lua_tostring(L,-1));
    assert(!result && closes==1);
    result=luaL_dostring(L,"local it=dir('valid');assert(it().name=='launch_app');assert(it()==nil);"
        "assert(not pcall(it));it=nil;collectgarbage('collect')");
    if(result) fprintf(stderr,"%s\n",lua_tostring(L,-1));
    assert(!result && closes==2);
    lua_close(L);assert(closes==2);return 0;
}
''', [f'-I{ROOT / "lib/lua/src"}', *[
            ROOT / f'lib/lua/src/{name}.c' for name in
            'lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser '
            'lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lstrlib'.split()]])


if __name__ == '__main__':
    unittest.main()
