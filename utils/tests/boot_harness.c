/* Exercise the production loader and menu without executing a console binary. */
#include "main.h"
#undef malloc
#undef aligned_alloc
#include <errno.h>
#include <limits.h>
#include <math.h>

static char fixture_dir[1024];
static struct { const char *virtual_path; const char *file; } maps[20];
static int map_count;
static const char *devices[16];
static int device_count, root_index;
static FILE *handles[32];
static int open_count, close_count;
static ssize_t size_override=-2, read_limit=-1, fail_at=-1;
static size_t max_read=SIZE_MAX;
static bool close_fail, no_memory, no_thread;
static int fail_allocation=-1, allocation_count;
static uint64 clock_ms;
static cont_state_t input;
static int executions, joins, detections;
static void *(*thread_fn)(void *);
static void *thread_arg;
static kthread_t fake_thread;
const char title[]="DreamShell NeXT boot v3.1";
volatile int start_pressed;
uint32 boot_detect_ms=17;

void *boot_test_alloc(size_t align,size_t size) {
    if(no_memory || allocation_count++==fail_allocation) return NULL;
    assert(size%align==0);
    return aligned_alloc(align,size);
}
void *boot_test_malloc(size_t size) {
    if(no_memory || allocation_count++==fail_allocation) return NULL;
    return malloc(size);
}
static const char *translate(const char *path, char *out) {
    for(int i=0;i<map_count;++i) if(!strcmp(path,maps[i].virtual_path)) {
        snprintf(out,2048,"%s/%s",fixture_dir,maps[i].file);
        return out;
    }
    return path;
}
file_t fs_open(const char *path,int flags) {
    if(!strcmp(path,"/") && (flags&O_DIR)) { root_index=0; return 99; }
    char translated[2048];
    FILE *f=fopen(translate(path,translated),"rb");
    if(!f) return FILEHND_INVALID;
    for(int i=0;i<32;++i) if(!handles[i]) {
        handles[i]=f; ++open_count; return i;
    }
    assert(0); return -1;
}
int fs_close(file_t fd) {
    if(fd==99) return 0;
    assert(fd>=0 && fd<32 && handles[fd]);
    fclose(handles[fd]); handles[fd]=NULL; ++close_count;
    return close_fail ? -1 : 0;
}
ssize_t fs_total(file_t fd) {
    if(size_override!=-2) return size_override;
    long pos=ftell(handles[fd]);
    fseek(handles[fd],0,SEEK_END); long size=ftell(handles[fd]);
    fseek(handles[fd],pos,SEEK_SET);
    return size;
}
ssize_t fs_read(file_t fd,void *out,size_t size) {
    long pos=ftell(handles[fd]);
    if(fail_at>=0 && pos>=fail_at) return -1;
    if(read_limit>=0) {
        if(pos>=read_limit) return 0;
        if(size>(size_t)(read_limit-pos)) size=(size_t)(read_limit-pos);
    }
    if(size>max_read) size=max_read;
    clock_ms++;
    return (ssize_t)fread(out,1,size,handles[fd]);
}
off_t fs_seek(file_t fd,off_t offset,int origin) {
    return fseek(handles[fd],offset,origin) ? -1 : ftell(handles[fd]);
}
const dirent_t *fs_readdir(file_t fd) {
    static dirent_t entry;
    assert(fd==99);
    if(root_index==device_count) return NULL;
    snprintf(entry.name,sizeof(entry.name),"%s",devices[root_index++]);
    return &entry;
}
int RootDeviceIsSupported(const char *name) {
    return !strncmp(name,"sd",2) || !strncmp(name,"ide",3) || !strcmp(name,"cd");
}
void mutex_lock(mutex_t *m) { assert(!*m); *m=1; }
void mutex_unlock(mutex_t *m) { assert(*m); *m=0; }
kthread_t *thd_create(bool detached,void *(*fn)(void *),void *arg) {
    assert(!detached);
    if(no_thread) return NULL;
    assert(!thread_fn); thread_fn=fn; thread_arg=arg; return &fake_thread;
}
int thd_join(kthread_t *thread,void **value) {
    (void)value; assert(thread==&fake_thread && !thread_fn); ++joins; return 0;
}
maple_device_t *maple_enum_type(int port,int type) {
    (void)port; (void)type; static maple_device_t device; return &device;
}
void *maple_dev_status(maple_device_t *device) { (void)device; return &input; }
uint64 timer_ms_gettime64(void) { return clock_ms; }
void arch_exec(const void *data,uint32 size) { assert(data && size); ++executions; }
uint32 boot_detect_devices(bool rescan) { assert(rescan); ++detections; return 23; }
void pvr_poly_cxt_txr(pvr_poly_cxt_t *a,int b,int c,int d,int e,void *f,int g) {
    a->unused=0;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;
}
void pvr_poly_cxt_col(pvr_poly_cxt_t *a,int b) { a->unused=0;(void)b; }
void pvr_poly_compile(pvr_poly_hdr_t *a,pvr_poly_cxt_t *b) { a->unused=b->unused; }
void pvr_prim(const void *p,size_t size) {
    if(size==sizeof(pvr_vertex_t)) {
        const pvr_vertex_t *v=p;
        assert(isfinite(v->x) && isfinite(v->y));
        assert(v->x>=0 && v->x<=640 && v->y>=0 && v->y<=480);
    }
}
pvr_ptr_t pvr_mem_malloc(size_t size) { return malloc(size); }
void bfont_draw(void *a,int b,int c,int d) { (void)a;(void)b;(void)c;(void)d; }

#include "../../firmware/bootloader/src/menu.c"

static void reset_faults(void) {
    assert(open_count==close_count);
    size_override=-2; read_limit=-1; fail_at=-1; max_read=SIZE_MAX;
    close_fail=no_memory=no_thread=false; fail_allocation=-1; allocation_count=0;
}
static boot_image_t load_fixture(const char *name,boot_format_t format) {
    char path[2048]; snprintf(path,sizeof(path),"%s/%s",fixture_dir,name);
    boot_image_t image;
    boot_load(path,format,NULL,NULL,&image);
    assert(open_count==close_count);
    if(image.error) assert(!image.data);
    return image;
}
static bool cancel_progress(boot_stage_t stage,uint32 count,uint32 size,void *arg) {
    (void)stage;(void)size;
    return count<(uint32)(uintptr_t)arg;
}
static void loader_tests(void) {
    reset_faults();
    boot_image_t image=load_fixture("raw.bin",BOOT_RAW);
    assert(image.error==BOOT_OK && image.size==65540 && image.count==image.size);
    for(unsigned i=0;i<image.size;++i) assert(image.data[i]==(uint8)(i*7));
    free(image.data);
    max_read=137;
    image=load_fixture("raw.bin",BOOT_RAW); assert(!image.error); free(image.data);
    reset_faults(); read_limit=513;
    image=load_fixture("raw.bin",BOOT_RAW); assert(image.error==BOOT_SHORT_READ && image.count==513);
    reset_faults(); fail_at=32768;
    image=load_fixture("raw.bin",BOOT_RAW); assert(image.error==BOOT_READ);
    reset_faults(); size_override=65536;
    image=load_fixture("raw.bin",BOOT_RAW); assert(image.error==BOOT_CHANGED);
    reset_faults(); close_fail=true;
    image=load_fixture("raw.bin",BOOT_RAW); assert(image.error==BOOT_CLOSE);
    reset_faults(); no_memory=true;
    image=load_fixture("raw.bin",BOOT_RAW); assert(image.error==BOOT_MEMORY);
    reset_faults();
    const ssize_t bad_sizes[]={-1,0,1,3,65539,BOOT_CORE_MAX+4};
    for(unsigned i=0;i<sizeof(bad_sizes)/sizeof(*bad_sizes);++i) {
        size_override=bad_sizes[i];
        image=load_fixture("raw.bin",BOOT_RAW); assert(image.error==BOOT_SIZE);
    }
    reset_faults();
    image=load_fixture("missing.bin",BOOT_RAW); assert(image.error==BOOT_OPEN);
    char path[2048]; snprintf(path,sizeof(path),"%s/raw.bin",fixture_dir);
    assert(boot_load(path,BOOT_RAW,cancel_progress,(void *)(uintptr_t)32768,&image)==BOOT_CANCELLED);
    assert(!image.data && image.count==32768 && open_count==close_count);
    image=load_fixture("good.gz",BOOT_GZIP);
    assert(image.error==BOOT_OK && image.count==65540); free(image.data);
    image=load_fixture("bad-crc.gz",BOOT_GZIP); assert(image.error==BOOT_GZIP_ERROR);
    image=load_fixture("short.gz",BOOT_GZIP); assert(image.error!=BOOT_OK);
    image=load_fixture("overflow.gz",BOOT_GZIP); assert(image.error!=BOOT_OK);
    image=load_fixture("not-gzip.bin",BOOT_GZIP); assert(image.error==BOOT_GZIP_ERROR);
    image=load_fixture("concat.gz",BOOT_GZIP); assert(image.error!=BOOT_OK);
    image=load_fixture("scrambled.bin",BOOT_SCRAMBLED); assert(!image.error);
    assert(image.size==2097192);
    for(unsigned i=0;i<image.size;++i) assert(image.data[i]==(uint8)(i*13+i/97));
    free(image.data);
    for(int failure=1;failure<=2;++failure) {
        reset_faults(); fail_allocation=failure;
        image=load_fixture("scrambled.bin",BOOT_SCRAMBLED);
        assert(image.error==(failure==1 ? BOOT_MEMORY : BOOT_DESCRAMBLE));
    }
    reset_faults();
}

static void config_tests(void) {
    boot_config_t cfg;
    const char *good="# boot\r\nboot_order=sd,ide,cd\ncore_path=/sd/DS/DS_CORE.BIN\n"
        "fallback_path=/ide/DS/DS_CORE.BIN\nboot_delay=3\nautoboot=1\ndiagnostics=1\n";
    assert(!boot_config_parse(good,strlen(good),&cfg));
    assert(cfg.delay_seconds==3 && cfg.diagnostics && cfg.autoboot);
    assert(boot_device_rank(&cfg,"sd2")==0 && boot_device_rank(&cfg,"ide")==1);
    assert(boot_device_rank(&cfg,"pc")==5);
    const char *bad[]={"autoboot=yes","boot_delay=-1","boot_delay=31","boot_order=sd,sd",
        "boot_order=sd,","boot_order=usb","core_path=/sd/../DS_CORE.BIN",
        "core_path=/sd//DS_CORE.BIN","core_path=/vmu/a1/DS_CORE.BIN","thing=1"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        assert(boot_config_parse(bad[i],strlen(bad[i]),&cfg)==1);
        assert(cfg.autoboot && !cfg.delay_seconds && !strcmp(cfg.order,"auto"));
    }
    assert(boot_config_parse("autoboot=0\nbad\n",15,&cfg)==2 && cfg.autoboot);
    const char nul[]={'a',0,'b'}; assert(boot_config_parse(nul,sizeof(nul),&cfg));
    char large[4097]; memset(large,'#',sizeof(large));
    assert(boot_config_parse(large,sizeof(large),&cfg));
    assert(boot_path_format("/sd/1ds_core.bin")==BOOT_SCRAMBLED);
    assert(boot_path_format("/sd/DS/ZDS_CORE.BIN")==BOOT_GZIP);
    cfg.delay_seconds=3;
    boot_countdown_t timer;
    boot_countdown_start(&timer,1000,&cfg,false,true);
    assert(!boot_countdown_due(&timer,3999,false));
    assert(boot_countdown_due(&timer,4000,false));
    assert(!boot_countdown_due(&timer,5000,false));
    boot_countdown_start(&timer,1000,&cfg,true,true);
    assert(!boot_countdown_due(&timer,9000,false));
    boot_countdown_start(&timer,1000,&cfg,false,true);
    assert(!boot_countdown_due(&timer,1001,true));
    assert(!boot_countdown_due(&timer,9000,false));
}

static void add_map(const char *path,const char *file) {
    maps[map_count].virtual_path=path; maps[map_count++].file=file;
}
static void ui_reset(void) {
    assert(!worker && !thread_fn);
    free(txr_font); txr_font=NULL;
    reset_faults(); map_count=0; device_count=0; input.buttons=last_buttons=0;
    start_pressed=0; executions=joins=detections=0; clock_ms=1000;
    memset(&job,0,sizeof(job));
}
static void press(uint32 value) {
    input.buttons=value; menu_update();
    input.buttons=0; menu_update();
}
static void run_worker(void) {
    assert(thread_fn);
    void *(*fn)(void *)=thread_fn; void *arg=thread_arg;
    thread_fn=NULL; fn(arg);
}
static void ui_tests(void) {
    ui_reset(); devices[device_count++]="sd";
    add_map("/sd/DS/DS_CORE.BIN","raw.bin");
    menu_init(); menu_autoboot(); assert(executions==1 && !worker);

    ui_reset();
    devices[device_count++]="sd";
    add_map("/sd/DS/DS_CORE.BIN","raw.bin");
    start_pressed=1;
    menu_init(); menu_autoboot(); menu_graphics_init();
    assert(!executions && !countdown.armed);
    menu_frame();
    clock_ms+=60000; menu_update(); assert(!executions && !worker);
    press(CONT_B); assert(!worker && !executions);
    press(CONT_A); assert(worker); run_worker(); menu_update();
    assert(executions==1 && joins==1);

    ui_reset(); devices[device_count++]="sd";
    add_map("/sd/DS/DS_CORE.BIN","raw.bin");
    menu_init(); read_limit=37; menu_autoboot(); menu_graphics_init();
    assert(!executions && !countdown.armed && strstr(job.message,"incomplete"));
    assert(job.count==37); menu_frame(); clock_ms+=60000; menu_update(); assert(!worker);
    reset_faults();
    press(CONT_A); assert(worker); press(CONT_B); run_worker(); menu_update();
    assert(!executions && job.image.error==BOOT_CANCELLED);
    press(CONT_A); run_worker();
    /* Even cancellation immediately before the handoff must prevent execution. */
    input.buttons=CONT_B; menu_update(); input.buttons=0; menu_update();
    assert(!executions && job.image.error==BOOT_CANCELLED);
    no_thread=true; press(CONT_A); assert(!worker && strstr(job.message,"Cannot start"));
    no_thread=false;
    fail_at=32768;
    press(CONT_A); run_worker(); menu_update();
    assert(!executions && job.image.error==BOOT_READ && !countdown.armed);
    reset_faults();
    press(CONT_A); run_worker(); menu_update(); assert(executions==1);

    ui_reset(); menu_init(); menu_graphics_init();
    press(CONT_A); menu_frame(); assert(!worker && !executions);
    devices[device_count++]="sd"; add_map("/sd/DS/DS_CORE.BIN","raw.bin");
    press(CONT_X); assert(worker); run_worker(); menu_update();
    assert(inventory.count==1 && !countdown.armed && detections==1);
    press(CONT_A); run_worker(); menu_update(); assert(executions==1);

    ui_reset(); devices[device_count++]="ide"; devices[device_count++]="sd";
    add_map("/ide/DS/DS_CORE.BIN","raw.bin");
    add_map("/sd/DS/DS_CORE.BIN","raw.bin");
    add_map("/sd/DS/boot.cfg","boot.cfg");
    menu_init();
    assert(!strcmp(inventory.items[0].path,"/sd/DS/DS_CORE.BIN"));
    assert(countdown.armed); menu_graphics_init();
    press(CONT_DPAD_DOWN); clock_ms+=10000; menu_update();
    assert(!worker && !countdown.armed && inventory.selected==1);
    press(CONT_Y); menu_frame(); assert(details);

    ui_reset(); devices[device_count++]="sd";
    add_map("/sd/DS/DS_CORE.BIN","raw.bin");
    add_map("/sd/DS/boot.cfg","bad.cfg");
    menu_init(); menu_autoboot(); assert(!executions && inventory.hold);
    assert(strstr(job.message,"boot.cfg error"));
    /* Controller edges: releasing/changing B while A is held cannot reboot. */
    menu_graphics_init(); input.buttons=CONT_A|CONT_B; menu_update();
    input.buttons=CONT_A; menu_update(); assert(!worker);
    input.buttons=0; menu_update();

    ui_reset(); devices[device_count++]="sd"; devices[device_count++]="ide";
    add_map("/sd/DS/DS_CORE.BIN","raw.bin");
    add_map("/ide/DS/DS_CORE.BIN","raw.bin");
    add_map("/sd/DS/boot.cfg","fallback.cfg");
    menu_init(); assert(!inventory.hold);
    assert(!strcmp(inventory.items[0].path,"/ide/DS/DS_CORE.BIN"));
    menu_autoboot(); assert(executions==1);

    ui_reset(); devices[device_count++]="sd";
    add_map("/sd/DS/DS_CORE.BIN","raw.bin");
    add_map("/sd/DS/boot.cfg","missing.cfg");
    menu_init(); menu_autoboot();
    assert(inventory.hold && !executions && inventory.count==1);

    ui_reset(); devices[device_count++]="sd";
    add_map("/sd/DS/DS_CORE.BIN","raw.bin");
    menu_init();
    /* Start caught after discovery still cancels a pending autoboot. */
    start_pressed=1; menu_graphics_init(); menu_update(); assert(!worker && !countdown.armed);
    for(int i=0;i<64;++i) {
        snprintf(inventory.items[i].path,BOOT_PATH_MAX,"/sd/DS/CORE_%02d.BIN",i);
        memset(inventory.items[i].label,'X',79); inventory.items[i].label[79]=0;
    }
    inventory.count=64; inventory.selected=63; menu_frame();
    ui_reset();
}
int main(int argc,char **argv) {
    assert(argc==2); snprintf(fixture_dir,sizeof(fixture_dir),"%s",argv[1]);
    loader_tests(); config_tests(); ui_tests();
    puts("boot loader, configuration, recovery and input tests passed");
    return 0;
}
