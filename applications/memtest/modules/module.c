/* DreamShell NeXT Memtest. Memory engine (C) 2026 SWAT, unchanged.
 * Native interface and diagnostic reports (C) 2026 contributors. */
#include "ds.h"
#include <dc/spu.h>
#include "memtest_core.h"
#include "../../maintenance_ui.h"
DEFAULT_MODULE_EXPORTS(app_memtest);

static const unsigned cycles[] = {1, 3, 10};
static const char *subnames[] = {"Data bus", "Address bus", "Device patterns"};
static struct {
    memtest_plan_t plan;
    memtest_region_t results[MEMTEST_REGIONS_MAX];
    int errors[MEMTEST_REGIONS_MAX], completed[MEMTEST_REGIONS_MAX];
    int options, cycle, stop_first, ran, saved;
    char folder[MA_PATH], report[24576];
} self;
static const char *state(int status) {
    switch(status) {
        case MEMTEST_ST_PASS: return "PASS";
        case MEMTEST_ST_FAIL: return "FAIL";
        case MEMTEST_ST_SKIPPED: return "SKIPPED";
        case MEMTEST_ST_RUNNING: return "RUNNING";
        default: return "Not tested";
    }
}
static void refresh(void) {
    if(self.options) {
        ma_row(0, "Mode:  %s", self.plan.quick ? "Quick / data + address bus" : "Full / all three subtests");
        ma_row(1, "Passes:  %u", cycles[self.cycle]);
        ma_row(2, "Stop on first failure:  %s", self.stop_first ? "On" : "Off");
        ma_row(3, "Report folder:  %s", *self.folder ? ma_tail(self.folder, 50) : "Choose...");
        ma_row(4, "Coverage:  A to read what the test covers");
        ma_row(5, "Return to regions and results");
    } else {
        for(int i = 0; i < 5; i++) {
            if(i < self.plan.region_count) {
                memtest_region_t *r = &self.plan.regions[i], *result = &self.results[i];
                ma_row(i, "%s  %-12s  %u MiB  /  %s%s", r->enabled ? "[x]" : "[ ]", r->name,
                    (unsigned)(r->size / 1048576), self.errors[i] ? "ERROR" : state(result->status),
                    self.completed[i] ? "  (details: X)" : "");
            } else ma_row(i, "--");
        }
        ma_row(5, "%s  /  %s  /  %u passes", self.plan.hw_name,
            self.plan.quick ? "Quick" : "Full", cycles[self.cycle]);
    }
    ma_text(ui.detail[0], "A toggles a region. Left / X shows its last subtests. Options sets the test mode.");
    ma_text(ui.detail[1], "Screen / sound may pause. B stops between regions; wait for the current test.");
}
static void region_details(int index) {
    if(index >= self.plan.region_count) return;
    memtest_region_t *r = &self.results[index];
    char body[900] = "";
    ma_append(body, sizeof(body), "%s / %s / %d completed passes\n", self.plan.regions[index].name,
        self.errors[index] ? "Engine / allocation error" : state(r->status), self.completed[index]);
    for(int s = 0; s < 3; s++) {
        memtest_sub_t *sub = &r->sub[s];
        ma_append(body, sizeof(body), "\n%s: %s", subnames[s], state(sub->status));
        if(sub->status == MEMTEST_ST_FAIL) ma_append(body, sizeof(body),
            "\nAddress %08lX / expected %08lX / got %08lX",
            (unsigned long)sub->fail_addr, (unsigned long)sub->expected, (unsigned long)sub->actual);
    }
    ma_append(body, sizeof(body), "\n\nFirst failure is retained across subsequent passes.");
    ma_ask(99, "Memory region results", body);
}
static void run(void) {
    int enabled = 0;
    for(int i = 0; i < self.plan.region_count; i++) enabled += !!self.plan.regions[i].enabled;
    if(!enabled) { ma_status("Select at least one memory region.", 1); return; }
    ma_busy(1); self.plan.cancel = 0; self.ran = 1; self.saved = 0; self.options = 0;
    memset(self.results, 0, sizeof(self.results));
    memset(self.errors, 0, sizeof(self.errors)); memset(self.completed, 0, sizeof(self.completed));
    snprintf(self.report, sizeof(self.report), "DreamShell NeXT Memtest\nHardware: %s\nMode: %s\nRequested passes: %u\nStop on failure: %s\nCoverage: detected memory regions; live RAM execution island excluded.\nEngine backs up/restores tested chunks. Quick omits device patterns.\nStopping takes effect between regions. No claim of exhaustive hardware coverage.\n",
        self.plan.hw_name, self.plan.quick ? "Quick" : "Full", cycles[self.cycle], self.stop_first ? "yes" : "no");
    int failures = 0, tested = 0, stop = 0;
    for(unsigned pass = 0; pass < cycles[self.cycle] && !stop; pass++) {
        for(int i = 0; i < self.plan.region_count; i++) {
            memtest_region_t *r = &self.plan.regions[i];
            if(!r->enabled) {
                if(!self.completed[i]) self.results[i].status = MEMTEST_ST_SKIPPED;
                continue;
            }
            if(!ma_pump()) { self.plan.cancel = 1; stop = 1; break; }
            /* Reset this pass, while keeping the first failed result separately. */
            memset(r->sub, 0, sizeof(r->sub)); r->status = MEMTEST_ST_IDLE;
            r->fail_addr = 0; r->msec = 0;
            char text[150]; snprintf(text, sizeof(text), "Pass %u/%u: testing %s. Please wait...", pass + 1, cycles[self.cycle], r->name);
            ma_status(text, 0);
            ma_note("The current memory test must finish before B can stop the remaining regions.");
            ma_row(i, "[x]  %s  /  RUNNING", r->name);
            thd_sleep(50);
            int video = r->type == MEMTEST_REGION_RAM || r->type == MEMTEST_REGION_VRAM;
            if(video) { ShutdownVideoThread(); pvr_wait_ready(); pvr_wait_render_done(); }
            if(r->type == MEMTEST_REGION_AICA) spu_disable();
            int rc = memtest_run_region(&self.plan, i);
            if(r->type == MEMTEST_REGION_AICA) spu_enable();
            if(video) {
                InitVideoThread();
                LockVideo(); GUI_ScreenDoUpdate(GUI_GetScreen(), 1); UnlockVideo();
            }
            int data_failure = 0;
            for(int s = 0; s < 3; s++) data_failure |= r->sub[s].status == MEMTEST_ST_FAIL;
            int error = rc < 0 && !data_failure;
            int failed = rc < 0 || r->status == MEMTEST_ST_FAIL;
            if(failed) failures++;
            if(self.results[i].status != MEMTEST_ST_FAIL && !self.errors[i]) {
                self.results[i] = *r; self.errors[i] = error;
            }
            self.completed[i]++; tested++;
            ma_append(self.report, sizeof(self.report), "\nPass %u / %s / %s / %lu bytes at %08lX / %lu ms\n",
                pass + 1, r->name, error ? "ENGINE ERROR" : state(r->status),
                (unsigned long)r->size, (unsigned long)r->base, (unsigned long)r->msec);
            for(int s = 0; s < 3; s++) ma_append(self.report, sizeof(self.report),
                "%s: %s / address %08lX / expected %08lX / actual %08lX\n", subnames[s],
                state(r->sub[s].status), (unsigned long)r->sub[s].fail_addr,
                (unsigned long)r->sub[s].expected, (unsigned long)r->sub[s].actual);
            refresh(); ma_progress(tested, enabled * cycles[self.cycle]);
            if(failed && self.stop_first) { stop = 1; break; }
        }
    }
    char status[150];
    snprintf(status, sizeof(status), "%s: %d/%u region tests completed, %d failures / errors.",
        failures ? "FAILED" : self.plan.cancel ? "STOPPED" : "PASS", tested,
        enabled * cycles[self.cycle], failures);
    ma_append(self.report, sizeof(self.report), "\n%s\n", status);
    ma_status(status, failures != 0); ma_note("X shows subtests for the selected region. Save report keeps the full run history.");
    ma_busy(0); refresh();
}
static void save_report(void) {
    if(!self.ran) { ma_status("Run a test before saving a report.", 0); return; }
    char path[MA_PATH] = "";
    int rc = ma_save_verified(self.folder, "memtest", "txt", self.report, strlen(self.report), path, sizeof(path));
    if(!rc) self.saved = 1;
    ma_status(rc ? "Could not save and verify the report." : "Memory test report saved and verified.", rc != 0); ma_note(path);
}
static void confirm(int action) { if(action == 1) run(); else if(action == 2) OpenMainApp(); }
static void picked(int purpose, const char *path) {
    (void)purpose; snprintf(self.folder, MA_PATH, "%s", path);
    refresh(); ma_status("Report folder selected.", 0); ma_note(path);
}
static void row(int index, int step) {
    if(self.options) {
        if(index == 0) self.plan.quick = !self.plan.quick;
        else if(index == 1) self.cycle = (self.cycle + step + 3) % 3;
        else if(index == 2) self.stop_first = !self.stop_first;
        else if(index == 3) { ma_browse(0, 1, self.folder); return; }
        else if(index == 4) { ma_ask(99, "Test coverage", "Quick: data bus and address bus tests.\nFull: also runs device data patterns.\n\nMemory chunks are backed up and restored.\nLive RAM code / workspace is excluded.\nThe screen can pause during RAM / video tests.\nB stops between regions, not inside a subtest."); return; }
        else if(index == 5) self.options = 0;
    } else if(index < self.plan.region_count) {
        if(step < 0) { region_details(index); return; }
        self.plan.regions[index].enabled = !self.plan.regions[index].enabled;
    } else if(index == 5) self.options = 1;
    refresh();
}
static void action(int index) {
    if(index == 0) ma_ask(1, "Start memory test?", "Screen and sound may pause during testing.\nMemory chunks are backed up and restored.\nB stops after the current region finishes.\n\nThis replaces the previous in-app report.\nSave it first if you want to keep it.\n\nStart the selected tests?");
    else if(index == 1) save_report();
    else { self.options = !self.options; refresh(); }
}
static void back(void) {
    if(self.ran && !self.saved) ma_ask(2, "Leave without saving results?", "The current test report has not been saved.\nReturn to the menu?");
    else OpenMainApp();
}
void Memtest_Init(App_t *app) {
    memset(&self, 0, sizeof(self)); ma_init(app, "NextMemtestInput");
    memtest_plan_init(&self.plan); self.stop_first = 1;
    snprintf(self.folder, MA_PATH, "%s", ma_default_folder());
    ui.row = row; ui.action = action; ui.confirm = confirm; ui.picked = picked; ui.back = back;
    refresh(); ma_status("Select memory regions, review Options, then Run test.", 0);
}
MAINTENANCE_CALLBACKS(Memtest)
