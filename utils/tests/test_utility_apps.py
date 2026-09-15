"""Behavior checks for NeXT utility operations and fixed-width disc metadata."""
import ctypes
import os
import ctypes.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as E

ROOT=Path(__file__).resolve().parents[2]

class UtilityAppsTests(unittest.TestCase):
    def test_all_redesigned_screens_fit_tv_safe_area(self):
        # Independent of generator constants: these margins are the regression
        # requirement from the console photo, not just the 640x480 canvas bounds.
        apps=('filemanager','gdplay','settings','bios_flasher','region_changer',
              'speedtest','memtest','network')
        def check(element, ox, oy, parent_width, parent_height, app):
            x,y=int(element.get('x',0)),int(element.get('y',0))
            w,h=int(element.get('width',0)),int(element.get('height',0))
            identity=(app, element.tag, element.get('name'))
            self.assertGreater(w,0,identity); self.assertGreater(h,0,identity)
            self.assertGreaterEqual(x,0,identity); self.assertGreaterEqual(y,0,identity)
            self.assertLessEqual(x+w,parent_width,identity)
            self.assertLessEqual(y+h,parent_height,identity)
            self.assertGreaterEqual(ox+x,32,identity)
            self.assertGreaterEqual(oy+y,32,identity)
            self.assertLessEqual(ox+x+w,608,identity)
            self.assertLessEqual(oy+y+h,448,identity)
            for child in element: check(child,ox+x,oy+y,w,h,app)
        for app in apps:
            body=E.parse(ROOT/'applications'/app/'app.xml').getroot().find('body')
            for element in body: check(element,0,0,640,480,app)

    def test_bounded_disc_metadata_and_clock_edges(self):
        source=r'''
#include <assert.h>
#include "applications/gdplay/modules/disc_metadata.h"
#include "applications/settings/modules/settings_model.h"
int main(void) {
    unsigned char data[256]; disc_metadata_t result;
    memset(data,' ',sizeof(data)); memcpy(data,"SEGA SEGAKATANA ",15);
    memcpy(data+48,"JUE",3); data[61]='1';
    memcpy(data+80,"20000229",8); memset(data+128,'X',128);
    assert(disc_metadata_read(&result,data,sizeof(data)));
    assert(strlen(result.title)==128 && !strcmp(result.region,"Japan / USA / Europe"));
    assert(!strcmp(result.date,"2000-02-29") && !strcmp(result.vga,"Yes"));
    for(size_t size=0;size<256;size++) assert(!disc_metadata_read(&result,data,size));
    for(int c=0;c<256;c++) {
        memset(data+128,c,128); assert(disc_metadata_read(&result,data,256));
        assert(strlen(result.title)<=128);
    }
    memset(data+48,'?',8); assert(disc_metadata_read(&result,data,256));
    assert(!strcmp(result.region,"Unknown"));
    data[0]='?'; assert(!disc_metadata_read(&result,data,256));
    struct tm date={.tm_year=124,.tm_mon=1,.tm_mday=29,.tm_hour=0,.tm_min=59};
    settings_adjust_clock(&date,0,1); assert(date.tm_mday==28);
    settings_adjust_clock(&date,3,-1); assert(date.tm_hour==23);
    settings_adjust_clock(&date,4,1); assert(date.tm_min==0);
    date.tm_year=100; date.tm_mon=1; date.tm_mday=29;
    settings_adjust_clock(&date,1,1); assert(date.tm_mon==2 && date.tm_mday==29);
    assert(settings_days(2000,1)==29 && settings_days(2100,1)==28);
    char tz[32]; settings_format_timezone(tz,sizeof(tz),-30); assert(!strcmp(tz,"UTC-00:30"));
    settings_format_timezone(tz,sizeof(tz),345); assert(!strcmp(tz,"UTC+05:45"));
    assert(settings_clamp(-1,0,255)==0 && settings_clamp(260,0,255)==255);
    return 0;
}'''
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp); (path/'test.c').write_text(source)
            subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(ROOT),str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))

    def test_file_operations_reject_parent_self_nested_and_existing_targets(self):
        name=next((ctypes.util.find_library(n) for n in ('lua5.4','lua5.3','lua5.2') if ctypes.util.find_library(n)),None)
        if not name: self.skipTest('Lua shared library unavailable')
        lib=ctypes.CDLL(name)
        lib.luaL_newstate.restype=ctypes.c_void_p
        lib.luaL_openlibs.argtypes=[ctypes.c_void_p]
        lib.luaL_loadstring.argtypes=[ctypes.c_void_p,ctypes.c_char_p]
        lib.lua_pcallk.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_ssize_t,ctypes.c_void_p]
        lib.lua_tolstring.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p]; lib.lua_tolstring.restype=ctypes.c_char_p
        lib.lua_close.argtypes=[ctypes.c_void_p]
        code=(ROOT/'applications/filemanager/lua/main.lua').read_text()+r'''
local fm=FileManager
assert(fm:validName('Evolution 2'))
for _, name in ipairs({'', '.', '..', '../bad', 'bad/name', 'bad\\name', 'bad\nname'}) do assert(not fm:validName(name),name) end
local errors=0
function fm:showError(msg) errors=errors+1; return false end
function fm:tooltip(msg) end
local f={name='game',file='/sd/game',path='/sd',attr=1,size=2}
function fm:getFile() return f end
local destination='/sd'
function fm:getUnfocusedManager() return {widget=destination} end
GUI={FileManagerGetPath=function(w) return w end}
DS={FileExists=function() return false end, DirExists=function() return false end}
lfs={mkdir=function() error('unsafe mkdir reached') end,copyfile=function() error('unsafe copy reached') end}
fm:copyPath(); assert(errors==1) -- exact self copy
f.file='/sd/game'; destination='/sd/game/sub'; fm:copyPath(); assert(errors==2)
destination='/ide'; DS.DirExists=function() return true end; fm:copyPath(); assert(errors==3)
f.name='..'; fm:copyPath(); assert(errors==4)
f.name='sd'; f.path='/'; fm:toolbarDelete(); assert(errors==5)
f.name='Game'; f.path='/sd'; f.file='/sd/Game'; destination='/SD/game/sub'
DS.DirExists=function() return false end
fm:copyPath(); assert(errors==6) -- FAT/exFAT case-insensitive subtree
local copied=false
fm.modal.progress_copied_size=0; fm.modal.progress_total=0; fm.modal.copy_cancelled=false
GUI.DialogSetProgress=function(w,v) assert(v==v and v~=math.huge) end
FileManagerPump=function() return true end
lfs.copyfile=function(src,dst,callback) callback(0); copied=true; return true end
assert(fm:copyFile('/sd/empty','/ide/empty',0) and copied)
FileManagerPump=function() return false end
assert(not fm:continueOperation())
'''
        state=lib.luaL_newstate();lib.luaL_openlibs(state)
        try:
            rv=lib.luaL_loadstring(state,code.encode())
            if not rv: rv=lib.lua_pcallk(state,0,0,0,0,None)
            self.assertEqual(rv,0,lib.lua_tolstring(state,-1,None))
        finally: lib.lua_close(state)

    def test_app_layout_references_and_native_icons(self):
        from PIL import Image
        for app in ('filemanager','gdplay','settings'):
            path=ROOT/'applications'/app
            tree=E.parse(path/'app.xml').getroot()
            self.assertEqual(tree.get('version'),'2.0.1')
            names=[e.get('name') for e in tree.find('body').iter() if e.get('name')]
            self.assertEqual(len(names),len(set(names)),app)
            exports=(path/'modules/exports.txt').read_text().splitlines()
            for element in tree.iter():
                for value in element.attrib.values():
                    if value.startswith('export:'):
                        self.assertIn(value[7:].split('(')[0],exports)
                if element.tag in ('label','input','filemanager'):
                    self.assertGreater(int(element.get('width','1')),0)
            with Image.open(path/tree.get('icon')) as image:
                self.assertEqual(image.size,(64,64))
