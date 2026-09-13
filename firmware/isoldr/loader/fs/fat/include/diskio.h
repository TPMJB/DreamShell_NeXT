/* DreamShell device selection and async extensions to the shared disk API. */
#ifndef DS_ISOLDR_DISKIO_H
#define DS_ISOLDR_DISKIO_H
#include "ff.h"
#include "../../../../../../lib/fatfs/fatfs/src/diskio.h"
#if defined(DEV_TYPE_IDE) && defined(DEV_TYPE_SD)
enum {
	DISK_DRV_IDE = 0,
	DISK_DRV_SD_SCIF = 1,
	DISK_DRV_SD_SCI = 2
};
#elif defined(DEV_TYPE_IDE)
enum {
	DISK_DRV_IDE = 0
};
#elif defined(DEV_TYPE_SD)
enum {
	DISK_DRV_SD_SCIF = 0,
	DISK_DRV_SD_SCI  = 1
};
#endif


#define _USE_IOCTL 1
typedef unsigned char uchar;
typedef unsigned long ulong;
DRESULT disk_read_part(BYTE, BYTE *, LBA_t, UINT);
DRESULT disk_read_async(BYTE, BYTE *, LBA_t, UINT);
DRESULT disk_pre_read(BYTE, LBA_t, UINT);
int disk_poll(BYTE);
DRESULT disk_abort(BYTE);
#endif
