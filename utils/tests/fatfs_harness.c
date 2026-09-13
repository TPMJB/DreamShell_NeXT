/* Run the production logger/checkpoint writer against the pinned FatFs, on an
 * in-memory FAT volume. Only block I/O and the KOS VFS boundary are adapted. */
#include "../../applications/gd_ripper/modules/module.c"
#include "ff.h"
#include "diskio.h"
#include <assert.h>

PARTITION VolToPart[FF_VOLUMES];
static BYTE *disk;
static DWORD sectors;
static FIL files[16];
static bool used[16];
static bool fail_writes;

DSTATUS disk_initialize(BYTE drv) { return drv ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE drv) { return disk_initialize(drv); }
DRESULT disk_read(BYTE drv, BYTE *buffer, DWORD sector, UINT count) {
    if (drv || sector >= sectors || count > sectors - sector) return RES_PARERR;
    memcpy(buffer, disk + (size_t)sector * 512, (size_t)count * 512);
    return RES_OK;
}
DRESULT disk_write(BYTE drv, const BYTE *buffer, DWORD sector, UINT count) {
    if (fail_writes) { errno = EIO; return RES_ERROR; }
    if (drv || sector >= sectors || count > sectors - sector) return RES_PARERR;
    memcpy(disk + (size_t)sector * 512, buffer, (size_t)count * 512);
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drv, BYTE cmd, void *buffer) {
    if (drv) return RES_PARERR;
    if (cmd == GET_SECTOR_COUNT) *(DWORD *)buffer = sectors;
    else if (cmd == GET_BLOCK_SIZE) *(DWORD *)buffer = 1;
    else if (cmd != CTRL_SYNC) return RES_PARERR;
    return RES_OK;
}
DWORD get_fattime(void) { return (DWORD)(2026 - 1980) << 25 | 9 << 21 | 12 << 16; }

static int result(FRESULT rc) {
    if (rc == FR_OK) return 0;
    /* Relevant mappings from the pinned dc.c fatfs_set_errno(). */
    switch (rc) {
        case FR_NO_FILE: case FR_NO_PATH: errno = ENOENT; break;
        case FR_EXIST: errno = EACCES; break;
        case FR_DENIED: errno = ENOSPC; break;
        case FR_LOCKED: errno = EAGAIN; break;
        default: errno = EIO; break;
    }
    return -1;
}

file_t fs_open(const char *path, int flags) {
    /* Same flag translation as the pinned fat_open() in dc.c. */
    int access = flags & (O_RDONLY | O_WRONLY | O_RDWR);
    BYTE mode = access == O_RDWR ? FA_READ | FA_WRITE :
        access == O_WRONLY ? FA_WRITE : FA_READ;
    if ((flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) mode |= FA_CREATE_NEW;
    else if (flags & O_TRUNC) mode |= FA_CREATE_ALWAYS;
    else if (flags & O_CREAT) mode |= FA_OPEN_ALWAYS;
    for (unsigned i = 0; i < 16; ++i) {
        if (used[i]) continue;
        if (result(f_open(&files[i], path, mode))) return FILEHND_INVALID;
        if (mode & FA_WRITE) f_sync(&files[i]);
        used[i] = true;
        return i;
    }
    errno = EMFILE;
    return FILEHND_INVALID;
}
ssize_t fs_total(file_t fd) { return f_size(&files[fd]); }
off_t fs_seek(file_t fd, off_t offset, int whence) {
    DWORD pos = offset;
    if (whence == SEEK_CUR) pos += f_tell(&files[fd]);
    if (whence == SEEK_END) pos += f_size(&files[fd]);
    return result(f_lseek(&files[fd], pos)) ? -1 : (off_t)f_tell(&files[fd]);
}
ssize_t fs_write(file_t fd, const void *buffer, size_t size) {
    UINT written;
    return result(f_write(&files[fd], buffer, size, &written)) ? -1 : (ssize_t)written;
}
ssize_t fs_read(file_t fd, void *buffer, size_t size) {
    UINT bytes;
    return result(f_read(&files[fd], buffer, size, &bytes)) ? -1 : (ssize_t)bytes;
}
int fs_close(file_t fd) { used[fd] = false; return result(f_close(&files[fd])); }
int fs_complete(file_t fd, ssize_t *n) { *n = 0; return result(f_sync(&files[fd])); }
int fs_unlink(const char *path) { return result(f_unlink(path)); }
int rename(const char *from, const char *to) { return result(f_rename(from, to)); }
void thd_pass(void) {}
uint64_t timer_ms_gettime64(void) { return 123456; }
void ds_printf(const char *format, ...) { (void)format; }

/* The production parser uses stdio on top of the same VFS. Adapt its read-only
 * FILE streams to the real FAT volume, instead of silently testing host files. */
static ssize_t cookie_read(void *cookie, char *buffer, size_t n) {
    return fs_read(*(file_t *)cookie, buffer, n);
}
static int cookie_close(void *cookie) {
    int rv = fs_close(*(file_t *)cookie); free(cookie); return rv;
}
FILE *fopen(const char *path, const char *mode) {
    assert(!strcmp(mode,"r") || !strcmp(mode,"rb"));
    file_t *cookie = malloc(sizeof(*cookie));
    if (!cookie) return NULL;
    *cookie = fs_open(path, O_RDONLY);
    if (*cookie == FILEHND_INVALID) { free(cookie); return NULL; }
    cookie_io_functions_t io = { .read = cookie_read, .close = cookie_close };
    FILE *fp = fopencookie(cookie,"r",io);
    if (!fp) cookie_close(cookie);
    return fp;
}

static bool stubborn;
static int audio_read(void *data, uint8_t *buffer, uint32_t fad) {
    (void)data;
    if (stubborn && fad == 45152) return 1;
    memset(buffer, (fad-45150)*17, 2352);
    return 0;
}

static void read_text(const char *path, char *buffer, size_t size) {
    file_t fd = fs_open(path, O_RDONLY);
    assert(fd != FILEHND_INVALID);
    ssize_t n = fs_read(fd, buffer, size - 1);
    assert(n >= 0);
    buffer[n] = '\0';
    assert(fs_close(fd) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    sectors = atoi(argv[1]) == 32 ? 262144 : 32768;
    disk = calloc(sectors, 512);
    assert(disk);
    FATFS volume = {0};
    assert(f_mount(&volume, "0:", 0) == FR_OK);
    unsigned char work[4096];
    MKFS_PARM format = { .fmt = (atoi(argv[1]) == 32 ? FM_FAT32 :
        !strcmp(argv[1], "exfat") ? FM_EXFAT : FM_FAT) | FM_SFD, .au_size = 512 };
    assert(f_mkfs("0:", &format, work, sizeof(work)) == FR_OK);
    assert(f_mount(&volume, "0:", 1) == FR_OK);
    assert(volume.fs_type == (atoi(argv[1]) == 32 ? FS_FAT32 : !strcmp(argv[1], "exfat") ? FS_EXFAT : FS_FAT16));
    assert(f_mkdir("/TIME_STALKERS") == FR_OK);
    strcpy(self.sync_mount, "/sd");

    /* Reproduce the 2.0.1 sequence: the probe passes but the first call to
     * rip_log used to fail because FA_OPEN_ALWAYS could not create it. */
    assert(check_storage("/") == CMD_OK);

    strcpy(self.log_path, "/TIME_STALKERS/rip.log");
    assert(rip_log("first log entry") == CMD_OK);
    assert(rip_log("second log entry") == CMD_OK);
    uint32_t tag = gd_crc_tag(1, 150, 300, 2352);
    assert(gd_crc_checkpoint("/TIME_STALKERS/track01.bin", tag, 705600, 0x12345678, true) == CMD_OK);
    assert(gd_crc_checkpoint("/TIME_STALKERS/track01.bin", tag, 705600, 0x12345678, true) == CMD_OK);
    assert(gd_crc_checkpoint("/TIME_STALKERS/track02.raw", 2, 526 * 2352, 0x87654321, true) == CMD_OK);
    assert(record_bad_sector("/TIME_STALKERS/track03.bin", 3, 1, 45151, 2352) == CMD_OK);
    assert(record_bad_sector("/TIME_STALKERS/track03.bin", 3, 2, 45152, 2352) == CMD_OK);

    /* Close/reopen/remount must retain both old and new records. */
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    assert(f_mount(&volume, "0:", 1) == FR_OK);
    char text[512];
    read_text(self.log_path, text, sizeof(text));
    assert(strstr(text, "first log entry") && strstr(text, "second log entry"));
    read_text("/TIME_STALKERS/track01.bin.crc", text, sizeof(text));
    assert(strstr(text, "705600 12345678"));
    char *record = strstr(text, "CRC1");
    assert(record && strstr(record + 4, "CRC1"));
    read_text("/TIME_STALKERS/track02.raw.crc", text, sizeof(text));
    assert(strstr(text, "1237152 87654321"));
    read_text("/TIME_STALKERS/track03.bin.bad", text, sizeof(text));
    assert(strstr(text, "3,1,45001,45151,2352\n3,2,45002,45152,4704\n"));
    assert(strstr(text, "track,track_sector") == text);
    assert(!strstr(text + 1, "track,track_sector"));

    /* Baseline creation/rename, in-place writes, audio confirmation and CRC
     * updates must survive an actual FAT unmount, including an unfinished queue. */
    const char *track_path = "/TIME_STALKERS/track03.bin";
    uint8_t track_data[2352*3] = {0};
    file_t fd = fs_open(track_path, O_WRONLY | O_CREAT | O_TRUNC);
    assert(fd != FILEHND_INVALID);
    assert(fs_write(fd, track_data, sizeof(track_data)) == sizeof(track_data));
    assert(fs_close(fd) == 0);
    tag = gd_crc_tag(3, 45150, 3, 2352);
    assert(gd_crc_checkpoint(track_path, tag, sizeof(track_data), crc32(0, track_data, sizeof(track_data)), true) == CMD_OK);
    volatile int active = 1;
    gd_recovery_result_t recovery;
    stubborn = true;
    assert(gd_recover_track(track_path, 3, 45150, 3, 0, true, 1, &active,
        audio_read, NULL, NULL, &recovery) == CMD_OK);
    assert(recovery.recovered == 1 && recovery.remaining == 1);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    assert(f_mount(&volume, "0:", 1) == FR_OK);
    stubborn = false;
    assert(gd_recover_track(track_path, 3, 45150, 3, 0, true, 1, &active,
        audio_read, NULL, NULL, &recovery) == CMD_OK);
    assert(recovery.recovered == 1 && recovery.remaining == 0);
    fd = fs_open(track_path, O_RDONLY);
    assert(fd != FILEHND_INVALID && fs_read(fd, track_data, sizeof(track_data)) == sizeof(track_data));
    assert(fs_close(fd) == 0);
    assert(recovery.crc == crc32(0, track_data, sizeof(track_data)));
    assert(track_data[2352] == 17 && track_data[4704] == 34);
    uint64_t saved_bytes; uint32_t saved_crc;
    assert(gd_crc_restore(track_path, tag, sizeof(track_data), 2352, &saved_bytes, &saved_crc));
    assert(saved_bytes == sizeof(track_data) && saved_crc == recovery.crc);
    assert(fs_open("/TIME_STALKERS/track03.bin.bad", O_RDONLY) == FILEHND_INVALID && errno == ENOENT);

    /* Failed opens must not turn into truncation or silent success. */
    assert(gd_open_append("/MISSING/rip.log") == FILEHND_INVALID);
    assert(f_chmod(self.log_path, AM_RDO, AM_RDO) == FR_OK);
    assert(rip_log("must not overwrite read-only log") == CMD_ERROR);
    assert(f_chmod(self.log_path, 0, AM_RDO) == FR_OK);
    read_text(self.log_path, text, sizeof(text));
    assert(strstr(text, "first log entry") && strstr(text, "second log entry"));
    assert(!strstr(text, "must not overwrite"));
    fail_writes = true;
    assert(rip_log("failed flush") == CMD_ERROR);
    fail_writes = false;
    puts("FAT log creation, recovery, remount, CRC checkpoints and failure handling passed");
    free(disk);
    return 0;
}
