"""Run the real startup script in bundled Lua with simulated boot failures."""
from pathlib import Path
import json
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StartupRecoveryTests(unittest.TestCase):
    def test_startup_recovery_and_diagnostics(self):
        script = str(ROOT / "resources/lua/startup.lua")
        scenarios = r'''
local script = SCRIPT
local function boot(options)
    local result = {log = "", opened = {}, console = false, closed = false}
    local env = {PATH="/sd/DS", HOST="K-UI", VERSION="1.0", ARCH="Dreamcast",
                 BOARD_ID="test", USER="Default", STARTUP_APP=options.target}
    os = {getenv=function(name) return env[name] end,
          time=function() return 1735689600 end,
          date=function() return "test date" end,
          execute=function(command) result.command=command end}
    io = {open=function(path, mode)
        assert(path=="/sd/DS/kui-startup.log" and mode=="w")
        if options.readonly then return nil, "read only" end
        return {write=function(self, text) result.log=result.log..text end,
                flush=function() result.flushed=true end,
                close=function() result.closed=true end}
    end}
    print = function() end
    MapleAttached = function() return true end
    OpenModule = function(path)
        if options.module_error then error("module load exception") end
        return 1
    end
    lfs = {dir=function(path)
        assert(path=="/sd/DS/apps")
        if options.directory_error then error("cannot open /sd/DS/apps") end
        local once=false
        return function()
            if once then return nil end
            once=true
            return {name="launch_app", attr=16}
        end
    end}
    AddApp = function(path)
        assert(path=="/sd/DS/apps/launch_app/app.xml")
        if options.missing then return nil end
        result.registered=true
        return "Launch App"
    end
    OpenApp = function(name)
        table.insert(result.opened, name)
        if result.registered and (name=="Launch App" or name=="File Manager") then return 1 end
    end
    ShowConsole = function() result.console=true end
    result.ok, result.error=pcall(dofile, script)
    return result
end
local good=boot({target="Launch App"})
assert(good.ok and not good.console and good.closed and good.flushed)
assert(#good.opened==1 and good.opened[1]=="Launch App")
assert(good.log:find("Startup app opened",1,true))
local custom=boot({target="File Manager"})
assert(custom.ok and #custom.opened==1 and custom.opened[1]=="File Manager")
local fallback=boot({target="Removed app"})
assert(fallback.ok and not fallback.console and #fallback.opened==2)
assert(fallback.opened[2]=="Launch App" and fallback.log:find("unavailable",1,true))
local unset=boot({})
assert(unset.ok and unset.opened[1]=="Launch App")
local missing=boot({target="Launch App",missing=true})
assert(not missing.ok and missing.console and missing.closed)
assert(missing.log:find("could not open the launcher",1,true))
local directory=boot({target="Launch App",directory_error=true})
assert(not directory.ok and directory.console and #directory.opened==0)
assert(directory.log:find("cannot open /sd/DS/apps",1,true))
local module_failure=boot({target="Launch App",module_error=true})
assert(not module_failure.ok and module_failure.console and module_failure.closed)
assert(module_failure.log:find("module load exception",1,true))
local readonly=boot({target="Launch App",readonly=true})
assert(readonly.ok and not readonly.console)
'''.replace("SCRIPT", json.dumps(script), 1)
        source = r'''
#include <stdio.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
int main(void) {
    lua_State *L=luaL_newstate();
    if(!L) return 2;
    luaopen_base(L); luaopen_table(L); luaopen_string(L); luaopen_debug(L);
    lua_settop(L, 0);
    int status=luaL_dostring(L, SCENARIOS);
    if(status) fprintf(stderr, "%s\n", lua_tostring(L,-1));
    lua_close(L);
    return status ? 1 : 0;
}
'''
        units = "lapi lcode ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lstrlib ltablib ldblib".split()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            program = path / "startup.c"
            program.write_text("#define SCENARIOS " + json.dumps(scenarios) + "\n" + source)
            command = ["gcc", "-std=gnu11", "-O1", "-I" + str(ROOT / "lib/lua/src"),
                       str(program), *[str(ROOT / f"lib/lua/src/{n}.c") for n in units],
                       "-lm", "-o", str(path / "startup")]
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            checked = subprocess.run([str(path / "startup")], capture_output=True, text=True)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
