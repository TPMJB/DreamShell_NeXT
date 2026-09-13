/* Run the production KOS adapter + discovery over a host file block device.
 * No file-operation translations are duplicated here. */
#include "../../lib/fatfs/fatfs/src/dc.c"
#include "../../lib/fatfs/fatfs/src/dc_bdev.c"
#include <assert.h>
#include <unistd.h>

static int image_fd;
static uint64_t image_sectors;
static int flush_failure, write_failure;
static unsigned reads, writes, flushes;
static vfs_handler_t *registered[8];

int nmmgr_handler_add(nmmgr_handler_t *h) {
    for (int i = 0; i < 8; ++i) if (!registered[i]) {
        registered[i] = (vfs_handler_t *)h; return 0;
    }
    return -1;
}
int nmmgr_handler_remove(nmmgr_handler_t *h) {
    for (int i = 0; i < 8; ++i) if (registered[i] == (vfs_handler_t *)h) {
        registered[i] = NULL; return 0;
    }
    return -1;
}
static int block_init(kos_blockdev_t *d) { (void)d; return 0; }
static int block_shutdown(kos_blockdev_t *d) { d->dev_data = NULL; return 0; }
static uint64_t block_count(const kos_blockdev_t *d) { (void)d; return image_sectors; }
static int block_flush(kos_blockdev_t *d) {
    (void)d; ++flushes;
    if (flush_failure) { errno = EIO; return -1; }
    return fsync(image_fd);
}
static int block_read(const kos_blockdev_t *d, uint64_t block, size_t n, void *p) {
    (void)d; ++reads;
    if (block >= image_sectors || n > image_sectors - block) return -1;
    return pread(image_fd, p, n * 512, block * 512) == (ssize_t)(n * 512) ? 0 : -1;
}
static int block_write(const kos_blockdev_t *d, uint64_t block, size_t n, const void *p) {
    (void)d; ++writes;
    if (write_failure || block >= image_sectors || n > image_sectors - block) return -1;
    return pwrite(image_fd, p, n * 512, block * 512) == (ssize_t)(n * 512) ? 0 : -1;
}
static void block_device(kos_blockdev_t *d) {
    *d = (kos_blockdev_t){ .dev_data = &image_fd, .l_block_size = 9,
        .init = block_init, .shutdown = block_shutdown, .read_blocks = block_read,
        .write_blocks = block_write, .count_blocks = block_count, .flush = block_flush };
}
int sd_init_ex(const sd_init_params_t *p) { (void)p; return 0; }
int sd_blockdev_for_device(kos_blockdev_t *d) { block_device(d); return 0; }
int g1_ata_init(void) { return 0; }
int g1_ata_blockdev_for_device(int dma, kos_blockdev_t *d) {
    (void)dma; block_device(d); return 0;
}

static vfs_handler_t *mount_test(int ide) {
    unsigned before = reads;
    assert((ide ? fs_fat_mount_ide() : fs_fat_mount_sd()) == 0);
    assert(reads - before < 512); /* No free-space scan while booting. */
    for (int i = 0; i < 8; ++i) if (registered[i]) return registered[i];
    assert(0); return NULL;
}
static void unmount_test(int ide) {
    if (ide) fs_fat_unmount_ide(); else fs_fat_unmount_sd();
    for (int i = 0; i < 8; ++i) assert(!registered[i]);
}

static void roundtrip(vfs_handler_t *v, const char *path, const void *p, size_t n) {
    void *h = v->open(v, path, O_CREAT | O_TRUNC | O_RDWR);
    assert(h);
    assert(v->write(h, p, n) == (ssize_t)n);
    ssize_t completed = -1;
    assert(v->complete(h, &completed) == 0 && completed == 0);
    assert(v->close(h) == 0);
    h = v->open(v, path, O_RDONLY);
    assert(h);
    void *out = malloc(n);
    assert(out && v->read(h, out, n) == (ssize_t)n && !memcmp(p, out, n));
    free(out);
    assert(v->close(h) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 5);
    int kind = atoi(argv[2]), ide = atoi(argv[3]), large = atoi(argv[4]);
    image_fd = open(argv[1], O_RDWR);
    assert(image_fd >= 0);
    struct stat st; assert(fstat(image_fd, &st) == 0);
    image_sectors = st.st_size / 512;
    assert(fs_fat_init() == 0);
    if (!kind) {
        assert(fs_fat_mount_sd() < 0 && !writes);
        for (int i = 0; i < 8; ++i) assert(!registered[i]);
        close(image_fd);
        return 0;
    }
    vfs_handler_t *v = mount_test(ide);
    fatfs_mnt_t *mnt = v->privdata;
    assert(mnt->fs->fs_type == (kind == 64 ? FS_EXFAT : kind == 32 ? FS_FAT32 : FS_FAT16));
    if (ide) assert(mnt->dma_sectors <= 64 && mnt->dmabuf);
    assert(v->mkdir(v, "/DS") == 0);
    const char text[] = "boot core and log content\n";
    roundtrip(v, "/DS/DS_CORE.BIN", text, sizeof(text));
    roundtrip(v, "/DS/caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac.bin", text, sizeof(text));
    assert(v->rename(v, "/DS/DS_CORE.BIN", "/DS/core.tmp") == 0);
    assert(v->rename(v, "/DS/core.tmp", "/DS/DS_CORE.BIN") == 0);
    void *h = v->open(v, "/DS/rip.log", O_WRONLY | O_CREAT | O_APPEND);
    assert(h && v->write(h, "first\n", 6) == 6 && v->close(h) == 0);
    h = v->open(v, "/DS/rip.log", O_WRONLY | O_CREAT | O_APPEND);
    assert(h && v->write(h, "second\n", 7) == 7 && v->close(h) == 0);
    h = v->open(v, "/DS/rip.log", O_RDONLY);
    assert(h && v->total64(h) == 13);
    assert(v->seek64(h, -1, SEEK_SET) == -1 && errno == EINVAL);
    assert(v->tell64(h) == 0 && v->close(h) == 0);

    /* Force allocation to become fragmented; verify long unaligned reads. */
    unsigned char *buffer = malloc(512 * 1024 + 1), *out = malloc(512 * 1024 + 1);
    assert(buffer && out);
    for (unsigned i = 0; i < 512 * 1024; ++i) buffer[i + 1] = i * 13 + i / 257;
    void *a = v->open(v, "/DS/fragmented.bin", O_CREAT | O_RDWR);
    void *b = v->open(v, "/DS/spacer.bin", O_CREAT | O_RDWR);
    assert(a && b);
    for (int i = 0; i < 8; ++i) {
        assert(v->write(a, buffer + 1, 512 * 1024) == 512 * 1024);
        assert(v->write(b, buffer + 1, 512 * 1024) == 512 * 1024);
    }
    assert(v->close(a) == 0 && v->close(b) == 0);
    unmount_test(ide); v = mount_test(ide);
    a = v->open(v, "/DS/fragmented.bin", O_RDONLY);
    assert(a);
    for (int i = 0; i < 8; ++i)
        assert(v->read(a, out + 1, 512 * 1024) == 512 * 1024 &&
               !memcmp(out + 1, buffer + 1, 512 * 1024));
    assert(v->close(a) == 0);
    free(buffer); free(out);

    if (large) {
        assert(kind == 64);
        const int64_t pos = INT64_C(0x100000000) + 512;
        h = v->open(v, "/DS/over4g.bin", O_CREAT | O_RDWR);
        assert(h && v->seek64(h, pos, SEEK_SET) == pos);
        assert(v->write(h, text, sizeof(text)) == sizeof(text));
        assert(v->total64(h) == (uint64_t)pos + sizeof(text));
        assert(v->tell(h) == -1 && errno == EOVERFLOW);
        assert(v->mmap(h) == NULL && errno == EOVERFLOW);
        assert(v->seek(h, 0, SEEK_END) == -1 && errno == EOVERFLOW);
        assert(v->close(h) == 0);
        unmount_test(ide); v = mount_test(ide);
        h = v->open(v, "/DS/over4g.bin", O_RDONLY);
        assert(h && v->seek64(h, pos, SEEK_SET) == pos);
        char out_text[sizeof(text)];
        assert(v->read(h, out_text, sizeof(text)) == sizeof(text) && !memcmp(out_text, text, sizeof(text)));
        assert(v->close(h) == 0);
    }

    /* The production adapter must propagate block-device flush failures. */
    h = v->open(v, "/DS/flush-test.bin", O_CREAT | O_RDWR);
    assert(h && v->write(h, text, sizeof(text)) == sizeof(text));
    flush_failure = 1;
    ssize_t complete = 0;
    assert(v->complete(h, &complete) == -1 && errno == EIO);
    flush_failure = 0;
    assert(v->close(h) == 0);
    void *dir = v->open(v, "/DS", O_DIR);
    assert(dir);
    int found = 0;
    const dirent_t *ent;
    while ((ent = v->readdir(dir))) if (!strcmp(ent->name, "DS_CORE.BIN")) ++found;
    assert(found == 1 && v->close(dir) == 0);
    unmount_test(ide);
    assert(close(image_fd) == 0);
    printf("VFS %s: mount/UTF-8/append/remount/fragmentation/flush%s passed (%u reads, %u writes, %u flushes)\n",
        kind == 64 ? "exFAT" : "FAT", large ? "/64-bit" : "", reads, writes, flushes);
    return 0;
}
