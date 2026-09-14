"""Exercise the firmware transaction and real IO helpers with fault injection."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as E

ROOT = Path(__file__).resolve().parents[2]

class MaintenanceTests(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path/'test.c').write_text(source)
            subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I'+str(ROOT),
                            str(path/'test.c'), '-o', str(path/'test')], check=True)
            subprocess.run([str(path/'test')], check=True,
                           env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))

    def test_firmware_backup_before_erase_and_every_failure_phase(self):
        self.compile_run(r'''
#include <assert.h>
#include "applications/maintenance_model.h"
static unsigned char flash[8192], backup[8192], old[8192], check[8192], image[8192];
static int fault, erased, locked, writes, reads, saved;
static int read_flash(void *c, void *p, size_t n) {
    (void)c; reads++;
    if(fault == MF_READ || (fault == MF_VERIFY && reads > 1)) return -1;
    memcpy(p,flash,n); return 0;
}
static int save(void *c, const void *p, size_t n) {
    (void)c; assert(!erased && !locked);
    if(fault == MF_BACKUP) return -1;
    memcpy(backup,p,n); saved=1; return 0;
}
static int unchanged(void *c, const void *p, size_t n) {
    (void)c; assert(saved && !locked && !memcmp(backup,p,n));
    return fault == MF_CHANGED ? -1 : 0;
}
static void critical(void *c, int active) {
    (void)c; assert(locked != active); locked=active;
}
static int erase(void *c) {
    (void)c; assert(saved && locked); erased++;
    if(fault == MF_ERASE) return -1;
    memset(flash,255,sizeof(flash)); return 0;
}
static int write_flash(void *c, size_t pos, const void *p, size_t n) {
    (void)c; assert(saved && erased && locked && n && pos+n<=sizeof(flash)); writes++;
    if(fault == MF_WRITE) return -1;
    memcpy(flash+pos,p,n);
    if(fault == 88) flash[pos]^=1;
    return 0;
}
static void progress(void *c,int phase,size_t done,size_t total) {
    (void)c; (void)phase; assert(done<=total);
}
static void reset(int f) {
    fault=f; saved=locked=erased=writes=reads=0;
    memset(flash,0x55,sizeof(flash)); memset(image,0xAA,sizeof(image));
}
int main(void) {
    maintenance_flash_ops ops={0,read_flash,save,unchanged,critical,erase,write_flash,progress};
    for(int f=MF_READ;f<=MF_VERIFY;f++) {
        reset(f);
        assert(maintenance_flash_run(&ops,image,sizeof(image),1000,old,check)==f);
        assert(!locked);
        if(f <= MF_CHANGED) assert(!erased && !writes);
        if(f == MF_ERASE) assert(!writes);
    }
    reset(0);
    assert(maintenance_flash_run(&ops,image,sizeof(image),1000,old,check)==MF_DONE);
    assert(writes==9 && !locked && !memcmp(image,flash,sizeof(image)));
    for(size_t i=0;i<sizeof(backup);i++) assert(backup[i]==0x55);
    reset(0);
    assert(maintenance_flash_run(&ops,image,sizeof(image),4096,old,old)==MF_DONE);
    assert(!memcmp(image,old,sizeof(image)) && backup[0]==0x55);
    reset(88);
    assert(maintenance_flash_run(&ops,image,sizeof(image),4096,old,check)==MF_VERIFY);
    reset(0); ops.write=0; memset(image,255,sizeof(image));
    assert(maintenance_flash_run(&ops,image,sizeof(image),4096,old,check)==MF_DONE);
    assert(!writes && erased==1 && !locked);
    reset(0);
    assert(maintenance_flash_run(&ops,image,0,4096,old,check)==MF_READ && !reads);
    assert(maintenance_flash_run(&ops,image,8192,0,old,check)==MF_READ && !reads);
    return 0;
}''')

    def test_bank_bounds_factory_fields_and_64_bit_timings(self):
        self.compile_run(r'''
#include <assert.h>
#include "applications/maintenance_model.h"
int main(void) {
    uint32_t sectors[]={0,0x100000,0x200000,0x300000};
    assert(maintenance_bank_layout(0x400000,0x200000,sectors,4,0));
    assert(!maintenance_bank_layout(0x400000,0x200000,sectors,4,1));
    assert(maintenance_bank_layout(0x200000,0x200000,0,0,1));
    assert(!maintenance_bank_layout(0x200000,0x100000,0,0,1));
    assert(!maintenance_bank_layout(0x400000,0x300000,sectors,4,0));
    assert(!maintenance_bank_layout(0x400000,0,sectors,4,0));
    sectors[2]=0x210000;
    assert(!maintenance_bank_layout(0x400000,0x200000,sectors,4,0));
    sectors[0]=0x1000;
    assert(!maintenance_bank_layout(0x400000,0x200000,sectors,4,0));
    uint8_t factory[8192], before[8192]; memset(before,0xAC,sizeof(before));
    for(int r=0;r<3;r++) for(int l=0;l<6;l++) for(int b=0;b<4;b++) for(int s=0;s<2;s++) {
        memcpy(factory,before,sizeof(factory)); maintenance_factory_edit(factory,r,l,b,s);
        assert(maintenance_factory_valid(factory,sizeof(factory)));
        for(size_t i=0;i<sizeof(factory);i++) if(i<2 || i>4) assert(factory[i]==before[i]);
    }
    assert(!maintenance_factory_valid(factory,8191));
    assert(!maintenance_factory_valid(factory,8193));
    factory[3]=0xff; assert(!maintenance_factory_valid(factory,8192));
    for(size_t i=0;i<8192;i++) assert(!maintenance_factory_valid(factory,i));
    assert(maintenance_mibps(32*1048576,UINT64_C(8000000000))==4.0);
    assert(maintenance_mibps(UINT64_C(5368709120),UINT64_C(10000000000))==512.0);
    assert(maintenance_mibps(10,0)==0.0);
    int different=0;
    for(unsigned i=0;i<256;i++) different+=maintenance_pattern(i)!=maintenance_pattern(i+262144);
    assert(different>200); /* repeated/reordered chunks do not match */
    return 0;
}''')

    def test_real_io_helpers_handle_short_transfers_collisions_and_bad_backups(self):
        self.compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
typedef int file_t;
#define FILEHND_INVALID (-1)
static unsigned char disk[12000];
static size_t length, cursor;
static int fail_read, fail_write, fail_close, corrupt, mode, exists, opened;
static int FileExists(const char *p) { return exists && strstr(p,"-000.bin"); }
static int DirExists(const char *p) { return !strcmp(p,"/sd") || !strcmp(p,"/ide"); }
static file_t fs_open(const char *p,int m) {
    (void)p; mode=m; cursor=0; opened++;
    if(m&O_WRONLY) { assert((m&(O_EXCL|O_CREAT))==(O_EXCL|O_CREAT)); length=0; }
    return 1;
}
static int fs_close(file_t f) { (void)f; return fail_close ? -1 : 0; }
static size_t fs_total(file_t f) { (void)f; return length; }
static ssize_t fs_write(file_t f,const void *p,size_t n) {
    (void)f; if(fail_write) return fail_write==1 ? 0 : -1;
    if(n>37)n=37;
    assert(cursor+n<=sizeof(disk)); memcpy(disk+cursor,p,n);
    cursor+=n; length=cursor; return n;
}
static ssize_t fs_read(file_t f,void *p,size_t n) {
    (void)f; if(fail_read) return -1;
    if(n>29)n=29;
    if(n>length-cursor)n=length-cursor;
    memcpy(p,disk+cursor,n); if(corrupt && n) ((uint8_t*)p)[0]^=1;
    cursor+=n; return n;
}
#include "applications/maintenance_io.h"
int main(void) {
    uint8_t data[8192],result[8192]; char path[512]; memset(data,0x55,sizeof(data));
    assert(ma_save_verified("/sd","test","bin",data,sizeof(data),path,sizeof(path))==0);
    assert(length==sizeof(data));
    assert(ma_load(path,result,sizeof(result))==0 && !memcmp(result,data,sizeof(data)));
    assert(ma_load(path,result,sizeof(result)-1)<0);
    exists=1;
    assert(ma_save_verified("/sd","test","bin",data,sizeof(data),path,sizeof(path))==0);
    assert(strstr(path,"-001.bin"));
    corrupt=1; assert(ma_save_verified("/sd","test","bin",data,sizeof(data),path,sizeof(path))<0); corrupt=0;
    fail_read=1; assert(ma_save_verified("/sd","test","bin",data,sizeof(data),path,sizeof(path))<0); fail_read=0;
    fail_write=1; assert(ma_save_verified("/sd","test","bin",data,sizeof(data),path,sizeof(path))<0);
    fail_write=2; assert(ma_save_verified("/sd","test","bin",data,sizeof(data),path,sizeof(path))<0); fail_write=0;
    fail_close=1; assert(ma_save_verified("/sd","test","bin",data,sizeof(data),path,sizeof(path))<0); fail_close=0;
    int old=opened;
    assert(ma_save_verified("/ram","test","bin",data,sizeof(data),path,sizeof(path))<0 && opened==old);
    assert(ma_persistent("/sd1/backups") && ma_persistent("/ide") && ma_persistent("/pc/backups"));
    assert(!ma_persistent("/sd_fake") && !ma_persistent("/cd") && !ma_persistent("/"));
    assert(ma_join(path,5,"/sd","long")<0 && ma_join(path,sizeof(path),"/sd","../bad")<0);
    return 0;
}''')

    def test_layout_callbacks_geometry_and_dependencies(self):
        for app,prefix in [('bios_flasher','BiosFlasher'),('region_changer','RegionChanger'),
                           ('speedtest','Speedtest'),('memtest','Memtest'),('network','NetworkApp')]:
            path=ROOT/'applications'/app
            xml=E.parse(path/'app.xml').getroot()
            exports=(path/'modules/exports.txt').read_text().splitlines()
            self.assertEqual(xml.get('version'), '3.0.0' if app=='bios_flasher' else '2.0.0')
            names=[e.get('name') for e in xml.find('body').iter() if e.get('name')]
            self.assertEqual(len(names),len(set(names)))
            for name in ('main-panel','browser-panel','dialog','menu','file-picker','progress'):
                self.assertIn(name,names)
            for e in xml.iter():
                for value in e.attrib.values():
                    if value.startswith('export:'): self.assertIn(value[7:].split('(')[0],exports)
                if e.tag in ('label','input','panel','filemanager') and e.get('name')!='dialog':
                    self.assertLessEqual(int(e.get('x',0))+int(e.get('width',0)),640)
                    self.assertLessEqual(int(e.get('y',0))+int(e.get('height',0)),480)
            self.assertEqual(xml.find('body').get('onclose'),f'export:{prefix}_Close()')
            if app=='bios_flasher':
                self.assertEqual(xml.find('resources/module').get('src'),'../../modules/bflash.klf')
            if app=='region_changer': self.assertIsNone(xml.find('resources/script'))

if __name__=='__main__': unittest.main()
