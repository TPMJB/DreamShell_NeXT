/* DreamShell NeXT BIOS Flasher.
 * Original (C) 2009-2025 SWAT, 2013 Yev. NeXT (C) 2026 contributors. */
#include "ds.h"
#include <drivers/bflash.h>
#include "../../maintenance_ui.h"
#include "../../maintenance_model.h"
DEFAULT_MODULE_EXPORTS(app_bios_flasher);

static struct {
    bflash_manufacturer_t *maker;
    bflash_dev_t *chip;
    size_t bank;
    int writable, sector_erase;
    char image[MA_PATH], folder[MA_PATH], backup[MA_PATH];
} self;

static void refresh(void) {
    ma_row(0, "Chip:  %s", self.chip ? self.chip->name : "Not detected");
    ma_row(1, "Visible bank:  %u KiB  /  %s", (unsigned)(self.bank / 1024),
        self.writable ? "Programming supported" : "Read only / unsupported layout");
    ma_row(2, "BIOS image:  %s", *self.image ? ma_tail(self.image, 54) : "Choose a file...");
    ma_row(3, "Backup folder:  %s", *self.folder ? ma_tail(self.folder, 50) : "Choose SD / IDE / PC...");
    ma_row(4, "Bank selection:  Physical switch on your hardware");
    ma_row(5, "Detect chip again");
    ma_text(ui.detail[0], "Exact full-bank images only. No partial offsets or cross-bank erases.");
    ma_text(ui.detail[1], "Write: verified backup first, full read-back verification afterwards.");
}
static void detect(void) {
    self.maker = NULL; self.chip = NULL; self.bank = 0; self.writable = 0;
    LockVideo();
    int rc = bflash_detect(&self.maker, &self.chip);
    UnlockVideo();
    if(rc < 0 || !self.chip) { self.chip = NULL; ma_status("No supported chip detected.", 1); refresh(); return; }
    size_t capacity = (size_t)self.chip->size * 1024;
    self.bank = capacity > 0x200000 ? 0x200000 : capacity;
    self.sector_erase = !!(self.chip->flags & F_FLASH_ERASE_SECTOR);
    self.writable = (self.chip->flags & F_FLASH_PROGRAM) &&
        (self.sector_erase || (self.chip->flags & F_FLASH_ERASE_ALL)) &&
        maintenance_bank_layout(capacity, self.bank, self.chip->sectors,
            self.chip->sec_count, !self.sector_erase) &&
        self.chip->page_size && !(self.bank % self.chip->page_size) &&
        !(65536 % self.chip->page_size);
    ma_status(self.writable ? "Chip detected. Select an image to compare or write." :
        "Chip detected. Backup and compare are available.", 0);
    ma_text(ui.note, "%s / ID %04X / %u KiB chip", self.maker ? self.maker->name : "Unknown maker",
        self.chip->id, self.chip->size);
    refresh();
}
static int read_chip(void *ctx, void *data, size_t size) {
    (void)ctx;
    if(!self.chip || size != self.bank) return -1;
    if(self.chip->flags & F_FLASH_PROGRAM) bflash_reset(self.chip);
    /* Complete the G1 read before starting any filesystem IO. */
    memcpy(data, (const void *)BIOS_FLASH_ADDR, size); return 0;
}
static int backup_chip(void *ctx, const void *data, size_t size) {
    (void)ctx;
    return ma_save_verified(self.folder, "bios-backup", "bin", data, size,
        self.backup, sizeof(self.backup));
}
static int unchanged(void *ctx, const void *old, size_t size) {
    (void)ctx; bflash_manufacturer_t *maker = NULL; bflash_dev_t *chip = NULL;
    LockVideo();
    int rc = bflash_detect(&maker, &chip);
    if(rc >= 0 && (chip != self.chip || maker != self.maker ||
            memcmp(old, (const void *)BIOS_FLASH_ADDR, size))) rc = -1;
    UnlockVideo(); return rc;
}
static void critical(void *ctx, int begin) {
    (void)ctx;
    if(begin) {
        ma_status("Writing BIOS. Keep power on; do not change the bank switch.", 1);
        ma_note("The display pauses during erase / programming. Exit is available after verification.");
        thd_sleep(50); LockVideo();
    } else { bflash_reset(self.chip); UnlockVideo(); }
}
static int erase_chip(void *ctx) {
    (void)ctx;
    if(!self.sector_erase) return bflash_erase_all(self.chip);
    for(unsigned i = 0; i < self.chip->sec_count && self.chip->sectors[i] < self.bank; i++)
        if(bflash_erase_sector(self.chip, self.chip->sectors[i]) < 0) return -1;
    return 0;
}
static int program(void *ctx, size_t offset, const void *data, size_t size) {
    (void)ctx;
    return bflash_write_data(self.chip, offset, (void *)data, size);
}
static void progress(void *ctx, int phase, size_t done, size_t total) {
    (void)ctx;
    if(phase == MF_BACKUP) ma_status("Saving and verifying the current BIOS backup...", 0);
    ma_progress(done, total);
}
static void run(int action) {
    if(!self.chip || !self.bank) { ma_status("Detect a supported chip first.", 1); return; }
    if(action == 3 && !self.writable) { ma_status("This chip or bank layout cannot be programmed.", 1); return; }
    uint8_t *old = memalign(32, self.bank), *image = NULL;
    if(action != 1) image = memalign(32, self.bank);
    if(!old || (action != 1 && !image)) {
        ma_status("Not enough free RAM. Nothing was written.", 1); goto cleanup;
    }
    ma_busy(1); self.backup[0] = 0;
    if(action != 1 && ma_load(self.image, image, self.bank) < 0) {
        ma_status("Image must be readable and exactly match the visible bank size.", 1); goto done;
    }
    if(action == 1) {
        read_chip(NULL, old, self.bank);
        ma_status("Saving and verifying BIOS backup...", 0);
        int rc = backup_chip(NULL, old, self.bank);
        ma_status(rc ? "Backup failed verification. Do not use this file to restore." : "BIOS backup saved and verified.", rc != 0);
        ma_note(self.backup);
    } else if(action == 2) {
        read_chip(NULL, old, self.bank);
        size_t i; for(i = 0; i < self.bank && old[i] == image[i]; i++) {}
        ma_status(i == self.bank ? "Exact match. The image equals the current BIOS bank." : "The image differs from the current BIOS bank.", 0);
        if(i != self.bank) ma_text(ui.note, "First difference at 0x%06lX: chip %02X / image %02X", (unsigned long)i, old[i], image[i]);
        else ma_note(ma_tail(self.image, 80));
    } else {
        maintenance_flash_ops ops = {NULL, read_chip, backup_chip, unchanged, critical,
            erase_chip, program, progress};
        /* Reuse the original snapshot only after its verified backup and the
         * unchanged check. Saves 2 MiB on a stock Dreamcast. */
        int result = maintenance_flash_run(&ops, image, self.bank, 65536, old, old);
        static const char *messages[] = {"", "BIOS read failed. Nothing erased.",
            "Backup failed verification. Nothing erased.", "Chip or bank changed. Nothing erased.",
            "Erase failed. BIOS may be incomplete; retain the backup.",
            "Programming failed. BIOS is incomplete; retain the backup.",
            "Verification failed. BIOS differs from the image; retain the backup.",
            "BIOS written and fully verified. Backup retained."};
        ma_status(messages[result], result != MF_DONE); ma_note(self.backup);
    }
    ma_progress(1, 1);
done:
    ma_busy(0);
cleanup:
    free(image); free(old);
}
static void picked(int purpose, const char *path) {
    snprintf(purpose ? self.folder : self.image, MA_PATH, "%s", path);
    refresh(); ma_status(purpose ? "Backup folder selected." : "Image selected. Compare before writing if unsure.", 0); ma_note(path);
}
static void row(int index, int step) {
    (void)step;
    if(index == 2) ma_browse(0, 0, self.folder);
    else if(index == 3) ma_browse(1, 1, self.folder);
    else if(index == 5) detect();
}
static void action(int index) {
    if(index < 2) { run(index + 1); return; }
    if(!self.writable || !*self.image || !ma_persistent(self.folder)) {
        ma_status("Choose an image, a backup folder and a programmable chip.", 1); return;
    }
    char text[600];
    snprintf(text, sizeof(text), "Chip: %s\nSelected hardware bank: %u KiB\nImage: %s\n\nA verified backup is required before erase.\nThe image will be read back and compared.\nKeep power on and the bank switch unchanged.\n\nWrite this BIOS bank?", self.chip->name,
        (unsigned)(self.bank / 1024), ma_tail(self.image, 52));
    ma_ask(3, "Write BIOS bank", text);
}
void BiosFlasher_Init(App_t *app) {
    memset(&self, 0, sizeof(self)); ma_init(app, "NextBiosInput");
    snprintf(self.folder, sizeof(self.folder), "%s", ma_default_folder());
    ui.row = row; ui.action = action; ui.confirm = run; ui.picked = picked; detect();
}
MAINTENANCE_CALLBACKS(BiosFlasher)
