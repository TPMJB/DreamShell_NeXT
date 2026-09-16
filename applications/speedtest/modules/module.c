/* DreamShell NeXT storage benchmark. Original (C) 2014 megavolt85,
 * 2014-2026 SWAT. Verified file tests and native UI (C) 2026 contributors. */
#include "ds.h"
#include <dc/sd.h>
#include <dc/g1ata.h>
#include <kos/blockdev.h>
#include "../../maintenance_ui.h"
#include "../../maintenance_model.h"
DEFAULT_MODULE_EXPORTS(app_speedtest);

#define TEST_BUFFER (256 * 1024)
static const unsigned sizes[] = {2, 8, 32};
static const unsigned repeats[] = {1, 3, 5};
static const char *modes[] = {"File write + verify", "Read an existing file", "Raw device read only"};
static struct {
    int mode, size, repeat, runs;
    char folder[MA_PATH], file[MA_PATH], reports[MA_PATH];
    char report[12288], summary[180];
    double read, write;
} self;
static void refresh(void) {
    ma_row(0, "Test:  %s", modes[self.mode]);
    const char *path = self.mode == 1 ? self.file : self.folder;
    ma_row(1, "%s:  %s", self.mode == 1 ? "Source file" : "Test folder", *path ? ma_tail(path, 54) : "Choose...");
    ma_row(2, "%s:  %u MiB", self.mode == 1 ? "Maximum read" : "Data per pass", sizes[self.size]);
    ma_row(3, "Passes:  %u  /  Buffer: 256 KiB", repeats[self.repeat]);
    ma_row(4, "Report folder:  %s", *self.reports ? ma_tail(self.reports, 50) : "Choose...");
    ma_row(5, "Results:  %d runs  /  A to view latest", self.runs);
    ma_text(ui.detail[0], "Last run: write %.2f MiB/s / read %.2f MiB/s", self.write, self.read);
    ma_text(ui.detail[1], self.mode == 0 ? "Unique temporary file; reopen, verify every byte, then remove it." :
        self.mode == 1 ? "Reads the selected file without changing it. Suitable for GD-ROM too." :
        "Raw reads start at sector 0 of the selected SD / IDE device. No raw writes.");
}
static const char *route(void) {
    const char *p = self.mode == 1 ? self.file : self.folder;
    if(!strncmp(p, "/sd", 3)) return sd_get_interface() == SD_IF_SCI ? "SD SCI-SPI" : "SD SCIF-SPI";
    if(!strncmp(p, "/ide", 4)) return "IDE G1 ATA";
    if(!strncmp(p, "/cd", 3)) return "GD-ROM";
    if(!strncmp(p, "/pc", 3)) return "PC link";
    return "mounted filesystem";
}
/* IO time excludes pattern generation/checking and GUI work. Close/flush time
 * is included. Reports also include end-to-end wall time; caching is not disabled. */
static int file_pass(uint8_t *buffer, uint64_t *written, uint64_t *readbytes,
        uint64_t *write_ns, uint64_t *read_ns, char *leftover) {
    char temp[MA_PATH] = "";
    const char *path = self.file;
    file_t fd = FILEHND_INVALID;
    int rc = -1, owned = 0;
    size_t limit = (size_t)sizes[self.size] * 1048576;
    if(self.mode == 0) {
        fd = ma_create(self.folder, "next-speedtest", "tmp", temp, sizeof(temp));
        if(fd == FILEHND_INVALID) { ma_status("Cannot create a unique test file in this folder.", 1); return -1; }
        owned = 1; path = temp;
        ma_status("Writing temporary test data. B stops after this chunk.", 0);
        for(size_t pos = 0; pos < limit; pos += TEST_BUFFER) {
            if(!ma_pump()) { rc = -2; goto cleanup; }
            size_t n = limit - pos < TEST_BUFFER ? limit - pos : TEST_BUFFER;
            for(size_t i = 0; i < n; i++) buffer[i] = maintenance_pattern(pos + i);
            uint64_t start = timer_ns_gettime64();
            int ok = ma_write_exact(fd, buffer, n);
            *write_ns += timer_ns_gettime64() - start;
            if(ok < 0) { ma_status("Write failed. Storage may be full or disconnected.", 1); goto cleanup; }
            *written += n; ma_progress(pos + n, limit * 2);
        }
        uint64_t start = timer_ns_gettime64(); int ok = fs_close(fd);
        *write_ns += timer_ns_gettime64() - start; fd = FILEHND_INVALID;
        if(ok < 0) { ma_status("Closing / flushing the test file failed.", 1); goto cleanup; }
    }
    fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID) { ma_status("Cannot open the file for reading.", 1); goto cleanup; }
    size_t actual = fs_total(fd);
    if(!actual || actual == (size_t)-1 || (owned && actual != limit)) {
        ma_status("The file is empty, unreadable or has an unexpected size.", 1); goto cleanup;
    }
    if(actual < limit) limit = actual;
    ma_status(owned ? "Reading back and verifying every byte. B stops." : "Reading existing file. B stops after this chunk.", 0);
    for(size_t pos = 0; pos < limit; pos += TEST_BUFFER) {
        if(!ma_pump()) { rc = -2; goto cleanup; }
        size_t n = limit - pos < TEST_BUFFER ? limit - pos : TEST_BUFFER;
        uint64_t start = timer_ns_gettime64(); int ok = ma_read_exact(fd, buffer, n);
        *read_ns += timer_ns_gettime64() - start;
        if(ok < 0) { ma_status("Read failed before the expected end of the file.", 1); goto cleanup; }
        if(owned) for(size_t i = 0; i < n; i++) if(buffer[i] != maintenance_pattern(pos + i)) {
            ma_status("DATA MISMATCH. Read-back verification failed.", 1);
            ma_append(self.report, sizeof(self.report), "Mismatch at byte %lu: expected %02X, got %02X\n",
                (unsigned long)(pos + i), maintenance_pattern(pos + i), buffer[i]); goto cleanup;
        }
        *readbytes += n; ma_progress((owned ? limit : 0) + pos + n, limit * (owned ? 2 : 1));
    }
    { uint64_t start = timer_ns_gettime64(); int ok = fs_close(fd);
      *read_ns += timer_ns_gettime64() - start; fd = FILEHND_INVALID;
      if(ok < 0) { ma_status("Closing the read file failed.", 1); goto cleanup; } }
    rc = 0;
 cleanup:
    if(fd != FILEHND_INVALID && fs_close(fd) < 0 && rc == 0) rc = -1;
    if(owned && fs_unlink(temp) < 0) {
        snprintf(leftover, MA_PATH, "%s", temp); rc = -3;
        ma_status("Test file cleanup failed. Remove the file shown below.", 1);
    }
    return rc;
}
static int raw_pass(uint8_t *buffer, uint64_t *readbytes, uint64_t *read_ns) {
    kos_blockdev_t dev; memset(&dev, 0, sizeof(dev));
    int rv = -1;
    if(!strncmp(self.folder, "/sd", 3)) rv = sd_blockdev_for_device(&dev);
    else if(!strncmp(self.folder, "/ide", 4)) rv = g1_ata_blockdev_for_device(0, &dev);
    if(rv < 0) { ma_status("Raw reads require an available SD or IDE device.", 1); return -1; }
    int rc = -1;
    if(dev.l_block_size != 9 || !dev.count_blocks || !dev.read_blocks) {
        ma_status("Unsupported raw device block size or interface.", 1); goto done;
    }
    uint64_t bytes = (uint64_t)sizes[self.size] * 1048576;
    uint64_t available = dev.count_blocks(&dev);
    if(!available || available == UINT64_MAX) { ma_status("Cannot read device capacity.", 1); goto done; }
    if(available < bytes / 512) bytes = available * 512;
    ma_status("Reading device sectors. B stops after this chunk.", 0);
    for(uint64_t pos = 0; pos < bytes; pos += TEST_BUFFER) {
        if(!ma_pump()) { rc = -2; goto done; }
        size_t n = bytes - pos < TEST_BUFFER ? (size_t)(bytes - pos) : TEST_BUFFER;
        uint64_t start = timer_ns_gettime64();
        rv = dev.read_blocks(&dev, pos / 512, n / 512, buffer);
        *read_ns += timer_ns_gettime64() - start;
        if(rv < 0) { ma_status("Raw device read failed.", 1); goto done; }
        *readbytes += n; ma_progress(pos + n, bytes);
    }
    rc = 0;
 done:
    if(dev.shutdown) dev.shutdown(&dev);
    return rc;
}
static void run(void) {
    if(strlen(self.report) > sizeof(self.report) - 2200) {
        ma_status("Report is full. Save it, then clear results before another run.", 1); return;
    }
    uint8_t *buffer = memalign(32, TEST_BUFFER);
    if(!buffer) { ma_status("Cannot allocate the test buffer.", 1); return; }
    ma_busy(1);
    uint64_t written = 0, readbytes = 0, write_ns = 0, read_ns = 0;
    uint64_t start = timer_ns_gettime64();
    int rc = 0; unsigned completed = 0; char leftover[MA_PATH] = "";
    ma_append(self.report, sizeof(self.report), "\nRun %d / %s / %s\nPath: %s\n%u MiB maximum per pass; %u passes; 256 KiB buffer\n",
        ++self.runs, modes[self.mode], route(), self.mode == 1 ? self.file : self.folder, sizes[self.size], repeats[self.repeat]);
    for(unsigned pass = 0; pass < repeats[self.repeat]; pass++) {
        ma_text(ui.note, "Pass %u of %u / %s", pass + 1, repeats[self.repeat], route());
        rc = self.mode == 2 ? raw_pass(buffer, &readbytes, &read_ns) :
            file_pass(buffer, &written, &readbytes, &write_ns, &read_ns, leftover);
        if(rc) break;
        completed++;
    }
    self.write = maintenance_mibps(written, write_ns);
    self.read = maintenance_mibps(readbytes, read_ns);
    const char *result = !rc ? "PASS" : rc == -2 ? "STOPPED" : "FAILED";
    snprintf(self.summary, sizeof(self.summary), "%s: %u/%u passes / write %.2f / read %.2f MiB/s",
        result, completed, repeats[self.repeat], self.write, self.read);
    ma_append(self.report, sizeof(self.report), "%s\nWrite: %llu bytes / %llu ns\nRead: %llu bytes / %llu ns\nWall: %.3f s\nVerification: %s\n",
        self.summary, (unsigned long long)written, (unsigned long long)write_ns,
        (unsigned long long)readbytes, (unsigned long long)read_ns,
        (timer_ns_gettime64() - start) / 1000000000.0,
        self.mode != 0 ? "not applicable (read only)" : !rc ? "every byte matched" : "incomplete / failed");
    if(*leftover) ma_append(self.report, sizeof(self.report), "REMOVE TEMP FILE: %s\n", leftover);
    if(!rc || rc == -2) ma_status(self.summary, rc != 0);
    if(*leftover) ma_note(leftover);
    else ma_note("MiB/s measures IO calls plus close. Caches stay enabled; compare the same mode.");
    if(!rc) ma_progress(1, 1);
    free(buffer); ma_busy(0); refresh();
}
static void save_report(void) {
    if(!self.runs) { ma_status("Run a test before saving a report.", 0); return; }
    char path[MA_PATH] = "";
    int rc = ma_save_verified(self.reports, "speedtest", "txt", self.report, strlen(self.report), path, sizeof(path));
    ma_status(rc ? "Could not save and verify the report." : "Speedtest report saved and verified.", rc != 0); ma_note(path);
}
static void confirm(int action) {
    if(action == 2) {
        self.runs = 0; self.write = self.read = 0; self.summary[0] = 0;
        snprintf(self.report, sizeof(self.report), "K-UI Speedtest\nIO timings include close/flush, exclude pattern and GUI work.\nCaches remain enabled. Repeated reads may be cached.\nFailed/stopped runs contain partial timings, not successful benchmark results.\n");
        refresh(); ma_status("Results cleared.", 0); ma_note("");
    }
}
static void picked(int purpose, const char *path) {
    snprintf(purpose == 2 ? self.reports : purpose == 1 ? self.file : self.folder, MA_PATH, "%s", path);
    refresh(); ma_status("Location selected.", 0); ma_note(path);
}
static void row(int index, int step) {
    if(index == 0) self.mode = (self.mode + step + 3) % 3;
    else if(index == 1) { ma_browse(self.mode == 1 ? 1 : 0, self.mode != 1, self.folder); return; }
    else if(index == 2) self.size = (self.size + step + 3) % 3;
    else if(index == 3) self.repeat = (self.repeat + step + 3) % 3;
    else if(index == 4) { ma_browse(2, 1, self.reports); return; }
    else if(index == 5) { ma_ask(99, "Latest storage result", *self.summary ? self.summary : "No results yet. Choose a target and run a test."); return; }
    refresh();
}
static void action(int index) {
    if(index == 0) run();
    else if(index == 1) save_report();
    else ma_ask(2, "Clear results?", "Save your report first if you want to keep these results.\nClear this session's results?");
}
void Speedtest_Init(App_t *app) {
    memset(&self, 0, sizeof(self)); ma_init(app, "NextSpeedtestInput");
    snprintf(self.folder, MA_PATH, "%s", ma_default_folder());
    snprintf(self.reports, MA_PATH, "%s", self.folder); self.size = 1;
    ui.row = row; ui.action = action; ui.confirm = confirm; ui.picked = picked;
    confirm(2); ma_status("Choose a target and a test mode, then Run test.", 0);
}
MAINTENANCE_CALLBACKS(Speedtest)
