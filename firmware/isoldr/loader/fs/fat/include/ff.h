/* DreamShell ISO Loader: shared FatFs R0.16 types and local configuration. */
#ifndef DS_ISOLDR_FF_H
#define DS_ISOLDR_FF_H
#include "ffconf.h"
#include "../../../../../../lib/fatfs/fatfs/src/ff.h"
FRESULT f_read_async(FIL *fp, void *buff, UINT btr);
FRESULT f_pre_read(FIL *fp, DWORD btr);
FRESULT f_poll(FIL *fp, UINT *bp);
FRESULT f_abort(FIL *fp);
LBA_t ds_fat_file_lba(const FIL *fp);
#endif
