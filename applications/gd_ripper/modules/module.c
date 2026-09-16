/* DreamShell ##version##

   module.c - GD Ripper app module
   Copyright (C)2014 megavolt85
   Copyright (C)2025 SWAT
   DreamShell NeXT recovery improvements coordinated and tested by TPMJB

*/

#include "ds.h"
#include "isofs/isofs.h"
#include "verify.h"
#include "readback.h"
#include "checksum.h"
#include "recovery.h"
#include "app_module.h"
#include "folders.h"
#include <zlib/zlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <dc/cdrom.h>
#include "destination.h"
#include "music.h"

DEFAULT_MODULE_EXPORTS(app_gd_ripper);

#define SEC_BUF_SIZE 16
#define UI_UPDATE_INTERVAL 500
#define GD_COMMAND_TIMEOUT_MS 8000
#define FAT_CHECKPOINT_SECTORS 4096
#define DRIVE_SETTLE_MS 1000
#define MAX_TRACKS 99
#define RIP_STATE_HEADER "DreamShell GD Ripper state v1"

static void *service_thread(void *arg);
static void input_event(void *event, void *param, int action);
static void video_event(void *event, void *param, int action);
static void refresh_controls(void);
static void set_message(const char *text);
static bool claim_worker(void);
static int check_storage(const char *folder);
static void select_page(int page);
static void focus_step(int direction);
static int check_disc_identity(const char *folder, bool resume, int disc_type);
static int retire_repaired_bad_map(const char *path, uint32_t first, uint32_t count);
static int repair_suspects(const char *path, uint32_t first, uint32_t count);
static int rip_sec(uint32_t tn, uint32_t first, uint32_t count, uint32_t type, char *dst_file);
static void* gd_ripper_thread(void *arg);
static void* gd_verify_thread(void *arg);
int create_gdi_file(char *dst_folder, char *dst_file, char *text, int disc_type);
static int get_disc_status_and_type(int *status, int *disc_type);
static int safe_cdrom_read_toc(cd_toc_t *toc, bool high_density);
static int safe_cdrom_reinit(void);
static int prepare_destination_paths(char *dst_folder, char *dst_file, char *text, int disc_type, bool *resume);
static int process_tracks(char *dst_folder, char *dst_file);
static int get_existing_file_size(const char *path, uint64_t *size);
static const char *get_sector_info(uint32_t track_type, uint32_t *sector_size);

typedef struct {
	uint32_t track_num;
	uint32_t start_lba;
	uint32_t sector_count;
	uint32_t type;
	char filename[NAME_MAX + 1];
} track_info_t;

static int get_track_info(int area, int disc_type, track_info_t *tracks,
	uint32_t *track_count, uint32_t capacity);
static int collect_track_info(int area_count, int disc_type);
static uint64_t calculate_total_sectors(void);
static void set_io_status(const char *operation, uint32_t fad);
static void update_ui_display(uint32_t current_sector_size, bool force);
static void update_verify_display(void *data, const char *filename,
	uint32_t track_index, uint32_t track_count, uint64_t processed_bytes,
	uint64_t total_bytes);
static void show_verify_result(const gd_verify_summary_t *summary,
	gd_verify_result_t result, const char *rip_label);

static struct self {
	App_t *app;
	GUI_Widget *bad;
	GUI_Widget *gname;
	GUI_Widget *pbar;
	GUI_Widget *track_label;
	GUI_Widget *num_read;
	GUI_Widget *start_btn;
	GUI_Widget *cancel_btn;
	GUI_Widget *message;
	GUI_Widget *disc_label;
	GUI_Widget *advanced_btn;
	GUI_Widget *exit_btn;
	GUI_Widget *browse_btn;
	GUI_Widget *edc_btn;
	GUI_Widget *verify_btn;
    GUI_Widget *recover_btn;

	GUI_Widget *speed_label;
	GUI_Widget *time_label;
	GUI_Widget *progress_percent_label;
	GUI_Widget *current_lba_label;
	GUI_Widget *sectors_total_label;
	GUI_Widget *sectors_processed_label;
	GUI_Widget *destination_path;
	GUI_Widget *file_browser;
	GUI_Widget *pages;
	GUI_Widget *use_bin_btn;
	uint32_t last_track;
	volatile int rip_active;
	uint64_t start_time;
	uint64_t total_sectors;
	uint64_t processed_sectors;
	uint64_t session_sectors;
	uint64_t last_ui_update;
	uint64_t drive_ready_after;
	uint32_t track_count;
	uint32_t current_fad;
	int max_attempts;
	bool zero_fill;
	bool use_bin;
    bool advanced;
    bool recovery_mode;
    bool recovery_prompt;
    bool recovery_counts_valid;
    gd_recovery_status_t recovery_totals, recovery_tracks[MAX_TRACKS];
    char recovery_name[NAME_MAX], recovery_destination[NAME_MAX];
    volatile int busy;
    volatile int request;
    volatile int shutdown;
    volatile int drive_command;
    volatile uint32_t command_started;
    volatile uint32_t io_started;
    char io_operation[24];
    uint32_t heartbeat_second;
    bool disc_ready;
    bool media_seen;
    bool auto_named;
    uint8_t disc_header[2048] __attribute__((aligned(32)));
    bool disc_header_valid;
    kthread_t *worker;
    Event_t *input_event, *video_event;
    int page, focus;
    folder_browser_t folders;
    char chosen_name[NAME_MAX];
    uint32_t current_crc, crc_tag;
    uint64_t crc_bytes;
    char crc_path[NAME_MAX];
    bool crc_failed;
    char failure_detail[208];
    const char *failure_stage;
    int analog_x, analog_y;
	track_info_t tracks[MAX_TRACKS];
	char selected_path[NAME_MAX];
	char rip_name[NAME_MAX];
	char rip_destination[NAME_MAX];
	char log_path[NAME_MAX];
	char database_path[NAME_MAX];
	char sync_mount[8];
} self;

static void sanitize_rip_name(char *output, size_t output_size, const char *input) {
	size_t written = 0;

	if (!output_size) {
		return;
	}
	if (!input || !input[0]) {
		input = "ripped_disc";
	}

	while (*input && written + 1 < output_size) {
		unsigned char ch = (unsigned char)*input++;

		if (ch <= 31 || ch == ' ' || strchr("<>:\"/\\|?*", ch)) {
			ch = '_';
		}
		output[written++] = ch;
	}

	while (written && (output[written - 1] == '.' || output[written - 1] == '_')) {
		written--;
	}
	if (!written) {
		snprintf(output, output_size, "%s", "ripped_disc");
	}
	else {
		output[written] = '\0';
	}
}

static void set_sync_mount(const char *path) {
	self.sync_mount[0] = '\0';

	if (!strncmp(path, "/sd", 3) && (path[3] == '\0' || path[3] == '/')) {
		strcpy(self.sync_mount, "/sd");
	}
	else if (!strncmp(path, "/ide", 4) && (path[4] == '\0' || path[4] == '/')) {
		strcpy(self.sync_mount, "/ide");
	}
}

static int sync_track(file_t hnd) {
	ssize_t completed = 0;

	if (!self.sync_mount[0]) {
		return CMD_OK;
	}

	/* DreamShell's FatFs VFS maps fs_complete() to f_sync() for this handle. */
	if (fs_complete(hnd, &completed) != 0) {
		ds_printf("DS_ERROR: Failed to sync open track on %s\n", self.sync_mount);
		return CMD_ERROR;
	}

	return CMD_OK;
}

static int rip_log(const char *format, ...) {
	char message[256];
	char line[320];
	va_list args;
	int line_len;
	file_t hnd;
	uint64_t elapsed = self.start_time ? timer_ms_gettime64() - self.start_time : 0;

	va_start(args, format);
	vsnprintf(message, sizeof(message), format, args);
	va_end(args);

	ds_printf("DS_GD_RIPPER: %s\n", message);

	if (!self.log_path[0]) {
		return CMD_OK;
	}

	line_len = snprintf(line, sizeof(line), "%llu.%03llu %s\n",
		(unsigned long long)(elapsed / 1000),
		(unsigned long long)(elapsed % 1000), message);
	if (line_len < 0) {
		return CMD_ERROR;
	}
	if (line_len >= (int)sizeof(line)) {
		line_len = sizeof(line) - 1;
	}

	hnd = gd_open_append(self.log_path);
	if (hnd == FILEHND_INVALID) {
		ds_printf("DS_WARN: Can't open rip log %s\n", self.log_path);
		return CMD_ERROR;
	}

	if (fs_write(hnd, line, line_len) != line_len) {
		int error = errno ? errno : EIO;
		ds_printf("DS_WARN: Can't append to rip log %s\n", self.log_path);
		fs_close(hnd);
		errno = error;
		return CMD_ERROR;
	}
	else if (self.sync_mount[0]) {
		ssize_t completed = 0;
		if (fs_complete(hnd, &completed) != 0) {
			int error = errno ? errno : EIO;
			ds_printf("DS_WARN: Can't sync rip log %s\n", self.log_path);
			fs_close(hnd);
			errno = error;
			return CMD_ERROR;
		}
	}
	return fs_close(hnd) ? CMD_ERROR : CMD_OK;
}

static int storage_error(const char *stage, const char *path, int error) {
    const char *name = strrchr(path, '/');
    self.failure_stage = stage;
    snprintf(self.failure_detail, sizeof(self.failure_detail),
        "%s: %.90s (filesystem error %d). Progress preserved; check storage and rip.log.",
        stage, name ? name + 1 : path, error ? error : EIO);
    rip_log("%s: %s, errno=%d", stage, path, error ? error : EIO);
    return CMD_ERROR;
}

static int drive_error(const char *stage, uint32_t track, uint32_t fad, int error) {
    self.failure_stage = stage;
    snprintf(self.failure_detail, sizeof(self.failure_detail),
        "Track %lu, FAD %lu: drive error %d. Progress preserved. Select Start / Resume to retry.",
        (unsigned long)track, (unsigned long)fad, error);
    rip_log("%s: track %lu FAD %lu, GD error=%d", stage,
        (unsigned long)track, (unsigned long)fad, error);
    return CMD_ERROR;
}

/* Test the running core's create/reopen/seek/sync behavior before touching tracks.
 * Old FAT handlers reject O_APPEND or treat writable reopen as CREATE_NEW.
 * A bootloader update alone does not establish which core was loaded. */
static int check_storage(const char *folder) {
    static const char first[] = "GD Ripper storage probe\n";
    static const char second[] = "reopen OK\n";
    char path[NAME_MAX], data[sizeof(first) + sizeof(second) - 2];
    file_t fd = FILEHND_INVALID;
    bool created = false;
    const char *stage = "Storage probe create failed";
    ssize_t completed = 0;
    int error = 0;

    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        if (snprintf(path, sizeof(path), "%s/rip-io-%08lx-%u.tmp", folder,
                (unsigned long)(uint32_t)timer_ms_gettime64(), attempt) >= (int)sizeof(path))
            return storage_error(stage, folder, ENAMETOOLONG);
        errno = 0;
        fd = fs_open(path, O_WRONLY | O_CREAT | O_EXCL);
        if (fd != FILEHND_INVALID) { created = true; break; }
        if (errno != EEXIST) break;
    }
    if (!created) goto failed;
    stage = "Storage probe write failed";
    if (fs_write(fd, first, sizeof(first) - 1) != sizeof(first) - 1) goto failed;
    stage = "Storage probe sync failed";
    if (self.sync_mount[0] && fs_complete(fd, &completed)) goto failed;
    stage = "Storage probe close failed";
    if (fs_close(fd)) { fd = FILEHND_INVALID; goto failed; }
    fd = FILEHND_INVALID;

    stage = "Storage reopen failed";
    errno = 0;
    fd = gd_open_append(path);
    if (fd == FILEHND_INVALID) goto failed;
    stage = "Storage append failed";
    if (fs_write(fd, second, sizeof(second) - 1) != sizeof(second) - 1) goto failed;
    stage = "Storage append sync failed";
    if (self.sync_mount[0] && fs_complete(fd, &completed)) goto failed;
    stage = "Storage append close failed";
    if (fs_close(fd)) { fd = FILEHND_INVALID; goto failed; }
    fd = FILEHND_INVALID;

    stage = "Storage probe read failed";
    fd = fs_open(path, O_RDONLY);
    if (fd == FILEHND_INVALID || fs_total(fd) != sizeof(data) ||
            fs_read(fd, data, sizeof(data)) != sizeof(data) ||
            memcmp(data, first, sizeof(first) - 1) ||
            memcmp(data + sizeof(first) - 1, second, sizeof(second) - 1)) goto failed;
    if (fs_close(fd)) { fd = FILEHND_INVALID; goto failed; }
    fd = FILEHND_INVALID;
    if (fs_unlink(path)) return storage_error("Storage probe cleanup failed", path, errno);
    return CMD_OK;

failed:
    error = errno ? errno : EIO;
    if (fd != FILEHND_INVALID) fs_close(fd);
    if (created) fs_unlink(path); /* Remove only the file this invocation created. */
    storage_error(stage, path, error);
    if (!strcmp(stage, "Storage reopen failed") && (error == EINVAL || error == EEXIST)) {
        snprintf(self.failure_detail, sizeof(self.failure_detail),
            "File reopen failed (error %d). An older core may be loaded. Boot the updated DS_CORE.BIN from SD/IDE before ripping.", error);
    }
    return CMD_ERROR;
}

static int timed_cdrom_read(void *buffer, uint32_t first, size_t count) {
	cd_read_params_t params;

	params.start_sec = first;
	params.num_sec = count;
	params.buffer = buffer;
	params.is_test = 0;

	/* PIO is used because KOS's DMA helper has no bounded wait path. */
	self.command_started = (uint32_t)timer_ms_gettime64();
    self.drive_command = 1;
    int rv = cdrom_exec_cmd_timed(CD_CMD_PIOREAD, &params, GD_COMMAND_TIMEOUT_MS);
    self.drive_command = 0;
    return rv;
}

static int safe_cdrom_reinit(void) {
	int rv = ERR_SYS;

	for (int attempt = 1; attempt <= 3; attempt++) {
		rv = cdrom_exec_cmd_timed(CD_CMD_INIT, NULL, GD_COMMAND_TIMEOUT_MS);
		if (rv != ERR_DISC_CHG) {
			break;
		}
		thd_sleep(200);
	}

	if (rv != ERR_OK) {
		return rv;
	}

	return cdrom_change_datatype(CDROM_READ_DEFAULT, -1, -1);
}

static int safe_cdrom_set_sector_size(uint32_t sector_size) {
	return cdrom_change_datatype(CDROM_READ_DEFAULT, -1, sector_size);
}

static int prepare_track_mode(uint32_t track, uint32_t fad, uint32_t size) {
    int rv = ERR_SYS;
    set_io_status("Sector mode", fad);
    for (int attempt = 1; attempt <= 3 && self.rip_active; ++attempt) {
        rv = safe_cdrom_set_sector_size(size);
        if (rv == ERR_OK) return CMD_OK;
        rip_log("Track %lu sector mode %lu attempt %d/3 failed: GD error=%d",
            (unsigned long)track, (unsigned long)size, attempt, rv);
        if (rv == ERR_NO_DISC || rv == ERR_DISC_CHG || attempt == 3) break;
        rv = safe_cdrom_reinit();
        if (rv != ERR_OK) break;
    }
    return drive_error("Sector mode failed", track, fad, rv);
}

static void safe_cdrom_spin_down(void) {
	int rv = cdrom_exec_cmd_timed(CD_CMD_STOP, NULL, GD_COMMAND_TIMEOUT_MS);

	if (rv != ERR_OK && rv != ERR_NO_ACTIVE && rv != ERR_NO_DISC) {
		ds_printf("DS_WARN: GD-ROM spin-down returned %d\n", rv);
	}
}

static void reset_rip_state(void) {
	self.rip_active = 0;
	GUI_WidgetSetEnabled(self.start_btn, 1);
	GUI_WidgetSetEnabled(self.cancel_btn, 0);

	GUI_WidgetSetEnabled(self.verify_btn, 1);
	GUI_LabelSetText(self.speed_label, " ");
	GUI_LabelSetText(self.time_label, " ");
    GUI_LabelSetTextColor(self.track_label, 231, 238, 244);
    GUI_LabelSetText(self.track_label, "GD Ripper");
    self.io_started = 0;
	GUI_LabelSetText(self.progress_percent_label, " ");
	GUI_LabelSetText(self.current_lba_label, " ");
	GUI_LabelSetText(self.sectors_total_label, " ");
	GUI_LabelSetText(self.sectors_processed_label, " ");
	GUI_ProgressBarSetPosition(self.pbar, 0.0);
	self.start_time = 0;
	self.total_sectors = 0;
	self.processed_sectors = 0;
	self.session_sectors = 0;
	self.last_ui_update = 0;
	self.track_count = 0;
	self.current_fad = 0;
	self.log_path[0] = '\0';
	self.sync_mount[0] = '\0';
	self.failure_stage = NULL;
	self.failure_detail[0] = '\0';
}

static void wait_for_drive_settle(void) {
	while (self.rip_active && self.drive_ready_after &&
			timer_ms_gettime64() < self.drive_ready_after) {
		GUI_LabelSetText(self.track_label, "Settling drive...");
		thd_sleep(50);
	}
	self.drive_ready_after = 0;
}

void gd_ripper_Number_read(void) {
    char label[64];
    if (self.busy) return;
    self.max_attempts = self.max_attempts == 1 ? 5 : self.max_attempts == 5 ? 10 :
        self.max_attempts == 10 ? 20 : self.max_attempts == 20 ? 50 : 1;
    snprintf(label, sizeof(label), "Retry / recovery pass limit: %d", self.max_attempts);
    GUI_LabelSetText(GUI_ButtonGetCaption(self.num_read), label);
}

void gd_ripper_Gamename()
{
	char text[NAME_MAX];

	if (self.chosen_name[0] && !strcmp(self.chosen_name, GUI_TextEntryGetText(self.gname))) return;
    self.chosen_name[0] = 0;
    sanitize_rip_name(text, sizeof(text), GUI_TextEntryGetText(self.gname));
	GUI_TextEntrySetText(self.gname, text);
}

void gd_ripper_ipbin_name()
{
	cd_toc_t toc;
	int status = 0, disc_type = 0;
	self.disc_header_valid = false;
	uint8_t *pbuff;
	char text[NAME_MAX];
	uint32_t lba = 0;

	if (safe_cdrom_reinit() != ERR_OK ||
		get_disc_status_and_type(&status, &disc_type) != CMD_OK) {
		return;
	}

	if (disc_type == CD_GDROM) {
		lba = 45150;
	}
	else {
		if(safe_cdrom_read_toc(&toc, false)) {
			ds_printf("DS_ERROR: Toc read error\n");
			return;
		}
		lba = cdrom_locate_data_track(&toc);

		if(!lba) {
			ds_printf("DS_ERROR: Failed to locate data track: lba=%d first=%d last=%d\n",
				lba, TOC_TRACK(toc.first), TOC_TRACK(toc.last));
			return;
		}
	}

	pbuff = (uint8_t *)memalign(32, 2048);

	if(!pbuff) {
		ds_printf("DS_ERROR: Failed to allocate buffer\n");
		return;
	}

	if (safe_cdrom_set_sector_size(2048) != ERR_OK) {
		ds_printf("DS_ERROR: Failed to select 2048-byte sector mode\n");
		free(pbuff);
		return;
	}

	ds_printf("DS_PROCESS: Reading IP.BIN from LBA: %d\n", lba);

	int read_rv = timed_cdrom_read(pbuff, lba, 1);
	self.drive_ready_after = timer_ms_gettime64() + DRIVE_SETTLE_MS;
	if (read_rv != ERR_OK) {
		ds_printf("DS_ERROR: GD read error\n");
		free(pbuff);
		return;
	}

	ipbin_meta_t *meta = (ipbin_meta_t*) pbuff;

	if(memcmp(meta->hardware_ID, "SEGA SEGAKATANA ", 15)) {
		free(pbuff);
		GUI_TextEntrySetText(self.gname, "ripped_disc");
		return;
	}

    memcpy(self.disc_header, pbuff, 2048);
    self.disc_header_valid = true;
    char *p;
    char *o;

	p = meta->title;
	o = text;

	// skip any spaces at the beginning
	while(*p == ' ' && meta->title + sizeof(meta->title) > p)
		p++;

	// copy rest to output buffer
	while(meta->title + sizeof(meta->title) > p) {
		*o++ = *p++;
	}

	// remove trailing spaces and null terminate without underflowing on blank titles
	while(o > text && o[-1] == ' ') o--;
	*o = '\0';

	if (strlen(text) == 0) {
		GUI_TextEntrySetText(self.gname, "ripped_disc");
	}
	else {
		sanitize_rip_name(text, sizeof(text), text);
		GUI_TextEntrySetText(self.gname, text);
	}
	GUI_LabelSetText(self.disc_label, GUI_TextEntryGetText(self.gname));
	free(pbuff);
}

void gd_ripper_Init(App_t *app, const char* fileName)
{
	(void)fileName;

	if(app != NULL)
	{
		memset(&self, 0, sizeof(self));

		self.app = app;
		self.bad = APP_GET_WIDGET("bad_btn");
		self.gname = APP_GET_WIDGET("gname-text");
		self.pbar = APP_GET_WIDGET("progress_bar");
		self.track_label = APP_GET_WIDGET("track-label");
		self.num_read = APP_GET_WIDGET("num-read");
		self.start_btn = APP_GET_WIDGET("start_btn");
		self.cancel_btn = APP_GET_WIDGET("cancel_btn");
        self.message = APP_GET_WIDGET("message");
        self.disc_label = APP_GET_WIDGET("disc-label");
        self.advanced_btn = APP_GET_WIDGET("advanced-btn");
        self.exit_btn = APP_GET_WIDGET("exit-btn");
        self.browse_btn = APP_GET_WIDGET("browse-btn");
        self.edc_btn = APP_GET_WIDGET("edc-btn");
		self.verify_btn = APP_GET_WIDGET("verify-btn");
        self.recover_btn = APP_GET_WIDGET("recover-btn");

		self.speed_label = APP_GET_WIDGET("speed-label");
		self.time_label = APP_GET_WIDGET("time-label");
		self.progress_percent_label = APP_GET_WIDGET("progress-percent-label");
		self.current_lba_label = APP_GET_WIDGET("current-lba-label");
		self.sectors_total_label = APP_GET_WIDGET("sectors-total-label");
		self.sectors_processed_label = APP_GET_WIDGET("sectors-processed-label");
		self.destination_path = APP_GET_WIDGET("destination-path");

		self.pages = APP_GET_WIDGET("pages");
		self.use_bin_btn = APP_GET_WIDGET("use_bin_btn");
        self.max_attempts = 10;
        GUI_WidgetSetState(self.use_bin_btn, 1);

		char app_path[NAME_MAX];
		GetAppPath(app_path, sizeof(app_path), app->fn);
		if (snprintf(self.database_path, sizeof(self.database_path),
				"%s/redump.db", app_path) >= (int)sizeof(self.database_path)) {
			self.database_path[0] = '\0';
		}

		snprintf(self.selected_path, sizeof(self.selected_path), "%s", gd_default_destination());
		if(gd_prepare_destination(self.selected_path) < 0)
			set_message("Games folder unavailable. Check the device or choose another folder.");

		GUI_LabelSetText(self.destination_path, self.selected_path);
		GUI_WidgetSetEnabled(self.cancel_btn, 0);
        GUI_LabelSetText(self.track_label, "Waiting for disc");
        self.input_event = AddEvent("GDRipperInput", EVENT_TYPE_INPUT, EVENT_PRIO_DEFAULT, input_event, NULL);
        self.video_event = AddEvent("GDRipperStatus", EVENT_TYPE_VIDEO, EVENT_PRIO_DEFAULT, video_event, NULL);
        if (self.input_event) SetEventActive(self.input_event, 0);
        self.worker = thd_create(0, service_thread, NULL);
        if (!self.worker) set_message("Could not start drive worker. Reopen GD Ripper.");
	}
	else
	{
		ds_printf("DS_ERROR: %s: Attempting to call %s is not by the app initiate.\n",
					lib_get_name(), __func__);
	}
}


static void queue_operation(int operation) {
    if (!self.worker || (operation != 2 && !self.disc_ready) ||
            (operation == 3 && !self.recovery_prompt) || !claim_worker()) return;
    reset_rip_state();
    if (self.max_attempts < 1) self.max_attempts = 1;
    if (self.max_attempts > 50) self.max_attempts = 50;
    self.zero_fill = !!GUI_WidgetGetState(self.bad);
    self.use_bin = !!GUI_WidgetGetState(self.use_bin_btn);
    self.advanced = !!GUI_WidgetGetState(self.edc_btn);
    self.recovery_mode = !!GUI_WidgetGetState(self.recover_btn) || operation == 3;
    if (self.recovery_mode) { self.use_bin = true; self.advanced = true; self.zero_fill = false; }
    if (operation == 3) {
        /* The prompt owns a frozen target. Disc auto-naming must never redirect
         * a pending recovery into another game's folder. */
        snprintf(self.rip_name, sizeof(self.rip_name), "%s", self.recovery_name);
        snprintf(self.rip_destination, sizeof(self.rip_destination), "%s", self.recovery_destination);
    } else {
        if (self.chosen_name[0] && !strcmp(self.chosen_name, GUI_TextEntryGetText(self.gname)))
            snprintf(self.rip_name, sizeof(self.rip_name), "%s", self.chosen_name);
        else sanitize_rip_name(self.rip_name, sizeof(self.rip_name), GUI_TextEntryGetText(self.gname));
        snprintf(self.rip_destination, sizeof(self.rip_destination), "%s", self.selected_path);
    }
    GUI_TextEntrySetText(self.gname, self.rip_name);
    self.recovery_prompt = false;
    self.rip_active = 1;
    self.busy = 1;
    self.request = operation;
    select_page(0);
    refresh_controls();
    GUI_LabelSetText(self.track_label, operation == 3 ? "Preparing recovery..." :
        operation == 1 ? "Starting rip..." : "Reading saved dump...");
    set_message(operation == 3 ? "Checking the disc and saved recovery queue before rereading flagged sectors." :
        operation == 1 && self.recovery_mode ? "First pass: two tries per failed sector, then defer it. Recovery will ask before starting." :
        operation == 1 ? "Sector EDC and track CRC are checked while ripping." :
        "Advanced CRC: storage read-back + data-sector scan. This can take 30 minutes on SD.");
}

void gd_ripper_StartRip(GUI_Widget *widget) { (void)widget; queue_operation(1); }
void gd_ripper_Verify(GUI_Widget *widget) { (void)widget; queue_operation(2); }
void gd_ripper_Recover(GUI_Widget *widget) { (void)widget; queue_operation(3); }

void gd_ripper_CancelRip(GUI_Widget *widget) {
    (void)widget;
    if (!self.busy) return;
    self.rip_active = 0;
    GUI_LabelSetText(self.track_label, "Stopping...");
    set_message("Stop requested. Waiting for I/O to return, then saving the checkpoint.");
    GUI_WidgetSetEnabled(self.cancel_btn, 0);
}

static int get_disc_status_and_type(int *status, int *disc_type)
{
	for (int attempt = 0; attempt < 50; attempt++) {
		int rv = cdrom_get_status(status, disc_type);

		if (rv != ERR_OK) {
			ds_printf("DS_ERROR: GD-ROM status error: %d\n", rv);
			return CMD_ERROR;
		}

		switch (*status) {
			case CD_STATUS_PAUSED:
			case CD_STATUS_STANDBY:
			case CD_STATUS_PLAYING:
				return CMD_OK;
			case CD_STATUS_BUSY:
			case CD_STATUS_SEEKING:
			case CD_STATUS_SCANNING:
			case CD_STATUS_RETRY:
				thd_sleep(200);
				break;
			case CD_STATUS_OPEN:
			case CD_STATUS_NO_DISC:
				ds_printf("DS_ERROR: Disc not inserted\n");
				return CMD_ERROR;
			default:
				ds_printf("DS_ERROR: GD-ROM drive status: %d\n", *status);
				return CMD_ERROR;
		}
	}

	ds_printf("DS_ERROR: GD-ROM stayed busy for 10 seconds\n");
	return CMD_ERROR;
}

static int safe_cdrom_read_toc(cd_toc_t *toc, bool high_density)
{
	cd_cmd_toc_params_t params;
	int terr = 0;
	int rv;

	params.area = high_density ? CD_AREA_HIGH : CD_AREA_LOW;
	params.buffer = toc;

	while ((rv = cdrom_exec_cmd_timed(CD_CMD_GETTOC2, &params,
			GD_COMMAND_TIMEOUT_MS)) != ERR_OK) {
		if (!(self.app->state & APP_STATE_OPENED) ||
			(self.start_time && !self.rip_active)) {
			ds_printf("DS_INFO: TOC reading cancelled\n");
			return CMD_ERROR;
		}

		terr++;
		if (terr == 3) {
			ds_printf("DS_INFO: Reinitializing CDROM for TOC read\n");
			if (safe_cdrom_reinit() != ERR_OK) {
				ds_printf("DS_ERROR: Failed to reinitialize GD-ROM\n");
				return CMD_ERROR;
			}
		}

		if (terr > 8) {
			ds_printf("DS_ERROR: Failed to read TOC after %d attempts (error %d)\n",
				terr, rv);
			return CMD_ERROR;
		}

		thd_sleep(200);
	}

	return CMD_OK;
}

static int directory_is_empty(const char *path) {
	file_t fd = fs_open(path, O_DIR | O_RDONLY);
	const dirent_t *dir;

	if (fd == FILEHND_INVALID) {
		return 0;
	}

	while ((dir = fs_readdir(fd))) {
		if (strcmp(dir->name, ".") && strcmp(dir->name, "..")) {
			fs_close(fd);
			return 0;
		}
	}

	fs_close(fd);
	return 1;
}

/* Returns 1 for a match, 0 when absent, and -1 for a mismatch/error. */
static int validate_resume_state(const char *dst_folder, int disc_type) {
	char path[NAME_MAX];
	char line[NAME_MAX + 96];
	char filename[NAME_MAX + 1];
	FILE *fp;
	unsigned long track_count;
	int saved_disc_type, saved_use_bin;

	snprintf(path, sizeof(path), "%s/rip.state", dst_folder);
	if (!FileExists(path)) {
		return 0;
	}

	fp = fopen(path, "r");
	if (!fp) {
		return -1;
	}

	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return -1;
	}
	line[strcspn(line, "\r\n")] = '\0';
	if (strcmp(line, RIP_STATE_HEADER) ||
		!fgets(line, sizeof(line), fp) || sscanf(line, "disc_type %d", &saved_disc_type) != 1 ||
		!fgets(line, sizeof(line), fp) || sscanf(line, "use_bin %d", &saved_use_bin) != 1 ||
		!fgets(line, sizeof(line), fp) || sscanf(line, "tracks %lu", &track_count) != 1 ||
		saved_disc_type != disc_type ||
		saved_use_bin != self.use_bin ||
		track_count != self.track_count) {
		fclose(fp);
		return -1;
	}

	for (uint32_t i = 0; i < self.track_count; i++) {
		unsigned long track, start, count, type, sector_size;
		uint32_t expected_sector_size;

		if (!fgets(line, sizeof(line), fp) ||
			sscanf(line, "%lu %lu %lu %lu %lu %255s",
				&track, &start, &count, &type, &sector_size, filename) != 6) {
			fclose(fp);
			return -1;
		}

		get_sector_info(self.tracks[i].type, &expected_sector_size);
		if (track != self.tracks[i].track_num ||
			start != self.tracks[i].start_lba ||
			count != self.tracks[i].sector_count ||
			type != self.tracks[i].type ||
			sector_size != expected_sector_size ||
			strcmp(filename, self.tracks[i].filename)) {
			fclose(fp);
			return -1;
		}
	}

	fclose(fp);
	return 1;
}

/* Allows partial dumps made by older builds to gain the new resume state. */
static int validate_gdi_file(const char *dst_folder, const char *text) {
	char path[NAME_MAX];
	char line[NAME_MAX + 96];
	char filename[NAME_MAX + 1];
	FILE *fp;
	unsigned long last_track;
	char extra;

	snprintf(path, sizeof(path), "%s/%s.gdi", dst_folder, text);
	if (!FileExists(path)) {
		return 0;
	}

	fp = fopen(path, "r");
	if (!fp) {
		return -1;
	}

	if (!fgets(line, sizeof(line), fp) ||
		sscanf(line, "%lu %c", &last_track, &extra) != 1 ||
		last_track != self.last_track) {
		fclose(fp);
		return -1;
	}

	for (uint32_t i = 0; i < self.track_count; i++) {
		unsigned long track, lba, type, sector_size, offset;
		uint32_t expected_sector_size;

		if (!fgets(line, sizeof(line), fp) ||
			sscanf(line, "%lu %lu %lu %lu %255s %lu %c",
				&track, &lba, &type, &sector_size, filename, &offset, &extra) != 6) {
			fclose(fp);
			return -1;
		}

		get_sector_info(self.tracks[i].type, &expected_sector_size);
		if (track != self.tracks[i].track_num ||
			lba != self.tracks[i].start_lba - 150 ||
			type != self.tracks[i].type ||
			sector_size != expected_sector_size || offset != 0 ||
			strcmp(filename, self.tracks[i].filename)) {
			fclose(fp);
			return -1;
		}
	}

	while (fgets(line, sizeof(line), fp)) {
		for (char *p = line; *p; p++) {
			if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
				fclose(fp);
				return -1;
			}
		}
	}

	fclose(fp);
	return 1;
}

static int write_resume_state(const char *dst_folder, int disc_type) {
	char path[NAME_MAX];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/rip.state", dst_folder);
	fp = fopen(path, "w");
	if (!fp) {
		ds_printf("DS_ERROR: Can't create resume state %s\n", path);
		return CMD_ERROR;
	}

	fprintf(fp, "%s\n", RIP_STATE_HEADER);
	fprintf(fp, "disc_type %d\n", disc_type);
	fprintf(fp, "use_bin %d\n", self.use_bin);
	fprintf(fp, "tracks %lu\n", (unsigned long)self.track_count);

	for (uint32_t i = 0; i < self.track_count; i++) {
		uint32_t sector_size;
		get_sector_info(self.tracks[i].type, &sector_size);
		fprintf(fp, "%lu %lu %lu %lu %lu %s\n",
			(unsigned long)self.tracks[i].track_num,
			(unsigned long)self.tracks[i].start_lba,
			(unsigned long)self.tracks[i].sector_count,
			(unsigned long)self.tracks[i].type,
			(unsigned long)sector_size,
			self.tracks[i].filename);
	}

	if (fclose(fp) != 0) {
		ds_printf("DS_ERROR: Failed to close resume state %s\n", path);
		return CMD_ERROR;
	}

	return CMD_OK;
}

static int prepare_destination_paths(char *dst_folder, char *dst_file, char *text,
		int disc_type, bool *resume) {
	int validation;

	*resume = false;
	memset(dst_file, 0, NAME_MAX);
	snprintf(dst_file, NAME_MAX, "%s", dst_folder);

	if (!DirExists(dst_folder)) {
		if (fs_mkdir(dst_folder) != 0) {
			ds_printf("DS_ERROR: Failed to create folder: %s\n", dst_folder);
			return CMD_ERROR;
		}
		return CMD_OK;
	}

	validation = validate_resume_state(dst_folder, disc_type);
	if (validation == 1) {
		*resume = true;
		return CMD_OK;
	}

	validation = validate_gdi_file(dst_folder, text);
	if (validation == 1) {
		*resume = true;
		return CMD_OK;
	}
	if (validation < 0 || !directory_is_empty(dst_folder)) {
		ds_printf("DS_ERROR: Destination already contains a different rip: %s\n",
			dst_folder);
		return CMD_ERROR;
	}

	return CMD_OK;
}

static const char *get_sector_info(uint32_t track_type, uint32_t *sector_size) {
	if (track_type == 4 && self.use_bin) {
		if(sector_size) *sector_size = 2352;
		return "bin";
	}
	else if (track_type == 4) {
		if(sector_size) *sector_size = 2048;
		return "iso";
	}
	else {
		if(sector_size) *sector_size = 2352;
		return "raw";
	}
}

static int get_track_info(int area, int disc_type, track_info_t *tracks,
		uint32_t *track_count, uint32_t capacity) {
	cd_toc_t toc;

	if (safe_cdrom_read_toc(&toc, area == 1) != CMD_OK) {
		return CMD_ERROR;
	}

	uint32_t first = TOC_TRACK(toc.first);
	uint32_t last = TOC_TRACK(toc.last);
	uint32_t count = 0;
    if (!first || first > last || last > MAX_TRACKS) return CMD_ERROR;

	for (uint32_t tn = first; tn <= last; tn++) {
		if (count >= capacity) {
			ds_printf("DS_ERROR: Disc has too many tracks\n");
			return CMD_ERROR;
		}

		uint32_t type = TOC_CTRL(toc.entry[tn-1]);
		uint32_t start = TOC_LBA(toc.entry[tn-1]);
		uint32_t s_end = TOC_LBA((tn == last ? toc.leadout_sector : toc.entry[tn]));
		uint32_t nsec;
        if (start < 150 || s_end <= start || s_end > 1000000) return CMD_ERROR;
        nsec = s_end - start;

        uint32_t gap = 0;
        if (disc_type != CD_GDROM && type == 4) gap = 2;
        else if (area == 1 && tn != last && type != TOC_CTRL(toc.entry[tn])) gap = 150;
        else if (area == 0 && type == 4) gap = 150;
        if (nsec <= gap) return CMD_ERROR;
        nsec -= gap;

		tracks[count].track_num = tn;
		tracks[count].start_lba = start;
		tracks[count].sector_count = nsec;
		tracks[count].type = type;

		const char *extension = get_sector_info(type, NULL);
		snprintf(tracks[count].filename, NAME_MAX, "track%02lu.%s",
			(unsigned long)tn, extension);

		count++;
	}

	*track_count = count;
	return CMD_OK;
}

static int collect_track_info(int area_count, int disc_type) {
	self.track_count = 0;

	for (int area = 0; area < area_count; area++) {
		uint32_t area_track_count = 0;

		if (!self.rip_active || !(self.app->state & APP_STATE_OPENED)) {
			return CMD_ERROR;
		}

		if (get_track_info(area, disc_type, self.tracks + self.track_count,
				&area_track_count, MAX_TRACKS - self.track_count) != CMD_OK) {
			return CMD_ERROR;
		}

		self.track_count += area_track_count;
	}

	if (!self.track_count) {
		ds_printf("DS_ERROR: Disc contains no readable tracks\n");
		return CMD_ERROR;
	}

	self.last_track = self.tracks[self.track_count - 1].track_num;
	return CMD_OK;
}

static uint64_t calculate_total_sectors(void) {
	uint64_t total = 0;

	for (uint32_t i = 0; i < self.track_count; i++) {
		total += self.tracks[i].sector_count;
		ds_printf("DS_PROCESS: Track %lu: %lu sectors\n",
			(unsigned long)self.tracks[i].track_num,
			(unsigned long)self.tracks[i].sector_count);
	}

	ds_printf("DS_INFO: Total sectors: %llu\n", (unsigned long long)total);
	return total;
}

static int process_tracks(char *dst_folder, char *dst_file) {
	char riplabel[64];

	for (uint32_t i = 0; i < self.track_count; i++) {
		if (!self.rip_active || !(self.app->state & APP_STATE_OPENED)) {
			return CMD_ERROR;
		}

		uint32_t tn = self.tracks[i].track_num;

		snprintf(riplabel, sizeof(riplabel), "Track %lu of %lu",
			(unsigned long)tn, (unsigned long)self.last_track);
		GUI_LabelSetText(self.track_label, riplabel);

		snprintf(dst_file, NAME_MAX, "%s/%s", dst_folder, self.tracks[i].filename);

		if (rip_sec(tn, self.tracks[i].start_lba, self.tracks[i].sector_count,
				self.tracks[i].type, dst_file) != CMD_OK) {
			return CMD_ERROR;
		}
	}

	return CMD_OK;
}

/* Recovery markers are exact, durable records. A partial marker is an error,
 * never permission to skip acquisition or bind a different disc. */
static int recovery_marker(const char *folder, const char *name, const char *value, bool create) {
    char path[NAME_MAX], saved[100];
    file_t fd;
    int length = strlen(value), rv;
    if (length > (int)sizeof(saved)) return -1;
    if (snprintf(path, sizeof(path), "%s/%s", folder, name) >= (int)sizeof(path)) return -1;
    fd = fs_open(path, O_RDONLY);
    if (fd != FILEHND_INVALID) {
        rv = fs_total(fd) == length && fs_read(fd, saved, length) == length &&
            !memcmp(saved, value, length) ? 1 : -1;
        if (fs_close(fd)) rv = -1;
        return rv;
    }
    if (errno != ENOENT) return -1;
    if (!create) return 0;
    fd = fs_open(path, O_WRONLY | O_CREAT | O_EXCL);
    if (fd == FILEHND_INVALID) return -1;
    rv = fs_write(fd, value, length) == length && sync_track(fd) == CMD_OK ? 1 : -1;
    if (fs_close(fd)) rv = -1;
    return rv;
}

static int count_recovery_targets(const char *folder) {
    char path[NAME_MAX];
    memset(&self.recovery_totals, 0, sizeof(self.recovery_totals));
    self.recovery_counts_valid = false;
    for (uint32_t i = 0; i < self.track_count; ++i) {
        track_info_t *t = &self.tracks[i];
        gd_recovery_status_t *status = &self.recovery_tracks[i];
        if (snprintf(path, sizeof(path), "%s/%s", folder, t->filename) >= (int)sizeof(path) ||
                gd_recovery_inspect(path, t->track_num, t->start_lba, t->sector_count,
                    t->type, 2352, &self.rip_active, status) != CMD_OK) {
            GUI_LabelSetText(self.progress_percent_label, "Recovery counts unavailable");
            GUI_LabelSetText(self.speed_label, " ");
            GUI_LabelSetText(self.time_label, " ");
            return storage_error("Recovery records unreadable / invalid", path, errno);
        }
        self.recovery_totals.flagged += status->flagged;
        self.recovery_totals.recovered += status->recovered;
        self.recovery_totals.remaining += status->remaining;
        self.recovery_totals.pending |= status->pending;
    }
    self.recovery_counts_valid = true;
    return CMD_OK;
}

static void show_recovery_prompt(void) {
    char line[128];
    const gd_recovery_status_t *totals = &self.recovery_totals;
    self.recovery_prompt = true;
    snprintf(self.recovery_name, sizeof(self.recovery_name), "%s", self.rip_name);
    snprintf(self.recovery_destination, sizeof(self.recovery_destination), "%s", self.rip_destination);
    GUI_LabelSetText(self.track_label, totals->remaining ? "Incomplete - recovery available" : "Recovery needs finalization");
    GUI_LabelSetText(APP_GET_WIDGET("recovery-title"), totals->remaining ? "THIS DUMP'S A MESS." : "REPAIRS SAVED.");
    snprintf(line, sizeof(line), "%lu unresolved sectors.", (unsigned long)totals->remaining);
    GUI_LabelSetText(APP_GET_WIDGET("recovery-count"), line);
    GUI_LabelSetText(APP_GET_WIDGET("recovery-folder"), self.recovery_name);
    snprintf(line, sizeof(line), "Originally flagged: %lu    Recovered: %lu",
        (unsigned long)totals->flagged, (unsigned long)totals->recovered);
    GUI_LabelSetText(APP_GET_WIDGET("recovery-history"), line);
    snprintf(line, sizeof(line), totals->remaining ? "Up to %d passes; only unresolved sectors are reread." :
        "All targets recovered. Finish to save the final checkpoint.", self.max_attempts);
    GUI_LabelSetText(APP_GET_WIDGET("recovery-limit"), line);
    GUI_LabelSetText(GUI_ButtonGetCaption(APP_GET_WIDGET("recovery-start")),
        totals->remaining ? "Try recovery" : "Finish recovery");
    set_message("Progress saved. Start / Resume offers recovery for the remaining sectors, or finalizes completed repairs.");
    GUI_LabelSetText(self.progress_percent_label, "Recovery needed");
    GUI_LabelSetText(self.time_label, "Progress saved");
    GUI_LabelSetText(self.speed_label, "Saved recovery progress");
    rip_log("Recovery offered: %lu originally flagged, %lu recovered, %lu unresolved; %d pass limit",
        (unsigned long)totals->flagged, (unsigned long)totals->recovered,
        (unsigned long)totals->remaining, self.max_attempts);
    select_page(3);
}

typedef struct {
    uint32_t track, type, last_pass, drive_pass;
    gd_recovery_status_t *status;
} recovery_context_t;

static void recovery_remaining(recovery_context_t *context, uint32_t remaining) {
    gd_recovery_status_t *status = context->status;
    self.recovery_totals.remaining = self.recovery_totals.remaining - status->remaining + remaining;
    status->remaining = remaining;
    status->recovered = status->flagged - remaining;
    self.recovery_totals.recovered = self.recovery_totals.flagged - self.recovery_totals.remaining;
}

static void show_recovery_totals(void) {
    char line[80];
    snprintf(line, sizeof(line), "Total: %lu unresolved", (unsigned long)self.recovery_totals.remaining);
    GUI_LabelSetText(self.speed_label, line);
    snprintf(line, sizeof(line), "%lu / %lu recovered", (unsigned long)self.recovery_totals.recovered,
        (unsigned long)self.recovery_totals.flagged);
    GUI_LabelSetText(self.time_label, line);
}

static int recovery_read_sector(void *data, uint8_t *buffer, uint32_t fad) {
    recovery_context_t *context = data;
    int rv;
    if (!self.rip_active) return -1;
    if (context->last_pass > 1 && context->last_pass != context->drive_pass) {
        set_io_status("Recovery reset", fad);
        rv = safe_cdrom_reinit();
        if (rv == ERR_OK) rv = safe_cdrom_set_sector_size(2352);
        if (rv != ERR_OK) {
            drive_error("Drive recovery failed", context->track, fad, rv); return -1;
        }
    }
    context->drive_pass = context->last_pass;
    set_io_status("Recovery read", fad);
    rv = timed_cdrom_read(buffer, fad, 1);
    if (rv == ERR_OK) {
        unsigned flags = context->type == 4 ? gd_check_sector(buffer, fad) : 0;
        if (!flags) return 0;
        rip_log("Recovery track %lu FAD %lu: validation failed, flags=%u",
            (unsigned long)context->track, (unsigned long)fad, flags);
        return 1;
    }
    rip_log("Recovery track %lu FAD %lu: read error %d", (unsigned long)context->track,
        (unsigned long)fad, rv);
    if (rv == ERR_NO_DISC || rv == ERR_DISC_CHG) {
        drive_error("Disc removed / changed", context->track, fad, rv); return -1;
    }
    if (rv == ERR_TIMEOUT) {
        rv = safe_cdrom_reinit();
        if (rv == ERR_OK) rv = safe_cdrom_set_sector_size(2352);
        if (rv != ERR_OK) {
            drive_error("Drive recovery failed", context->track, fad, rv); return -1;
        }
    }
    return 1;
}

static void recovery_progress(void *data, uint32_t pass, uint32_t fad,
        uint32_t remaining, bool recovered) {
    recovery_context_t *context = data;
    char line[128];
    /* Reconciliation starts from the immutable original list. Keep the
     * inspected saved count visible until that reconciliation is complete. */
    if (pass) recovery_remaining(context, remaining);
    if (pass && pass != context->last_pass) {
        rip_log("Recovery track %lu: pass %lu/%d, %lu remaining",
            (unsigned long)context->track, (unsigned long)pass, self.max_attempts,
            (unsigned long)remaining);
        context->last_pass = pass;
    }
    if (recovered) rip_log("Recovered track %lu FAD %lu; %lu remaining",
        (unsigned long)context->track, (unsigned long)fad, (unsigned long)remaining);
    snprintf(line, sizeof(line), pass ? "Recovery T%lu pass %lu/%d" : "Checking saved recovery T%lu",
        (unsigned long)context->track, (unsigned long)pass, self.max_attempts);
    GUI_LabelSetText(self.track_label, line);
    snprintf(line, sizeof(line), "Track %lu: %lu unresolved", (unsigned long)context->track,
        (unsigned long)context->status->remaining);
    GUI_LabelSetText(self.progress_percent_label, line);
    show_recovery_totals();
    set_io_status(pass ? "Recover" : "Reconcile", fad);
    set_message(context->type == 4 ? "Data must pass address, EDC and ECC checks. Each repaired sector is read back from storage." :
        "Audio needs two matching reads. Drive cache may affect independence; the final catalog CRC is still required.");
}

static int recover_tracks(const char *folder) {
    for (uint32_t i = 0; i < self.track_count; ++i) {
        char path[NAME_MAX];
        track_info_t *t = &self.tracks[i];
        gd_recovery_result_t result;
        recovery_context_t context = { t->track_num, t->type, 0, 0, &self.recovery_tracks[i] };
        uint32_t targets;
        snprintf(path, sizeof(path), "%s/%s", folder, t->filename);
        if (gd_recovery_count(path, t->track_num, t->start_lba, t->sector_count, &targets) != CMD_OK)
            return storage_error("Recovery queue invalid", path, errno);
        if (!targets) continue;
        if (!self.rip_active || prepare_track_mode(t->track_num, t->start_lba, 2352) != CMD_OK)
            return CMD_ERROR;
        if (gd_recover_track(path, t->track_num, t->start_lba, t->sector_count, t->type,
                self.sync_mount[0] != '\0', self.max_attempts, &self.rip_active,
                recovery_read_sector, recovery_progress, &context, &result) != CMD_OK) {
            rip_log("Recovery stopped: %s; queue and backups preserved", result.error ? result.error : "unknown error");
            if (self.rip_active && !self.failure_stage)
                storage_error(result.error ? result.error : "Recovery failed", path, errno);
            return CMD_ERROR;
        }
        recovery_remaining(&context, result.remaining);
        context.status->pending = result.remaining != 0;
        rip_log("Recovery track %lu: %lu newly recovered, %lu unresolved, CRC %08lx",
            (unsigned long)t->track_num, (unsigned long)result.recovered,
            (unsigned long)result.remaining, (unsigned long)result.crc);
    }
    self.recovery_totals.pending = self.recovery_totals.remaining != 0;
    show_recovery_totals();
    return CMD_OK;
}

static int write_completion_marker(const char *dst_folder) {
    char path[NAME_MAX], line[100];
    int n, rv;
    if (snprintf(path, sizeof(path), "%s/rip.complete", dst_folder) >= (int)sizeof(path)) return CMD_ERROR;
    file_t fd = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd == FILEHND_INVALID) return CMD_ERROR;
    n = snprintf(line, sizeof(line), "Complete: %llu sectors\n", (unsigned long long)self.total_sectors);
    rv = fs_write(fd, line, n) == n && sync_track(fd) == CMD_OK ? CMD_OK : CMD_ERROR;
    if (fs_close(fd)) rv = CMD_ERROR;
    if (rv != CMD_OK) fs_unlink(path);
    return rv;
}

static void* gd_ripper_thread(void *arg) {
	int status, disc_type;
	char dst_folder[NAME_MAX];
	char dst_file[NAME_MAX];
	char text[NAME_MAX];
	int area_count;
	bool resume = false;
	bool destination_ready = false;
	bool success = false;
	bool verification_ran = false;
    bool deferred = false;
    bool recovering = arg != NULL;
    int pass_complete = 0;
    char pass_record[100];
	bool cancelled;
	char complete_path[NAME_MAX];
	const char *failure_label = "Rip failed";
	gd_verify_summary_t verification_summary;
	gd_verify_result_t verification_result = GD_VERIFY_ERROR;

	memset(&verification_summary, 0, sizeof(verification_summary));
	verification_summary.catalog_result = GD_VERIFY_ERROR;

	ds_printf("DS_PROCESS: Starting disc ripping process\n");
	self.start_time = timer_ms_gettime64();
	self.processed_sectors = 0;
    self.recovery_counts_valid = false;
	self.session_sectors = 0;
	self.log_path[0] = '\0';
	set_sync_mount(self.rip_destination);
	self.failure_stage = NULL;
	self.failure_detail[0] = '\0';
	GUI_LabelSetText(self.track_label, "Checking destination...");
	if(gd_prepare_destination(self.rip_destination) < 0) {
		storage_error("Destination unavailable", self.rip_destination, errno); goto out;
	}
	if (check_storage(self.rip_destination) != CMD_OK) goto out;

	wait_for_drive_settle();
	if (!self.rip_active) {
		goto out;
	}
	GUI_LabelSetText(self.track_label, "Initializing...");
	if (safe_cdrom_reinit() != ERR_OK) {
		ds_printf("DS_ERROR: Failed to initialize GD-ROM\n");
		failure_label = "Drive init failed";
		goto out;
	}

	snprintf(text, NAME_MAX, "%s", self.rip_name);
	if (snprintf(dst_folder, NAME_MAX, "%s/%s", self.rip_destination, text) >= NAME_MAX) {
		ds_printf("DS_ERROR: Destination path is too long\n");
		failure_label = "Destination too long";
		goto out;
	}

	if(get_disc_status_and_type(&status, &disc_type) != CMD_OK) {
		failure_label = "No disc / drive error";
		goto out;
	}

    if (gd_readback_blocked(dst_folder)) {
        storage_error("Storage reads disagree", dst_folder, EIO);
        snprintf(self.failure_detail, sizeof(self.failure_detail),
            "Run Scan saved dump again; keep readback.log and readback.bin. Disc repair is blocked until storage reads agree.");
        goto out;
    }

	area_count = disc_type == CD_GDROM ? 2 : 1;
	if (collect_track_info(area_count, disc_type) != CMD_OK) {
		ds_printf("DS_ERROR: Failed to read disc track information\n");
		failure_label = "TOC read failed";
		goto out;
	}
	self.total_sectors = calculate_total_sectors();
	if (!self.total_sectors) {
		goto out;
	}
    int recovery_saved = recovery_marker(dst_folder, "rip.recovery", "DreamShell targeted recovery v1\n", false);
    if (recovery_saved < 0) {
        storage_error("Recovery marker invalid", dst_folder, errno); goto out;
    }
    if (recovery_saved) {
        self.recovery_mode = true;
        GUI_WidgetSetState(self.recover_btn, 1);
        GUI_LabelSetText(GUI_ButtonGetCaption(self.recover_btn), "Recover damaged disc: ON (first pass + prompt)");
    }
    if (self.recovery_mode) { self.use_bin = true; self.advanced = true; self.zero_fill = false; }

	GUI_LabelSetText(self.track_label, "Preparing...");
	ds_printf("DS_PROCESS: Preparing destination: %s\n", dst_folder);

	if(prepare_destination_paths(dst_folder, dst_file, text, disc_type, &resume) != CMD_OK) {
		ds_printf("DS_ERROR: Failed to prepare destination paths\n");
		failure_label = "Name/options conflict";
		goto out;
	}
	if (snprintf(self.log_path, sizeof(self.log_path), "%s/rip.log", dst_folder) >=
			(int)sizeof(self.log_path)) {
		ds_printf("DS_ERROR: Destination log path is too long\n");
		goto out;
	}

	if (rip_log("GD Ripper 2.2.2 diagnostic: destination reopen/sync/read-back passed") != CMD_OK) {
        storage_error("Rip log creation failed", self.log_path, errno);
        goto out;
    }
    if (check_disc_identity(dst_folder, resume, disc_type) != CMD_OK) {
        failure_label = "Disc identity mismatch / unreadable";
        rip_log("Disc identity check failed before track extraction");
        goto out;
    }
    destination_ready = true;
    if (recovering && (!resume || !recovery_saved)) {
        failure_label = "No saved recovery for this folder"; goto out;
    }
    if (self.recovery_mode && recovery_marker(dst_folder, "rip.recovery",
            "DreamShell targeted recovery v1\n", true) != 1) {
        storage_error("Recovery marker write failed", dst_folder, errno); goto out;
    }
    snprintf(pass_record, sizeof(pass_record), "First pass complete: %llu sectors\n",
        (unsigned long long)self.total_sectors);
    if (self.recovery_mode) {
        pass_complete = recovery_marker(dst_folder, "rip.first-pass", pass_record, false);
        if (pass_complete < 0) {
            storage_error("First-pass marker invalid", dst_folder, errno); goto out;
        }
    }
	if (rip_log("Started %s rip with %lu track(s), %llu total sectors, retries=%d, zero-fill=%d, sector-EDC=ON, advanced-ECC=%d",
		resume ? "resumed" : "new", (unsigned long)self.track_count,
		(unsigned long long)self.total_sectors, self.max_attempts, self.zero_fill, self.advanced) != CMD_OK) {
        storage_error("Rip log reopen failed", self.log_path, errno);
        goto out;
    }

	if (resume) {
		rip_log("Existing rip matches; checking track files for resume points");
	}
	ds_printf("DS_PROCESS: Creating GDI file\n");
	GUI_LabelSetText(self.track_label, "Creating GDI");

	/* Rewriting a validated descriptor also repairs an interrupted metadata write. */
	if (create_gdi_file(dst_folder, dst_file, text, disc_type) != CMD_OK) {
		goto out;
	}

	if (write_resume_state(dst_folder, disc_type) != CMD_OK) {
		goto out;
	}

	snprintf(complete_path, sizeof(complete_path), "%s/rip.complete", dst_folder);
    if (fs_unlink(complete_path) && errno != ENOENT) {
        storage_error("Completion marker cleanup failed", complete_path, errno); goto out;
    }

	ds_printf("DS_PROCESS: Starting track extraction\n");

	if (!pass_complete && process_tracks(dst_folder, dst_file) != CMD_OK) {
		goto out;
	}
    if (pass_complete) {
        /* Do not hash the entire track if recovery was interrupted after CRC
         * invalidation. The recovery baseline can reconcile just its targets. */
        for (uint32_t i = 0; i < self.track_count; ++i) {
            uint64_t size = 0;
            snprintf(dst_file, sizeof(dst_file), "%s/%s", dst_folder, self.tracks[i].filename);
            if (get_existing_file_size(dst_file, &size) != 1 ||
                    size != (uint64_t)self.tracks[i].sector_count * 2352) {
                storage_error("First-pass track size changed", dst_file, errno); goto out;
            }
        }
        self.processed_sectors = self.total_sectors;
    }
    if (self.recovery_mode) {
        if (recovery_marker(dst_folder, "rip.first-pass", pass_record, true) != 1) {
            storage_error("First-pass checkpoint failed", dst_folder, errno); goto out;
        }
        if (count_recovery_targets(dst_folder) != CMD_OK) goto out;
        if (self.recovery_totals.pending && recovering && recover_tracks(dst_folder) != CMD_OK) goto out;
        /* Zero outstanding reads can still need a CRC checkpoint/queue cleanup
         * after interruption. Inspection alone must not approve the dump. */
        if (self.recovery_totals.remaining || self.recovery_totals.pending) { deferred = true; goto out; }
    }

	if (write_completion_marker(dst_folder) != CMD_OK) {
		rip_log("All tracks finished, but the completion marker could not be written");
		goto out;
	}

    update_ui_display(2352, true);
    rip_log("Rip completed successfully: %llu sectors",
		(unsigned long long)self.processed_sectors);
	success = true;

	if (self.rip_active) {
		verification_ran = true;
		safe_cdrom_spin_down();
		GUI_LabelSetText(self.track_label, "Checking stream CRC...");
		GUI_LabelSetText(self.speed_label, "Catalog lookup...");
		GUI_LabelSetText(self.time_label, "Please wait");
		self.start_time = timer_ms_gettime64();
		self.last_ui_update = 0;
		verification_result = gd_verify_dump_ex(dst_folder, self.database_path,
			self.sync_mount[0] != '\0', &self.rip_active, update_verify_display,
			NULL, &verification_summary, true, false);
	}

out:
	cancelled = !self.rip_active;
	if (!success && !deferred && self.log_path[0]) {
		rip_log(cancelled ? "Rip cancelled; partial files preserved" :
			"Rip paused after an error; partial files preserved for resume");
	}

	self.rip_active = 0;
	GUI_WidgetSetEnabled(self.start_btn, 1);
	GUI_WidgetSetEnabled(self.cancel_btn, 0);

	GUI_WidgetSetEnabled(self.verify_btn, 1);
	if (deferred) {
		show_recovery_prompt();
    }
    else if (success && verification_ran) {
		show_verify_result(&verification_summary, verification_result,
			verification_result == GD_VERIFY_CANCELLED ?
			"Rip complete / verify stopped" : NULL);
	}
	else if (success) {
		char final_done[64];
		char final_total[64];

		snprintf(final_done, sizeof(final_done), "Done: %llu",
			(unsigned long long)self.processed_sectors);
		snprintf(final_total, sizeof(final_total), "Total: %llu",
			(unsigned long long)self.total_sectors);
		GUI_LabelSetText(self.track_label, "Rip complete");
		GUI_ProgressBarSetPosition(self.pbar, 1.0);
		GUI_LabelSetText(self.time_label, "Finished");
		GUI_LabelSetText(self.progress_percent_label, "Overall: 100.00%");
		GUI_LabelSetText(self.sectors_total_label, final_total);
		GUI_LabelSetText(self.sectors_processed_label, final_done);
		set_io_status("Finished", 0);
	}
	else if (cancelled) {
		GUI_LabelSetText(self.track_label, "Stopped - progress saved");
		set_message("Keep the same disc and folder; select Start / Resume to continue.");
		set_io_status("Stopped", self.current_fad);
	}
	else if (self.failure_stage) {
        GUI_LabelSetText(self.track_label, self.failure_stage);
        GUI_LabelSetTextColor(self.track_label, 255, 154, 136);
        set_message(self.failure_detail);
        set_io_status("Paused", self.current_fad);
    }
	else if (destination_ready) {
		GUI_LabelSetText(self.track_label, "Stopped - read/write error");
        GUI_LabelSetTextColor(self.track_label, 255, 154, 136);
        set_message("Partial dump preserved. Check rip.log, then select Start / Resume to retry.");
		set_io_status("Paused", self.current_fad);
	}
	else {
        GUI_LabelSetTextColor(self.track_label, 255, 154, 136);
        GUI_LabelSetText(self.track_label, failure_label);
		set_message("Check the disc and destination folder, then select Start / Resume.");
	}
	self.io_started = 0; /* Keep the error visible during cleanup. */
    if (!success && !deferred && self.recovery_counts_valid) {
        show_recovery_totals();
        rip_log("Recovery saved: %lu originally flagged, %lu recovered, %lu unresolved",
            (unsigned long)self.recovery_totals.flagged, (unsigned long)self.recovery_totals.recovered,
            (unsigned long)self.recovery_totals.remaining);
    }
	safe_cdrom_spin_down();
	self.start_time = 0;
	return NULL;
}

static void set_io_status(const char *operation, uint32_t fad) {
	char status_text[64];
    self.current_fad = fad;
    self.io_started = (uint32_t)timer_ms_gettime64();
    self.heartbeat_second = 0;
    snprintf(self.io_operation, sizeof(self.io_operation), "%s", operation);

    if (fad) {
		snprintf(status_text, sizeof(status_text), "%s FAD %lu",
			operation, (unsigned long)fad);
	}
	else {
		snprintf(status_text, sizeof(status_text), "%s", operation);
	}
	GUI_LabelSetText(self.current_lba_label, status_text);
}

static void update_ui_display(uint32_t current_sector_size, bool force) {
	uint64_t current_time = timer_ms_gettime64();
	uint64_t elapsed_time = current_time - self.start_time;
	double progress_percent = self.total_sectors ?
		(double)self.processed_sectors * 100.0 / self.total_sectors : 0.0;

	GUI_ProgressBarSetPosition(self.pbar, progress_percent / 100.0);

	if (!force && current_time - self.last_ui_update < UI_UPDATE_INTERVAL) {
		return;
	}

	char speed_text[64];
	char time_text[64];
	char percent_text[64];
	char total_sectors_text[64];
	char processed_sectors_text[64];

	double speed_sectors_per_sec = elapsed_time ?
		(double)self.session_sectors / (elapsed_time / 1000.0) : 0.0;
	double speed_kb_per_sec = speed_sectors_per_sec * current_sector_size / 1024.0;

	if (speed_sectors_per_sec > 0) {
		snprintf(speed_text, sizeof(speed_text), "Speed: %.1f KB/s", speed_kb_per_sec);
	}
	else {
		snprintf(speed_text, sizeof(speed_text), "Speed: --");
	}

	snprintf(percent_text, sizeof(percent_text), "Overall: %.2f%%", progress_percent);

	if (self.total_sectors > 0) {
		snprintf(total_sectors_text, sizeof(total_sectors_text), "Total: %llu",
			(unsigned long long)self.total_sectors);
	}
	else {
		snprintf(total_sectors_text, sizeof(total_sectors_text), "Total: --");
	}

	snprintf(processed_sectors_text, sizeof(processed_sectors_text), "Done: %llu",
		(unsigned long long)self.processed_sectors);

	if (self.total_sectors > self.processed_sectors && speed_sectors_per_sec > 0) {
		uint64_t remaining_sectors = self.total_sectors - self.processed_sectors;
		uint64_t remaining_time_sec = remaining_sectors / speed_sectors_per_sec;
		uint32_t remaining_hours = (uint32_t)(remaining_time_sec / 3600);
		uint32_t remaining_min = (uint32_t)((remaining_time_sec % 3600) / 60);

		if (remaining_hours > 0) {
			snprintf(time_text, sizeof(time_text), "Time left: %luh %lum",
					(unsigned long)remaining_hours, (unsigned long)remaining_min);
		}
		else if (remaining_time_sec < 60) {
            snprintf(time_text, sizeof(time_text), "Time left: <1m");
        }
        else {
			snprintf(time_text, sizeof(time_text), "Time left: %lum",
				(unsigned long)remaining_min);
		}
	}
	else {
		snprintf(time_text, sizeof(time_text), "Time left: --");
	}

	GUI_LabelSetText(self.speed_label, speed_text);
	GUI_LabelSetText(self.time_label, time_text);
	GUI_LabelSetText(self.progress_percent_label, percent_text);
	GUI_LabelSetText(self.sectors_total_label, total_sectors_text);
	GUI_LabelSetText(self.sectors_processed_label, processed_sectors_text);

	self.last_ui_update = current_time;
}

static void format_verify_bytes(char *buffer, size_t size, uint64_t bytes,
		const char *prefix) {
	if (bytes >= 1024ULL * 1024 * 1024) {
		snprintf(buffer, size, "%s %.2f GB", prefix,
			(double)bytes / (1024.0 * 1024.0 * 1024.0));
	}
	else if (bytes >= 1024ULL * 1024) {
		snprintf(buffer, size, "%s %.1f MB", prefix,
			(double)bytes / (1024.0 * 1024.0));
	}
	else {
		snprintf(buffer, size, "%s %.1f KB", prefix, (double)bytes / 1024.0);
	}
}

static void update_verify_display(void *data, const char *filename,
		uint32_t track_index, uint32_t track_count, uint64_t processed_bytes,
		uint64_t total_bytes) {
	uint64_t now = timer_ms_gettime64();
	double percent = total_bytes ? (double)processed_bytes * 100.0 / total_bytes : 0.0;
	char track_text[64];
	char current_text[64];
	char percent_text[64];
	char speed_text[64];
	char time_text[64];
	char total_text[64];
	char done_text[64];

	(void)data;
	if (!(self.app->state & APP_STATE_OPENED)) {
		self.rip_active = 0;
		return;
	}
	GUI_ProgressBarSetPosition(self.pbar, percent / 100.0);
    if (now - self.last_ui_update < UI_UPDATE_INTERVAL && processed_bytes != total_bytes) {
		return;
	}

	snprintf(track_text, sizeof(track_text), "Verify %lu of %lu",
		(unsigned long)track_index, (unsigned long)track_count);
	snprintf(current_text, sizeof(current_text), "Hash: %.44s", filename);
	snprintf(percent_text, sizeof(percent_text), "Verify: %.2f%%", percent);
	format_verify_bytes(total_text, sizeof(total_text), total_bytes, "Total:");
	format_verify_bytes(done_text, sizeof(done_text), processed_bytes, "Done:");

	uint64_t elapsed = now - self.start_time;
	double bytes_per_second = elapsed ?
		(double)processed_bytes * 1000.0 / elapsed : 0.0;
	if (bytes_per_second > 0) {
		snprintf(speed_text, sizeof(speed_text), "Speed: %.1f KB/s",
			bytes_per_second / 1024.0);
	}
	else {
		snprintf(speed_text, sizeof(speed_text), "Speed: --");
	}
	if (bytes_per_second > 0 && total_bytes > processed_bytes) {
		uint64_t seconds = (uint64_t)((total_bytes - processed_bytes) /
			bytes_per_second);
        if (seconds < 60) snprintf(time_text, sizeof(time_text), "Time left: <1m");
        else snprintf(time_text, sizeof(time_text), "Time left: %lum",
            (unsigned long)((seconds + 59) / 60));
	}
	else {
		snprintf(time_text, sizeof(time_text), "Time left: --");
	}

	GUI_LabelSetText(self.track_label, track_text);
	GUI_LabelSetText(self.current_lba_label, current_text);
	GUI_LabelSetText(self.progress_percent_label, percent_text);
	GUI_LabelSetText(self.speed_label, speed_text);
	GUI_LabelSetText(self.time_label, time_text);
	GUI_LabelSetText(self.sectors_total_label, total_text);
	GUI_LabelSetText(self.sectors_processed_label, done_text);
	self.last_ui_update = now;
}

static void show_verify_result(const gd_verify_summary_t *summary,
		gd_verify_result_t result, const char *rip_label) {
	const char *label;
	char game_text[256];

	switch (result) {
		case GD_VERIFY_FULL_MATCH: label = "All track CRCs match"; break;
		case GD_VERIFY_DATA_MATCH: label = "Data track CRCs match"; break;
		case GD_VERIFY_IDENTIFIED: label = "Known data match"; break;
		case GD_VERIFY_PARTIAL_MATCH: label = "Partial match only"; break;
		case GD_VERIFY_NO_MATCH: label = "No catalog match"; break;
		case GD_VERIFY_NO_DATABASE: label = "Hashed - no database"; break;
		case GD_VERIFY_INCOMPATIBLE: label = "Verify needs BIN tracks"; break;
		case GD_VERIFY_INTEGRITY_FAILED: label = "Dump integrity FAILED"; break;
		case GD_VERIFY_READBACK_UNSTABLE: label = "Storage reads disagree"; break;
		case GD_VERIFY_CANCELLED: label = rip_label ? rip_label : "Verify cancelled"; break;
		default: label = "Verification failed"; break;
	}
    bool failed = result == GD_VERIFY_INTEGRITY_FAILED || result == GD_VERIFY_READBACK_UNSTABLE;
    GUI_LabelSetTextColor(self.track_label,
        result == GD_VERIFY_NO_MATCH || result == GD_VERIFY_PARTIAL_MATCH || failed ? 255 : 83,
        failed ? 154 : 225, failed ? 136 : 227);
    GUI_LabelSetText(self.track_label, label);
    if (result == GD_VERIFY_READBACK_UNSTABLE) {
        set_message("Console read-back is inconsistent. Keep the dump; do not run disc repair. Save verify.log, readback.log and readback.bin for diagnosis.");
    } else if (summary->suspect_sectors) {
        char message[160];
        snprintf(message, sizeof(message), "%lu suspect sectors. To reread them: turn Advanced CRC ON, then Start / Resume with the same disc and folder.",
            (unsigned long)summary->suspect_sectors);
        set_message(message);
    } else if (result == GD_VERIFY_NO_MATCH || result == GD_VERIFY_PARTIAL_MATCH) {
        set_message("No catalog match: check revision/layout or run Advanced CRC to locate sector errors.");
    } else if (result == GD_VERIFY_INTEGRITY_FAILED) {
        set_message("Dump is incomplete or has recorded bad sectors. See verify.log; use Start / Resume.");
    } else if (result == GD_VERIFY_ERROR) {
        set_message("Verification failed. Check destination, track files and available storage.");
    } else {
        set_message(summary->streaming ? (summary->recovery_flagged ?
            "Stream CRC checked. Saved recovery targets were checked; untouched sectors were not reread. See verify.log." :
            "Stream CRC checked. Storage was not reread; see verify.log for catalog and track results.") :
            "Storage read-back finished. See verify.log and sector maps for details.");
    }

	if (summary->game_name[0]) {
		snprintf(game_text, sizeof(game_text), "%s: %.64s",
            result == GD_VERIFY_PARTIAL_MATCH ? "Candidate" : "Matched", summary->game_name);
        GUI_LabelSetText(self.disc_label, game_text);
        snprintf(game_text, sizeof(game_text), "Catalog: %s", summary->catalog);
        GUI_LabelSetText(self.speed_label, game_text);
	}
	else {
		GUI_LabelSetText(self.speed_label, " ");
	}
	GUI_LabelSetText(self.time_label,
		summary->report_written ? "Saved verify.log" : "No verification report");
	GUI_LabelSetText(self.current_lba_label, gd_verify_result_text(summary->catalog_result));
	if (result != GD_VERIFY_CANCELLED && result != GD_VERIFY_ERROR) {
		GUI_ProgressBarSetPosition(self.pbar, 1.0);
		GUI_LabelSetText(self.progress_percent_label, "Verification complete");
	}
    if (summary->recovery_counts_valid && summary->recovery_flagged) {
        char counts[80];
        snprintf(counts, sizeof(counts), "%lu unresolved", (unsigned long)summary->bad_sector_count);
        GUI_LabelSetText(self.progress_percent_label, counts);
        snprintf(counts, sizeof(counts), "%lu / %lu recovered", (unsigned long)summary->recovery_recovered,
            (unsigned long)summary->recovery_flagged);
        GUI_LabelSetText(self.speed_label, counts);
        if (summary->recovery_pending && !summary->bad_sector_count &&
                (result == GD_VERIFY_INTEGRITY_FAILED || result == GD_VERIFY_ERROR))
            set_message("All flagged sectors are recovered. Start / Resume, then Finish recovery to save the final checkpoint.");
    } else if (!summary->recovery_counts_valid && result == GD_VERIFY_ERROR) {
        GUI_LabelSetText(self.progress_percent_label, "Recovery counts unavailable");
    }
}

static void* gd_verify_thread(void *arg) {
	char folder[NAME_MAX];
	gd_verify_summary_t summary;
	gd_verify_result_t result;

	(void)arg;
	if (snprintf(folder, sizeof(folder), "%s/%s", self.rip_destination,
			self.rip_name) >= (int)sizeof(folder)) {
		memset(&summary, 0, sizeof(summary));
		summary.catalog_result = GD_VERIFY_ERROR;
		result = GD_VERIFY_ERROR;
	}
	else {
		set_sync_mount(self.rip_destination);
		result = gd_verify_dump(folder, self.database_path, self.sync_mount[0] != '\0',
			&self.rip_active, update_verify_display, NULL, &summary);
	}

	self.rip_active = 0;
	GUI_WidgetSetEnabled(self.start_btn, 1);
	GUI_WidgetSetEnabled(self.cancel_btn, 0);

	GUI_WidgetSetEnabled(self.verify_btn, 1);
	show_verify_result(&summary, result, NULL);
	self.start_time = 0;
	return NULL;
}

static int get_existing_file_size(const char *path, uint64_t *size) {
	file_t hnd = fs_open(path, O_RDONLY);
	ssize_t total;

	if (hnd == FILEHND_INVALID) {
		*size = 0;
		return 0;
	}

	total = fs_total(hnd);
	fs_close(hnd);
	if (total < 0) {
		*size = 0;
		return -1;
	}
	*size = total;
	return 1;
}

static int record_bad_sector(const char *dst_file, uint32_t tn,
		uint32_t track_sector, uint32_t fad, uint32_t sector_size) {
	char path[NAME_MAX];
	char line[192];
	file_t hnd;
	uint64_t existing_size;
	int len;

	if (snprintf(path, sizeof(path), "%s.bad", dst_file) >= (int)sizeof(path)) {
		rip_log("Bad-sector map path is too long");
		return CMD_ERROR;
	}

	if (get_existing_file_size(path, &existing_size) < 0) {
		return CMD_ERROR;
	}
	hnd = gd_open_append(path);
	if (hnd == FILEHND_INVALID) {
		rip_log("Can't open bad-sector map %s", path);
		return CMD_ERROR;
	}

	if (!existing_size) {
		static const char header[] = "track,track_sector,disc_lba,disc_fad,file_offset\n";
		if (fs_write(hnd, header, sizeof(header) - 1) != sizeof(header) - 1) {
			fs_close(hnd);
			return CMD_ERROR;
		}
	}

	len = snprintf(line, sizeof(line), "%lu,%lu,%lu,%lu,%llu\n",
		(unsigned long)tn, (unsigned long)track_sector,
		(unsigned long)(fad >= 150 ? fad - 150 : fad), (unsigned long)fad,
		(unsigned long long)((uint64_t)track_sector * sector_size));
	if (len < 0 || len >= (int)sizeof(line) || fs_write(hnd, line, len) != len) {
		fs_close(hnd);
		return CMD_ERROR;
	}

	return fs_close(hnd) ? CMD_ERROR : CMD_OK;
}

static int checkpoint_track(file_t hnd, uint32_t tn, uint32_t written_sectors,
		uint32_t fad) {
	set_io_status("Sync", fad);
	if (rip_log("Checkpoint track %lu at track sector %lu (FAD %lu)",
		(unsigned long)tn, (unsigned long)written_sectors, (unsigned long)fad) != CMD_OK)
        return storage_error("Rip log write failed", self.log_path, errno);
	if (sync_track(hnd) != CMD_OK)
        return storage_error("Track sync failed", self.crc_path, errno);
    if (gd_crc_checkpoint(self.crc_path, self.crc_tag, self.crc_bytes,
            self.current_crc, self.sync_mount[0] != '\0') != CMD_OK) {
        self.crc_failed = true;
        return storage_error("CRC checkpoint failed", self.crc_path, errno);
    }
    return CMD_OK;
}

static int restore_stream_crc(const char *path, uint64_t existing, uint32_t tn,
        uint32_t first, uint32_t count, uint32_t secbyte) {
    uint8_t *buffer;
    file_t fd;
    self.crc_tag = gd_crc_tag(tn, first, count, secbyte);
    self.crc_bytes = 0;
    self.current_crc = 0;
    self.crc_failed = false;
    snprintf(self.crc_path, sizeof(self.crc_path), "%s", path);
    if (!existing) {
        char journal[NAME_MAX];
        if (snprintf(journal, sizeof(journal), "%s.crc", path) >= (int)sizeof(journal)) return CMD_ERROR;
        if (FileExists(journal) && fs_unlink(journal)) return CMD_ERROR;
        return CMD_OK;
    }
    gd_crc_restore(path, self.crc_tag, existing, secbyte, &self.crc_bytes, &self.current_crc);
    if (self.crc_bytes == existing) return CMD_OK;
    set_message(self.crc_bytes ? "Restoring CRC: reading the uncheckpointed tail only." :
        "Older dump has no CRC checkpoint: hashing the saved portion once to resume safely.");
    fd = fs_open(path, O_RDONLY);
    if (fd == FILEHND_INVALID) return CMD_ERROR;
    if (fs_seek(fd, (off_t)self.crc_bytes, SEEK_SET) != (off_t)self.crc_bytes) { fs_close(fd); return CMD_ERROR; }
    buffer = memalign(32, 2352 * 16);
    if (!buffer) { fs_close(fd); return CMD_ERROR; }
    while (self.crc_bytes < existing) {
        uint64_t left = existing - self.crc_bytes;
        size_t n = left > 2352 * 16 ? 2352 * 16 : (size_t)left;
        if (!self.rip_active || fs_read(fd, buffer, n) != (ssize_t)n) {
            free(buffer); fs_close(fd); return CMD_ERROR;
        }
        self.current_crc = crc32(self.current_crc, buffer, n);
        self.crc_bytes += n;
        set_io_status("Resume CRC", first + self.crc_bytes / secbyte);
        thd_pass();
    }
    free(buffer); fs_close(fd);
    return gd_crc_checkpoint(path, self.crc_tag, self.crc_bytes, self.current_crc,
        self.sync_mount[0] != '\0');
}

static int stream_written(const void *data, uint32_t crc_before_write, size_t bytes,
        const char *path, uint32_t tn, uint32_t track_sector, uint32_t fad,
        uint32_t secbyte) {
    uint32_t after = crc32(self.current_crc, data, bytes);
    if (after != crc_before_write) {
        /* The storage call may yield. Never bless a CRC sampled before an
           observed buffer mutation, or let resume forget the affected span. */
        self.crc_failed = true;
        rip_log("Memory buffer changed during track %lu write at FAD %lu: before=%08lx after=%08lx, %lu bytes",
            (unsigned long)tn, (unsigned long)fad, (unsigned long)crc_before_write,
            (unsigned long)after, (unsigned long)bytes);
        for (uint32_t i = 0; i < bytes / secbyte; ++i) {
            if (record_bad_sector(path, tn, track_sector + i, fad + i, secbyte) != CMD_OK)
                return storage_error("Untrusted-sector map write failed", path, errno);
        }
        self.failure_stage = "Memory changed during write";
        snprintf(self.failure_detail, sizeof(self.failure_detail),
            "Track %lu FAD %lu: the source buffer changed during a storage write. Run a saved-dump scan before resuming; the affected sectors are recorded in .bad.",
            (unsigned long)tn, (unsigned long)fad);
        return CMD_ERROR;
    }
    self.current_crc = crc_before_write;
    self.crc_bytes += bytes;
    return CMD_OK;
}

static int checked_sector_read(void *buffer, uint32_t fad, size_t count,
        uint32_t type, uint32_t secbyte) {
    int rv = timed_cdrom_read(buffer, fad, count);
    if (rv != ERR_OK || type != 4 || secbyte != 2352) return rv;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *sector = (const uint8_t *)buffer + i * 2352;
        unsigned flags = self.advanced ? gd_check_sector(sector, fad + i) :
            gd_check_sector_edc(sector, fad + i);
        if (flags && (self.recovery_mode || flags != GD_SECTOR_UNSUPPORTED)) {
            rip_log("Sector validation failed at FAD %lu (flags=%u: sync=1 address=2 EDC=4 ECC=8)",
                (unsigned long)(fad + i), flags);
            return ERR_SYS;
        }
    }
    return rv;
}

static int rip_sec(uint32_t tn, uint32_t first, uint32_t count, uint32_t type, char *dst_file) {
	file_t hnd;
	uint32_t secbyte, bad = 0;
	uint32_t original_first = first;
	uint32_t original_count = count;
	uint32_t checkpoint_sectors = 0;
	uint64_t existing_bytes = 0;
	uint64_t expected_bytes;
	uint32_t resumed_sectors;
	uint8_t *buffer;
	char bad_file[NAME_MAX];
	char track_text[64];
	int max_attempts = self.recovery_mode ? 2 : self.max_attempts;
	bool zero_fill = self.zero_fill || self.recovery_mode;
	int size_status;

	get_sector_info(type, &secbyte);
	expected_bytes = (uint64_t)count * secbyte;
	size_status = get_existing_file_size(dst_file, &existing_bytes);
	if (size_status < 0) {
		return storage_error("Track size check failed", dst_file, errno);
	}

	if (existing_bytes > expected_bytes || existing_bytes % secbyte) {
		rip_log("Track %lu cannot resume: file size %llu is not an aligned partial of %llu bytes",
			(unsigned long)tn, (unsigned long long)existing_bytes,
			(unsigned long long)expected_bytes);
		return CMD_ERROR;
	}

    if (!self.recovery_mode && self.advanced && type == 4 && secbyte == 2352 && existing_bytes &&
            repair_suspects(dst_file, first, existing_bytes / secbyte) != CMD_OK) {
        rip_log("Repair pass failed; original backups and partial files preserved");
        return CMD_ERROR;
    }
    if (restore_stream_crc(dst_file, existing_bytes, tn, first, count, secbyte) != CMD_OK) {
        return storage_error("Resume CRC failed", dst_file, errno);
    }
    resumed_sectors = existing_bytes / secbyte;
	self.processed_sectors += resumed_sectors;
	set_io_status("Resume", first + resumed_sectors);
	update_ui_display(secbyte, true);

	if (resumed_sectors == original_count) {
		rip_log("Track %lu already complete (%lu sectors); skipping",
			(unsigned long)tn, (unsigned long)original_count);
		return CMD_OK;
	}

	buffer = (uint8_t *)memalign(32, SEC_BUF_SIZE * secbyte);
	if (!buffer) {
		rip_log("Failed to allocate sector buffer");
		return CMD_ERROR;
	}

	if (prepare_track_mode(tn, first + resumed_sectors, secbyte) != CMD_OK) {
		free(buffer);
		return CMD_ERROR;
	}

	if (!resumed_sectors) {
		int bad_path_len;

		hnd = fs_open(dst_file, O_WRONLY | O_TRUNC | O_CREAT);
		bad_path_len = snprintf(bad_file, sizeof(bad_file), "%s.bad", dst_file);
		if (bad_path_len >= 0 && bad_path_len < (int)sizeof(bad_file) &&
			FileExists(bad_file)) {
			fs_unlink(bad_file);
		}
        /* A genuinely new track must not inherit an older recovery baseline. */
        const char *suffixes[] = { ".recovery-base", ".recovery-base.tmp", ".recovery-audio" };
        for (unsigned i = 0; i < sizeof(suffixes)/sizeof(suffixes[0]); ++i) {
            if (snprintf(bad_file, sizeof(bad_file), "%s%s", dst_file, suffixes[i]) >= (int)sizeof(bad_file) ||
                    (fs_unlink(bad_file) && errno != ENOENT)) {
                if (hnd != FILEHND_INVALID) fs_close(hnd);
                free(buffer); return storage_error("Old recovery cleanup failed", dst_file, errno);
            }
        }
	}
	else {
		hnd = fs_open(dst_file, O_WRONLY);
	}

	if (hnd == FILEHND_INVALID) {
		storage_error("Track open failed", dst_file, errno);
		free(buffer);
		return CMD_ERROR;
	}

	if (resumed_sectors && fs_seek(hnd, (off_t)existing_bytes, SEEK_SET) != (off_t)existing_bytes) {
		storage_error("Track resume seek failed", dst_file, errno);
		fs_close(hnd);
		free(buffer);
		return CMD_ERROR;
	}

	first += resumed_sectors;
	count -= resumed_sectors;
	if (resumed_sectors) {
		rip_log("Resuming track %lu at sector %lu/%lu (FAD %lu, byte %llu)",
			(unsigned long)tn, (unsigned long)resumed_sectors,
			(unsigned long)original_count, (unsigned long)first,
			(unsigned long long)existing_bytes);
	}
	else {
		rip_log("Starting track %lu at FAD %lu (%lu sectors, %lu bytes each)",
			(unsigned long)tn, (unsigned long)first, (unsigned long)count,
			(unsigned long)secbyte);
	}

	while (count) {
		uint32_t nsects = count > SEC_BUF_SIZE ? SEC_BUF_SIZE : count;
		int cdstat;

		if (!self.rip_active || !(self.app->state & APP_STATE_OPENED)) {
			checkpoint_track(hnd, tn, original_count - count, first);
			fs_close(hnd);
			free(buffer);
			return CMD_ERROR;
		}

		/* This updates the visible address before a potentially slow command. */
		set_io_status("Read", first);
		update_ui_display(secbyte, false);
		cdstat = checked_sector_read(buffer, first, nsects, type, secbyte);

		if (cdstat == ERR_OK) {
			size_t bytes_to_write = nsects * secbyte;
			/* Snapshot the drive data before the storage call can yield. */
			uint32_t crc_before_write = crc32(self.current_crc, buffer, bytes_to_write);

			set_io_status("Write", first);
			errno = 0;
			if (fs_write(hnd, buffer, bytes_to_write) != (ssize_t)bytes_to_write) {
				storage_error("Track write failed", dst_file, errno);
				checkpoint_track(hnd, tn, original_count - count, first);
				fs_close(hnd);
				free(buffer);
				return CMD_ERROR;
			}
			if (stream_written(buffer, crc_before_write, bytes_to_write, dst_file,
					tn, original_count - count, first, secbyte) != CMD_OK) {
				checkpoint_track(hnd, tn, original_count - count, first);
				fs_close(hnd); free(buffer); return CMD_ERROR;
			}
			self.processed_sectors += nsects; self.session_sectors += nsects;
		}
		else {
			/* Commit everything already written before doing slow recovery. */
			rip_log("Bulk read error %d at FAD %lu (%lu sectors); trying sectors individually",
				cdstat, (unsigned long)first, (unsigned long)nsects);
			if (checkpoint_track(hnd, tn, original_count - count, first) != CMD_OK) {
				fs_close(hnd);
				free(buffer);
				return CMD_ERROR;
			}
			checkpoint_sectors = 0;

			if (cdstat == ERR_NO_DISC || cdstat == ERR_DISC_CHG) {
				drive_error("Disc removed / changed", tn, first, cdstat);
				fs_close(hnd);
				free(buffer);
				return CMD_ERROR;
			}
			if (cdstat == ERR_TIMEOUT) {
				int reinit_rv;

				rip_log("Timed-out bulk read was aborted; reinitializing GD-ROM");
				reinit_rv = safe_cdrom_reinit();
				if (reinit_rv == ERR_OK) reinit_rv = safe_cdrom_set_sector_size(secbyte);
				if (reinit_rv != ERR_OK) {
					drive_error("Drive recovery failed", tn, first, reinit_rv);
					fs_close(hnd);
					free(buffer);
					return CMD_ERROR;
				}
			}
			else if (prepare_track_mode(tn, first, secbyte) != CMD_OK) {
				fs_close(hnd);
				free(buffer);
				return CMD_ERROR;
			}

			for (uint32_t s = 0; s < nsects; s++) {
				uint32_t fad = first + s;
				uint32_t track_sector = fad - original_first;
				int attempt;

				for (attempt = 1; attempt <= max_attempts; attempt++) {
					int reinit_rv;

					if (!self.rip_active || !(self.app->state & APP_STATE_OPENED)) {
						checkpoint_track(hnd, tn, track_sector, fad);
						fs_close(hnd);
						free(buffer);
						return CMD_ERROR;
					}

					snprintf(track_text, sizeof(track_text), "Retry T%lu %d/%d",
						(unsigned long)tn, attempt, max_attempts);
					GUI_LabelSetText(self.track_label, track_text);
					set_io_status("Retry", fad);
					update_ui_display(secbyte, true);
					cdstat = checked_sector_read(buffer, fad, 1, type, secbyte);
					if (cdstat == ERR_OK) {
						break;
					}

					rip_log("Read attempt %d/%d failed at track %lu sector %lu (LBA %lu, FAD %lu), error %d",
						attempt, max_attempts, (unsigned long)tn,
						(unsigned long)track_sector,
						(unsigned long)(fad >= 150 ? fad - 150 : fad),
						(unsigned long)fad, cdstat);

					if (cdstat == ERR_NO_DISC || cdstat == ERR_DISC_CHG) {
						drive_error("Disc removed / changed", tn, fad, cdstat);
						checkpoint_track(hnd, tn, track_sector, fad);
						fs_close(hnd);
						free(buffer);
						return CMD_ERROR;
					}

					if (attempt < max_attempts && (cdstat == ERR_TIMEOUT || attempt == 3)) {
						rip_log("Reinitializing GD-ROM before retrying FAD %lu",
							(unsigned long)fad);
						reinit_rv = safe_cdrom_reinit();
						if (reinit_rv == ERR_OK) reinit_rv = safe_cdrom_set_sector_size(secbyte);
						if (reinit_rv != ERR_OK) {
							drive_error("Drive recovery failed", tn, fad, reinit_rv);
							checkpoint_track(hnd, tn, track_sector, fad);
							fs_close(hnd);
							free(buffer);
							return CMD_ERROR;
						}
					}

					if (attempt < max_attempts) {
						thd_sleep(100);
					}
				}

				if (cdstat != ERR_OK) {
					if (!zero_fill) {
						drive_error("Sector retries exhausted", tn, fad, cdstat);
						checkpoint_track(hnd, tn, track_sector, fad);
						fs_close(hnd);
						free(buffer);
						return CMD_ERROR;
					}

					memset(buffer, 0, secbyte);
					bad++;
					if (record_bad_sector(dst_file, tn, track_sector, fad, secbyte) != CMD_OK) {
						rip_log("Failed to record zero-filled FAD %lu", (unsigned long)fad);
						fs_close(hnd); free(buffer); return CMD_ERROR;
					}
					rip_log(self.recovery_mode ? "Deferred FAD %lu after %d attempts; placeholder is NOT recovered data" :
                        "Zero-filled unreadable FAD %lu after %d attempts",
						(unsigned long)fad, max_attempts);
				}

				uint32_t crc_before_write = crc32(self.current_crc, buffer, secbyte);
				set_io_status("Write", fad);
				errno = 0;
				if (fs_write(hnd, buffer, secbyte) != (ssize_t)secbyte) {
					storage_error("Recovered sector write failed", dst_file, errno);
					checkpoint_track(hnd, tn, track_sector, fad);
					fs_close(hnd);
					free(buffer);
					return CMD_ERROR;
				}

				if (stream_written(buffer, crc_before_write, secbyte, dst_file,
						tn, track_sector, fad, secbyte) != CMD_OK) {
					checkpoint_track(hnd, tn, track_sector, fad);
					fs_close(hnd); free(buffer); return CMD_ERROR;
				}
				self.processed_sectors++; self.session_sectors++;

				if (cdstat != ERR_OK &&
					checkpoint_track(hnd, tn, track_sector + 1, fad + 1) != CMD_OK) {
					fs_close(hnd);
					free(buffer);
					return CMD_ERROR;
				}
			}

			snprintf(track_text, sizeof(track_text), "Track %lu of %lu",
				(unsigned long)tn, (unsigned long)self.last_track);
			GUI_LabelSetText(self.track_label, track_text);
		}

		first += nsects;
		count -= nsects;

		checkpoint_sectors += nsects;
		update_ui_display(secbyte, false);

		if (checkpoint_sectors >= FAT_CHECKPOINT_SECTORS) {
			if (checkpoint_track(hnd, tn, original_count - count, first) != CMD_OK) {
				fs_close(hnd);
				free(buffer);
				return CMD_ERROR;
			}
			checkpoint_sectors = 0;
		}
	}

	if (bad) {
		rip_log("Track %lu completed with %lu zero-filled sector(s)",
			(unsigned long)tn, (unsigned long)bad);
	}
	else {
		rip_log("Track %lu completed successfully", (unsigned long)tn);
	}

	if (checkpoint_track(hnd, tn, original_count,
			original_first + original_count) != CMD_OK) {
		fs_close(hnd);
		free(buffer);
		return CMD_ERROR;
	}
	if (fs_close(hnd) != 0) {
		storage_error("Track close failed", dst_file, errno);
		free(buffer);
		return CMD_ERROR;
	}

	free(buffer);
	return CMD_OK;
}

int create_gdi_file(char *dst_folder, char *dst_file, char *text, int disc_type) {
	FILE *fp;

	(void)disc_type;

	snprintf(dst_file, NAME_MAX, "%s/%s.gdi", dst_folder, text);

	fp = fopen(dst_file, "w");
	if (!fp) {
		ds_printf("DS_ERROR: Error open %s.gdi for write\n", text);
		return CMD_ERROR;
	}

	fprintf(fp, "%lu\n", (unsigned long)self.last_track);

	for (uint32_t i = 0; i < self.track_count; i++) {
		uint32_t sector_size;
		get_sector_info(self.tracks[i].type, &sector_size);

		fprintf(fp, "%lu %lu %lu %lu %s 0\n",
			(unsigned long)self.tracks[i].track_num,
			(unsigned long)(self.tracks[i].start_lba - 150),
			(unsigned long)self.tracks[i].type,
			(unsigned long)sector_size,
			self.tracks[i].filename);
	}

	if (fclose(fp) != 0) {
		ds_printf("DS_ERROR: Error closing %s.gdi\n", text);
		return CMD_ERROR;
	}
	ds_printf("DS_OK: %s.gdi created successfully\n", text);

	return CMD_OK;
}

void gd_ripper_Open(void) {
    if (!self.input_event) return;
    GUI_DisableInput();
    SDL_DC_EmulateMouse(SDL_FALSE);
    GUI_ScreenSetJoySelectState(GUI_GetScreen(), 0);
    SetEventActive(self.input_event, 1);
    select_page(0);
    RipperMusicOpen(getenv("PATH"));
}

void gd_ripper_Music(GUI_Widget *widget) { (void)widget; MenuMusicCycle(); }

void gd_ripper_Close(void) {
    self.rip_active = 0;
    MenuMusicClose();
    if (self.input_event) SetEventActive(self.input_event, 0);
    GUI_ScreenSetJoySelectState(GUI_GetScreen(), 1);
    GUI_EnableInput();
}

void gd_ripper_Exit(void) {
    gd_ripper_Close();
    self.shutdown = 1;
    self.rip_active = 0;
    if (self.worker) { thd_join(self.worker, NULL); self.worker = NULL; }
    if (self.input_event) RemoveEvent(self.input_event);
    if (self.video_event) RemoveEvent(self.video_event);
}

void gd_ripper_Quit(GUI_Widget *widget) {
    (void)widget;
    if (!self.busy && !self.request) OpenMainApp();
}

static const char *folder_rows[] = {"folder-0", "folder-1", "folder-2", "folder-3", "folder-4", "folder-5", "folder-6"};

static int selected_is_dump(void) {
    char path[NAME_MAX];
    return self.folders.valid && strlen(self.folders.path) > (size_t)folder_root(self.folders.path) && snprintf(path, sizeof(path), "%s/rip.state", self.folders.path) < (int)sizeof(path) && FileExists(path);
}

static void folder_display(void) {
    folder_browser_t *b = &self.folders;
    char line[120];
    size_t len = strlen(b->path);
    snprintf(line, sizeof(line), "%s%s", len > 68 ? "..." : "", b->path + (len > 68 ? len-68 : 0));
    GUI_LabelSetText(APP_GET_WIDGET("folder-path"), line);
    for (unsigned i = 0; i < FOLDER_ROWS; ++i) {
        GUI_Widget *w = APP_GET_WIDGET(folder_rows[i]);
        GUI_WidgetSetEnabled(w, b->valid && i < b->count);
        /* Measure with the actual font; wide names must not escape the row. */
        snprintf(line, sizeof(line), "%s", b->names[i]);
        size_t cut = strlen(line);
        if (cut > sizeof(line)-4) cut = sizeof(line)-4;
        if (GUI_FontGetTextSize(APP_GET_FONT("small"), line).w > 566 || strlen(b->names[i]) >= sizeof(line)) {
            while (cut > 3) {
                --cut; memcpy(line+cut, "...", 4);
                if (GUI_FontGetTextSize(APP_GET_FONT("small"), line).w <= 566) break;
            }
        }
        GUI_LabelSetText(GUI_ButtonGetCaption(w), i < b->count ? line : "");
    }
    char parent[NAME_MAX]; strcpy(parent, b->path);
    GUI_WidgetSetEnabled(APP_GET_WIDGET("folder-up"), folder_parent(parent));
    GUI_WidgetSetEnabled(APP_GET_WIDGET("folder-prev"), b->valid && b->offset > 0);
    GUI_WidgetSetEnabled(APP_GET_WIDGET("folder-next"), b->valid && b->offset+b->count < b->total);
    GUI_WidgetSetEnabled(APP_GET_WIDGET("destination-confirm"), b->valid);
    if (b->valid && b->count) snprintf(line, sizeof(line), "%u-%u of %u folders", b->offset+1, b->offset+b->count, b->total);
    else snprintf(line, sizeof(line), "%s", b->valid ? "No subfolders" : "Device unavailable or folder cannot be read");
    GUI_LabelSetText(APP_GET_WIDGET("folder-count"), line);
    int dump = selected_is_dump();
    GUI_LabelSetText(GUI_ButtonGetCaption(APP_GET_WIDGET("destination-confirm")), dump ? "Select this dump" : "Use this folder");
    GUI_LabelSetText(APP_GET_WIDGET("destination-error"), !b->valid ? "Choose another device or go Up." : dump ?
        "Existing dump: selects its folder name for Resume / Verify." : "New dumps will get their own named folder here.");
}

void gd_ripper_ShowFileBrowser(GUI_Widget *widget) {
    (void)widget;
    if (self.busy) return;
    snprintf(self.folders.path, sizeof(self.folders.path), "%s", self.selected_path);
    folder_scan(&self.folders, 0);
    folder_display();
    select_page(1);
}

void gd_ripper_ShowMainPage(GUI_Widget *widget) {
    (void)widget;
    select_page(0);
}

void gd_ripper_Folder(GUI_Widget *widget) {
    if (self.busy) return;
    int rv = 0;
    if (widget == APP_GET_WIDGET("folder-up")) { folder_parent(self.folders.path); rv = folder_scan(&self.folders, 0); }
    else if (widget == APP_GET_WIDGET("folder-prev")) rv = folder_scan(&self.folders, -1);
    else if (widget == APP_GET_WIDGET("folder-next")) rv = folder_scan(&self.folders, 1);
    else for (unsigned i = 0; i < FOLDER_ROWS; ++i) if (widget == APP_GET_WIDGET(folder_rows[i])) {
        rv = folder_enter(&self.folders, i); break;
    }
    folder_display();
    if (rv) GUI_LabelSetText(APP_GET_WIDGET("destination-error"), "Cannot open folder: unavailable, or path is too long.");
    self.focus = 3; focus_step(1); /* First folder, or the next available action. */
}

void gd_ripper_FileBrowserConfirm(GUI_Widget *widget) {
    (void)widget;
    if (self.busy || !self.folders.valid) return;
    char path[NAME_MAX]; strcpy(path, self.folders.path);
    self.chosen_name[0] = 0;
    if (selected_is_dump()) {
        const char *name = strrchr(path, '/');
        if (!name || name == path || strlen(name+1) > 254) return;
        strcpy(self.chosen_name, name+1);
        folder_parent(path);
        GUI_TextEntrySetText(self.gname, self.chosen_name);
    }
    snprintf(self.selected_path, sizeof(self.selected_path), "%s", path);
    GUI_LabelSetText(self.destination_path, self.selected_path);
    select_page(0);
}

/* All drive commands (including insertion probes) run on this worker. */
static void refresh_controls(void) {
    GUI_WidgetSetEnabled(self.start_btn, !self.busy && self.disc_ready && self.worker != NULL);
    GUI_WidgetSetEnabled(self.cancel_btn, self.busy && self.rip_active);
    GUI_WidgetSetEnabled(self.verify_btn, !self.busy && self.worker != NULL);
    GUI_WidgetSetEnabled(self.exit_btn, !self.busy);
    GUI_WidgetSetEnabled(self.advanced_btn, !self.busy);
    GUI_WidgetSetEnabled(self.browse_btn, !self.busy);
    GUI_WidgetSetEnabled(self.gname, !self.busy);
    bool recovery = !!GUI_WidgetGetState(self.recover_btn);
    GUI_WidgetSetEnabled(self.recover_btn, !self.busy);
    GUI_WidgetSetEnabled(self.bad, !self.busy && !recovery);
    GUI_WidgetSetEnabled(self.use_bin_btn, !self.busy && !recovery);
    GUI_WidgetSetEnabled(self.edc_btn, !self.busy && !recovery);
    GUI_WidgetSetEnabled(APP_GET_WIDGET("recovery-start"), !self.busy && self.disc_ready && self.recovery_prompt);
    GUI_WidgetSetEnabled(APP_GET_WIDGET("recovery-later"), !self.busy);
}

static void *service_thread(void *arg) {
    uint64_t stable_since = 0, next_poll = timer_ms_gettime64() + 1500;
    (void)arg;
    while (!self.shutdown) {
        if (!(self.app->state & APP_STATE_OPENED)) { thd_sleep(50); continue; }
        if (self.request) {
            int operation = self.request;
            self.request = 0;
            self.start_time = timer_ms_gettime64();
            /* Wait for any initial music load before touching the disc; then
             * allow RAM playback only throughout ripping/recovery/verification. */
            MenuMusicStorageLock();
            if (operation == 1) gd_ripper_thread(NULL);
            else if (operation == 3) gd_ripper_thread((void *)1);
            else gd_verify_thread(NULL);
            MenuMusicStorageUnlock();
            self.busy = 0;
            self.rip_active = 0;
            self.drive_command = 0;
            self.io_started = 0;
            refresh_controls();
            next_poll = timer_ms_gettime64() + 1000;
        } else if (timer_ms_gettime64() >= next_poll && claim_worker()) {
            int status = 0, type = 0;
            int rv;
            MenuMusicStorageLock();
            rv = cdrom_get_status(&status, &type);
            next_poll = timer_ms_gettime64() + 500;
            if (rv == ERR_OK && (status == CD_STATUS_OPEN || status == CD_STATUS_NO_DISC)) {
                self.disc_ready = false;
                self.media_seen = false;
                self.disc_header_valid = false;
                stable_since = 0;
                GUI_LabelSetText(self.disc_label, status == CD_STATUS_OPEN ? "Lid open - insert disc" : "No disc inserted");
            } else if (rv == ERR_OK && (status == CD_STATUS_PAUSED || status == CD_STATUS_STANDBY || status == CD_STATUS_PLAYING)) {
                self.disc_ready = self.media_seen;
                if (!self.media_seen) {
                    if (!stable_since) stable_since = timer_ms_gettime64();
                    if (timer_ms_gettime64() - stable_since >= DRIVE_SETTLE_MS) {
                        self.rip_active = 1;
                        refresh_controls();
                        GUI_LabelSetText(self.disc_label, "Disc detected - reading title...");
                        GUI_LabelSetText(self.track_label, "Reading disc title...");
                        gd_ripper_ipbin_name();
                        self.media_seen = true;
                        self.disc_ready = true;
                        self.rip_active = 0;
                        GUI_LabelSetText(self.track_label, "Ready to rip");
                        set_message(self.disc_header_valid ?
                            "Disc ready. Select Start / Resume; CRC checking runs while ripping." :
                            "Title unavailable. Set a folder name and select Start / Resume, or reinsert the disc.");
                        if (!self.disc_header_valid) {
                            GUI_LabelSetText(self.disc_label, "Disc detected - title unavailable");
                            GUI_TextEntrySetText(self.gname, "ripped_disc");
                        }
                    }
                }
            } else {
                stable_since = 0;
                self.disc_ready = false;
                /* DISC_CHG also detects a quick swap between two polls. */
                if (rv == ERR_DISC_CHG) { self.media_seen = false; self.disc_ready = false; }
            }
            MenuMusicStorageUnlock();
            self.busy = 0;
            refresh_controls();
        }
        thd_sleep(50);
    }
    return NULL;
}

static const char *main_focus[] = {"start_btn", "cancel_btn", "advanced-btn", "exit-btn", "gname-text", "browse-btn", "music-btn"};
static const char *advanced_focus[] = {"recover-btn", "edc-btn", "verify-btn", "bad_btn", "use_bin_btn", "num-read", "advanced-back"};
static const char *destination_focus[] = {"device-sd", "device-ide", "device-pc", "folder-up",
    "folder-0", "folder-1", "folder-2", "folder-3", "folder-4", "folder-5", "folder-6",
    "folder-prev", "folder-next", "destination-confirm", "destination-back"};
static const char *recovery_focus[] = {"recovery-start", "recovery-later"};

static int focus_count(void) { return self.page == 3 ? 2 : self.page == 2 ? 7 : self.page == 1 ? 15 : 7; }

static GUI_Widget *focus_widget(int index) {
    const char **names = self.page == 3 ? recovery_focus : self.page == 2 ? advanced_focus :
        self.page == 1 ? destination_focus : main_focus;
    return APP_GET_WIDGET(names[index]);
}

static void focus_step(int direction) {
    int count = focus_count();
    for (int i = 0; i < count; ++i) GUI_WidgetClearFlags(focus_widget(i), WIDGET_INSIDE);
    for (int i = 0; i < count; ++i) {
        self.focus = (self.focus + direction + count) % count;
        if (!(GUI_WidgetGetFlags(focus_widget(self.focus)) & (WIDGET_DISABLED | WIDGET_HIDDEN))) break;
    }
    GUI_WidgetSetFlags(focus_widget(self.focus), WIDGET_INSIDE);
}

static void select_page(int page) {
    for (int i = 0; i < focus_count(); ++i) GUI_WidgetClearFlags(focus_widget(i), WIDGET_INSIDE);
    self.page = page;
    GUI_CardStackShowIndex(self.pages, page);
    self.focus = focus_count()-1;
    focus_step(1);
}

void gd_ripper_Advanced(GUI_Widget *widget) { (void)widget; if (!self.busy) select_page(2); }

void gd_ripper_Toggle(GUI_Widget *widget) {
    if (self.busy) return;
    int enabled = !GUI_WidgetGetState(widget);
    GUI_WidgetSetState(widget, enabled);
    const char *label = widget == self.recover_btn ?
        (enabled ? "Recover damaged disc: ON (first pass + prompt)" : "Recover damaged disc: OFF") :
        widget == self.bad ?
        (enabled ? "Zero-fill unreadable sectors: ON" : "Zero-fill unreadable sectors: OFF") :
        widget == self.edc_btn ?
        (enabled ? "Advanced CRC (ECC + repair): ON" : "Advanced CRC (ECC + repair): OFF") :
        (enabled ? "Track format: BIN (raw, recommended)" : "Track format: ISO (no catalog CRC match)");
    GUI_LabelSetText(GUI_ButtonGetCaption(widget), label);
    if (widget == self.recover_btn && enabled) {
        GUI_WidgetSetState(self.use_bin_btn, 1);
        GUI_WidgetSetState(self.edc_btn, 1);
        GUI_WidgetSetState(self.bad, 0);
        GUI_LabelSetText(GUI_ButtonGetCaption(self.use_bin_btn), "Track format: BIN (raw, recommended)");
        GUI_LabelSetText(GUI_ButtonGetCaption(self.edc_btn), "Advanced CRC (ECC + repair): ON");
        GUI_LabelSetText(GUI_ButtonGetCaption(self.bad), "Zero-fill unreadable sectors: OFF");
    }
    refresh_controls();
}

void gd_ripper_Destination(GUI_Widget *widget) {
    const char *path = widget == APP_GET_WIDGET("device-sd") ? NEXT_SD_GAMES_PATH :
        widget == APP_GET_WIDGET("device-ide") ? NEXT_IDE_GAMES_PATH : NEXT_PC_GAMES_PATH;
    snprintf(self.folders.path, sizeof(self.folders.path), "%s", path);
    int ready = gd_prepare_destination(path);
    folder_scan(&self.folders, 0);
    folder_display();
    if(ready < 0) GUI_LabelSetText(APP_GET_WIDGET("destination-error"),
        "Games folder unavailable. Check the device, or go Up to choose a folder.");
}

static void activate_focus(void) {
    GUI_Widget *w = focus_widget(self.focus);
    if (!(GUI_WidgetGetFlags(w) & (WIDGET_DISABLED | WIDGET_HIDDEN))) GUI_WidgetClicked(w, 0, 0);
}

static void input_event(void *event, void *param, int action) {
    SDL_Event *e = param;
    (void)event;
    if (action != EVENT_ACTION_UPDATE || !e || !(self.app->state & APP_STATE_OPENED)) return;
    /* Console input and system shortcuts retain their global handlers. */
    if (ConsoleIsVisible() || (e->type == SDL_KEYDOWN &&
        (e->key.keysym.sym == SDLK_F1 || e->key.keysym.sym == SDLK_PRINT ||
         (e->key.keysym.mod & (KMOD_CTRL | KMOD_ALT))))) return;
    /* Let text entry / DreamShell's keyboard process actual typing. */
    if (GUI_ScreenGetFocusWidget(GUI_GetScreen())) {
        GUI_ScreenEvent(GUI_GetScreen(), e, 0, 0);
        e->type = SDL_NOEVENT; /* GUI_Input must not type it a second time. */
        return;
    }
    if (e->type == SDL_JOYHATMOTION && e->jhat.hat == 0) {
        if (e->jhat.value & (SDL_HAT_UP | SDL_HAT_LEFT)) focus_step(-1);
        else if (e->jhat.value & (SDL_HAT_DOWN | SDL_HAT_RIGHT)) focus_step(1);
    } else if (e->type == SDL_JOYAXISMOTION) {
        int dir = e->jaxis.value < -48 ? -1 : e->jaxis.value > 48 ? 1 : 0;
        int *old = e->jaxis.axis == 0 ? &self.analog_x : &self.analog_y;
        if (e->jaxis.axis <= 1) { if (dir && dir != *old) focus_step(dir); *old = dir; }
    } else if (e->type == SDL_JOYBUTTONDOWN) {
        if (e->jbutton.button == SDL_DC_Y) gd_ripper_Music(NULL);
        else if (e->jbutton.button == SDL_DC_A) activate_focus();
        else if (e->jbutton.button == SDL_DC_B) {
            if (self.busy) gd_ripper_CancelRip(NULL);
            else if (self.page == 1 && folder_root(self.folders.path) && strlen(self.folders.path) > (size_t)folder_root(self.folders.path))
                gd_ripper_Folder(APP_GET_WIDGET("folder-up"));
            else if (self.page) select_page(0);
        }
    } else if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
            case SDLK_UP: case SDLK_LEFT: focus_step(-1); break;
            case SDLK_DOWN: case SDLK_RIGHT: case SDLK_TAB: focus_step(1); break;
            case SDLK_m: gd_ripper_Music(NULL); break;
            case SDLK_RETURN: case SDLK_SPACE: activate_focus(); break;
            case SDLK_ESCAPE: if (self.busy) gd_ripper_CancelRip(NULL); else select_page(0); break;
            default: GUI_ScreenEvent(GUI_GetScreen(), e, 0, 0); break;
        }
    } else if (e->type == SDL_MOUSEMOTION || e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP || e->type >= SDL_USEREVENT) {
        /* Real mouse remains optional; the controller never moves a pointer. */
        GUI_ScreenEvent(GUI_GetScreen(), e, 0, 0);
    }
    e->type = SDL_NOEVENT; /* The app owns this input; do not replay it globally. */
}

static void video_event(void *event, void *param, int action) {
    (void)event; (void)param;
    if (action != EVENT_ACTION_RENDER || !(self.app->state & APP_STATE_OPENED)) return;
    char music_text[48];
    MenuMusicLabel(music_text,sizeof(music_text));
    GUI_Widget *caption = GUI_ButtonGetCaption(APP_GET_WIDGET("music-btn"));
    if(strcmp(GUI_LabelGetText(caption),music_text)) GUI_LabelSetText(caption,music_text);
    if (self.busy && self.io_started) {
        unsigned seconds = ((uint32_t)timer_ms_gettime64() - self.io_started) / 1000;
        if (seconds >= 2 && seconds != self.heartbeat_second) {
            self.heartbeat_second = seconds;
            char line[120];
            snprintf(line, sizeof(line), "%s waiting %us at FAD %lu%s", self.io_operation, seconds,
                (unsigned long)self.current_fad, self.rip_active ? " - B / Stop to request pause" : " - Stop requested");
            set_message(line);
        }
        if (seconds >= 15 && self.drive_command) GUI_LabelSetText(self.track_label, "Drive command stalled");
    }
}

static void set_message(const char *text) {
    const char *p = text;
    for (int line = 0; line < 3; ++line) {
        char part[76];
        size_t len = strlen(p), take = len > 70 ? 70 : len;
        if (len > take) {
            size_t space = take;
            while (space && p[space] != ' ') --space;
            if (space) take = space;
        }
        memcpy(part, p, take); part[take] = '\0'; p += take;
        while (*p == ' ') ++p;
        GUI_LabelSetText(line == 0 ? self.message : APP_GET_WIDGET(line == 1 ? "message-2" : "message-3"), part);
    }
}

/* Bind resumable files to the disc's boot sector, not just its TOC shape. */
static int check_disc_identity(const char *folder, bool resume, int disc_type) {
    uint8_t *data;
    char path[NAME_MAX];
    file_t fd;
    ssize_t completed = 0;
    uint32_t boot_fad = disc_type == CD_GDROM ? 45150 : 0;
    if (!boot_fad) return CMD_OK; /* Existing CD/CD-R workflow uses TOC checks. */
    if (safe_cdrom_set_sector_size(2048) != ERR_OK ||
            timed_cdrom_read(self.disc_header, boot_fad, 1) != ERR_OK) return CMD_ERROR;
    self.disc_header_valid = true;
    data = memalign(32, 2352);
    if (!data) return CMD_ERROR;
    snprintf(path, sizeof(path), "%s/rip.disc", folder);
    if (resume && FileExists(path)) {
        fd = fs_open(path, O_RDONLY);
        bool same = fd != FILEHND_INVALID && fs_total(fd) == 2048 &&
            fs_read(fd, data, 2048) == 2048 && !memcmp(data, self.disc_header, 2048);
        if (fd != FILEHND_INVALID) fs_close(fd);
        free(data);
        return same ? CMD_OK : CMD_ERROR;
    }
    if (resume) {
        char track_path[NAME_MAX];
        uint64_t bytes = 0;
        /* Older versions have no identity file: compare existing track 3's IP.BIN. */
        snprintf(track_path, sizeof(track_path), "%s/track03.%s", folder, self.use_bin ? "bin" : "iso");
        fd = fs_open(track_path, O_RDONLY);
        if (fd != FILEHND_INVALID) {
            ssize_t size = fs_total(fd);
            if (size > 0) bytes = size;
            if (bytes >= (self.use_bin ? 2352 : 2048)) {
                ssize_t want = self.use_bin ? 2352 : 2048;
                if (fs_read(fd, data, want) != want ||
                        memcmp(data + (self.use_bin ? 16 : 0), self.disc_header, 2048)) {
                    fs_close(fd); free(data); return CMD_ERROR;
                }
            }
            fs_close(fd);
        }
        /* For a zero-byte legacy track 3, match the already-dumped low data track. */
        if (!bytes && self.track_count && self.tracks[0].type == 4) {
            uint8_t *current = memalign(32, 2048);
            snprintf(track_path, sizeof(track_path), "%s/%s", folder, self.tracks[0].filename);
            fd = fs_open(track_path, O_RDONLY);
            ssize_t want = self.use_bin ? 2352 : 2048;
            bool same = current && fd != FILEHND_INVALID && fs_read(fd, data, want) == want &&
                timed_cdrom_read(current, self.tracks[0].start_lba, 1) == ERR_OK &&
                !memcmp(data + (self.use_bin ? 16 : 0), current, 2048);
            if (fd != FILEHND_INVALID) fs_close(fd);
            free(current);
            if (!same) { free(data); return CMD_ERROR; }
        }
    }
    fd = fs_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    int rv = CMD_OK;
    if (fd == FILEHND_INVALID) rv = CMD_ERROR;
    else {
        if (fs_write(fd, self.disc_header, 2048) != 2048 ||
            (self.sync_mount[0] && fs_complete(fd, &completed))) rv = CMD_ERROR;
        if (fs_close(fd)) rv = CMD_ERROR;
    }
    free(data);
    return rv;
}

/* Repair only addresses found by the storage scan, keeping the original bytes. */
static int repair_suspects(const char *path, uint32_t first, uint32_t count) {
    char map_path[NAME_MAX], backup_path[NAME_MAX], journal[NAME_MAX], line[120];
    FILE *map;
    file_t track = FILEHND_INVALID, backup = FILEHND_INVALID;
    uint8_t *old = NULL, *read = NULL;
    int result = CMD_ERROR;
    bool changed = false;
    if (snprintf(map_path, sizeof(map_path), "%s.suspect", path) >= (int)sizeof(map_path) ||
        snprintf(backup_path, sizeof(backup_path), "%s.repair-backup", path) >= (int)sizeof(backup_path) ||
        snprintf(journal, sizeof(journal), "%s.crc", path) >= (int)sizeof(journal)) return CMD_ERROR;
    if (!FileExists(map_path)) return CMD_OK;
    map = fopen(map_path, "r");
    if (!map) return CMD_ERROR;
    track = fs_open(path, O_RDWR);
    old = memalign(32, 2352); read = memalign(32, 2352);
    if (track == FILEHND_INVALID || !old || !read || safe_cdrom_set_sector_size(2352) != ERR_OK) goto out;
    while (fgets(line, sizeof(line), map)) {
        unsigned long sector, fad, flags;
        char extra;
        if (sscanf(line, "%lu %lu %lu %c", &sector, &fad, &flags, &extra) != 3 ||
            sector >= count || fad != first + sector) goto out;
        (void)flags;
        off_t offset = (off_t)(sector * 2352UL);
        if (!self.rip_active || fs_seek(track, offset, SEEK_SET) != offset || fs_read(track, old, 2352) != 2352) goto out;
        if (!gd_check_sector(old, fad)) continue;
        int rv = ERR_SYS;
        for (int attempt = 1; attempt <= self.max_attempts && self.rip_active; ++attempt) {
            char message[128];
            snprintf(message, sizeof(message), "Repair FAD %lu: attempt %d/%d", fad, attempt, self.max_attempts);
            set_message(message); set_io_status("Repair", fad);
            rv = timed_cdrom_read(read, fad, 1);
            if (rv == ERR_OK && !gd_check_sector(read, fad)) break;
            if (rv == ERR_NO_DISC || rv == ERR_DISC_CHG) goto out;
            rv = ERR_SYS;
            if (attempt == 3 && (safe_cdrom_reinit() != ERR_OK || safe_cdrom_set_sector_size(2352) != ERR_OK)) goto out;
            thd_sleep(100);
        }
        if (!self.rip_active || rv != ERR_OK) {
            rip_log("Repair stopped: FAD %lu still invalid; original sector retained", fad);
            goto out;
        }
        if (!changed) {
            if (FileExists(journal) && fs_unlink(journal)) goto out;
            changed = true;
        }
        if (backup == FILEHND_INVALID) backup = gd_open_append(backup_path);
        if (backup == FILEHND_INVALID) goto out;
        /* Records: ASCII 'FAD <n>\n', then exactly 2352 original bytes. */
        int n = snprintf(line, sizeof(line), "FAD %lu\n", fad);
        ssize_t completed = 0;
        if (fs_write(backup, line, n) != n || fs_write(backup, old, 2352) != 2352 ||
            (self.sync_mount[0] && fs_complete(backup, &completed))) goto out;
        if (fs_seek(track, offset, SEEK_SET) != offset || fs_write(track, read, 2352) != 2352 ||
            sync_track(track) != CMD_OK) goto out;
        rip_log("Replaced FAD %lu with an EDC/ECC/address-valid reread; original saved in repair-backup", fad);
    }
    if (ferror(map)) goto out;
    result = CMD_OK;
out:
    if (track != FILEHND_INVALID) fs_close(track);
    if (backup != FILEHND_INVALID) fs_close(backup);
    free(old); free(read); fclose(map);
    if (changed) set_message("Repair pass finished. Rebuilding the whole-track CRC from storage is required after edits.");
    if (result == CMD_OK) result = retire_repaired_bad_map(path, first, count);
    return result;
}

/* One SH-4 CPU: protect the UI/service ownership hand-off from preemption. */
static bool claim_worker(void) {
    irq_mask_t irq = irq_disable();
    bool available = !self.busy && !self.request;
    if (available) self.busy = 1;
    irq_restore(irq);
    return available;
}

/* Only retire a zero-fill map when every listed sector now validates. */
static int retire_repaired_bad_map(const char *path, uint32_t first, uint32_t count) {
    char map_path[NAME_MAX], history_path[NAME_MAX], line[192];
    FILE *map;
    file_t track, history;
    uint8_t *sector;
    ssize_t completed = 0;
    if (snprintf(map_path, sizeof(map_path), "%s.bad", path) >= (int)sizeof(map_path) ||
        snprintf(history_path, sizeof(history_path), "%s.bad.history", path) >= (int)sizeof(history_path)) return CMD_ERROR;
    if (!FileExists(map_path)) return CMD_OK;
    map = fopen(map_path, "r");
    track = fs_open(path, O_RDONLY);
    sector = memalign(32, 2352);
    if (!map || track == FILEHND_INVALID || !sector) {
        if (map) fclose(map);
        if (track != FILEHND_INVALID) fs_close(track);
        free(sector); return CMD_ERROR;
    }
    bool valid = true;
    while (fgets(line, sizeof(line), map)) {
        unsigned long tn, index, lba, fad;
        unsigned long long offset;
        if (!strncmp(line, "track,", 6)) continue;
        if (sscanf(line, "%lu,%lu,%lu,%lu,%llu", &tn, &index, &lba, &fad, &offset) != 5 ||
            index >= count || fad != first + index || offset != (uint64_t)index * 2352 ||
            fs_seek(track, (off_t)offset, SEEK_SET) != (off_t)offset ||
            fs_read(track, sector, 2352) != 2352 || gd_check_sector(sector, fad)) { valid = false; break; }
    }
    if (ferror(map)) valid = false;
    free(sector); fs_close(track);
    if (!valid) { fclose(map); return CMD_OK; }
    rewind(map);
    history = gd_open_append(history_path);
    if (history == FILEHND_INVALID) { fclose(map); return CMD_ERROR; }
    while (fgets(line, sizeof(line), map)) {
        size_t n = strlen(line);
        if (fs_write(history, line, n) != (ssize_t)n) valid = false;
    }
    if (ferror(map) || (self.sync_mount[0] && fs_complete(history, &completed))) valid = false;
    fclose(map);
    if (fs_close(history)) valid = false;
    if (!valid) return CMD_ERROR;
    return fs_unlink(map_path) ? CMD_ERROR : CMD_OK;
}
