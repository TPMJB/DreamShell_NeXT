/* Minimal mock ABI, checked against KOS a78fa2a2761360d96b66ba913e447812d5f2b889. */
#ifndef GD_TEST_CDROM_H
#define GD_TEST_CDROM_H
#include <stdint.h>
#include <stddef.h>
#define ERR_OK 0
#define ERR_NO_DISC 1
#define ERR_DISC_CHG 2
#define ERR_SYS 3
#define ERR_NO_ACTIVE 5
#define ERR_TIMEOUT 6
#define CD_GDROM 128
#define TOC_LBA(n) ((n)&0xffffff)
#define TOC_CTRL(n) (((n)>>28)&15)
#define TOC_TRACK(n) (((n)>>16)&255)
enum {CD_STATUS_BUSY,CD_STATUS_PAUSED,CD_STATUS_STANDBY,CD_STATUS_PLAYING,
    CD_STATUS_SEEKING,CD_STATUS_SCANNING,CD_STATUS_OPEN,CD_STATUS_NO_DISC,CD_STATUS_RETRY};
typedef enum {CD_CMD_PIOREAD=16,CD_CMD_GETTOC2=19,CD_CMD_INIT=24,CD_CMD_STOP=33} cd_cmd_code_t;
typedef enum {CDROM_READ_DEFAULT=-1} cd_read_sec_part_t;
typedef enum {CD_AREA_LOW,CD_AREA_HIGH} cd_area_t;
typedef struct {uint32_t entry[99],first,last,leadout_sector;} cd_toc_t;
typedef struct {uint32_t start_sec;size_t num_sec;void *buffer;uint32_t is_test;} cd_read_params_t;
typedef struct {cd_area_t area;cd_toc_t *buffer;} cd_cmd_toc_params_t;
int cdrom_exec_cmd_timed(cd_cmd_code_t,void*,uint32_t);
int cdrom_change_datatype(cd_read_sec_part_t,int,int);
int cdrom_get_status(int*,int*);
uint32_t cdrom_locate_data_track(cd_toc_t*);
#endif
