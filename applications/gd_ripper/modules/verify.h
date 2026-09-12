#ifndef GD_RIPPER_VERIFY_H
#define GD_RIPPER_VERIFY_H

#include "ds.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
	GD_VERIFY_ERROR = -1,
	GD_VERIFY_CANCELLED = 0,
	GD_VERIFY_NO_DATABASE,
	GD_VERIFY_INCOMPATIBLE,
	GD_VERIFY_NO_MATCH,
	GD_VERIFY_PARTIAL_MATCH,
	GD_VERIFY_IDENTIFIED,
	GD_VERIFY_DATA_MATCH,
	GD_VERIFY_FULL_MATCH,
	GD_VERIFY_INTEGRITY_FAILED
} gd_verify_result_t;

typedef struct {
	gd_verify_result_t result;
	gd_verify_result_t catalog_result;
	bool clean;
	bool streaming;
	uint32_t suspect_sectors;
	uint32_t unsupported_sectors;
	char catalog[32];
	bool report_written;
	uint32_t track_count;
	uint32_t bad_sector_count;
	char game_name[192];
	char report_path[NAME_MAX];
} gd_verify_summary_t;

typedef void (*gd_verify_progress_cb_t)(void *data, const char *filename,
	uint32_t track_index, uint32_t track_count, uint64_t processed_bytes,
	uint64_t total_bytes);

gd_verify_result_t gd_verify_dump(const char *folder, const char *database_path,
	bool sync_report, volatile int *active, gd_verify_progress_cb_t progress_cb,
	void *progress_data, gd_verify_summary_t *summary);
gd_verify_result_t gd_verify_dump_ex(const char *folder, const char *database_path,
    bool sync_report, volatile int *active, gd_verify_progress_cb_t progress_cb,
    void *progress_data, gd_verify_summary_t *summary, bool streaming, bool scan);
const char *gd_verify_result_text(gd_verify_result_t result);

#endif
