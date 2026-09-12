/* DreamShell ##version##

   module.c - GD Ripper app module
   Copyright (C)2014 megavolt85
   Copyright (C)2025 SWAT
   DreamShell NeXT recovery improvements coordinated and tested by TPMJB

*/

#include "ds.h"
#include "isofs/isofs.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <dc/cdrom.h>

DEFAULT_MODULE_EXPORTS(app_gd_ripper);

#define SEC_BUF_SIZE 16
#define UI_UPDATE_INTERVAL 500
#define GD_COMMAND_TIMEOUT_MS 8000
#define FAT_CHECKPOINT_SECTORS 4096
#define MAX_TRACKS 99
#define RIP_STATE_HEADER "DreamShell GD Ripper state v1"

static int rip_sec(uint32_t tn, uint32_t first, uint32_t count, uint32_t type, char *dst_file);
static void* gd_ripper_thread(void *arg);
static int create_gdi_file(char *dst_folder, char *dst_file, char *text, int disc_type);
static int get_disc_status_and_type(int *status, int *disc_type);
static int safe_cdrom_read_toc(cd_toc_t *toc, bool high_density);
static int safe_cdrom_reinit(void);
static int prepare_destination_paths(char *dst_folder, char *dst_file, char *text, int disc_type, bool *resume);
static int process_tracks(char *dst_folder, char *dst_file);
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

static struct self {
	App_t *app;
	GUI_Widget *bad;
	GUI_Widget *gname;
	GUI_Widget *pbar;
	GUI_Widget *track_label;
	GUI_Widget *num_read;
	GUI_Widget *start_btn;
	GUI_Widget *cancel_btn;
	GUI_Widget *read_name_btn;
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
	uint32_t track_count;
	uint32_t current_fad;
	int max_attempts;
	bool zero_fill;
	bool use_bin;
	track_info_t tracks[MAX_TRACKS];
	char selected_path[NAME_MAX];
	char rip_name[NAME_MAX];
	char rip_destination[NAME_MAX];
	char log_path[NAME_MAX];
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

static void rip_log(const char *format, ...) {
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
		return;
	}

	line_len = snprintf(line, sizeof(line), "%llu.%03llu %s\n",
		(unsigned long long)(elapsed / 1000),
		(unsigned long long)(elapsed % 1000), message);
	if (line_len < 0) {
		return;
	}
	if (line_len >= (int)sizeof(line)) {
		line_len = sizeof(line) - 1;
	}

	hnd = fs_open(self.log_path, O_WRONLY | O_CREAT | O_APPEND);
	if (hnd == FILEHND_INVALID) {
		ds_printf("DS_WARN: Can't open rip log %s\n", self.log_path);
		return;
	}

	if (fs_write(hnd, line, line_len) != line_len) {
		ds_printf("DS_WARN: Can't append to rip log %s\n", self.log_path);
	}
	else if (self.sync_mount[0]) {
		ssize_t completed = 0;
		if (fs_complete(hnd, &completed) != 0) {
			ds_printf("DS_WARN: Can't sync rip log %s\n", self.log_path);
		}
	}
	fs_close(hnd);
}

static int timed_cdrom_read(void *buffer, uint32_t first, size_t count) {
	cd_read_params_t params;

	params.start_sec = first;
	params.num_sec = count;
	params.buffer = buffer;
	params.is_test = 0;

	/* PIO is used because KOS's DMA helper has no bounded wait path. */
	return cdrom_exec_cmd_timed(CD_CMD_PIOREAD, &params, GD_COMMAND_TIMEOUT_MS);
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
	GUI_WidgetSetEnabled(self.read_name_btn, 1);
	GUI_LabelSetText(self.speed_label, " ");
	GUI_LabelSetText(self.time_label, " ");
	GUI_LabelSetText(self.track_label, "GD Ripper");
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
}

void gd_ripper_Number_read()
{
	char name[4];
	int attempts = atoi(GUI_TextEntryGetText(self.num_read));

	if (attempts > 50)
	{
		GUI_TextEntrySetText(self.num_read, "50");
	}
	else if (attempts < 1)
	{
		GUI_TextEntrySetText(self.num_read, "1");
	}
	else
	{
		snprintf(name, sizeof(name), "%d", attempts);
		GUI_TextEntrySetText(self.num_read, name);
	}
}

void gd_ripper_Gamename()
{
	char text[NAME_MAX];

	sanitize_rip_name(text, sizeof(text), GUI_TextEntryGetText(self.gname));
	GUI_TextEntrySetText(self.gname, text);
}

void gd_ripper_ipbin_name()
{
	cd_toc_t toc;
	int status = 0, disc_type = 0;
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

	if (timed_cdrom_read(pbuff, lba, 1) != ERR_OK) {
		ds_printf("DS_ERROR: GD read error\n"); 
		free(pbuff);
		return;
	}

	ipbin_meta_t *meta = (ipbin_meta_t*) pbuff;

	if(meta->boot_file[0] != '0' && meta->boot_file[0] != '1') {
		free(pbuff);
		GUI_TextEntrySetText(self.gname, "ripped_disc");
		return;
	}

	char *p;
	char *o;
	
	p = meta->title;
	o = text;

	// skip any spaces at the beginning
	while(*p == ' ' && meta->title + 29 > p) 
		p++;

	// copy rest to output buffer
	while(meta->title + 29 > p) { 
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
		self.read_name_btn = APP_GET_WIDGET("Read-name");
		self.speed_label = APP_GET_WIDGET("speed-label");
		self.time_label = APP_GET_WIDGET("time-label");
		self.progress_percent_label = APP_GET_WIDGET("progress-percent-label");
		self.current_lba_label = APP_GET_WIDGET("current-lba-label");
		self.sectors_total_label = APP_GET_WIDGET("sectors-total-label");
		self.sectors_processed_label = APP_GET_WIDGET("sectors-processed-label");
		self.destination_path = APP_GET_WIDGET("destination-path");
		self.file_browser = APP_GET_WIDGET("file-browser");
		self.pages = APP_GET_WIDGET("pages");
		self.use_bin_btn = APP_GET_WIDGET("use_bin_btn");

		if(DirExists("/ide")) {
			strcpy(self.selected_path, "/ide");
		}
		else if(DirExists("/sd")) {
			strcpy(self.selected_path, "/sd");
		}
		else if(DirExists("/pc")) {
			strcpy(self.selected_path, "/pc");
		}
		else {
			strcpy(self.selected_path, "/ram");
		}

		GUI_LabelSetText(self.destination_path, self.selected_path);
		GUI_WidgetSetEnabled(self.cancel_btn, 0);
		gd_ripper_ipbin_name();
	} 
	else 
	{
		ds_printf("DS_ERROR: %s: Attempting to call %s is not by the app initiate.\n", 
					lib_get_name(), __func__);
	}
}


void gd_ripper_StartRip(GUI_Widget *widget) 
{
	(void)widget;
	if(self.app->thd)
	{
		self.rip_active = 0;
		thd_join(self.app->thd, NULL);
		self.app->thd = NULL;
	}

	reset_rip_state();
	self.max_attempts = atoi(GUI_TextEntryGetText(self.num_read));
	if (self.max_attempts < 1) self.max_attempts = 1;
	if (self.max_attempts > 50) self.max_attempts = 50;
	self.zero_fill = !!GUI_WidgetGetState(self.bad);
	self.use_bin = !!GUI_WidgetGetState(self.use_bin_btn);
	sanitize_rip_name(self.rip_name, sizeof(self.rip_name),
		GUI_TextEntryGetText(self.gname));
	GUI_TextEntrySetText(self.gname, self.rip_name);
	snprintf(self.rip_destination, sizeof(self.rip_destination), "%s",
		self.selected_path);
	self.rip_active = 1;

	GUI_WidgetSetEnabled(self.start_btn, 0);
	GUI_WidgetSetEnabled(self.cancel_btn, 1);
	GUI_WidgetSetEnabled(self.read_name_btn, 0);
	
	GUI_LabelSetText(self.track_label, "Starting...");
	GUI_LabelSetText(self.speed_label, "Preparing...");
	GUI_LabelSetText(self.time_label, "Please wait");

	self.app->thd = thd_create(0, gd_ripper_thread, NULL);
	if (!self.app->thd) {
		reset_rip_state();
		GUI_LabelSetText(self.track_label, "Thread start failed");
	}
}

void gd_ripper_CancelRip(GUI_Widget *widget)
{
	(void)widget;
	ds_printf("DS_PROCESS: Cancelling ripping\n");
	self.rip_active = 0;

	if(self.app->thd)
	{
		thd_join(self.app->thd, NULL);
		self.app->thd = NULL;
		ds_printf("DS_INFO: Ripping cancelled\n");
	}

	reset_rip_state();
	GUI_LabelSetText(self.track_label, "Cancelled");
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

	for (uint32_t tn = first; tn <= last; tn++) {
		if (count >= capacity) {
			ds_printf("DS_ERROR: Disc has too many tracks\n");
			return CMD_ERROR;
		}

		uint32_t type = TOC_CTRL(toc.entry[tn-1]);
		uint32_t start = TOC_LBA(toc.entry[tn-1]);
		uint32_t s_end = TOC_LBA((tn == last ? toc.leadout_sector : toc.entry[tn]));
		uint32_t nsec = s_end - start;

		if (disc_type != CD_GDROM && type == 4) nsec -= 2;
		else if (area == 1 && tn != last && type != TOC_CTRL(toc.entry[tn])) nsec -= 150;
		else if (area == 0 && type == 4) nsec -= 150;

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

static int write_completion_marker(const char *dst_folder) {
	char path[NAME_MAX];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/rip.complete", dst_folder);
	fp = fopen(path, "w");
	if (!fp) {
		return CMD_ERROR;
	}
	fprintf(fp, "Complete: %llu sectors\n",
		(unsigned long long)self.total_sectors);
	if (fclose(fp) != 0) {
		return CMD_ERROR;
	}
	return CMD_OK;
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
	bool cancelled;
	char complete_path[NAME_MAX];
	const char *failure_label = "Rip failed";

	(void)arg;

	ds_printf("DS_PROCESS: Starting disc ripping process\n");
	self.start_time = timer_ms_gettime64();
	self.processed_sectors = 0;
	self.session_sectors = 0;
	self.log_path[0] = '\0';
	set_sync_mount(self.rip_destination);

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

	GUI_LabelSetText(self.track_label, "Preparing...");
	ds_printf("DS_PROCESS: Preparing destination: %s\n", dst_folder);

	if(prepare_destination_paths(dst_folder, dst_file, text, disc_type, &resume) != CMD_OK) {
		ds_printf("DS_ERROR: Failed to prepare destination paths\n");
		failure_label = "Name/options conflict";
		goto out;
	}
	destination_ready = true;

	if (snprintf(self.log_path, sizeof(self.log_path), "%s/rip.log", dst_folder) >=
			(int)sizeof(self.log_path)) {
		ds_printf("DS_ERROR: Destination log path is too long\n");
		goto out;
	}

	rip_log("Started %s rip with %lu track(s), %llu total sectors, retries=%d, zero-fill=%d",
		resume ? "resumed" : "new", (unsigned long)self.track_count,
		(unsigned long long)self.total_sectors, self.max_attempts, self.zero_fill);

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
	if (FileExists(complete_path)) {
		fs_unlink(complete_path);
	}

	ds_printf("DS_PROCESS: Starting track extraction\n");

	if(process_tracks(dst_folder, dst_file) != CMD_OK) {
		goto out;
	}

	if (write_completion_marker(dst_folder) != CMD_OK) {
		rip_log("All tracks finished, but the completion marker could not be written");
		goto out;
	}

	rip_log("Rip completed successfully: %llu sectors",
		(unsigned long long)self.processed_sectors);
	success = true;

out:
	cancelled = !self.rip_active;
	if (!success && self.log_path[0]) {
		rip_log(cancelled ? "Rip cancelled; partial files preserved" :
			"Rip paused after an error; partial files preserved for resume");
	}

	self.rip_active = 0;
	GUI_WidgetSetEnabled(self.start_btn, 1);
	GUI_WidgetSetEnabled(self.cancel_btn, 0);
	GUI_WidgetSetEnabled(self.read_name_btn, 1);
	if (success) {
		char final_done[64];
		char final_total[64];

		snprintf(final_done, sizeof(final_done), "Done: %llu",
			(unsigned long long)self.processed_sectors);
		snprintf(final_total, sizeof(final_total), "Total: %llu",
			(unsigned long long)self.total_sectors);
		GUI_LabelSetText(self.track_label, "Rip complete");
		GUI_ProgressBarSetPosition(self.pbar, 1.0);
		GUI_LabelSetText(self.time_label, "Time left: 0m");
		GUI_LabelSetText(self.progress_percent_label, "Overall: 100.00%");
		GUI_LabelSetText(self.sectors_total_label, final_total);
		GUI_LabelSetText(self.sectors_processed_label, final_done);
		set_io_status("Finished", 0);
	}
	else if (cancelled) {
		GUI_LabelSetText(self.track_label, "Cancelled");
		set_io_status("Stopped", self.current_fad);
	}
	else if (destination_ready) {
		GUI_LabelSetText(self.track_label, "Paused - retry");
		set_io_status("Paused", self.current_fad);
	}
	else {
		GUI_LabelSetText(self.track_label, failure_label);
	}
	safe_cdrom_spin_down();
	self.start_time = 0;
	return NULL;
}

static void set_io_status(const char *operation, uint32_t fad) {
	char status_text[64];
	self.current_fad = fad;

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
	hnd = fs_open(path, O_WRONLY | O_CREAT);
	if (hnd == FILEHND_INVALID) {
		rip_log("Can't open bad-sector map %s", path);
		return CMD_ERROR;
	}

	if (fs_seek(hnd, 0, SEEK_END) < 0) {
		fs_close(hnd);
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

	fs_close(hnd);
	return CMD_OK;
}

static int checkpoint_track(file_t hnd, uint32_t tn, uint32_t written_sectors,
		uint32_t fad) {
	set_io_status("Sync", fad);
	rip_log("Checkpoint track %lu at track sector %lu (FAD %lu)",
		(unsigned long)tn, (unsigned long)written_sectors, (unsigned long)fad);
	return sync_track(hnd);
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
	int max_attempts = self.max_attempts;
	bool zero_fill = self.zero_fill;
	int size_status;

	get_sector_info(type, &secbyte);
	expected_bytes = (uint64_t)count * secbyte;
	size_status = get_existing_file_size(dst_file, &existing_bytes);
	if (size_status < 0) {
		rip_log("Can't determine the existing size of %s", dst_file);
		return CMD_ERROR;
	}

	if (existing_bytes > expected_bytes || existing_bytes % secbyte) {
		rip_log("Track %lu cannot resume: file size %llu is not an aligned partial of %llu bytes",
			(unsigned long)tn, (unsigned long long)existing_bytes,
			(unsigned long long)expected_bytes);
		return CMD_ERROR;
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

	if (safe_cdrom_set_sector_size(secbyte) != ERR_OK) {
		rip_log("Failed to select %lu-byte sector mode", (unsigned long)secbyte);
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
	}
	else {
		hnd = fs_open(dst_file, O_WRONLY);
	}

	if (hnd == FILEHND_INVALID) {
		rip_log("Can't open track file %s", dst_file);
		free(buffer);
		return CMD_ERROR;
	}

	if (resumed_sectors && fs_seek(hnd, (off_t)existing_bytes, SEEK_SET) != (off_t)existing_bytes) {
		rip_log("Can't seek %s to resume offset %llu", dst_file,
			(unsigned long long)existing_bytes);
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
		cdstat = timed_cdrom_read(buffer, first, nsects);

		if (cdstat == ERR_OK) {
			size_t bytes_to_write = nsects * secbyte;

			set_io_status("Write", first);
			if (fs_write(hnd, buffer, bytes_to_write) != (ssize_t)bytes_to_write) {
				rip_log("Write error in track %lu at FAD %lu",
					(unsigned long)tn, (unsigned long)first);
				checkpoint_track(hnd, tn, original_count - count, first);
				fs_close(hnd);
				free(buffer);
				return CMD_ERROR;
			}
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
				fs_close(hnd);
				free(buffer);
				return CMD_ERROR;
			}
			if (cdstat == ERR_TIMEOUT) {
				int reinit_rv;

				rip_log("Timed-out bulk read was aborted; reinitializing GD-ROM");
				reinit_rv = safe_cdrom_reinit();
				if (reinit_rv != ERR_OK || safe_cdrom_set_sector_size(secbyte) != ERR_OK) {
					rip_log("GD-ROM reinitialization failed with error %d", reinit_rv);
					fs_close(hnd);
					free(buffer);
					return CMD_ERROR;
				}
			}
			else if (safe_cdrom_set_sector_size(secbyte) != ERR_OK) {
				rip_log("Failed to restore %lu-byte sector mode", (unsigned long)secbyte);
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
					cdstat = timed_cdrom_read(buffer, fad, 1);
					if (cdstat == ERR_OK) {
						break;
					}

					rip_log("Read attempt %d/%d failed at track %lu sector %lu (LBA %lu, FAD %lu), error %d",
						attempt, max_attempts, (unsigned long)tn,
						(unsigned long)track_sector,
						(unsigned long)(fad >= 150 ? fad - 150 : fad),
						(unsigned long)fad, cdstat);

					if (cdstat == ERR_NO_DISC || cdstat == ERR_DISC_CHG) {
						checkpoint_track(hnd, tn, track_sector, fad);
						fs_close(hnd);
						free(buffer);
						return CMD_ERROR;
					}

					if (attempt < max_attempts && (cdstat == ERR_TIMEOUT || attempt == 3)) {
						rip_log("Reinitializing GD-ROM before retrying FAD %lu",
							(unsigned long)fad);
						reinit_rv = safe_cdrom_reinit();
						if (reinit_rv != ERR_OK || safe_cdrom_set_sector_size(secbyte) != ERR_OK) {
							rip_log("GD-ROM reinitialization failed with error %d", reinit_rv);
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
						rip_log("FAD %lu remains unreadable; pausing without writing a substitute sector",
							(unsigned long)fad);
						checkpoint_track(hnd, tn, track_sector, fad);
						fs_close(hnd);
						free(buffer);
						return CMD_ERROR;
					}

					memset(buffer, 0, secbyte);
					bad++;
					if (record_bad_sector(dst_file, tn, track_sector, fad, secbyte) != CMD_OK) {
						rip_log("Failed to record zero-filled FAD %lu", (unsigned long)fad);
					}
					rip_log("Zero-filled unreadable FAD %lu after %d attempts",
						(unsigned long)fad, max_attempts);
				}

				set_io_status("Write", fad);
				if (fs_write(hnd, buffer, secbyte) != (ssize_t)secbyte) {
					rip_log("Write error in recovered track %lu at FAD %lu",
						(unsigned long)tn, (unsigned long)fad);
					checkpoint_track(hnd, tn, track_sector, fad);
					fs_close(hnd);
					free(buffer);
					return CMD_ERROR;
				}

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
		self.processed_sectors += nsects;
		self.session_sectors += nsects;
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
		rip_log("Failed to close track %lu after syncing", (unsigned long)tn);
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

void gd_ripper_Exit()  {
	if (self.app && self.app->thd) {
		self.rip_active = 0;
		thd_join(self.app->thd, NULL);
		self.app->thd = NULL;
	}
	safe_cdrom_set_sector_size(2048);
	safe_cdrom_spin_down();
}

void gd_ripper_ShowFileBrowser(GUI_Widget *widget) {
	(void)widget;
	if (self.pages) {
		GUI_CardStackShowIndex(self.pages, 1);
	}
}

void gd_ripper_ShowMainPage(GUI_Widget *widget) {
	(void)widget;
	if (self.pages) {
		GUI_CardStackShowIndex(self.pages, 0);
	}
}

void gd_ripper_FileBrowserItemClick(dirent_fm_t *fm_ent) {
	if (!fm_ent) {
		return;
	}
	dirent_t *ent = &fm_ent->ent;
	GUI_FileManagerChangeDir(self.file_browser, ent->name, ent->size);
}

void gd_ripper_FileBrowserConfirm(GUI_Widget *widget) {
	(void)widget;

	if (self.file_browser && self.destination_path && self.pages) {
		const char *path = GUI_FileManagerGetPath(self.file_browser);
		if (path) {
			strncpy(self.selected_path, path, NAME_MAX - 1);
			self.selected_path[NAME_MAX - 1] = '\0';
			GUI_LabelSetText(self.destination_path, self.selected_path);
		}
		GUI_CardStackShowIndex(self.pages, 0);
	}
}
