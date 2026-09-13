/*
 * Copyright (C) 2014, ChaN, all right reserved.
 * Copyright (C) 2014-2023 SWAT <http://www.dc-swat.ru>
 *
 * The FatFs module is a free software and there is NO WARRANTY.
 * No restriction on use. You can use, modify and redistribute it for
 * personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
 * Redistributions of source code must retain the above copyright notice.
 */
/* DreamShell ISO Loader FatFs integration.
 * Upstream parser/writer: ChaN R0.16. Transfer extensions adapted from
 * DreamShell's R0.10b port, (c) 2011-2026 SWAT, under the original license.
 */
#include "ff.h"
#include "diskio.h"
#ifdef DEV_TYPE_IDE
#include "fs.h"
#include <arch/cache.h>
#include <mmu.h>
#endif
/* Keep the upstream engine shared with the core. The specialized read path
 * below preserves DreamShell's contiguous DMA and asynchronous interfaces. */
#define f_read ds_ff_reference_read
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../../../../../lib/fatfs/fatfs/src/ff.c"
#pragma GCC diagnostic pop
#undef f_read

LBA_t ds_fat_file_lba(const FIL *fp) {
    return clst2sect(fp->obj.fs, fp->obj.sclust);
}

static
DWORD contiguous_sect(
	FIL* fp		/* Pointer to the file object */
)
{
	DWORD csect, ncl, cl, *tbl;
	csect = (fp->fptr / SS(fp->obj.fs)) & (fp->obj.fs->csize - 1);

	if (!fp->cltbl) {
		return fp->obj.fs->csize - csect;
	}

	tbl = fp->cltbl + 1;	/* Top of CLMT */

	cl = fp->fptr / SS(fp->obj.fs) / fp->obj.fs->csize;
	for (;;) {
		ncl = *tbl++;		/* Number of clusters in the fragment */
		if (!ncl) {
			return 0;		/* End of table? (error) */
		}
		if (cl < ncl) {
			break;
		}
		cl -= ncl;
		tbl++;
	}

	ncl = ncl - cl;
	if (ncl == 0) {
		return 0;
	}
	return (ncl * fp->obj.fs->csize) - csect;
}


static int f_fragmented(FIL *fp) {
	if (!fp->cltbl || fp->cltbl[0] > 4) {
		return 1;
	}
	return 0;
}

FRESULT f_read (
	FIL* fp, 		/* Pointer to the file object */
	void* buff,		/* Pointer to data buffer */
	UINT btr,		/* Number of bytes to read */
	UINT* br		/* Pointer to number of bytes read */
)
{
	FRESULT res;
	FATFS *checked_fs;
	DWORD clst;
	LBA_t sect;
	FSIZE_t remain;
	UINT rcnt = 0, cc, cs;
	UINT csect;
	BYTE *rbuff = (BYTE*)buff;


	*br = 0;	/* Clear read byte counter */

	res = validate(&fp->obj, &checked_fs);							/* Check validity */
	if (res != FR_OK) LEAVE_FF(fp->obj.fs, res);
	if (fp->err)								/* Check error */
		LEAVE_FF(fp->obj.fs, (FRESULT)fp->err);
	if (!(fp->flag & FA_READ)) 					/* Check access mode */
		LEAVE_FF(fp->obj.fs, FR_DENIED);
	remain = fp->obj.objsize - fp->fptr;
	if (btr > remain) btr = (UINT)remain;		/* Truncate btr by remaining bytes */

	for ( ;  btr;								/* Repeat until all data read */
		rbuff += rcnt, fp->fptr += rcnt, *br += rcnt, btr -= rcnt) {
		if ((fp->fptr % SS(fp->obj.fs)) == 0) {		/* On the sector boundary? */
			csect = (UINT)(fp->fptr / SS(fp->obj.fs) & (fp->obj.fs->csize - 1));	/* Sector offset in the cluster */
			if (!csect) {						/* On the cluster boundary? */
				if (fp->fptr == 0) {			/* On the top of the file? */
					clst = fp->obj.sclust;			/* Follow from the origin */
				} else {						/* Middle or end of the file */
#if _USE_FASTSEEK
					if (fp->cltbl)
						clst = clmt_clust(fp, fp->fptr);	/* Get cluster# from the CLMT */
					else
#endif
						clst = get_fat(&fp->obj, fp->clust);	/* Follow cluster chain on the FAT */
				}
				if (clst < 2) ABORT(fp->obj.fs, FR_INT_ERR);
				if (clst == 0xFFFFFFFF) ABORT(fp->obj.fs, FR_DISK_ERR);
				fp->clust = clst;				/* Update current cluster */
			}
			sect = clst2sect(fp->obj.fs, fp->clust);	/* Get current sector */
			if (!sect) ABORT(fp->obj.fs, FR_INT_ERR);
			sect += csect;
			cc = btr / SS(fp->obj.fs);				/* When remaining bytes >= sector size, */
			if (cc) {							/* Read maximum contiguous sectors directly */
				if (csect + cc > fp->obj.fs->csize) {
#if _USE_FASTSEEK
					if (f_fragmented(fp)) {
						cs = contiguous_sect(fp);
						if (!cs) ABORT(fp->obj.fs, FR_INT_ERR);
						if (cc > cs) {
							cc = cs;
						}
					}
					if (csect + cc > fp->obj.fs->csize) {
						FSIZE_t next = fp->fptr + ((DWORD)SS(fp->obj.fs) * cc);
						DWORD cl;
						if (next < fp->obj.objsize) {
							cl = clmt_clust(fp, next);
							if (cl >= 2)
								fp->clust = cl;
						}
					}
#else
					/* Clip at cluster boundary */
					cc = fp->obj.fs->csize - csect;
#endif
				}
				rcnt = SS(fp->obj.fs) * cc;			/* Number of bytes transferred */
#ifdef DEV_TYPE_IDE
				g1_dma_set_irq_mask((btr - rcnt) == 0);
#endif
				if (disk_read(fp->obj.fs->pdrv, rbuff, sect, cc))
					ABORT(fp->obj.fs, FR_DISK_ERR);
#if !_FS_READONLY && _FS_MINIMIZE <= 2			/* Replace one of the read sectors with cached data if it contains a dirty sector */
#if _FS_TINY
				if (fp->obj.fs->wflag && fp->obj.fs->winsect - sect < cc)
					memcpy(rbuff + ((fp->obj.fs->winsect - sect) * SS(fp->obj.fs)), fp->obj.fs->win, SS(fp->obj.fs));
#else
				if ((fp->flag & FA_DIRTY) && fp->sect - sect < cc)
					memcpy(rbuff + ((fp->sect - sect) * SS(fp->obj.fs)), fp->buf, SS(fp->obj.fs));
#endif
#endif
				continue;
			}
#if !_FS_TINY
			if (fp->sect != sect) {			/* Load data sector if not in cache */
#if !_FS_READONLY
				if (fp->flag & FA_DIRTY) {		/* Write-back dirty sector cache */
					if (disk_write(fp->obj.fs->pdrv, fp->buf, fp->sect, 1))
						ABORT(fp->obj.fs, FR_DISK_ERR);
					fp->flag &= ~FA_DIRTY;
				}
#endif
#ifdef DEV_TYPE_IDE
				rcnt = SS(fp->obj.fs) - ((UINT)fp->fptr % SS(fp->obj.fs));
				if (rcnt > btr) rcnt = btr;
				g1_dma_set_irq_mask((btr - rcnt) == 0);
#endif
				if (disk_read(fp->obj.fs->pdrv, fp->buf, sect, 1))	/* Fill sector cache */
					ABORT(fp->obj.fs, FR_DISK_ERR);
			}
#endif
			fp->sect = sect;
		}

		rcnt = SS(fp->obj.fs) - ((UINT)fp->fptr % SS(fp->obj.fs));	/* Get partial sector data from sector buffer */
		if (rcnt > btr) rcnt = btr;

#if _FS_TINY
		if (move_window(fp->obj.fs, fp->sect))		/* Move sector window */
			ABORT(fp->obj.fs, FR_DISK_ERR);
		memcpy(rbuff, &fp->obj.fs->win[fp->fptr % SS(fp->obj.fs)], rcnt);	/* Pick partial sector */

#ifdef DEV_TYPE_IDE
		if(fs_dma_enabled()) {
			dcache_purge_range((uint32)rbuff, rcnt);
		}
#endif

#else
		memcpy(rbuff, &fp->buf[fp->fptr % SS(fp->obj.fs)], rcnt);	/* Pick partial sector */
#endif
	}

	LEAVE_FF(fp->obj.fs, FR_OK);
}

#if _FS_ASYNC
/*-----------------------------------------------------------------------*/
/* Read File Async                                                       */
/*-----------------------------------------------------------------------*/
FRESULT f_poll(FIL* fp, UINT *bp) {

	if (fp->err)
		LEAVE_FF(fp->obj.fs, (FRESULT)fp->err);

	int rs = 0;

	if (fp->cur) {
		rs = disk_poll(fp->obj.fs->pdrv);
	}

	if (!rs) {

		DWORD clst, sect;
		UINT cc, cs, rcnt;
		UINT csect;

		if (!fp->btr) {
			*bp = fp->cur;
			return FR_OK;
		}

		if ((fp->fptr % SS(fp->obj.fs)) == 0) {		/* On the sector boundary? */

			csect = (UINT)(fp->fptr / SS(fp->obj.fs) & (fp->obj.fs->csize - 1));	/* Sector offset in the cluster */

			if (!csect) {						/* On the cluster boundary? */
				if (fp->fptr == 0) {			/* On the top of the file? */
					clst = fp->obj.sclust;		/* Follow from the origin */
				} else {						/* Middle or end of the file */
#if _USE_FASTSEEK
					if (fp->cltbl)
						clst = clmt_clust(fp, fp->fptr);	/* Get cluster# from the CLMT */
					else
#endif
						clst = get_fat(&fp->obj, fp->clust);	/* Follow cluster chain on the FAT */
				}

				if (clst < 2) ABORT(fp->obj.fs, FR_INT_ERR);
				if (clst == 0xFFFFFFFF) ABORT(fp->obj.fs, FR_DISK_ERR);
				fp->clust = clst;				/* Update current cluster */
			}

			sect = clst2sect(fp->obj.fs, fp->clust);	/* Get current sector */

			if (!sect)
				ABORT(fp->obj.fs, FR_INT_ERR);

			sect += csect;
			cc = fp->btr / SS(fp->obj.fs);		/* When remaining bytes >= sector size, */

			if (cc) {							/* Read maximum contiguous sectors directly */

				if (csect + cc > fp->obj.fs->csize) {
#if _USE_FASTSEEK
					if (f_fragmented(fp)) {
						cs = contiguous_sect(fp);
						if (!cs) ABORT(fp->obj.fs, FR_INT_ERR);
						if (cc > cs) {
							cc = cs;
						}
					}
					if (csect + cc > fp->obj.fs->csize) {
						FSIZE_t next = fp->fptr + ((DWORD)SS(fp->obj.fs) * cc);
						DWORD cl;
						if (next < fp->obj.objsize) {
							cl = clmt_clust(fp, next);
							if (cl >= 2)
								fp->clust = cl;
						}
					}
#else
					/* Clip at cluster boundary */
					cc = fp->obj.fs->csize - csect;
#endif
				}

				rcnt = SS(fp->obj.fs) * cc;			/* Number of bytes transferred */

#ifdef DEV_TYPE_IDE
				UINT part = fp->btr - rcnt;

				if (fs_dma_enabled() == FS_DMA_STREAM && part < SS(fp->obj.fs)) {
					rcnt += part;
				}

				g1_dma_set_irq_mask( ((fp->btr - rcnt) == 0) );

				if (fs_dma_enabled() == FS_DMA_STREAM && part && fp->btr == rcnt) {
					if (disk_read_part(fp->obj.fs->pdrv, fp->rbuff, sect, rcnt)) {
						ABORT(fp->obj.fs, FR_DISK_ERR);
					}
				}
				else
#endif
				if (disk_read_async(fp->obj.fs->pdrv, fp->rbuff, sect, cc)) {
					ABORT(fp->obj.fs, FR_DISK_ERR);
				}
				goto _continue;
			}
			fp->sect = sect;
		}
		rcnt = SS(fp->obj.fs) - ((UINT)fp->fptr % SS(fp->obj.fs));	/* Get partial sector data from sector buffer */

		if (rcnt > fp->btr) rcnt = fp->btr;

#ifdef DEV_TYPE_IDE
		g1_dma_set_irq_mask( ((fp->btr - rcnt) == 0) );

		if (fs_dma_enabled() == FS_DMA_STREAM) {

			if (disk_read_part(fp->obj.fs->pdrv, fp->rbuff, fp->sect, rcnt)) {
				ABORT(fp->obj.fs, FR_DISK_ERR);
			}

		} else {
			if (move_window(fp->obj.fs, fp->sect))		/* Move sector window */
				ABORT(fp->obj.fs, FR_DISK_ERR);

			memcpy(fp->rbuff, &fp->obj.fs->win[fp->fptr % SS(fp->obj.fs)], rcnt);	/* Pick partial sector */
			dcache_purge_range((uint32)fp->rbuff, rcnt);
		}
#else
		if (move_window(fp->obj.fs, fp->sect))		/* Move sector window */
			ABORT(fp->obj.fs, FR_DISK_ERR);

		memcpy(fp->rbuff, &fp->obj.fs->win[fp->fptr % SS(fp->obj.fs)], rcnt);	/* Pick partial sector */
#endif

_continue:
		*bp = fp->cur;

		fp->fptr += rcnt;
		fp->rbuff += rcnt;
		fp->cur += rcnt;
		fp->btr -= rcnt;
		fp->rcnt = rcnt;

	} else if(rs < 0) {
		*bp = fp->cur;
		ABORT(fp->obj.fs, FR_DISK_ERR);
	}

	*bp = fp->cur - (fp->rcnt - rs);
	return FR_NOT_READY;
}

FRESULT f_abort(FIL* fp) {
	disk_abort(fp->obj.fs->pdrv);
	fp->rbuff = NULL;
	fp->cur = fp->btr = fp->fptr = fp->rcnt = 0;
	fp->clust = fp->obj.sclust;
	ABORT(fp->obj.fs, FR_OK);
}


FRESULT f_read_async (
	FIL* fp, 		/* Pointer to the file object */
	void* buff,		/* Pointer to data buffer */
	UINT btr		/* Number of bytes to read */
)
{
	FRESULT res;
	FATFS *checked_fs;
	FSIZE_t remain;
	UINT bp;

	res = validate(&fp->obj, &checked_fs);							/* Check validity */
	if (res != FR_OK)
		LEAVE_FF(fp->obj.fs, res);

	if (fp->err)								   /* Check error */
		LEAVE_FF(fp->obj.fs, (FRESULT)fp->err);

	if (!(fp->flag & FA_READ)) 					/* Check access mode */
		LEAVE_FF(fp->obj.fs, FR_DENIED);

	remain = fp->obj.objsize - fp->fptr;
	fp->btr = btr > remain ? remain : btr; /* Truncate btr by remaining bytes */
	fp->rbuff = (BYTE*)buff;
	fp->cur = 0;
	fp->rcnt = 0;

	/* Start the transfer */
	res = f_poll(fp, &bp);

	if(res == FR_OK || res == FR_NOT_READY) {
		LEAVE_FF(fp->obj.fs, FR_OK);
	} else {
		fp->btr = fp->cur = 0;
		fp->rbuff = NULL;
		LEAVE_FF(fp->obj.fs, FR_DISK_ERR);
	}
}

FRESULT f_pre_read (
	FIL* fp, 	/* Pointer to the file object */
	DWORD btr	/* Number of bytes to pre-read */
)
{
	FRESULT res;
	FATFS *checked_fs;
	DWORD sect, clst;
	UINT csect;

	res = validate(&fp->obj, &checked_fs);							/* Check validity */
	if (res != FR_OK)
		LEAVE_FF(fp->obj.fs, res);

	if (fp->err)								   /* Check error */
		LEAVE_FF(fp->obj.fs, (FRESULT)fp->err);

	if (!(fp->flag & FA_READ)) 					/* Check access mode */
		LEAVE_FF(fp->obj.fs, FR_DENIED);

	/* The raw streaming command can issue only one physical extent. Refuse
	 * a request crossing a fragment rather than reading another file's data.
	 * Normal f_read/f_read_async handle fragmented files without this limit. */
	if (btr > fp->obj.objsize - fp->fptr)
		LEAVE_FF(fp->obj.fs, FR_INVALID_PARAMETER);
	if (!btr) LEAVE_FF(fp->obj.fs, FR_OK);
	if ((FSIZE_t)btr + fp->fptr % SS(fp->obj.fs) >
		(FSIZE_t)contiguous_sect(fp) * SS(fp->obj.fs))
		LEAVE_FF(fp->obj.fs, FR_DENIED);

	csect = (UINT)(fp->fptr / SS(fp->obj.fs) & (fp->obj.fs->csize - 1));	/* Sector offset in the cluster */

	if (!csect) {						/* On the cluster boundary? */
		if (fp->fptr == 0) {			/* On the top of the file? */
			clst = fp->obj.sclust;			/* Follow from the origin */
		} else {						/* Middle or end of the file */
#if _USE_FASTSEEK
			if (fp->cltbl)
				clst = clmt_clust(fp, fp->fptr);	/* Get cluster# from the CLMT */
			else
#endif
				clst = get_fat(&fp->obj, fp->clust);	/* Follow cluster chain on the FAT */
		}
		if (clst < 2) ABORT(fp->obj.fs, FR_INT_ERR);
		if (clst == 0xFFFFFFFF) ABORT(fp->obj.fs, FR_DISK_ERR);
		fp->clust = clst;				/* Update current cluster */
	}

	sect = clst2sect(fp->obj.fs, fp->clust);	/* Get current sector */

	if (!sect)
		ABORT(fp->obj.fs, FR_INT_ERR);

	sect += csect;

	/* Start the pre-reading */
	if (disk_pre_read(fp->obj.fs->pdrv, sect, btr / SS(fp->obj.fs))) {
		ABORT(fp->obj.fs, FR_DISK_ERR);
	}

	fp->sect = sect;

#if _USE_FASTSEEK
	clst = clmt_clust(fp, fp->fptr + btr);

	if (clst >= 2 && clst != 0xFFFFFFFF) {
		fp->fptr += btr;
		fp->clust = clst;
	}
#endif
	LEAVE_FF(fp->obj.fs, res);
}

#endif
