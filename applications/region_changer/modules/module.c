/* DreamShell NeXT Region Changer. Original (C) 2007-2016 SWAT.
 * Transactional native frontend (C) 2026 contributors. */
#include "ds.h"
#include <dc/flashrom.h>
#include "../../maintenance_ui.h"
#include "../../maintenance_model.h"
#include "region_flash.h"
DEFAULT_MODULE_EXPORTS(app_region_changer);

static struct {
    uint8_t current[8192];
    int loaded, writable, region, language, broadcast, black, advanced, block;
    int part, start, size;
    uint8_t *check;
    char folder[MA_PATH], restore[MA_PATH], backup[MA_PATH];
} self;
static const char *regions[] = {"Japan", "USA", "Europe"};
static const char *languages[] = {"Japanese", "English", "German", "French", "Spanish", "Italian"};
static const char *broadcasts[] = {"NTSC", "PAL", "PAL-M", "PAL-N"};
static const int partitions[] = {FLASHROM_PT_BLOCK_1, FLASHROM_PT_SETTINGS};
static const char *blocks[] = {"Block 1 (16 KiB)", "Game settings (32 KiB)"};
static const volatile uint8_t *const physical_flash = (const volatile uint8_t *)0xa0200000;
static int layout(int part, int *start, int *size) {
    return part >= 0 && part < 5 && flashrom_info(part, start, size) == 0 &&
        region_partition_matches(part, *start, *size);
}
static int dirty(void) {
    if(!self.loaded) return 0;
    uint8_t draft[8192]; memcpy(draft, self.current, sizeof(draft));
    maintenance_factory_edit(draft, self.region, self.language, self.broadcast, self.black);
    return memcmp(draft, self.current, sizeof(draft));
}
static void load_current(void) {
    int start, size;
    self.writable = layout(FLASHROM_PT_SYSTEM, &start, &size);
    self.loaded = region_flash_read(physical_flash, REGION_FACTORY_START,
        self.current, sizeof(self.current)) == 0 &&
        maintenance_factory_valid(self.current, sizeof(self.current));
    if(self.loaded) {
        self.black = self.current[2] >= 'X';
        self.region = self.current[2] - (self.black ? 'X' : '0');
        self.language = self.current[3] - '0'; self.broadcast = self.current[4] - '0';
    }
}
static void read_status(void) {
    if(!self.loaded) {
        ma_status("Factory fields are unrecognized. Back up or restore a valid file.", 1);
        ma_note("A on an unknown field retries the read. Menu remains available.");
    } else if(!self.writable) {
        ma_status("Settings read. This boot BIOS permits viewing and backup only.", 0);
        ma_note("The BIOS did not confirm the flash partition; writes are unavailable.");
    } else {
        ma_status("Stored settings read. Edits stay in a draft until applied.", 0);
        ma_note("Reading needs no modification. Writing factory settings needs compatible hardware.");
    }
}
static void refresh(void) {
    if(self.advanced) {
        ma_row(0, "ADVANCED  /  Flash settings partitions");
        ma_row(1, "Selected partition:  %s", blocks[self.block]);
        ma_row(2, "Clear erases every setting in the selected partition");
        ma_row(3, "A verified partition backup is required before erase");
        ma_row(4, "Backup folder:  %s", *self.folder ? ma_tail(self.folder, 50) : "Choose folder...");
        ma_row(5, "Return to region settings");
        ma_text(ui.detail[0], "These are console flash settings, separate from VMU save files.");
        ma_text(ui.detail[1], "Restore uses the selected partition and requires its exact byte count.");
    } else {
        ma_row(0, "Region:  %s%s", self.loaded ? regions[self.region] : "Unknown / A to reread", dirty() ? "  [draft]" : "");
        ma_row(1, "Language:  %s", self.loaded ? languages[self.language] : "Unknown");
        ma_row(2, "Broadcast:  %s", self.loaded ? broadcasts[self.broadcast] : "Unknown");
        ma_row(3, "Black swirl:  %s", self.loaded ? (self.black ? "On" : "Off") : "Unknown");
        ma_row(4, "Backup folder:  %s", *self.folder ? ma_tail(self.folder, 50) : "Choose folder...");
        ma_row(5, "Advanced:  Backup / restore / clear other settings");
        if(self.loaded) {
            int r = self.current[2] - (self.current[2] >= 'X' ? 'X' : '0');
            ma_text(ui.detail[0], "On console: %s / %s / %s / %s swirl", regions[r],
                languages[self.current[3]-'0'], broadcasts[self.current[4]-'0'], self.current[2] >= 'X' ? "black" : "normal");
        } else ma_text(ui.detail[0], "Unknown factory fields: region %02X / language %02X / broadcast %02X",
            self.current[2], self.current[3], self.current[4]);
        ma_text(ui.detail[1], "Edits console region, language, video standard and swirl. Applies after restart.");
    }
    GUI_LabelSetText(GUI_ButtonGetCaption(ui.actions[2]), self.advanced ? "Clear partition" : "Apply changes");
}
static int read_flash(void *ctx, void *data, size_t size) {
    (void)ctx; return region_flash_read(physical_flash, self.start, data, size);
}
static int save_backup(void *ctx, const void *data, size_t size) {
    (void)ctx; char stem[40]; snprintf(stem, sizeof(stem), "flash-part%d-backup", self.part);
    return ma_save_verified(self.folder, stem, "bin", data, size, self.backup, sizeof(self.backup));
}
static int unchanged(void *ctx, const void *old, size_t size) {
    (void)ctx; int start, bytes;
    return layout(self.part, &start, &bytes) && start == self.start && bytes == self.size &&
        read_flash(NULL, self.check, size) == 0 && !memcmp(old, self.check, size) ? 0 : -1;
}
static void critical(void *ctx, int begin) {
    (void)ctx;
    if(begin) {
        ma_status("Updating flash. Keep power on until verification completes.", 1);
        ma_note("Exit is available after erase, write and verification finish.");
        thd_sleep(50); LockVideo();
    } else UnlockVideo();
}
static int erase_flash(void *ctx) { (void)ctx; return flashrom_delete(self.start); }
static int write_flash(void *ctx, size_t pos, const void *data, size_t size) {
    (void)ctx;
    if(pos > (size_t)self.size || size > (size_t)self.size - pos) return -1;
    int start = self.start + (int)pos;
    if(!region_flash_range(start, size)) return -1;
    int result = flashrom_write(start, (void *)data, size);
    return region_flash_written(physical_flash, start, data, size, result);
}
static void progress(void *ctx, int phase, size_t done, size_t size) {
    (void)ctx;
    if(phase == MF_BACKUP) ma_status("Saving and verifying the original flash partition...", 0);
    ma_progress(done, size);
}
static void run(int action) {
    if(action == 5) { load_current(); OpenMainApp(); return; }
    self.part = self.advanced ? partitions[self.block] : FLASHROM_PT_SYSTEM;
    if(!layout(self.part, &self.start, &self.size)) {
        if(action != 1 || !region_partition(self.part, &self.start, &self.size)) {
            ma_status("BIOS flash layout unavailable or unexpected. Writes are blocked.", 1); return;
        }
    }
    uint8_t *old = memalign(32, self.size), *image = memalign(32, self.size);
    self.check = memalign(32, self.size);
    if(!old || !image || !self.check) { ma_status("Not enough free RAM. Nothing was written.", 1); goto cleanup; }
    ma_busy(1); self.backup[0] = 0;
    if(read_flash(NULL, image, self.size) < 0) { ma_status("Could not read the current flash partition.", 1); goto done; }
    if(action == 1) {
        int rc = save_backup(NULL, image, self.size);
        ma_status(rc ? "Backup failed verification. Do not restore from this file." : "Flash partition backup saved and verified.", rc != 0);
        ma_note(self.backup); goto done;
    }
    if(action == 2) {
        /* Preserve all factory bytes other than the three edited fields. */
        if(!self.loaded || memcmp(image, self.current, 8192)) {
            ma_status("Factory data changed since opening. Reopen the app to review it.", 1); goto done;
        }
        maintenance_factory_edit(image, self.region, self.language, self.broadcast, self.black);
    } else if(action == 3) {
        if(ma_load(self.restore, image, self.size) < 0 ||
           (self.part == FLASHROM_PT_SYSTEM && !maintenance_factory_valid(image, self.size))) {
            ma_status("Restore rejected: wrong size, unreadable file or invalid factory fields.", 1); goto done;
        }
    } else if(action == 4) memset(image, 0xff, self.size);
    maintenance_flash_ops ops = {NULL, read_flash, save_backup, unchanged, critical,
        erase_flash, action == 4 ? NULL : write_flash, progress};
    int result = maintenance_flash_run(&ops, image, self.size, 4096, old, self.check);
    static const char *messages[] = {"", "Flash read failed. Nothing erased.",
        "Backup failed verification. Nothing erased.", "Partition changed. Nothing erased.",
        "Erase failed. Check hardware / write protection; keep the backup.",
        "Write failed. Settings may be incomplete; keep the backup.",
        "Verification failed. Settings differ from the requested data.",
        "Flash update verified. Original backup retained."};
    ma_status(messages[result], result != MF_DONE); ma_note(self.backup);
    if(self.part == FLASHROM_PT_SYSTEM) load_current();
    refresh(); ma_progress(1, 1);
 done:
    ma_busy(0);
 cleanup:
    free(old); free(image); free(self.check); self.check = NULL;
}
static void ask(int action) {
    char body[700];
    if(action == 2) snprintf(body, sizeof(body), "New region: %s\nLanguage: %s\nBroadcast: %s\nBlack swirl: %s\n\nThe current factory partition is backed up first.\nKeep power on until verification completes.\nApply these changes?", regions[self.region], languages[self.language], broadcasts[self.broadcast], self.black ? "On" : "Off");
    else snprintf(body, sizeof(body), "%s\nTarget: %s\n%s\n\nThe current partition is backed up first.\nKeep power on until verification completes.\n%s", action == 4 ? "Erase all settings in this partition." : "Restore a flash partition from file.", self.advanced ? blocks[self.block] : "Factory settings (8 KiB)", action == 3 ? ma_tail(self.restore, 55) : "VMU files are unaffected.", action == 4 ? "Clear this partition?" : "Restore this partition?");
    ma_ask(action, action == 2 ? "Review region changes" : action == 4 ? "Clear flash settings" : "Restore flash settings", body);
}
static void picked(int purpose, const char *path) {
    if(purpose == 0) { snprintf(self.folder, MA_PATH, "%s", path); refresh(); ma_status("Backup folder selected.", 0); ma_note(path); }
    else { snprintf(self.restore, MA_PATH, "%s", path); ask(3); }
}
static void row(int index, int step) {
    if(index == 4) { ma_browse(0, 1, self.folder); return; }
    if(!self.advanced && index < 4 && (!self.loaded || !self.writable)) {
        load_current(); refresh(); read_status(); return;
    }
    if(index == 5) self.advanced = !self.advanced;
    else if(self.advanced) { if(index == 1) self.block = !self.block; }
    else if(index == 0) self.region = (self.region + step + 3) % 3;
    else if(index == 1) self.language = (self.language + step + 6) % 6;
    else if(index == 2) self.broadcast = (self.broadcast + step + 4) % 4;
    else if(index == 3) self.black = !self.black;
    refresh();
    if(!self.advanced && (!self.loaded || !self.writable)) read_status();
    else { ma_status(dirty() ? "Draft changes. Review and apply when ready." : "Ready. No pending region changes.", 0); ma_note(""); }
}
static void action(int index) {
    if(index == 0) run(1);
    else if(!self.advanced && !self.writable) read_status();
    else if(index == 1) ma_browse(1, 0, self.folder);
    else if(!self.advanced && !self.loaded) read_status();
    else if(!self.advanced && !dirty()) ma_status("No region changes to apply.", 0);
    else ask(self.advanced ? 4 : 2);
}
static void back(void) {
    if(dirty()) ma_ask(5, "Discard draft changes?", "These edits have not been written to flash.\nReturn to the menu and discard them?");
    else OpenMainApp();
}
static void opened(void) { load_current(); refresh(); read_status(); }
void RegionChanger_Init(App_t *app) {
    memset(&self, 0, sizeof(self)); ma_init(app, "NextRegionInput");
    snprintf(self.folder, MA_PATH, "%s", ma_default_folder());
    ui.row = row; ui.action = action; ui.confirm = run; ui.picked = picked; ui.back = back;
    ui.opened = opened;
}
MAINTENANCE_CALLBACKS(RegionChanger)
