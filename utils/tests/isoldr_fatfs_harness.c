/* Exercise the actual ISO Loader parser and sync/async transfer extension. */
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

PARTITION VolToPart[FF_VOLUMES];
static int image_fd, pending, phase, fail_read, pre_reads;
static BYTE *destination;
static LBA_t pending_sector;
static UINT pending_count;

void *ff_memalloc(UINT n) { return malloc(n); }
void ff_memfree(void *p) { free(p); }
DWORD get_fattime(void) { return (DWORD)46 << 25 | 9 << 21 | 13 << 16; }
DSTATUS disk_initialize(BYTE d) { return d ? STA_NOINIT : 0; }
DSTATUS disk_status(BYTE d) { return disk_initialize(d); }
DRESULT disk_read(BYTE d, BYTE *p, LBA_t sector, UINT n) {
    if (d || fail_read) return RES_ERROR;
    assert(!pending); /* Metadata I/O must not overlap an active transfer. */
    return pread(image_fd, p, n * 512, (off_t)sector * 512) == (ssize_t)n * 512 ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE d, const BYTE *p, LBA_t sector, UINT n) {
    if (d) return RES_ERROR;
    return pwrite(image_fd, p, n * 512, (off_t)sector * 512) == (ssize_t)n * 512 ? RES_OK : RES_ERROR;
}
DRESULT disk_ioctl(BYTE d, BYTE cmd, void *p) {
    (void)p;
    return !d && cmd == CTRL_SYNC && !fsync(image_fd) ? RES_OK : RES_PARERR;
}
DRESULT disk_read_async(BYTE d, BYTE *p, LBA_t sector, UINT n) {
    if (d || fail_read) return RES_ERROR;
    assert(!pending);
    pending = 1; phase = 0; destination = p; pending_sector = sector; pending_count = n;
    return RES_OK;
}
int disk_poll(BYTE d) {
    assert(!d);
    if (!pending) return 0;
    if (!phase++) return pending_count * 256;
    pending = 0;
    return disk_read(0, destination, pending_sector, pending_count) == RES_OK ? 0 : -1;
}
DRESULT disk_abort(BYTE d) { assert(!d); pending = 0; return RES_OK; }
DRESULT disk_pre_read(BYTE d, LBA_t sector, UINT n) {
    assert(!d && sector && n); ++pre_reads; return RES_OK;
}

static void expected(BYTE *p, unsigned offset, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        unsigned at = (offset + i) % (512 * 1024);
        p[i] = at * 13 + at / 257;
    }
}

int main(int argc, char **argv) {
    assert(argc == 2);
    image_fd = open(argv[1], O_RDWR);
    assert(image_fd >= 0);
    FATFS volume; FIL file;
    memset(&volume, 0, sizeof(volume)); memset(&file, 0, sizeof(file));
    assert(f_mount(&volume, "0:", 1) == FR_OK);
    assert(f_open(&file, "/DS/fragmented.bin", FA_READ) == FR_OK);
    DWORD map[64];
    map[0] = 64; file.cltbl = map;
    assert(f_lseek(&file, CREATE_LINKMAP) == FR_OK && map[0] > 4);
    const unsigned starts[] = {0, 11, 130999, 511 * 1024, 1024 * 1024 + 3};
    const unsigned lengths[] = {13, 2352, 262144, 524301};
    BYTE *out = malloc(600000), *want = malloc(600000);
    assert(out && want);
    for (unsigned a = 0; a < sizeof(starts)/sizeof(*starts); ++a)
    for (unsigned b = 0; b < sizeof(lengths)/sizeof(*lengths); ++b) {
        UINT n = 0;
        expected(want, starts[a], lengths[b]);
        assert(f_lseek(&file, starts[a]) == FR_OK);
        assert(f_read(&file, out, lengths[b], &n) == FR_OK && n == lengths[b]);
        assert(!memcmp(out, want, n));
        assert(f_lseek(&file, starts[a]) == FR_OK);
        assert(f_read_async(&file, out, lengths[b]) == FR_OK);
        FRESULT rc;
        UINT previous = 0;
        for (unsigned polls = 0;; ++polls) {
            assert(polls < 10000);
            rc = f_poll(&file, &n);
            assert(n >= previous && n <= lengths[b]); previous = n;
            if (rc == FR_OK) break;
            assert(rc == FR_NOT_READY);
        }
        assert(n == lengths[b] && !memcmp(out, want, n));
    }
    /* Raw streaming must not cross into the spacer file's allocation. */
    assert(f_lseek(&file, 0) == FR_OK);
    assert(f_pre_read(&file, 512) == FR_OK && pre_reads == 1);
    assert(f_lseek(&file, 511 * 1024) == FR_OK);
    assert(f_pre_read(&file, 2048) == FR_DENIED && pre_reads == 1);
    assert(f_tell(&file) == 511 * 1024);
    assert(f_lseek(&file, f_size(&file) - 512) == FR_OK);
    assert(f_pre_read(&file, 1024) == FR_INVALID_PARAMETER);
    /* Missing CLMT still has to respect fragmentation/cluster boundaries. */
    file.cltbl = NULL;
    UINT n;
    assert(f_lseek(&file, 130999) == FR_OK);
    expected(want, 130999, 524301);
    assert(f_read(&file, out, 524301, &n) == FR_OK && n == 524301);
    assert(!memcmp(out, want, n));
    assert(f_lseek(&file, 0) == FR_OK);
    assert(f_read_async(&file, out, 262144) == FR_OK);
    assert(f_abort(&file) == FR_OK && !pending);
    assert(f_lseek(&file, 0) == FR_OK);
    fail_read = 1;
    assert(f_read_async(&file, out, 262144) == FR_DISK_ERR);
    fail_read = 0;
    assert(f_close(&file) == FR_OK);
    assert(f_open(&file, "/DS/caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac.bin", FA_READ) == FR_OK);
    assert(f_read(&file, out, 100, &n) == FR_OK && n == sizeof("boot core and log content\n"));
    assert(f_close(&file) == FR_OK);
#if !FF_FS_READONLY
    /* VMU-enabled loader builds use this same engine to update saves. */
    assert(f_open(&file, "/DS/save.vmd", FA_CREATE_ALWAYS | FA_WRITE | FA_READ) == FR_OK);
    memset(want, 0xa5, 131072);
    assert(f_write(&file, want, 131072, &n) == FR_OK && n == 131072);
    assert(f_sync(&file) == FR_OK);
    assert(f_lseek(&file, 129001) == FR_OK);
    assert(f_write(&file, "save", 4, &n) == FR_OK && n == 4);
    assert(f_close(&file) == FR_OK);
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    assert(f_mount(&volume, "0:", 1) == FR_OK);
    assert(f_open(&file, "/DS/save.vmd", FA_READ) == FR_OK);
    memcpy(want + 129001, "save", 4);
    assert(f_read(&file, out, 131072, &n) == FR_OK && n == 131072);
    assert(!memcmp(out, want, n));
    assert(f_close(&file) == FR_OK);
#endif
    assert(f_mount(NULL, "0:", 0) == FR_OK);
    free(out); free(want); close(image_fd);
    puts("ISO Loader: fragmented sync/async, unaligned reads, UTF-8, abort and I/O error passed");
    return 0;
}
