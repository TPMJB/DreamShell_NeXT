/* Execute production counter/sampler/logger code with a deterministic heap. */
#include "console_shim/ds.h"
#include "memory_mock.h"
#include <assert.h>
static size_t pvr_mem_available(void) { return 6*1024*1024; }
#include "../../src/memory_stats.c"

static const char *log_root;
static int writes, warnings, open_fail;
static uint64_t clock_ms;
uint64_t timer_ms_gettime64(void) { return clock_ms; }
void ds_printf(const char *format, ...) {
    if (strstr(format,"log unavailable")) ++warnings;
}
file_t fs_open(const char *path, int flags) {
    char mapped[1024];
    assert(!strcmp(path,"/sd/DS/kui-memory.log"));
    assert(!(flags & O_TRUNC) && !(flags & O_APPEND));
    if (open_fail) { errno = EIO; return FILEHND_INVALID; }
    snprintf(mapped,sizeof(mapped),"%s/kui-memory.log",log_root);
    return open(mapped,flags,0600);
}
ssize_t fs_total(file_t file) { struct stat s; return fstat(file,&s) ? -1 : s.st_size; }
off_t fs_seek(file_t file,off_t offset,int whence) { return lseek(file,offset,whence); }
ssize_t fs_write(file_t file,const void *data,size_t size) {
    ++writes; return write(file,data,size);
}
int fs_close(file_t file) { return close(file); }

int main(int argc,char **argv) {
    assert(argc==3); log_root=argv[2];
    if (!strcmp(argv[1],"counters")) {
        memory_snapshot_t s;
        errno = EBUSY;
        assert(MemoryStatsRead(&s) && errno==EBUSY);
        assert(s.main_bytes==16*1024*1024 && s.heap_used==1536*1024);
        assert(s.unclaimed==(11*1024*1024-64*1024-4));
        assert(s.available==s.unclaimed+512*1024);
        _arch_mem_top=0x8e000000u;
        assert(MemoryStatsRead(&s) && s.main_bytes==32*1024*1024);
        assert(s.unclaimed==(27*1024*1024-64*1024-4));
        _arch_mem_top=0x8d000000u;
        memory_growing=2; int before=memory_reads;
        assert(MemoryStatsRead(&s) && memory_reads-before==3);
        memory_growing=3; assert(!MemoryStatsRead(&s) && errno==EBUSY);
        memory_info.fordblks=-1; assert(!MemoryStatsRead(&s));
        memory_info.fordblks=1; assert(!MemoryStatsRead(&s));
        memory_info.fordblks=512*1024;
        uintptr_t valid_break=memory_break;
        memory_break=_arch_mem_top-THD_KERNEL_STACK_SIZE;
        assert(!MemoryStatsRead(&s));
        memory_break=valid_break;
        memory_meter_t meter; MemoryStatsReset(&meter);
        assert(MemoryStatsSample(&meter,0,0));
        uint32_t initial=meter.lowest_available;
        memory_info.uordblks+=100; memory_info.fordblks-=100;
        before=memory_reads;
        assert(!MemoryStatsSample(&meter,4999,0) && memory_reads==before);
        assert(MemoryStatsSample(&meter,5000,0) && meter.lowest_available==initial-100);
        memory_info.uordblks-=100; memory_info.fordblks+=100;
        assert(MemoryStatsSample(&meter,5001,1));
        assert(meter.current.available==initial && meter.lowest_available==initial-100);
        memory_growing=3; assert(!MemoryStatsSample(&meter,5002,1));
        assert(meter.rejected==1 && meter.samples==3 && !writes);
    } else {
        char path[1024], contents[4096]; struct stat s;
        snprintf(path,sizeof(path),"%s/kui-memory.log",log_root);
        setenv("PATH","/cd/DS",1);
        MemoryStatsAppEvent("opened","Launch App"); assert(!writes);
        setenv("PATH","/sd/DS",1); errno=EBUSY;
        MemoryStatsAppEvent("before-load","Launch App"); assert(errno==EBUSY);
        MemoryStatsAppEvent("opened","Launch App");
        FILE *f=fopen(path,"rb"); assert(f);
        size_t n=fread(contents,1,sizeof(contents)-1,f); contents[n]=0; fclose(f);
        assert(strstr(contents,"before-load Launch App") && strstr(contents,"opened Launch App"));
        assert(strstr(contents,"main=16777216") && strstr(contents,"pvr_free=6291456"));
        char *header=strstr(contents,"K-UI RAM session");
        assert(header && !strstr(header+1,"K-UI RAM session"));
        int fd=open(path,O_WRONLY); assert(fd>=0 && !ftruncate(fd,MEMORY_LOG_LIMIT)); close(fd);
        int before=writes;
        MemoryStatsAppEvent("closed","Launch App");
        assert(writes==before && warnings==1 && !stat(path,&s) && s.st_size==MEMORY_LOG_LIMIT);
        open_fail=1; errno=EBUSY; MemoryStatsAppEvent("opened","Settings");
        assert(writes==before && warnings==1 && errno==EBUSY);
    }
    puts("ok"); return 0;
}
