/* On-console verification for DreamShell GD Ripper.
 *
 * Track files are read back from the destination, checked against rip.state,
 * and hashed with CRC32.  A compact, line-oriented database allows lookup on
 * a 16 MiB Dreamcast without loading a full Redump DAT into memory.
 */

#include "verify.h"
#include "checksum.h"
#include <zlib/zlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERIFY_STATE_HEADER "DreamShell GD Ripper state v1"
#define VERIFY_DB_HEADER "DREAMSHELL_REDUMP_CRC_V1"
#define VERIFY_MAX_TRACKS 99
#define VERIFY_BUFFER_SIZE (2352 * 16)
#define VERIFY_LINE_SIZE 512

typedef struct {
	uint32_t number;
	uint32_t start;
	uint32_t sector_count;
	uint32_t control;
	uint32_t sector_size;
	char filename[NAME_MAX + 1];
	uint64_t expected_size;
	uint64_t actual_size;
	uint32_t crc32;
} verify_track_t;

typedef struct {
	char name[192];
	uint32_t declared_tracks;
	uint32_t seen_tracks;
	uint32_t exact_tracks;
	uint32_t exact_data_tracks;
	uint32_t represented_data_tracks;
	bool listed[100];
} db_candidate_t;

typedef struct {
	gd_verify_result_t result;
	char name[192];
	uint32_t rank;
	uint32_t ties;
} db_match_t;

static void trim_line(char *line) {
	size_t length = strlen(line);

	while (length && (line[length - 1] == '\r' || line[length - 1] == '\n')) {
		line[--length] = '\0';
	}
}

static int get_file_size(const char *path, uint64_t *size) {
	file_t hnd = fs_open(path, O_RDONLY);
	ssize_t total;

	if (hnd == FILEHND_INVALID) {
		return CMD_ERROR;
	}
	total = fs_total(hnd);
	fs_close(hnd);
	if (total < 0) {
		return CMD_ERROR;
	}
	*size = (uint64_t)total;
	return CMD_OK;
}

static verify_track_t *find_track(verify_track_t *tracks, uint32_t count,
		uint32_t number) {
	for (uint32_t index = 0; index < count; index++) {
		if (tracks[index].number == number) {
			return &tracks[index];
		}
	}
	return NULL;
}

static int load_rip_state(const char *folder, verify_track_t *tracks,
		uint32_t *track_count, uint64_t *total_sectors) {
	char path[NAME_MAX];
	char line[NAME_MAX + 96];
	FILE *fp;
	unsigned long declared_tracks;
	int disc_type, use_bin;

	if (snprintf(path, sizeof(path), "%s/rip.state", folder) >= (int)sizeof(path)) {
		return CMD_ERROR;
	}
	fp = fopen(path, "r");
	if (!fp) {
		ds_printf("DS_ERROR: Verification requires %s\n", path);
		return CMD_ERROR;
	}

	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return CMD_ERROR;
	}
	trim_line(line);
	if (strcmp(line, VERIFY_STATE_HEADER) ||
		!fgets(line, sizeof(line), fp) || sscanf(line, "disc_type %d", &disc_type) != 1 ||
		!fgets(line, sizeof(line), fp) || sscanf(line, "use_bin %d", &use_bin) != 1 ||
		!fgets(line, sizeof(line), fp) || sscanf(line, "tracks %lu", &declared_tracks) != 1 ||
		declared_tracks == 0 || declared_tracks > VERIFY_MAX_TRACKS) {
		fclose(fp);
		ds_printf("DS_ERROR: Invalid verification state %s\n", path);
		return CMD_ERROR;
	}

	(void)disc_type;
	(void)use_bin;
	*total_sectors = 0;
	for (uint32_t index = 0; index < declared_tracks; index++) {
		unsigned long number, start, count, control, sector_size;

		if (!fgets(line, sizeof(line), fp) ||
			sscanf(line, "%lu %lu %lu %lu %lu %255s", &number, &start, &count,
				&control, &sector_size, tracks[index].filename) != 6 ||
			!number || number > 99 || !count || start > 1000000 || count > 1000000 ||
            (sector_size != 2048 && sector_size != 2352) ||
            strchr(tracks[index].filename, '/') || strchr(tracks[index].filename, '\\') ||
            strstr(tracks[index].filename, "..") ||
			find_track(tracks, index, (uint32_t)number)) {
			fclose(fp);
			ds_printf("DS_ERROR: Invalid track row in %s\n", path);
			return CMD_ERROR;
		}

		tracks[index].start = start;
		tracks[index].number = (uint32_t)number;
		tracks[index].sector_count = (uint32_t)count;
		tracks[index].control = (uint32_t)control;
		tracks[index].sector_size = (uint32_t)sector_size;
		tracks[index].expected_size = (uint64_t)count * sector_size;
		tracks[index].actual_size = 0;
		tracks[index].crc32 = 0;
		*total_sectors += count;
	}

	fclose(fp);
	*track_count = (uint32_t)declared_tracks;
	return CMD_OK;
}

static uint32_t count_bad_sectors(const char *folder, verify_track_t *tracks,
		uint32_t track_count) {
	uint32_t bad_count = 0;
	char path[NAME_MAX];
	char line[VERIFY_LINE_SIZE];

	for (uint32_t index = 0; index < track_count; index++) {
		FILE *fp;

		if (snprintf(path, sizeof(path), "%s/%s.bad", folder,
			tracks[index].filename) >= (int)sizeof(path)) {
			continue;
		}
		fp = fopen(path, "r");
		if (!fp) {
			continue;
		}
		while (fgets(line, sizeof(line), fp)) {
			char *cursor = line;
			while (isspace((unsigned char)*cursor)) cursor++;
			if (isdigit((unsigned char)*cursor)) {
				bad_count++;
			}
		}
		fclose(fp);
	}
	return bad_count;
}

static bool completion_marker_is_valid(const char *folder,
		uint64_t expected_sectors) {
	char path[NAME_MAX];
	char line[128];
	unsigned long long completed;
	FILE *fp;

	if (snprintf(path, sizeof(path), "%s/rip.complete", folder) >= (int)sizeof(path)) {
		return false;
	}
	fp = fopen(path, "r");
	if (!fp) {
		return false;
	}
	if (!fgets(line, sizeof(line), fp) ||
		sscanf(line, "Complete: %llu sectors", &completed) != 1) {
		fclose(fp);
		return false;
	}
	fclose(fp);
	return (uint64_t)completed == expected_sectors;
}

static int hash_track(const char *folder, verify_track_t *track, uint8_t *buffer,
		uint64_t *processed_bytes, uint64_t total_bytes, uint32_t track_index,
		uint32_t track_count, volatile int *active,
		gd_verify_progress_cb_t progress_cb, void *progress_data,
        bool scan, gd_verify_summary_t *summary) {
	char path[NAME_MAX];
	file_t hnd;
	uLong crc = crc32(0L, Z_NULL, 0);
	uint64_t track_bytes = 0;
	file_t suspects = FILEHND_INVALID;
	char suspect_path[NAME_MAX];

	if (snprintf(path, sizeof(path), "%s/%s", folder, track->filename) >=
			(int)sizeof(path)) {
		return CMD_ERROR;
	}
	hnd = fs_open(path, O_RDONLY);
	if (hnd == FILEHND_INVALID) {
		ds_printf("DS_ERROR: Can't open verification track %s\n", path);
		return CMD_ERROR;
	}

    if (scan && track->control == 4 && track->sector_size == 2352) {
        if (snprintf(suspect_path, sizeof(suspect_path), "%s.suspect", path) >=
                (int)sizeof(suspect_path)) {
            fs_close(hnd);
            return CMD_ERROR;
        }
        suspects = fs_open(suspect_path, O_WRONLY | O_CREAT | O_TRUNC);
        if (suspects == FILEHND_INVALID) {
            fs_close(hnd);
            return CMD_ERROR;
        }
    }

	while (true) {
		ssize_t bytes;

		if (!*active) {
			if (suspects != FILEHND_INVALID) fs_close(suspects);
			fs_close(hnd);
			return 1;
		}
		bytes = fs_read(hnd, buffer, VERIFY_BUFFER_SIZE);
		if (bytes < 0) {
			ds_printf("DS_ERROR: Read-back failed for %s\n", path);
			if (suspects != FILEHND_INVALID) fs_close(suspects);
			fs_close(hnd);
			return CMD_ERROR;
		}
		if (!bytes) {
			break;
		}
        if (suspects != FILEHND_INVALID) {
            if (bytes % 2352) {
                fs_close(suspects); fs_close(hnd); return CMD_ERROR;
            }
            for (ssize_t offset = 0; offset < bytes; offset += 2352) {
                uint32_t sector = (uint32_t)((track_bytes + offset) / 2352);
                unsigned flags = gd_check_sector(buffer + offset, track->start + sector);
                if (flags == GD_SECTOR_UNSUPPORTED) { summary->unsupported_sectors++; continue; }
                if (flags) {
                    char row[96];
                    int n = snprintf(row, sizeof(row), "%lu %lu %u\n",
                        (unsigned long)sector, (unsigned long)(track->start + sector), flags);
                    summary->suspect_sectors++;
                    if (fs_write(suspects, row, n) != n) {
                        fs_close(suspects); fs_close(hnd); return CMD_ERROR;
                    }
                }
            }
        }
        crc = crc32(crc, buffer, (uInt)bytes);
		track_bytes += (uint64_t)bytes;
		*processed_bytes += (uint64_t)bytes;
		if (progress_cb) {
			progress_cb(progress_data, track->filename, track_index, track_count,
				*processed_bytes, total_bytes);
		}
		thd_pass();
	}

	if (suspects != FILEHND_INVALID && fs_close(suspects) < 0) {
		fs_close(hnd);
        return CMD_ERROR;
    }
    fs_close(hnd);
	if (track_bytes != track->actual_size) {
		ds_printf("DS_ERROR: Verification read %llu of %llu bytes from %s\n",
			(unsigned long long)track_bytes,
			(unsigned long long)track->actual_size, path);
		return CMD_ERROR;
	}
	track->crc32 = (uint32_t)crc;
	return CMD_OK;
}

static gd_verify_result_t evaluate_candidate(const db_candidate_t *candidate,
		uint32_t local_tracks, uint32_t local_data_tracks, uint32_t *rank) {
	bool full_match = candidate->declared_tracks == local_tracks &&
		candidate->seen_tracks == candidate->declared_tracks &&
		candidate->exact_tracks == local_tracks;
	bool all_data_match = local_data_tracks > 0 &&
		candidate->represented_data_tracks == local_data_tracks &&
		candidate->exact_data_tracks == local_data_tracks;

	if (full_match) {
		*rank = 400000 + candidate->exact_tracks;
		return GD_VERIFY_FULL_MATCH;
	}
	if (all_data_match) {
		if (candidate->declared_tracks >= local_tracks) {
			*rank = 300000 + candidate->exact_tracks;
			return GD_VERIFY_DATA_MATCH;
		}
		*rank = 200000 + candidate->exact_data_tracks * 100 +
			candidate->exact_tracks;
		return GD_VERIFY_IDENTIFIED;
	}
	if (candidate->exact_data_tracks &&
		candidate->represented_data_tracks < local_data_tracks) {
		*rank = 200000 + candidate->exact_data_tracks * 100 +
			candidate->exact_tracks;
		return GD_VERIFY_IDENTIFIED;
	}
	if (candidate->exact_data_tracks) {
		*rank = 100000 + candidate->exact_data_tracks * 100 +
			candidate->exact_tracks;
		return GD_VERIFY_PARTIAL_MATCH;
	}

	*rank = 0;
	return GD_VERIFY_NO_MATCH;
}

static void finish_candidate(const db_candidate_t *candidate,
		uint32_t local_tracks, uint32_t local_data_tracks, db_match_t *best) {
	uint32_t rank;
	gd_verify_result_t result = evaluate_candidate(candidate, local_tracks,
		local_data_tracks, &rank);

	if (!rank) {
		return;
	}
	if (rank > best->rank) {
		best->rank = rank;
		best->result = result;
		best->ties = 0;
		strncpy(best->name, candidate->name, sizeof(best->name) - 1);
		best->name[sizeof(best->name) - 1] = '\0';
	}
	else if (rank == best->rank) {
		best->ties++;
	}
}

static gd_verify_result_t search_database(const char *database_path,
		verify_track_t *tracks, uint32_t track_count, char *game_name,
		size_t game_name_size, volatile int *active) {
	char line[VERIFY_LINE_SIZE];
	FILE *fp = fopen(database_path, "r");
	db_candidate_t candidate;
	db_match_t best;
	uint32_t local_data_tracks = 0;
	bool in_game = false;

	if (!fp) {
		return GD_VERIFY_NO_DATABASE;
	}
	if (!fgets(line, sizeof(line), fp)) {
		fclose(fp);
		return GD_VERIFY_ERROR;
	}
	trim_line(line);
	if (strcmp(line, VERIFY_DB_HEADER)) {
		ds_printf("DS_ERROR: Unknown Redump database format: %s\n", database_path);
		fclose(fp);
		return GD_VERIFY_ERROR;
	}

	for (uint32_t index = 0; index < track_count; index++) {
		if (tracks[index].control == 4) {
			local_data_tracks++;
		}
	}
	memset(&candidate, 0, sizeof(candidate));
	memset(&best, 0, sizeof(best));
	best.result = GD_VERIFY_NO_MATCH;

	while (fgets(line, sizeof(line), fp)) {
		if (!*active) {
			fclose(fp);
			return GD_VERIFY_CANCELLED;
		}
		trim_line(line);
		if (!line[0] || line[0] == '#') {
			continue;
		}
		if (!strncmp(line, "G\t", 2)) {
			char *end;
			unsigned long declared;

			if (in_game) {
				finish_candidate(&candidate, track_count, local_data_tracks, &best);
			}
			memset(&candidate, 0, sizeof(candidate));
			declared = strtoul(line + 2, &end, 10);
			if (!declared || *end != '\t') {
				in_game = false;
				continue;
			}
			candidate.declared_tracks = (uint32_t)declared;
			strncpy(candidate.name, end + 1, sizeof(candidate.name) - 1);
			candidate.name[sizeof(candidate.name) - 1] = '\0';
			in_game = true;
		}
		else if (!strncmp(line, "T\t", 2) && in_game) {
			unsigned long number, crc;
			unsigned long long size;
			verify_track_t *track;

			if (sscanf(line, "T\t%lu\t%llu\t%lx", &number, &size, &crc) != 3) {
				continue;
			}
            if (!number || number > 99 || crc > UINT32_MAX || candidate.listed[number]) {
                in_game = false;
                continue;
            }
            candidate.listed[number] = true;
            candidate.seen_tracks++;
            track = find_track(tracks, track_count, (uint32_t)number);
			if (!track) {
				continue;
			}
			if (track->control == 4) {
				candidate.represented_data_tracks++;
			}
			if (track->actual_size == (uint64_t)size && track->crc32 == (uint32_t)crc) {
				candidate.exact_tracks++;
				if (track->control == 4) {
					candidate.exact_data_tracks++;
				}
			}
		}
		else if (!strcmp(line, "E") && in_game) {
			finish_candidate(&candidate, track_count, local_data_tracks, &best);
			in_game = false;
		}
	}
	if (in_game) {
		finish_candidate(&candidate, track_count, local_data_tracks, &best);
	}
	fclose(fp);

	if (best.rank) {
		strncpy(game_name, best.name, game_name_size - 1);
		game_name[game_name_size - 1] = '\0';
	}
	return best.result;
}

const char *gd_verify_result_text(gd_verify_result_t result) {
	switch (result) {
		case GD_VERIFY_CANCELLED: return "CANCELLED";
		case GD_VERIFY_NO_DATABASE: return "HASHED - NO DATABASE";
		case GD_VERIFY_INCOMPATIBLE: return "INCOMPATIBLE DATA-TRACK FORMAT";
		case GD_VERIFY_NO_MATCH: return "NO DATA-TRACK HASH MATCH";
		case GD_VERIFY_PARTIAL_MATCH: return "PARTIAL MATCH ONLY";
		case GD_VERIFY_IDENTIFIED: return "IDENTIFIED BY DATA TRACK";
		case GD_VERIFY_DATA_MATCH: return "DATA TRACKS MATCH";
		case GD_VERIFY_FULL_MATCH: return "FULL TRACK MATCH";
		case GD_VERIFY_INTEGRITY_FAILED: return "DUMP INTEGRITY FAILED";
		default: return "VERIFICATION ERROR";
	}
}

static int report_printf(file_t hnd, const char *format, ...) {
	char line[VERIFY_LINE_SIZE];
	va_list args;
	int length;

	va_start(args, format);
	length = vsnprintf(line, sizeof(line), format, args);
	va_end(args);
	if (length < 0) {
		return CMD_ERROR;
	}
	if (length >= (int)sizeof(line)) {
		length = sizeof(line) - 1;
	}
	return fs_write(hnd, line, length) == length ? CMD_OK : CMD_ERROR;
}

static int write_report(const char *folder, const char *database_path,
		verify_track_t *tracks, gd_verify_summary_t *summary, bool sync_report) {
	file_t hnd;
	ssize_t completed = 0;
	int status = CMD_OK;

	if (snprintf(summary->report_path, sizeof(summary->report_path),
			"%s/verify.log", folder) >= (int)sizeof(summary->report_path)) {
		return CMD_ERROR;
	}
	hnd = fs_open(summary->report_path, O_WRONLY | O_TRUNC | O_CREAT);
	if (hnd == FILEHND_INVALID) {
		return CMD_ERROR;
	}

	status |= report_printf(hnd, "DreamShell GD verification v2\n");
	status |= report_printf(hnd, "result %s\n", gd_verify_result_text(summary->result));
	status |= report_printf(hnd, "catalog_result %s\n",
		gd_verify_result_text(summary->catalog_result));
	status |= report_printf(hnd, "clean %d\n", summary->clean);
    status |= report_printf(hnd, "hash_origin %s\n", summary->streaming ?
        "disc stream / saved checkpoint (no storage read-back)" : "storage read-back");
    status |= report_printf(hnd, "catalog %s\n", summary->catalog);
    status |= report_printf(hnd, "suspect_sectors %lu\nunsupported_sectors %lu\n",
        (unsigned long)summary->suspect_sectors, (unsigned long)summary->unsupported_sectors);
    if (summary->catalog_result == GD_VERIFY_NO_MATCH) {
        status |= report_printf(hnd, "note No match is inconclusive: revision, catalog coverage, track boundaries, or read errors. Whole-track CRC cannot locate bad sectors. Use the sector scan or compare independent dumps.\n");
    }
	status |= report_printf(hnd, "bad_sectors %lu\n",
		(unsigned long)summary->bad_sector_count);
	status |= report_printf(hnd, "database %s\n", database_path);
	if (summary->game_name[0]) {
		status |= report_printf(hnd, "game %s\n", summary->game_name);
	}
	status |= report_printf(hnd, "tracks %lu\n", (unsigned long)summary->track_count);
	for (uint32_t index = 0; index < summary->track_count; index++) {
		status |= report_printf(hnd, "%lu %s %llu %08lx %s\n",
			(unsigned long)tracks[index].number, tracks[index].filename,
			(unsigned long long)tracks[index].actual_size,
			(unsigned long)tracks[index].crc32,
			tracks[index].actual_size == tracks[index].expected_size ? "size_ok" : "SIZE_BAD");
	}
	status |= report_printf(hnd,
		"note CRC/size comparison covers dumped track files, not lead-in, lead-out, subchannels, or drive offset.\n");

	if (sync_report && fs_complete(hnd, &completed) != 0) {
		status = CMD_ERROR;
	}
	if (fs_close(hnd) != 0) {
		status = CMD_ERROR;
	}
	return status == CMD_OK ? CMD_OK : CMD_ERROR;
}

gd_verify_result_t gd_verify_dump_ex(const char *folder, const char *database_path,
		bool sync_report, volatile int *active, gd_verify_progress_cb_t progress_cb,
		void *progress_data, gd_verify_summary_t *summary, bool streaming, bool scan) {
	verify_track_t *tracks = NULL;
	uint8_t *buffer = NULL;
	uint32_t track_count = 0;
	uint64_t total_sectors = 0;
	uint64_t total_bytes = 0;
	uint64_t processed_bytes = 0;
	bool sizes_ok = true;
	bool compatible = true;
	gd_verify_result_t catalog_result;

	memset(summary, 0, sizeof(*summary));
	summary->streaming = streaming;
	summary->result = GD_VERIFY_ERROR;
	summary->catalog_result = GD_VERIFY_ERROR;
	tracks = calloc(VERIFY_MAX_TRACKS, sizeof(*tracks));
	if (!tracks) {
		return summary->result;
	}

	if (load_rip_state(folder, tracks, &track_count, &total_sectors) != CMD_OK) {
		free(tracks);
		return summary->result;
	}
	summary->track_count = track_count;
	summary->bad_sector_count = count_bad_sectors(folder, tracks, track_count);
	summary->clean = completion_marker_is_valid(folder, total_sectors) &&
		summary->bad_sector_count == 0;

	for (uint32_t index = 0; index < track_count; index++) {
		char path[NAME_MAX];

		if (snprintf(path, sizeof(path), "%s/%s", folder, tracks[index].filename) >=
				(int)sizeof(path) || get_file_size(path, &tracks[index].actual_size) != CMD_OK) {
			ds_printf("DS_ERROR: Missing verification track %s\n", tracks[index].filename);
			summary->clean = false;
			sizes_ok = false;
			continue;
		}
		total_bytes += tracks[index].actual_size;
		if (tracks[index].actual_size != tracks[index].expected_size) {
			sizes_ok = false;
			summary->clean = false;
		}
		if (tracks[index].control == 4 && tracks[index].sector_size != 2352) {
			compatible = false;
		}
	}
	if (!sizes_ok) {
		summary->result = GD_VERIFY_INTEGRITY_FAILED;
		summary->catalog_result = GD_VERIFY_ERROR;
		summary->report_written = write_report(folder, database_path, tracks,
			summary, sync_report) == CMD_OK;
		free(tracks);
		return summary->result;
	}

	buffer = (uint8_t *)memalign(32, VERIFY_BUFFER_SIZE);
	if (!buffer) {
		free(tracks);
		return summary->result;
	}
	for (uint32_t index = 0; index < track_count; index++) {
		int hash_status;
        if (streaming) {
            char path[NAME_MAX];
            uint64_t saved_bytes = 0;
            verify_track_t *t = &tracks[index];
            hash_status = CMD_ERROR;
            if (snprintf(path, sizeof(path), "%s/%s", folder, t->filename) < (int)sizeof(path) &&
                gd_crc_restore(path, gd_crc_tag(t->number, t->start, t->sector_count,
                    t->sector_size), t->actual_size, t->sector_size, &saved_bytes, &t->crc32) &&
                    saved_bytes == t->actual_size) hash_status = CMD_OK;
        } else {
            hash_status = hash_track(folder, &tracks[index], buffer, &processed_bytes,
                total_bytes, index + 1, track_count, active, progress_cb, progress_data,
                scan, summary);
        }
        if (hash_status != CMD_OK) {
            summary->result = hash_status == 1 ? GD_VERIFY_CANCELLED : GD_VERIFY_ERROR;
            summary->report_written = write_report(folder, database_path, tracks, summary, sync_report) == CMD_OK;
            free(buffer); free(tracks);
            return summary->result;
        }
	}
	free(buffer);

	if (!compatible) {
		catalog_result = GD_VERIFY_INCOMPATIBLE;
	}
	else {
		char tosec_path[NAME_MAX], tosec_name[192] = {0};
        gd_verify_result_t tosec_result = GD_VERIFY_NO_DATABASE;
        snprintf(summary->catalog, sizeof(summary->catalog), "Redump");
        catalog_result = search_database(database_path, tracks, track_count,
            summary->game_name, sizeof(summary->game_name), active);
        if (snprintf(tosec_path, sizeof(tosec_path), "%s", database_path) < (int)sizeof(tosec_path)) {
            char *slash = strrchr(tosec_path, '/');
            if (slash && (size_t)(slash - tosec_path) + sizeof("/tosec.db") <= sizeof(tosec_path)) {
                strcpy(slash, "/tosec.db");
                tosec_result = search_database(tosec_path, tracks, track_count,
                    tosec_name, sizeof(tosec_name), active);
            }
        }
        if (tosec_result == GD_VERIFY_CANCELLED) catalog_result = tosec_result;
        else if ((catalog_result == GD_VERIFY_NO_DATABASE || catalog_result == GD_VERIFY_ERROR) &&
                tosec_result == GD_VERIFY_NO_MATCH) {
            catalog_result = tosec_result;
            snprintf(summary->catalog, sizeof(summary->catalog), "TOSEC");
        }
        else if (tosec_result >= GD_VERIFY_PARTIAL_MATCH && tosec_result <= GD_VERIFY_FULL_MATCH &&
                (catalog_result < tosec_result || catalog_result > GD_VERIFY_FULL_MATCH)) {
            catalog_result = tosec_result;
            snprintf(summary->game_name, sizeof(summary->game_name), "%s", tosec_name);
            snprintf(summary->catalog, sizeof(summary->catalog), "TOSEC");
        }
	}
	if (catalog_result == GD_VERIFY_CANCELLED) {
		summary->result = GD_VERIFY_CANCELLED;
		free(tracks);
		return summary->result;
	}
	summary->catalog_result = catalog_result;
	if (summary->suspect_sectors) summary->clean = false;
	summary->result = summary->clean ? catalog_result : GD_VERIFY_INTEGRITY_FAILED;
	summary->report_written = write_report(folder, database_path, tracks, summary,
		sync_report) == CMD_OK;

	ds_printf("DS_INFO: GD verification result: %s\n",
		gd_verify_result_text(summary->result));
	if (summary->game_name[0]) {
		ds_printf("DS_INFO: GD verification candidate: %s\n", summary->game_name);
	}
	free(tracks);
	return summary->result;
}

/* Compatibility entry point: a requested manual verify reads storage. */
gd_verify_result_t gd_verify_dump(const char *folder, const char *database_path,
        bool sync_report, volatile int *active, gd_verify_progress_cb_t progress_cb,
        void *progress_data, gd_verify_summary_t *summary) {
    return gd_verify_dump_ex(folder, database_path, sync_report, active,
        progress_cb, progress_data, summary, false, true);
}
