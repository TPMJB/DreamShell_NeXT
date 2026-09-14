"""Host regression tests compile the production loader read and parsing code.
No Dreamcast hardware or commercial game data is needed.
"""
import pathlib
import json
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]

def compile_run(source, flags=()):
    with tempfile.TemporaryDirectory() as tmp:
        c = pathlib.Path(tmp) / "test.c"
        exe = pathlib.Path(tmp) / "test"
        c.write_text(source)
        subprocess.run(["cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra",
                        "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-I", str(ROOT / "include"), *flags, str(c), "-o", str(exe)],
                       check=True)
        subprocess.run([str(exe)], check=True, cwd=tmp)

class LoaderChecks(unittest.TestCase):
    def test_crc_memory_and_elf_bounds(self):
        compile_run(r'''
#include <assert.h>
#include <isoldr/check.h>
#include <isoldr/elf_check.h>
int main(void) {
    const char *s="123456789";
    assert(~isoldr_crc32_update(~0U,s,9)==0xcbf43926U);
    unsigned crc=isoldr_crc32_update(~0U,s,4);
    assert(~isoldr_crc32_update(crc,s+4,5)==0xcbf43926U);
    assert(~isoldr_crc32_update(~0U,s,0)==0);
    uint32_t n=0;
    assert(isoldr_boot_extent(0xac010000,2049,2048,&n) && n==4096);
    assert(!isoldr_boot_extent(0xac010000,0,2048,&n));
    assert(!isoldr_boot_extent(0xac000100,2048,2048,&n));
    assert(!isoldr_boot_extent(0xacfff000,1,2048,&n));
    assert(!isoldr_boot_extent(0xac010000,UINT32_MAX,2048,&n));
    assert(!isoldr_boot_extent(0xac010000,2048,2352,&n));
    assert(isoldr_ranges_overlap(0x0c004000,0xe000,0x0c010000,0x100000));
    assert(!isoldr_ranges_overlap(0x0ce00000,0x30000,0x0c010000,0x100000));
    assert(!isoldr_ranges_overlap(0x100,0x100,0x200,0x100));
    assert(isoldr_ranges_overlap(0xfffffff0,0x100,0xfffffff8,8));
    assert(isoldr_elf_span(128,64,2,32));
    assert(!isoldr_elf_span(128,64,3,32));
    assert(!isoldr_elf_span(128,UINT32_MAX,4,4));
    assert(!isoldr_elf_span(128,4,UINT32_MAX,32));
    assert(!isoldr_elf_span(128,0,1,0));
    return 0;
}
''')

    def test_gdi_parser(self):
        compile_run(r'''
#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#pragma GCC poison isspace isdigit
#include <isofs/gdi_parse.h>
int main(void) {
    ds_gdi_track t;
    assert(ds_gdi_count("3\r\n")==3);
    assert(ds_gdi_count("0")==-1 && ds_gdi_count("100")==-1);
    assert(ds_gdi_count("3 junk")==-1 && ds_gdi_count("-1")==-1);
    assert(ds_gdi_parse("3 45000 4 2352 track03.bin 0\r\n",&t));
    assert(t.number==3 && t.lba==45000 && t.sector_size==2352 && !strcmp(t.name,"track03.bin"));
    assert(ds_gdi_parse("1 0 4 2048 \"track one.iso\" 4096",&t) && t.offset==4096);
    assert(ds_gdi_parse("1 0 4 2352 track01.bin 4294967295",&t) && t.offset==UINT32_MAX);
    assert(!ds_gdi_parse("1 0 4 2352 track01.bin 4294967296",&t));
    assert(!ds_gdi_parse("1 0 4 2048 \"unterminated 0",&t));
    assert(!ds_gdi_parse("1 0 4 2048 track01.iso",&t));
    assert(!ds_gdi_parse("1 0 4 0 track01.iso 0",&t));
    assert(!ds_gdi_parse("1 0 4 2048 track01.iso -1",&t));
    assert(!ds_gdi_parse("1 42949672960 4 2048 track01.iso 0",&t));
    assert(!ds_gdi_parse("1 0 4 2048 track01.iso 0 garbage",&t));
    char line[256]; memset(line,'x',sizeof(line)); line[200]=0;
    char longline[300]; snprintf(longline,sizeof(longline),"1 0 4 2048 %s 0",line);
    assert(!ds_gdi_parse(longline,&t));
    return 0;
}
''')

    def test_production_gdi_preflight(self):
        fixture=json.dumps((ROOT/"utils/tests/fixtures/evolution2.gdi").read_text())
        module=(ROOT/"modules/isoldr/module.c").read_text()
        check=module[module.index("static int isoldr_check_gdi("):module.index("static int get_image_info(")]
        support=r'''
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <isofs/gdi_parse.h>
#define NAME_MAX 256
#define FILEHND_INVALID -1
typedef int file_t;
#define fs_open open
#define fs_close close
#define fs_read read
static size_t fs_total(file_t fd) {
    struct stat st;
    assert(!fstat(fd,&st));
    return st.st_size;
}
static char error[256];
static void isoldr_error(const char *fmt,...) {
    va_list args; va_start(args,fmt);
    vsnprintf(error,sizeof(error),fmt,args); va_end(args);
}
static void descriptor(const char *text) {
    FILE *f=fopen("disc.gdi","wb"); assert(f);
    assert(fwrite(text,1,strlen(text),f)==strlen(text));
    assert(!fclose(f)); error[0]=0;
}
'''
        cases=r'''
int main(void) {
    /* GD Ripper's six-field output, through the production preflight rather
     * than just the line parser. Tiny synthetic tracks suffice for this check. */
    const char *tracks[]={"track01.bin","track02.raw","track03.bin"};
    char sector[2352]={0};
    for(int i=0;i<3;++i) {
        FILE *f=fopen(tracks[i],"wb"); assert(f);
        assert(fwrite(sector,1,sizeof(sector),f)==sizeof(sector));
        assert(!fclose(f));
    }
    descriptor(EVOLUTION2_DESCRIPTOR);
    assert(isoldr_check_gdi("disc.gdi")==0);
    descriptor("3\r\n1 0 4 2352 \"track01.bin\" 0\r\n2 450 0 2352 track02.raw 0\r\n3 45000 4 2352 track03.bin 0");
    assert(isoldr_check_gdi("disc.gdi")==0);
    descriptor("1\n1 0 4 invalid track01.bin 0\n");
    assert(isoldr_check_gdi("disc.gdi")==-1);
    assert(strstr(error,"Cannot parse GDI track 1") && strstr(error,"1 0 4 invalid"));
    descriptor("1\n2 0 4 2352 track01.bin 0\n");
    assert(isoldr_check_gdi("disc.gdi")==-1 && strstr(error,"entry 1 has track number 2"));
    descriptor("2\n1 0 4 2352 track01.bin 0\n2 0 0 2352 track02.raw 0\n");
    assert(isoldr_check_gdi("disc.gdi")==-1 && strstr(error,"must increase"));
    return 0;
}
'''
        compile_run(support+check+cases.replace('EVOLUTION2_DESCRIPTOR',fixture))

    def test_production_reader_faults_and_raw_tails(self):
        reader=(ROOT/"firmware/isoldr/loader/reader.c").read_text()
        raw=reader[reader.index("static int _read_sector_by_sector("):reader.index("#ifdef HAVE_LZO",reader.index("static int _read_sector_by_sector("))]
        readsectors=reader[reader.index("int ReadSectors("):reader.index("int PreReadSectors(")]
        support=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
typedef uint8_t uint8;
typedef uint32_t uint32;
typedef unsigned int uint;
typedef void fs_callback_f(size_t);
#define FAILED -1
#define COMPLETED 0
#define PROCESSING 1
#define ISOFS_IMAGE_TYPE_ISO 1
#define ISOFS_IMAGE_TYPE_CDI 2
#define ISOFS_IMAGE_TYPE_GDI 3
#define IMAGE_TYPE_ROM_NAOMI 4
#define DBGFF(...) ((void)0)
#define LOGFF(...) ((void)0)
#define FS_DMA_HIDDEN 2
#define FS_DMA_STREAM 3
static int dma=1, bad_seek=0, short_read=0, fail_call=0, calls=0;
static int fs_dma_enabled(void) { return dma; }
static void fs_enable_dma(int value) { dma=value; }
static unsigned char disk[2352*65];
static long pos;
static size_t disk_size;
static int iso_fd=1;
static uint16_t b_seek=16, a_seek=288, sec_size=2048;
static struct { uint32_t image_type, sector_size, track_offset, track_lba[2]; } info;
#define IsoInfo (&info)
typedef struct { int lba, data_track; } gd_state_t;
static gd_state_t gds={0,3};
static gd_state_t *get_GDS(void) { return &gds; }
static void switch_gdi_data_track(int sec, gd_state_t *s) { (void)sec; (void)s; }
static size_t total(int fd) { (void)fd; return disk_size; }
static long fake_seek(int fd,long off,int whence) {
    (void)fd;
    if(bad_seek) return -1;
    long target=whence==SEEK_SET?off:pos+off;
    if(target<0 || (size_t)target>disk_size) return -1;
    return pos=target;
}
static int fake_read(int fd,void *buf,size_t count) {
    (void)fd; ++calls;
    if(fail_call==calls) return -1;
    if(short_read && count) --count;
    if(count>disk_size-pos) count=disk_size-pos;
    memcpy(buf,disk+pos,count); pos+=count;
    return count;
}
#define lseek fake_seek
#define read fake_read
'''
        cases=r'''
int main(void) {
    unsigned char buf[2048*65+64];
    for(unsigned i=0;i<sizeof(disk);++i) disk[i]=(unsigned char)(i*31+i/2352);
    info.image_type=ISOFS_IMAGE_TYPE_GDI; info.sector_size=2352; info.track_lba[0]=45150;
    for(unsigned count=1;count<=64;++count) {
        disk_size=count*2352; pos=0; calls=0; dma=1;
        memset(buf,0xa5,sizeof(buf));
        assert(ReadSectors(buf+32,45150,count,NULL)==COMPLETED);
        for(unsigned i=0;i<count;++i) assert(!memcmp(buf+32+i*2048,disk+i*2352+16,2048));
        for(unsigned i=0;i<32;++i) {
            assert(buf[i]==0xa5); assert(buf[32+count*2048+i]==0xa5);
        }
        assert(dma==1);
    }
    disk_size=2352*4;
    short_read=1; assert(ReadSectors(buf,45150,4,NULL)==FAILED && dma==1); short_read=0;
    fail_call=calls+1; assert(ReadSectors(buf,45150,4,NULL)==FAILED && dma==1); fail_call=0;
    bad_seek=1; assert(ReadSectors(buf,45150,1,NULL)==FAILED); bad_seek=0;
    disk_size=2352-1; assert(ReadSectors(buf,45150,1,NULL)==FAILED);
    iso_fd=-1; assert(ReadSectors(buf,45150,1,NULL)==FAILED); iso_fd=1;
    info.image_type=ISOFS_IMAGE_TYPE_ISO; info.sector_size=2048; info.track_lba[0]=150;
    disk_size=2048; assert(ReadSectors(buf,150,1,NULL)==COMPLETED);
    assert(!memcmp(buf,disk,2048));
    short_read=1; assert(ReadSectors(buf,150,1,NULL)==FAILED); short_read=0;
    bad_seek=1; assert(ReadSectors(buf,150,1,NULL)==FAILED); bad_seek=0;
    assert(ReadSectors(buf,149,1,NULL)==FAILED);
    assert(ReadSectors(buf,150,2,NULL)==FAILED);
    assert(ReadSectors(buf,150,-1,NULL)==FAILED);
    assert(ReadSectors(buf,-1,1,NULL)==FAILED);
    assert(ReadSectors(buf,INT_MAX,INT_MAX,NULL)==FAILED);
    assert(ReadSectors(buf,150,0,NULL)==COMPLETED);
    return 0;
}
'''
        compile_run(support+raw+readsectors+cases,["-DDEV_TYPE_IDE"])

    def test_games_menu_launch_failure_cleanup(self):
        module=(ROOT/"applications/games_menu/modules/module.c").read_text()
        play=module[module.index("static bool PlayGame("):module.index("static void PostOptimizer(")]
        start=module.index("static void* MenuExitHelper(void *params)\n{")
        exit_helper=module[start:module.index("static void CreateMainView(",start)]
        support=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
static struct { void *tsunami; } app={(void*)1};
static struct {
    __typeof__(app) *app;
    char item_value_selected[64];
    int device_selected, game_index_selected, exit_app;
    void *isoldr;
    uintptr_t addr;
} self;
static struct {
    int last_device;
    char last_game[64];
    struct { int is_folder_name; char folder_name[64], game[64]; } games_array[1];
} menu_data;
static int load_ok, freed, saved, executed, opened, console, screen, gui, handoff;
static char output[1024];
static jmp_buf jump;
static int LoadPreset(void) { return load_ok; }
static void SaveMenuConfig(void) { ++saved; }
static void FreeAppData(void) { ++freed; assert(freed==1); }
static void EnableScreen(void) { ++screen; }
static void GUI_Enable(void) { ++gui; }
static void OpenMainApp(void) { ++opened; }
static void ShowConsole(void) { ++console; }
static const char *isoldr_get_last_error(void) { return "Synthetic launch error"; }
static void isoldr_exec(void *info,uintptr_t address) {
    (void)info; (void)address;
    assert(freed==1);
    ++executed;
    if(handoff) longjmp(jump,1);
}
static void ds_printf(const char *fmt,...) {
    size_t n=strlen(output);
    va_list args; va_start(args,fmt);
    vsnprintf(output+n,sizeof(output)-n,fmt,args); va_end(args);
}
static void reset(void) {
    memset(&self,0,sizeof(self)); memset(&menu_data,0,sizeof(menu_data));
    self.app=&app; strcpy(self.item_value_selected,"EVOLUTION2.gdi");
    strcpy(menu_data.games_array[0].game,"EVOLUTION2.gdi");
    freed=saved=executed=opened=console=screen=gui=handoff=0;
    output[0]=0;
}
'''
        cases=r'''
int main(void) {
    for(int stage=0;stage<2;++stage) {
        reset(); load_ok=stage;
        MenuExitHelper(NULL);
        assert(freed==1 && opened==1 && console==1 && screen==1 && gui==1);
        assert(executed==stage && saved==stage);
        assert(strstr(output,"Synthetic launch error"));
    }
    reset(); self.exit_app=1;
    MenuExitHelper(NULL);
    assert(freed==1 && opened==1 && !console && !executed);
    reset(); load_ok=1; handoff=1;
    if(!setjmp(jump)) {
        MenuExitHelper(NULL);
        assert(!"Successful handoff must not reach the failure cleanup");
    }
    assert(freed==1 && executed==1 && !opened && !console);
    return 0;
}
'''
        compile_run(support+play+exit_helper+cases)

    def test_native_ui_contract(self):
        root=ET.parse(ROOT/"applications/iso_loader/app.xml").getroot()
        body=root.find("body")
        names={e.get("name") for e in body.iter()}
        required={"launch-status","launch-summary","verify-boot","preview-media",
                  "message-panel","check-game","baseline","restore-profile","details",
                  "file_browser","run_iso","pages","run-panel",*(f"message-{i}" for i in range(6))}
        self.assertFalse(required-names)
        self.assertEqual(root.get("version"),"2.0.2")
        exports=(ROOT/"applications/iso_loader/modules/exports.txt").read_text()
        for e in body.iter():
            for attr in ("onclick","onselect","oncontextclick","onload","onopen","onclose","onunload"):
                callback=e.get(attr,"")
                if callback.startswith("export:"):
                    self.assertIn(callback[7:].split("(")[0],exports)
        self.assertEqual(body.get("onclose"),"export:isoLoader_Close()")
        self.assertEqual(root.find(".//*[@name='verify-boot']").get("checked"),"true")
        self.assertIsNone(root.find(".//*[@name='preview-media']").get("checked"))

if __name__=="__main__":
    unittest.main()
