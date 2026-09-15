#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#define O_META 0x100000
#define CMD_OK 0
#define CMD_ERROR 1
#define FILEHND_INVALID (-1)
typedef int file_t;
static uint8_t source[2048],dest[2048];
static int source_len,pos,opens,read_fault,write_fault,close_fault,sync_fault;
static file_t fs_open(const char *path,int flags) {
    if(flags&O_WRONLY) { ++opens; memset(dest,0,sizeof(dest)); return 2; }
    (void)path;pos=0;return 1;
}
static ssize_t fs_total(file_t fd) {(void)fd;return source_len;}
static off_t fs_seek(file_t fd,off_t offset,int whence) {(void)fd;(void)whence;pos=offset;return offset;}
static ssize_t fs_read(file_t fd,void *data,size_t n) {
    (void)fd;
    if(read_fault && pos>=17) return read_fault==1?-1:0;
    if(n>17)n=17;
    if(n>(size_t)(source_len-pos)) n=source_len-pos;
    memcpy(data,source+pos,n);pos+=n;return n;
}
static ssize_t fs_write(file_t fd,const void *data,size_t n) {
    (void)fd;if(write_fault) return n-1;
    memcpy(dest,data,n);return n;
}
static int fs_complete(file_t fd,ssize_t *count) {(void)fd;(void)count;return sync_fault;}
static int fs_close(file_t fd) {return close_fault==fd?-1:0;}
#include "../../applications/vmu_manager/modules/ui_logic.h"
#include "../../applications/vmu_manager/modules/transfer.h"
static void reset(void) {
    for(int i=0;i<2048;++i) source[i]=(uint8_t)i;
    memset(dest,0x55,sizeof(dest));source_len=1024;pos=opens=read_fault=write_fault=close_fault=sync_fault=0;
}
int main(void) {
    reset();assert(vmu_transfer_file("/sd/save.vms","/vmu/A1/SAVE",false)==CMD_OK);assert(opens==1);assert(!memcmp(source,dest,1024));
    for(int failure=1;failure<=2;++failure) {reset();read_fault=failure;assert(vmu_transfer_file("/sd/save.vms","/vmu/A1/SAVE",false)==CMD_ERROR);assert(!opens && dest[0]==0x55);}
    reset();close_fault=1;assert(vmu_transfer_file("/sd/save.vms","/vmu/A1/SAVE",false)==CMD_ERROR);assert(!opens);
    reset();write_fault=1;assert(vmu_transfer_file("/sd/save.vms","/vmu/A1/SAVE",false)==CMD_ERROR);
    reset();close_fault=2;assert(vmu_transfer_file("/sd/save.vms","/vmu/A1/SAVE",false)==CMD_ERROR);
    reset();sync_fault=1;assert(vmu_transfer_file("/vmu/A1/SAVE","/sd/save.vms",false)==CMD_ERROR);
    reset();source_len=1056;assert(vmu_transfer_file("/sd/save.dci","/vmu/A1/SAVE",true)==CMD_OK);assert(dest[0]==source[35] && dest[3]==source[32]);
    reset();source_len=1055;assert(vmu_transfer_file("/sd/save.dci","/vmu/A1/SAVE",true)==CMD_ERROR && !opens);
    reset();assert(vmu_transfer_file("/vmu/A1/SAVE","/cd/save.vms",false)==CMD_ERROR && !opens);
    assert(vmu_ui_read_only("/vmd/SAVE") && !vmu_ui_read_only("/sd/vmd/SAVE"));
    assert(vmu_ui_folder_name("Sonic saves") && !vmu_ui_folder_name("../Sonic") && !vmu_ui_folder_name("..") && !vmu_ui_folder_name("Sonic/Save"));
    assert(!vmu_ui_confirm_ready(false,1,false,false));assert(!vmu_ui_confirm_ready(false,0,true,false));assert(!vmu_ui_confirm_ready(false,0,false,true));
    assert(vmu_ui_confirm_ready(false,0,false,false));assert(vmu_ui_confirm_ready(true,1,false,false));
    puts("VMU transfer and confirmation checks passed");
}
