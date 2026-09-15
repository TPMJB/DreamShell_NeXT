#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#undef NAME_MAX
#define NAME_MAX 256
#define FILEHND_INVALID (-1)
#define VMUFS_OVERWRITE 1
#define VMUFS_VMUGAME 2
#define VMUFS_NOCOPY 4
typedef int file_t;
typedef struct { int id; } maple_device_t;
typedef struct { uint8_t filetype,copyprotect; char filename[12]; uint16_t filesize; } vmu_dir_t;
static maple_device_t source={0},target={1};
static vmu_dir_t directories[2][8];
static int counts[2],free_blocks_count,read_calls,write_calls,read_fault,write_fault,close_fault,sync_fault,stat_fault,cancel_after;
static int native_flags, removed;
static struct { char path[256]; bool exists; int size; unsigned char data[2048]; } files[8];
static int fs_stat(const char *path,struct stat *st,int flags) {
    (void)st;(void)flags;
    if(stat_fault) { errno=EIO; return -1; }
    for(int i=0;i<8;++i) if(files[i].exists && !strcasecmp(files[i].path,path)) return 0;
    errno=ENOENT;return -1;
}
static file_t fs_open(const char *path,int flags) {
    assert((flags&(O_CREAT|O_EXCL))==(O_CREAT|O_EXCL));
    assert(!(flags&O_TRUNC));
    for(int i=0;i<8;++i) if(files[i].exists && !strcasecmp(files[i].path,path)) { errno=EEXIST;return -1; }
    for(int i=0;i<8;++i) if(!files[i].exists) {
        snprintf(files[i].path,sizeof(files[i].path),"%s",path);files[i].exists=true;files[i].size=0;return i;
    }
    errno=ENOSPC;return -1;
}
static ssize_t fs_write(file_t fd,const void *data,size_t size) {
    ++write_calls;
    if(write_fault==write_calls) size/=2;
    assert(size<=sizeof(files[fd].data));
    memcpy(files[fd].data,data,size);files[fd].size=(int)size;return (ssize_t)size;
}
static int fs_close(file_t fd) { (void)fd;return close_fault?-1:0; }
static int fs_complete(file_t fd,ssize_t *done) { (void)fd;(void)done;return sync_fault?-1:0; }
static int fs_unlink(const char *path) {
    for(int i=0;i<8;++i) if(files[i].exists && !strcmp(files[i].path,path)) { files[i].exists=false;++removed;return 0; }
    return -1;
}
static int vmufs_readdir(maple_device_t *dev,vmu_dir_t **out,int *count) {
    *count=counts[dev->id];*out=malloc((size_t)*count*sizeof(**out));
    if(*count) memcpy(*out,directories[dev->id],(size_t)*count*sizeof(**out));
    return 0;
}
static int vmufs_free_blocks(maple_device_t *dev) { (void)dev;return free_blocks_count; }
static int vmufs_read_dirent(maple_device_t *dev,vmu_dir_t *entry,void **out,int *size) {
    assert(dev==&source);++read_calls;
    *size=entry->filesize*512;*out=malloc((size_t)*size);memset(*out,entry->filename[0],(size_t)*size);
    if(read_fault==read_calls) --*size; /* An incomplete read must never reach a destination. */
    return 0;
}
static int vmufs_write(maple_device_t *dev,const char *name,void *data,int size,int flags) {
    (void)data;
    assert(dev==&target && !(flags&VMUFS_OVERWRITE));
    for(int i=0;i<counts[1];++i) if(!strncmp(directories[1][i].filename,name,12)) return -2;
    native_flags=flags;++write_calls;
    if(write_fault==write_calls) return -7;
    vmu_dir_t *entry=&directories[1][counts[1]++];
    entry->filetype=0x33;entry->filesize=size/512;memcpy(entry->filename,name,strlen(name));return 0;
}
#include "../../applications/vmu_manager/modules/ui_logic.h"
#include "../../applications/vmu_manager/modules/bulk.h"
static bool progress(int done,int total,const char *name,void *arg) {
    (void)total;(void)name;(void)arg;return cancel_after<0 || done<cancel_after;
}
static void reset(void) {
    memset(directories,0,sizeof(directories));memset(files,0,sizeof(files));
    counts[0]=2;counts[1]=0;free_blocks_count=200;cancel_after=-1;
    read_calls=write_calls=read_fault=write_fault=close_fault=sync_fault=stat_fault=native_flags=removed=0;
    directories[0][0].filetype=directories[0][1].filetype=0x33;
    directories[0][0].filesize=1;directories[0][1].filesize=2;
    memcpy(directories[0][0].filename,"SHORT",5);memcpy(directories[0][1].filename,"TWELVECHARS!",12);
}
static void existing(void) {
    strcpy(files[0].path,"/sd/saves/SHORT.vms");files[0].exists=true;files[0].size=123;files[0].data[0]=0x7a;
}
int main(void) {
    vmu_bulk_plan_t p;char name[13];
    reset();assert(vmu_bulk_prepare(&source,NULL,"/sd/saves",&p));
    vmu_bulk_copy(&source,NULL,"/sd/saves",&p,progress,NULL);
    assert(!p.error && p.copied==2 && p.skipped==0);
    assert(!strcmp(files[0].path,"/sd/saves/SHORT.vms") && files[0].size==512 && files[0].data[0]=='S');
    assert(!strcmp(files[1].path,"/sd/saves/TWELVECHARS!.vms") && files[1].size==1024);
    reset();existing();assert(vmu_bulk_prepare(&source,NULL,"/sd/saves",&p) && p.pending==1);
    vmu_bulk_copy(&source,NULL,"/sd/saves",&p,progress,NULL);
    assert(p.copied==1 && p.skipped==1 && files[0].size==123 && files[0].data[0]==0x7a);
    reset();assert(vmu_bulk_prepare(&source,NULL,"/sd/saves",&p));existing();
    vmu_bulk_copy(&source,NULL,"/sd/saves",&p,progress,NULL);
    assert(!p.error && p.copied==1 && p.skipped==1 && files[0].data[0]==0x7a); /* Appeared after scan. */
    reset();read_fault=2;assert(vmu_bulk_prepare(&source,NULL,"/sd/saves",&p));
    vmu_bulk_copy(&source,NULL,"/sd/saves",&p,progress,NULL);
    assert(p.error==VMU_BULK_READ && p.copied==1 && write_calls==1 && !files[1].exists);
    for(int fault=0;fault<3;++fault) {
        reset();write_fault=fault==0?1:0;close_fault=fault==1;sync_fault=fault==2;
        assert(vmu_bulk_prepare(&source,NULL,"/sd/saves",&p));vmu_bulk_copy(&source,NULL,"/sd/saves",&p,progress,NULL);
        assert(p.error==VMU_BULK_WRITE && !p.copied && removed==1 && !files[0].exists && read_calls==1);
    }
    reset();stat_fault=1;assert(!vmu_bulk_prepare(&source,NULL,"/sd/saves",&p) && !write_calls);
    reset();assert(!vmu_bulk_prepare(&source,NULL,"/cd",&p));assert(!vmu_bulk_prepare(&source,NULL,"/pc",&p));
    assert(!vmu_bulk_prepare(&source,&source,"/vmu/A1",&p));
    reset();free_blocks_count=2;assert(!vmu_bulk_prepare(&source,&target,"/vmu/B1",&p) && p.error==VMU_BULK_SPACE && !write_calls);
    reset();directories[1][0]=directories[0][0];counts[1]=1;free_blocks_count=2;
    directories[0][1].filetype=0xcc;directories[0][1].copyprotect=0xff;
    assert(vmu_bulk_prepare(&source,&target,"/vmu/B1",&p));vmu_bulk_copy(&source,&target,"/vmu/B1",&p,progress,NULL);
    assert(!p.error && p.copied==1 && p.skipped==1 && native_flags==(VMUFS_VMUGAME|VMUFS_NOCOPY));
    reset();assert(vmu_bulk_prepare(&source,&target,"/vmu/B1",&p));
    directories[1][0]=directories[0][0];counts[1]=1;
    vmu_bulk_copy(&source,&target,"/vmu/B1",&p,progress,NULL);
    assert(!p.error && p.copied==1 && p.skipped==1 && write_calls==1);
    reset();cancel_after=0;assert(vmu_bulk_prepare(&source,NULL,"/sd/saves",&p));
    vmu_bulk_copy(&source,NULL,"/sd/saves",&p,progress,NULL);
    assert(p.error==VMU_BULK_CANCELLED && !read_calls && !write_calls);
    reset();cancel_after=1;assert(vmu_bulk_prepare(&source,NULL,"/sd/saves",&p));
    vmu_bulk_copy(&source,NULL,"/sd/saves",&p,progress,NULL);
    assert(p.error==VMU_BULK_CANCELLED && p.copied==1 && write_calls==1 && files[0].exists);
    reset();counts[0]=0;assert(vmu_bulk_prepare(&source,NULL,"/sd/saves",&p) && !p.count && !p.pending);
    vmu_ui_import_name(name,"SHORT.vms",false);assert(!strcmp(name,"SHORT"));
    vmu_ui_import_name(name,"ICONDATA_VMS.vms",false);assert(!strcmp(name,"ICONDATA_VMS"));
    vmu_ui_import_name(name,"TWELVECHARS!.VMS",false);assert(!strcmp(name,"TWELVECHARS!"));
    vmu_ui_import_name(name,"FILE.vms",true);assert(!strcmp(name,"FILE.vms"));
    assert(vmu_ui_confirm_ready(false,0,false,false));
    puts("VMU bulk copy checks passed");
}
