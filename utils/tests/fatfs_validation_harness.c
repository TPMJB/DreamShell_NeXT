/* Exercise the official R0.16 fixes through FatFs APIs on memory-backed media. */
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

PARTITION VolToPart[FF_VOLUMES];
static BYTE *disk;
static DWORD sectors;
static FATFS volume;

DSTATUS disk_initialize(BYTE drv) { return drv ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE drv) { return disk_initialize(drv); }
DRESULT disk_read(BYTE drv, BYTE *buffer, LBA_t sector, UINT count) {
    if (drv || sector >= sectors || count > sectors - sector) return RES_PARERR;
    memcpy(buffer, disk + (size_t)sector * 512, (size_t)count * 512);
    return RES_OK;
}
DRESULT disk_write(BYTE drv, const BYTE *buffer, LBA_t sector, UINT count) {
    if (drv || sector >= sectors || count > sectors - sector) return RES_PARERR;
    memcpy(disk + (size_t)sector * 512, buffer, (size_t)count * 512);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drv, BYTE cmd, void *buffer) {
    if (drv) return RES_PARERR;
    if (cmd == GET_SECTOR_COUNT) *(LBA_t *)buffer = sectors;
    else if (cmd == GET_BLOCK_SIZE) *(DWORD *)buffer = 1;
    else if (cmd != CTRL_SYNC) return RES_PARERR;
    return RES_OK;
}
DWORD get_fattime(void) { return (DWORD)(2026 - 1980) << 25 | 9 << 21 | 15 << 16; }

static void put32(BYTE *where, DWORD value) {
    for (unsigned i = 0; i < 4; ++i) where[i] = value >> (i * 8);
}

static void format_volume(BYTE kind, DWORD size, BYTE fats) {
    sectors = size;
    disk = calloc(sectors, 512);
    assert(disk);
    assert(f_mount(&volume, "0:", 0) == FR_OK);
    BYTE work[4096];
    MKFS_PARM format = {
        .fmt = kind | FM_SFD, .n_fat = fats, .align = 1,
        .n_root = 16, .au_size = 512
    };
    assert(f_mkfs("0:", &format, work, sizeof(work)) == FR_OK);
    assert(f_mount(&volume, "0:", 1) == FR_OK);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    if (!strcmp(argv[1], "small-volume")) {
        /* Patch 1 permits a valid 64-sector FAT12 volume. */
        format_volume(FM_FAT, 64, 1);
        assert(volume.fs_type == FS_FAT12);
        FIL file;
        UINT transferred;
        char data[5] = {0};
        assert(f_open(&file, "0:/TEST.TXT", FA_CREATE_NEW | FA_WRITE) == FR_OK);
        assert(f_write(&file, "test", 4, &transferred) == FR_OK && transferred == 4);
        assert(f_close(&file) == FR_OK);
        assert(f_mount(&volume, "0:", 1) == FR_OK);
        assert(f_open(&file, "0:/TEST.TXT", FA_READ) == FR_OK);
        assert(f_read(&file, data, 4, &transferred) == FR_OK && transferred == 4);
        assert(!strcmp(data, "test"));
        assert(f_close(&file) == FR_OK);
    } else if (!strcmp(argv[1], "fat-clusters")) {
        format_volume(FM_FAT, 128, 1);
        /* Inflated sectors/cluster leave fewer than 33 data clusters. */
        disk[13] = 4;
        assert(f_mount(&volume, "0:", 1) == FR_NO_FILESYSTEM);
    } else if (!strcmp(argv[1], "fat-size")) {
        format_volume(FM_FAT32, 131072, 2);
        assert(volume.fs_type == FS_FAT32);
        /* Two oversized FATs used to wrap the computed system-area size. */
        put32(disk + 36, volume.fsize | 0x80000000U);
        assert(f_mount(&volume, "0:", 1) == FR_NO_FILESYSTEM);
    } else if (!strcmp(argv[1], "exfat-clusters")) {
        format_volume(FM_EXFAT, 32768, 1);
        assert(volume.fs_type == FS_EXFAT);
        put32(disk + 92, 255);
        assert(f_mount(&volume, "0:", 1) == FR_NO_FILESYSTEM);
    } else if (!strcmp(argv[1], "exfat-label")) {
        format_volume(FM_EXFAT, 32768, 1);
        const char *expected = "ABCDEFGHIJK";
        char label[64];
        assert(f_setlabel("0:ABCDEFGHIJK") == FR_OK);
        assert(f_getlabel("0:", label, NULL) == FR_OK);
        assert(!strcmp(label, expected));
        size_t root = (volume.database + (volume.dirbase - 2) * volume.csize) * 512;
        BYTE *entry = NULL;
        for (unsigned i = 0; i < volume.csize * 512; i += 32) {
            if (disk[root + i] == 0x83) { entry = disk + root + i; break; }
        }
        assert(entry);
        /* A corrupt length must not expose the entry's reserved bytes. */
        entry[1] = 15;
        for (unsigned i = 24; i < 32; i += 2) {
            entry[i] = 'X';
            entry[i + 1] = 0;
        }
        assert(f_mount(&volume, "0:", 1) == FR_OK);
        assert(f_getlabel("0:", label, NULL) == FR_OK);
        assert(!strcmp(label, expected));
    } else {
        assert(!"unknown test case");
    }
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    free(disk);
    return 0;
}
