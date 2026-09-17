#ifndef BOOT_TEST_MAIN_H
#define BOOT_TEST_MAIN_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <zlib.h>
#include "boot.h"
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int file_t;
#define FILEHND_INVALID -1
#define O_DIR 0x400000
#undef NAME_MAX
#define NAME_MAX 256
typedef struct { char name[256]; } dirent_t;
file_t fs_open(const char *,int);
int fs_close(file_t);
ssize_t fs_total(file_t);
ssize_t fs_read(file_t,void *,size_t);
off_t fs_seek(file_t,off_t,int);
const dirent_t *fs_readdir(file_t);
void *boot_test_alloc(size_t,size_t);
void *boot_test_malloc(size_t);
#define aligned_alloc boot_test_alloc
#define malloc boot_test_malloc
typedef int mutex_t;
#define MUTEX_INITIALIZER 0
void mutex_lock(mutex_t *);
void mutex_unlock(mutex_t *);
typedef struct { int unused; } kthread_t;
kthread_t *thd_create(bool,void *(*)(void *),void *);
int thd_join(kthread_t *,void **);
typedef struct { uint32 buttons; } cont_state_t;
typedef struct { int unused; } maple_device_t;
#define MAPLE_FUNC_CONTROLLER 1
#define CONT_A 1u
#define CONT_B 2u
#define CONT_X 4u
#define CONT_Y 8u
#define CONT_START 16u
#define CONT_DPAD_UP 32u
#define CONT_DPAD_DOWN 64u
maple_device_t *maple_enum_type(int,int);
void *maple_dev_status(maple_device_t *);
uint64 timer_ms_gettime64(void);
void arch_exec(const void *,uint32);
typedef void *pvr_ptr_t;
typedef struct { int unused; } pvr_poly_cxt_t;
typedef struct { int unused; } pvr_poly_hdr_t;
typedef struct { uint32 flags; float x,y,z,u,v; uint32 argb,oargb; } pvr_vertex_t;
#define PVR_CMD_VERTEX 1
#define PVR_CMD_VERTEX_EOL 2
#define PVR_LIST_TR_POLY 1
#define PVR_TXRFMT_ARGB1555 1
#define PVR_TXRFMT_NONTWIDDLED 2
#define PVR_FILTER_NONE 0
#define PVR_FILTER_BILINEAR 1
#define PVR_PACK_COLOR(a,r,g,b) (((uint32)((a)*255)<<24)|((uint32)((r)*255)<<16)|((uint32)((g)*255)<<8)|(uint32)((b)*255))
void pvr_poly_cxt_txr(pvr_poly_cxt_t *,int,int,int,int,void *,int);
void pvr_poly_cxt_col(pvr_poly_cxt_t *,int);
void pvr_poly_compile(pvr_poly_hdr_t *,pvr_poly_cxt_t *);
void pvr_prim(const void *,size_t);
pvr_ptr_t pvr_mem_malloc(size_t);
size_t bfont_draw_ex(void *,uint32,uint32,uint32,uint8,bool,uint32,bool,bool);
void pvr_txr_load(const void *,pvr_ptr_t,size_t);
extern const char title[];
extern volatile int start_pressed;
extern uint32 boot_detect_ms;
uint32 boot_detect_devices(bool);
#endif
