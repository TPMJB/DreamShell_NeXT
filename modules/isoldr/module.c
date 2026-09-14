/* DreamShell ##version##

   DreamShell ISO Loader module
   Copyright (C)2009-2026 SWAT

*/

#include "ds.h"
#include "isoldr.h"
#include "fs.h"
#include "naomi/cart.h"

#define KOS_HDR_OFFSET 1
static uint8 kos_hdr[5] = {0xD0, 0x02, 0x01, 0x12, 0x20};
static uint8 ron_hdr[8] = {0x1B, 0xD0, 0x1A, 0xD1, 0x1B, 0x20, 0x2B, 0x40};
static uint8 win_hdr[4] = {0x45, 0x43, 0x45, 0x43};

void isoldr_exec_at(const void *image, uint32 length, uint32 address, uint32 params_len);
void isoldr_vm2_bank_switch(const char *ipbin_info_sec);
void isoldr_unmount_all_presets_romdisks(void);
int builtin_isoldr_cmd(int argc, char *argv[]);

#include <isoldr/check.h>
#include <isofs/gdi_parse.h>
#include <stdarg.h>

static char isoldr_last_error[256];

const char *isoldr_get_last_error(void) {
    return isoldr_last_error[0] ? isoldr_last_error : "Launch failed. Open the console for details.";
}

void isoldr_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(isoldr_last_error, sizeof(isoldr_last_error), fmt, args);
    va_end(args);
    ds_printf("DS_ERROR: %s", isoldr_last_error);
}

DEFAULT_MODULE_HEADER(isoldr);

void isoldr_naomi_eeprom_prepare(isoldr_info_t *info);
int isoldr_elf_load(const char *path, uint32_t dest,
	uint8_t **out_data, size_t *out_size);

int lib_open(klibrary_t *lib) {
	AddCmd(lib_get_name(), "ISO Loader command line", (CmdHandler *)builtin_isoldr_cmd);
	return nmmgr_handler_add(&ds_isoldr_hnd.nmmgr);
}

int lib_close(klibrary_t *lib) {
	isoldr_unmount_all_presets_romdisks();
	RemoveCmd(GetCmdByName(lib_get_name()));
	return nmmgr_handler_remove(&ds_isoldr_hnd.nmmgr);
}

static void get_ipbin_info(isoldr_info_t *info, file_t fd, uint8 *sec, char *psec) {

	uint32 len;

	if(fs_ioctl(fd, ISOFS_IOCTL_GET_BOOT_SECTOR_DATA, sec) < 0) {

		isoldr_error("Can't get boot sector data\n");

	} else {

//		kos_md5(sec, sizeof(sec), info->md5);

		if(sec[0x60] != 0 && sec[0x60] != 0x20) {

			strncpy(info->exec.file, psec + 0x60, sizeof(info->exec.file) - 1);
			info->exec.file[sizeof(info->exec.file) - 1] = '\0';

			for(len = 0; len < sizeof(info->exec.file); len++) {
				if(info->exec.file[len] == 0x20) {
					info->exec.file[len] = '\0';
					break;
				}
			}

		} else {
			info->exec.file[0] = 0;
		}

		isoldr_vm2_bank_switch(psec);
	}
}


static int is_homebrew(file_t fd) {

	uint8 src[sizeof(kos_hdr) + KOS_HDR_OFFSET];

	fs_seek(fd, 0, SEEK_SET);
	fs_read(fd, src, sizeof(src));
	fs_seek(fd, 0, SEEK_SET);

	/* Check for unscrambled homebrew */
	if(!memcmp(src + KOS_HDR_OFFSET, kos_hdr, sizeof(kos_hdr))
		|| !memcmp(src, ron_hdr, sizeof(ron_hdr))
	) {
		return 1;
	}

	/* TODO: Check for scrambled homebrew */
	return 0;
}

static int is_wince_rom(file_t fd) {

	uint8 src[sizeof(win_hdr)];

	fs_seek(fd, 64, SEEK_SET);
	fs_read(fd, src, sizeof(win_hdr));
	fs_seek(fd, 0, SEEK_SET);

	return !memcmp(src, win_hdr, sizeof(win_hdr));
}

static int get_executable_info(isoldr_info_t *info, file_t fd) {

	if(!strncasecmp(info->exec.file, "0WINCEOS.BIN", 12)) {

		info->exec.type = BIN_TYPE_WINCE;
		info->exec.lba++;
		info->exec.size -= 2048;

	} else if(is_wince_rom(fd)) {

		info->exec.type = BIN_TYPE_WINCE;

	} else if(is_homebrew(fd)) {

		info->exec.type = BIN_TYPE_KOS;

	} else {
		// By default is KATANA
		// FIXME: detect KATANA and set scrambled homebrew by default
		info->exec.type = BIN_TYPE_KATANA;
	}

	info->exec.addr = 0xac010000;
	return 0;
}

int isoldr_naomi_read_header(file_t fd, naomi_cart_header_t *hdr) {
	uint32_t addr;
	uint8_t peek[16];

	if(!hdr || fd == FILEHND_INVALID) {
		return -1;
	}

	for(addr = 0; addr < NAOMI_CART_HDR_SCAN_MAX; addr += NAOMI_CART_PROBE_STEP) {
		if(fs_seek64(fd, addr, SEEK_SET) < 0) {
			return -1;
		}
		if(fs_read(fd, peek, sizeof(peek)) != (ssize_t)sizeof(peek)) {
			return -1;
		}
		if((peek[0] != 'N' && peek[0] != 'S') || peek[15] != ' ') {
			continue;
		}
		if(fs_seek64(fd, addr, SEEK_SET) < 0) {
			return -1;
		}
		if(fs_read(fd, hdr, sizeof(*hdr)) != (ssize_t)sizeof(*hdr)) {
			return -1;
		}
		if(naomi_cart_valid(hdr)) {
			return 0;
		}
	}
	return -1;
}

static int get_naomi_rom_info(isoldr_info_t *info, file_t fd, const char *rom_file, int test_mode) {
	naomi_cart_header_t cart_hdr;
	char *pbuf = NULL;

	if(isoldr_naomi_read_header(fd, &cart_hdr) < 0) {
		isoldr_error("Invalid NAOMI ROM header\n");
		return -1;
	}

	info->image_type = IMAGE_TYPE_ROM_NAOMI;
	pbuf = strchr(rom_file + 1, '/');

	if(pbuf == NULL) {
		return -1;
	}

	int len = strlen(pbuf);

	if(len >= NAME_MAX) {
		len = NAME_MAX - 1;
	}

	strncpy(info->image_file, pbuf, len);
	info->image_file[len] = '\0';
	info->track_lba[0] = 0;
	info->track_lba[1] = 0;
	info->sector_size = 4;

	if (test_mode) {
		info->exec.addr = cart_hdr.test_execute_adr;
		info->exec.size = cart_hdr.test_exe[0].size;
		info->exec.lba = cart_hdr.test_exe[0].offset / info->sector_size;
	}
	else {
		info->exec.addr = cart_hdr.game_execute_adr;
		info->exec.size = cart_hdr.game_exe[0].size;
		info->exec.lba = cart_hdr.game_exe[0].offset / info->sector_size;
	}
	info->exec.type = BIN_TYPE_NAOMI;
	strncpy(info->exec.file, "NAOMI.BIN", 9);
	info->exec.file[9] = '\0';

	return 0;
}

/* Validate every referenced track before ISOFS can silently use a partial set.
 * The standalone reader reconstructs trackNN names, so reject unsupported names
 * and offsets with a useful error instead of accepting a mount-only success. */
static int isoldr_check_gdi(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if(!ext || strcasecmp(ext, ".gdi")) return 0;
    file_t fd = fs_open(filename, O_RDONLY);
    if(fd == FILEHND_INVALID) { isoldr_error("Cannot open GDI descriptor.\n"); return -1; }
    size_t size = fs_total(fd);
    char *data = size && size <= 32768 ? malloc(size + 1) : NULL;
    if(!data) { fs_close(fd); isoldr_error("GDI descriptor is too large or memory is unavailable.\n"); return -1; }
    if(fs_read(fd, data, size) != (ssize_t)size) {
        fs_close(fd); free(data); isoldr_error("Incomplete GDI descriptor read.\n"); return -1;
    }
    fs_close(fd); data[size] = 0;
    int rc = -1, count;
    char *line = data, *next = strchr(line, '\n');
    if(next) *next++ = 0;
    count = ds_gdi_count(line);
    if(count < 1) { isoldr_error("Invalid GDI track count.\n"); goto done; }
    uint32_t last_lba = 0;
    for(int i = 1; i <= count; ++i) {
        ds_gdi_track track;
        line = next;
        if(!line) { isoldr_error("GDI descriptor has missing track entries.\n"); goto done; }
        next = strchr(line, '\n');
        if(next) *next++ = 0;
        if(!ds_gdi_parse(line, &track) || track.number != (uint32_t)i ||
           (i > 1 && track.lba <= last_lba)) {
            isoldr_error("Invalid GDI entry for track %d.\n", i); goto done;
        }
        last_lba = track.lba;
        char expected[32], path[NAME_MAX];
        const char *suffix = strrchr(track.name, '.');
        snprintf(expected, sizeof(expected), "track%02d%s", i, suffix ? suffix : "");
        if(strcmp(expected, track.name) || !suffix || track.offset ||
           (track.flags == 4 && strcasecmp(suffix, track.sector_size == 2048 ? ".iso" : ".bin")) ||
           (track.flags == 0 && strcasecmp(suffix, ".raw") && strcasecmp(suffix, ".wav"))) {
            isoldr_error("Track %d needs standard trackNN.iso/bin/raw names and offset 0 for this loader.\n", i); goto done;
        }
        const char *slash = strrchr(filename, '/');
        int prefix = slash ? (int)(slash - filename + 1) : 0;
        if(snprintf(path, sizeof(path), "%.*s%s", prefix, filename, track.name) >= sizeof(path)) {
            isoldr_error("GDI track path is too long.\n"); goto done;
        }
        fd = fs_open(path, O_RDONLY);
        if(fd == FILEHND_INVALID) { isoldr_error("Missing GDI track: %s\n", track.name); goto done; }
        size_t bytes = fs_total(fd);
        fs_close(fd);
        if(!bytes || (strcasecmp(suffix, ".wav") && bytes % track.sector_size)) {
            isoldr_error("GDI track is empty or incomplete: %s\n", track.name); goto done;
        }
    }
    if(next) { const char *tail = next; ds_gdi_space(&tail); if(*tail) { isoldr_error("Unexpected extra GDI entries.\n"); goto done; } }
    rc = 0;
done:
    free(data); return rc;
}

static int get_image_info(isoldr_info_t *info, const char *iso_file) {
    if(isoldr_check_gdi(iso_file) < 0) return -1;


	file_t fd;
	char fn[NAME_MAX];
	char mount[8] = "/isoldr";
	uint8 sec[2048];
	char *psec = (char *)sec;
	mount[7] = '\0';
	int len = 0;

	info->track_lba[0] = 150;
	info->track_lba[1] = info->track_lba[0];
	info->sector_size = 2048;

	fd = fs_open(mount, O_DIR | O_RDONLY);

	if(fd != FILEHND_INVALID) {
		fs_close(fd);
		if(fs_iso_unmount(mount) < 0) {
			isoldr_error("Can't unmount %s\n", mount);
			return -1;
		}
	}

	if(fs_iso_mount(mount, iso_file) < 0) {
		isoldr_error("Can't mount %s to %s\n", iso_file, mount);
		return -1;
	}

	fd = fs_iso_first_file(mount);

	if(fd != FILEHND_INVALID) {
		get_ipbin_info(info, fd, sec, psec);
		fs_close(fd);
	}

	memset(&sec, 0, sizeof(sec));

	if(info->exec.file[0] == 0) {
		strncpy(info->exec.file, "1ST_READ.BIN", 12);
		info->exec.file[12] = '\0';
	}

	snprintf(fn, NAME_MAX, "%s/%s", mount, info->exec.file);
	fd = fs_open(fn, O_RDONLY);

	if(fd == FILEHND_INVALID) {
		isoldr_error("Can't open %s\n", fn);
		goto image_error;
	}

	if(fs_ioctl(fd, ISOFS_IOCTL_GET_FD_LBA, &info->exec.lba) < 0 ||
       fs_ioctl(fd, ISOFS_IOCTL_GET_IMAGE_TYPE, &info->image_type) < 0 ||
       fs_ioctl(fd, ISOFS_IOCTL_GET_DATA_TRACK_LBA, &info->track_lba[0]) < 0 ||
       fs_ioctl(fd, ISOFS_IOCTL_GET_DATA_TRACK_SECTOR_SIZE, &info->sector_size) < 0 ||
       fs_ioctl(fd, ISOFS_IOCTL_GET_TOC_DATA, &info->toc) < 0) {
        isoldr_error("Cannot read image metadata. Check the descriptor and tracks.\n");
        goto image_error;
    }

	if(info->image_type == ISOFS_IMAGE_TYPE_CDI) {

		uint32 *offset = (uint32 *)sec;
		fs_ioctl(fd, ISOFS_IOCTL_GET_CDDA_OFFSET, offset);
		memcpy(&info->cdda_offset, offset, sizeof(info->cdda_offset));
		memset(&sec, 0, sizeof(sec));

		fs_ioctl(fd, ISOFS_IOCTL_GET_DATA_TRACK_OFFSET, &info->track_offset);
	}

	if(info->image_type == ISOFS_IMAGE_TYPE_CSO ||
	        info->image_type == ISOFS_IMAGE_TYPE_ZSO) {

		uint32 ptr = 0;

		if(!fs_ioctl(fd, ISOFS_IOCTL_GET_IMAGE_HEADER_PTR, &ptr) && ptr != 0) {
			memcpy(&info->ciso, (void*)ptr, sizeof(CISO_header_t));
		}
	}

	if(info->image_type == ISOFS_IMAGE_TYPE_GDI) {

		fs_ioctl(fd, ISOFS_IOCTL_GET_DATA_TRACK_FILENAME, sec);
		fs_ioctl(fd, ISOFS_IOCTL_GET_DATA_TRACK_FILENAME2, info->image_second);
		fs_ioctl(fd, ISOFS_IOCTL_GET_DATA_TRACK_LBA2, &info->track_lba[1]);

		if(info->track_lba[1] == 50150 &&
			!strcasecmp(info->image_second, "track04.bin")) {
			info->bleem = 1;
		}

		psec = strchr(psec + 1, '/');

	} else {
		psec = strchr(iso_file + 1, '/');
	}

	if(psec == NULL) {
		goto image_error;
	}

	len = strlen(psec);

	if(len > NAME_MAX) {
		len = NAME_MAX - 1;
	}

	strncpy(info->image_file, psec, len);
	info->image_file[len] = '\0';

	info->exec.lba += 150;
	info->exec.size = fs_total(fd);

	if(get_executable_info(info, fd) < 0) {
		isoldr_error("Can't get executable info\n");
		goto image_error;
	}

	fs_close(fd);
	fs_iso_unmount(mount);
	return 0;

image_error:

	if(fd != FILEHND_INVALID) {
		fs_close(fd);
	}
	fs_iso_unmount(mount);
	return -1;
}


static int get_device_info(isoldr_info_t *info, const char *iso_file) {

	if(!strncasecmp(iso_file, "/pc/", 4)) {

		strncpy(info->fs_dev, ISOLDR_DEV_DCLOAD, 3);
		info->fs_dev[3] = '\0';

	} else if(!strncasecmp(iso_file, "/cd/", 4)) {

		strncpy(info->fs_dev, ISOLDR_DEV_GDROM, 2);
		info->fs_dev[2] = '\0';

	} else if(!strncasecmp(iso_file, "/sd", 3)) {

		strncpy(info->fs_dev, ISOLDR_DEV_SDCARD, 2);
		info->fs_dev[2] = '\0';

		if(iso_file[3] != '/') {
			info->fs_part = (iso_file[3] - '0');
		}

	} else if(!strncasecmp(iso_file, "/ide", 4)) {

		strncpy(info->fs_dev, ISOLDR_DEV_G1ATA, 3);
		info->fs_dev[3] = '\0';

		if(iso_file[4] != '/') {
			info->fs_part = (iso_file[4] - '0');
		}

	} else {
		isoldr_error("isoldr doesn't support this device\n");
		return -1;
	}

	info->fs_type[0] = '\0';
	return 0;
}


isoldr_info_t *isoldr_get_info(const char *file, int test_mode) {

	isoldr_last_error[0] = '\0';

	isoldr_info_t *info = NULL;
	file_t fd;
	const char *ext = NULL;

	fd = fs_open(file, O_RDONLY);

	if(fd == FILEHND_INVALID) {
		isoldr_error("Cannot open image: %s\n", file);
		goto error;
	}

	info = (isoldr_info_t *) malloc(sizeof(*info));

	if(info == NULL) {
		isoldr_error("No free memory\n");
		fs_close(fd);
		goto error;
	}

	memset(info, 0, sizeof(*info));
	ext = strrchr(file, '.');

	if(ext != NULL && !strcasecmp(ext, ".dni")) {
		if(get_naomi_rom_info(info, fd, file, test_mode) < 0) {
			fs_close(fd);
			goto error;
		}
		fs_close(fd);
	}
	else {
		fs_close(fd);

		if(get_image_info(info, file) < 0) {
			goto error;
		}
	}

	if(get_device_info(info, file) < 0) {
		goto error;
	}

	// Keep interface version 0.6.x up to 0.8.x loaders
	if (VER_MAJOR == 0 && VER_MINOR <= 8 && VER_MINOR >= 6) {
		snprintf(info->magic, 12, "DSISOLDR%d%d%d", VER_MAJOR, 6, VER_MICRO);
	} else {
		snprintf(info->magic, 12, "DSISOLDR%d%d%d", VER_MAJOR, VER_MINOR, VER_MICRO);
	}
	info->magic[11] = '\0';

	return info;

error:

	if(info)
		free(info);

	return NULL;
}


int isoldr_set_boot_file(isoldr_info_t *info, const char *iso_file, const char *boot_file) {
    char fn[NAME_MAX];
    const char *mount = "/isoldr";
    file_t fd = FILEHND_INVALID;
    int result = -1;
    if(!info || !boot_file || strlen(boot_file) >= sizeof(info->exec.file))
        return -1;
    if(fs_iso_mount(mount, iso_file) < 0) {
        isoldr_error("Cannot mount image for alternate executable.\n");
        return -1;
    }
    snprintf(fn, sizeof(fn), "%s/%s", mount, boot_file);
    fd = fs_open(fn, O_RDONLY);
    if(fd == FILEHND_INVALID ||
       fs_ioctl(fd, ISOFS_IOCTL_GET_FD_LBA, &info->exec.lba) < 0) {
        isoldr_error("Cannot open alternate executable: %s\n", boot_file);
        goto done;
    }
    info->exec.lba += 150;
    info->exec.size = fs_total(fd);
    strcpy(info->exec.file, boot_file);
    result = get_executable_info(info, fd);
done:
    if(fd != FILEHND_INVALID) fs_close(fd);
    fs_iso_unmount(mount);
    return result;
}

/* This checksum covers executable bytes, not the whole disc and not patches. */
int isoldr_check_boot(isoldr_info_t *info, const char *image_file) {
    const char *mount = "/iso_check";
    char path[NAME_MAX];
    file_t fd = FILEHND_INVALID;
    uint8_t *buf = NULL;
    uint32_t crc = ~0U, remaining, extent;
    int result = -1;
    if(!info || !image_file) { isoldr_error("Missing image information.\n"); return -1; }
    uint32_t skip = !strcasecmp(info->exec.file, "0WINCEOS.BIN") ? 2048 : 0;

    info->magic[10] = '\0';
    info->boot_crc32 = 0;
    if(info->image_type == IMAGE_TYPE_ROM_NAOMI || info->bleem) {
        isoldr_error("Executable CRC is available for Dreamcast disc images.\n");
        return -1;
    }
    if(!isoldr_boot_extent_limit(info->exec.addr, info->exec.size, 2048,
        hardware_sys_mode(NULL) == HW_TYPE_RETAIL ? 0x0cfff000U : 0x0dfff000U, &extent)) {
        isoldr_error("Executable has an invalid size or RAM address.\n");
        return -1;
    }
    if(fs_iso_mount(mount, image_file) < 0) {
        isoldr_error("Cannot mount image for executable check.\n");
        return -1;
    }
    snprintf(path, sizeof(path), "%s/%s", mount, info->exec.file);
    fd = fs_open(path, O_RDONLY);
    if(fd == FILEHND_INVALID || (uint64_t)info->exec.size + skip != fs_total(fd)) {
        isoldr_error("Executable is missing or its size changed.\n");
        goto done;
    }
    if(fs_seek(fd, skip, SEEK_SET) != (off_t)skip) {
        isoldr_error("Cannot seek to executable data.\n");
        goto done;
    }
    buf = memalign(32, 32768);
    if(!buf) {
        isoldr_error("Not enough memory for executable check.\n");
        goto done;
    }
    remaining = info->exec.size;
    while(remaining) {
        size_t want = remaining < 32768 ? remaining : 32768;
        if(fs_read(fd, buf, want) != (ssize_t)want) {
            isoldr_error("Incomplete executable read. Check the image and storage.\n");
            goto done;
        }
        crc = isoldr_crc32_update(crc, buf, want);
        remaining -= want;
        thd_pass();
    }
    info->boot_crc32 = ~crc;
    info->magic[10] = ISOLDR_VERIFY_MARKER;
    info->magic[11] = '\0';
    result = 0;
done:
    free(buf);
    if(fd != FILEHND_INVALID) fs_close(fd);
    fs_iso_unmount(mount);
    return result;
}


static int patch_loader_addr(uint8 *loader, uint32 size, uint32 addr) {

	uint32 i = 0, a = 0;
	int skip = 0;

	EXPT_GUARD_BEGIN;

	for(i = 0; i < size - 3; i += 4) {

		if(loader[i + 2] == 0xE0 && loader[i + 3] == 0x8C/* && loader[i - 1] < 0x10*/) {
			memcpy(&a, loader + i, sizeof(uint32));
//				printf("0x%08lx -> ", a);
			a -= ISOLDR_DEFAULT_ADDR;

			if(a == 0 && skip++) {
//					printf("skip\n");
				continue;
			}

//				printf("0x%04lx -> ", a);
			a += addr;
//				printf("0x%08lx at offset %ld\n", a, i);
			memcpy(loader + i, &a, sizeof(uint32));
		}
	}

	EXPT_GUARD_CATCH;

	isoldr_error("Loader memory patch failed\n");
	EXPT_GUARD_RETURN -1;

	EXPT_GUARD_END;

	return 0;
}

static void set_loader_type(isoldr_info_t *info) {
	if (info->image_type == IMAGE_TYPE_ROM_NAOMI || hardware_sys_mode(NULL) != HW_TYPE_RETAIL) {
		strncpy(info->fs_type, ISOLDR_TYPE_NAOMI, 5);
		info->fs_type[5] = '\0';
	}
	else if (info->syscalls != 0 || info->scr_hotkey != 0 || info->bleem != 0 ||
		info->firmware != 0) {
		strncpy(info->fs_type, ISOLDR_TYPE_FULL, 4);
		info->fs_type[4] = '\0';
	}
	else if ((info->emu_cdda != CDDA_MODE_DISABLED || info->use_irq != 0) && info->emu_vmu == 0) {
		strncpy(info->fs_type, ISOLDR_TYPE_CDDA, 4);
		info->fs_type[4] = '\0';
	}
	else if (info->emu_vmu != 0 && info->emu_cdda == CDDA_MODE_DISABLED) {
		strncpy(info->fs_type, ISOLDR_TYPE_VMU, 3);
		info->fs_type[3] = '\0';
	}
	else if (info->emu_vmu != 0 && info->emu_cdda != CDDA_MODE_DISABLED) {
		strncpy(info->fs_type, ISOLDR_TYPE_FEAT, 4);
		info->fs_type[4] = '\0';
	}
	else {
		info->fs_type[0] = '\0';
	}
}

void isoldr_exec(isoldr_info_t *info, uintptr_t addr) {

	isoldr_last_error[0] = '\0';
	if(!strcmp(info->fs_dev, ISOLDR_DEV_SDCARD)) {
		info->use_dma = 0;
		info->alt_read = 0;
	}

	char fn[NAME_MAX];
	uint8_t *loader = NULL;
	size_t len = 0;
	size_t buf_size;
	file_t fd;

	if (strcmp(info->fs_dev, ISOLDR_DEV_DCLOAD) == 0
		|| strcmp(info->fs_dev, ISOLDR_DEV_GDROM) == 0
		|| strcmp(info->fs_dev, ISOLDR_DEV_SDCARD) == 0
		|| strcmp(info->fs_dev, ISOLDR_DEV_G1ATA) == 0
	) {
		set_loader_type(info);
	}

	if(info->fs_type[0] != '\0') {
		snprintf(fn, NAME_MAX, "%s/firmware/%s/%s_%s.bin",
			getenv("PATH"), lib_get_name(), info->fs_dev, info->fs_type);
	}
	else {
		snprintf(fn, NAME_MAX, "%s/firmware/%s/%s.bin",
			getenv("PATH"), lib_get_name(), info->fs_dev);
	}

	char elf_path[NAME_MAX];
    snprintf(elf_path, sizeof(elf_path), "%s", fn);
    char *elf_dot = strrchr(elf_path, '.');
    if(elf_dot) strcpy(elf_dot, ".elf");
    if(FileExists(elf_path)) {
        if(isoldr_elf_load(elf_path, addr, &loader, &len) < 0) {
            isoldr_error("Cannot load ELF firmware: %s\n", elf_path);
            return;
        }
        ds_printf("DS_PROCESS: Loader: %s\n", elf_path);
    }
    else {
    fd = fs_open(fn, O_RDONLY);

    if(fd != FILEHND_INVALID) {
        size_t binary_size = fs_total(fd);
        if(binary_size < 4 || binary_size > 2 * 1024 * 1024) {
            fs_close(fd); isoldr_error("Invalid firmware size.\n"); return;
        }
        len = binary_size + ISOLDR_PARAMS_SIZE;
		buf_size = len < 0x20000 ? 0x25000 : len + 0x5000;
		loader = (uint8_t *) memalign(32, buf_size);

		if(loader == NULL) {
			fs_close(fd);
			isoldr_error("No free memory, needed %d bytes\n", len);
			return;
		}

		ds_printf("DS_PROCESS: Loading %s %d bytes to %08lx\n",
			fn, len - ISOLDR_PARAMS_SIZE, (uintptr_t)(loader + ISOLDR_PARAMS_SIZE));

		memset(loader, 0, buf_size);

		if(fs_read(fd, loader + ISOLDR_PARAMS_SIZE, len - ISOLDR_PARAMS_SIZE) != (len - ISOLDR_PARAMS_SIZE)) {
			fs_close(fd);
			free(loader);
			isoldr_error("Can't load %s\n", fn);
			return;
		}

		fs_close(fd);

		if(addr != ISOLDR_DEFAULT_ADDR) {
			if(patch_loader_addr(loader + ISOLDR_PARAMS_SIZE, len - ISOLDR_PARAMS_SIZE, addr) < 0) {
				free(loader);
				return;
			}
		}
	}
	else {
		char *dot = strrchr(fn, '.');

		if(dot != NULL) {
			strcpy(dot, ".elf");
		}
		if(isoldr_elf_load(fn, addr, &loader, &len) < 0) {
			return;
		}
	}

    }

    /* Validate the complete loader extent, including parameters and BSS. */
    uint32_t loader_phys = (uint32_t)addr & 0x1fffffff;
    uint32_t boot_phys = info->exec.addr & 0x1fffffff;
    uint32_t boot_bytes;
    if(info->image_type != IMAGE_TYPE_ROM_NAOMI &&
       info->syscalls != 1 && info->bleem != 1 &&
       (!isoldr_boot_extent_limit(info->exec.addr, info->exec.size, 2048,
        hardware_sys_mode(NULL) == HW_TYPE_RETAIL ? 0x0cfff000U : 0x0dfff000U, &boot_bytes) ||
        loader_phys < 0x0c000100 || (uint64_t)loader_phys + len + 32 > (hardware_sys_mode(NULL) == HW_TYPE_RETAIL ? 0x0cfff000U : 0x0dfff000U) ||
        isoldr_ranges_overlap(loader_phys, len + 32, boot_phys, boot_bytes))) {
        isoldr_error("Loader and executable do not fit at this address.\n"
                    "Try the baseline profile or adjust the loader address.\n");
        free(loader);
        return;
    }

	if(info->syscalls == 1) {

		snprintf(fn, NAME_MAX, "%s/firmware/%s/syscalls.bin",
			getenv("PATH"), lib_get_name());
		fd = fs_open(fn, O_RDONLY);

		if(fd == FILEHND_INVALID) {
			info->syscalls = 0;
		}
		else {
			size_t sc_len = fs_total(fd);
			uint8_t *buff = (uint8_t *) memalign(32, sc_len);

			if(buff == NULL) {
				fs_close(fd);
				isoldr_error("No free memory, needed %d bytes\n", sc_len);
				info->syscalls = 0;
			}
			else {
				ds_printf("DS_PROCESS: Loading %s %d bytes to %08lx\n",
					fn, sc_len, (uintptr_t)buff);

				if (fs_read(fd, buff, sc_len) != sc_len) {
					isoldr_error("Can't load %s\n", fn);
					info->syscalls = 0;
				}
				else {
					addr = ISOLDR_DEFAULT_ADDR;

					dcache_wback_range((uintptr_t)buff, sc_len);
					info->syscalls = (uintptr_t)buff;
					info->heap = HEAP_MODE_BEHIND;
					info->emu_cdda = 0;
					info->emu_vmu = 0;
					info->use_irq = 0;
				}
				fs_close(fd);
			}
		}
	}

	if(info->bleem == 1) {

		snprintf(fn, NAME_MAX, "%s/firmware/emu/bleem.bin", getenv("PATH"));
		fd = fs_open(fn, O_RDONLY);

		if(fd == FILEHND_INVALID) {
			info->bleem = 0;
		}
		else {
			size_t blen = fs_total(fd);
			uint8_t *buff = (uint8_t *) memalign(32, blen);

			if(buff == NULL) {
				fs_close(fd);
				isoldr_error("No free memory, needed %d bytes\n", blen);
				info->bleem = 0;
			}
			else {
				ds_printf("DS_PROCESS: Loading %s %d bytes to %08lx\n", fn, blen, (uintptr_t)buff);

				if(fs_read(fd, buff, blen) != blen) {
					isoldr_error("Can't load %s\n", fn);
					info->bleem = 0;
				}
				else {
					dcache_wback_range((uintptr_t)buff, blen);
					addr = ISOLDR_DEFAULT_ADDR_HIGH - 8000;
					info->bleem = (uintptr_t)buff;
					info->heap = HEAP_MODE_BEHIND;
				}
				fs_close(fd);
			}
		}
	}

	if(info->image_type == IMAGE_TYPE_ROM_NAOMI) {

		snprintf(fn, NAME_MAX, "%s/firmware/bios/naomi_irq_vec.bin", getenv("PATH"));
		fd = fs_open(fn, O_RDONLY);

		if(fd == FILEHND_INVALID) {
			isoldr_error("Can't open file: %s\n", fn);
			free(loader);
			return;
		}
		size_t hlen = fs_total(fd);
		uint8_t *buff = (uint8_t *) memalign(32, 0x10000);

		if(buff == NULL) {
			fs_close(fd);
			free(loader);
			isoldr_error("No memory for naomi irq table\n");
			return;
		}
		ds_printf("DS_PROCESS: Loading %s %d bytes to %08lx\n",
			fn, hlen, (uintptr_t)buff);

		if(fs_read(fd, buff, hlen) != hlen) {
			fs_close(fd);
			free(buff);
			free(loader);
			isoldr_error("Can't load %s\n", fn);
			return;
		}
		fs_close(fd);

		snprintf(fn, NAME_MAX, "%s/firmware/bios/naomi_irq_hnd.bin", getenv("PATH"));
		fd = fs_open(fn, O_RDONLY);

		if(fd == FILEHND_INVALID) {
			free(buff);
			free(loader);
			isoldr_error("Can't open file: %s\n", fn);
			return;
		}

		size_t vlen = fs_total(fd);
		ds_printf("DS_PROCESS: Loading %s %d bytes to %08lx\n",
			fn, vlen, (uintptr_t)(buff + hlen));

		if(fs_read(fd, buff + hlen, vlen) != vlen) {
			fs_close(fd);
			free(buff);
			free(loader);
			isoldr_error("Can't load %s\n", fn);
			return;
		}
		fs_close(fd);

		dcache_wback_range((uintptr_t)buff, hlen + vlen);
		info->firmware = (uintptr_t)buff;
	}
	else if(info->firmware == 1) {
		char *p;
		size_t plen = ISOLDR_FLASHROM_PATH_SIZE;

		snprintf(fn, NAME_MAX, "%s/firmware/flash/dcus_ntsc.bin", getenv("PATH"));
		p = fn + strlen(fn) - 11;

		switch(info->region) {
			case ISOLDR_REGION_JAPAN:
			case ISOLDR_REGION_KOREA:
				p[0] = 'j';
				p[1] = 'p';
				break;
			case ISOLDR_REGION_EUROPE:
			case ISOLDR_REGION_AUSTRALIA:
				memcpy(p, "eu_pal.bin", sizeof("eu_pal.bin"));
				break;
		}

		fd = fs_open(fn, O_RDONLY);

		if(fd != FILEHND_INVALID) {
			size_t flen = fs_total(fd);
			uint8_t *buff = (uint8_t *) memalign(32, plen + flen);
			char *pfs = strchr(fn + 1, '/');

			if(pfs != NULL) {
				memmove(fn, pfs, strlen(pfs) + 1);
			}

			if(buff != NULL) {
				memset(buff, 0, plen);
				snprintf((char *)buff, plen, "%s", fn);

				if(fs_read(fd, buff + plen, flen) == flen) {
					dcache_wback_range((uintptr_t)buff, plen + flen);
					info->firmware = (uintptr_t)buff;
					ds_printf("DS_PROCESS: Loading flashrom dump %s %d bytes to 0x%08lx\n",
						fn, (int)flen, (uintptr_t)(buff + plen));
				}
				else {
					isoldr_error("Can't read flashrom dump file: %s\n", fn);
					free(buff);
					info->firmware = 0;
				}
			}
			else {
				isoldr_error("No memory for flashrom\n");
				info->firmware = 0;
			}
			fs_close(fd);
		}
		else {
			isoldr_error("Can't open flashrom dump file: %s\n", fn);
			info->firmware = 0;
		}
	}

	memcpy(loader, info, sizeof(*info));

	if(info->image_type == IMAGE_TYPE_ROM_NAOMI) {
		isoldr_naomi_eeprom_prepare(info);
	}

	ds_printf("DS_PROCESS: Executing at 0x%08lx (0x%08lx)...\n",
		addr, addr + ISOLDR_PARAMS_SIZE);
	ShutdownDS(true);

	isoldr_exec_at(loader, len, addr, ISOLDR_PARAMS_SIZE);
}


int builtin_isoldr_cmd(int argc, char *argv[]) {

	if(argc < 2) {
		ds_printf("\n  ## ISO Loader v%d.%d.%d build %d ##\n\n"
		          "Usage: %s options args\n"
		          "Options: \n", VER_MAJOR, VER_MINOR, VER_MICRO, VER_BUILD, argv[0]);
		ds_printf(" -s, --fast       -Don't show loader text on screen\n"
		          " -i, --verbose    -Show additional info\n"
		          " -a, --dma        -Use DMA transfer if available\n"
		          " -q, --irq        -Use IRQ hooking\n"
		          " -c, --cdda       -Emulate CDDA audio (cddamode=1 by default)\n"
				  " -l, --low        -Use low-level syscalls emulation (disabled by default).\n");
		ds_printf("Arguments: \n"
		          " -e, --async      -Emulate async reading, 0=none default, >0=sectors per frame\n"
		          " -d, --device     -Loader device (sd/ide/cd), default auto\n"
		          " -p, --fspart     -Device partition (0-3), default auto\n"
		          " -t, --fstype     -Device filesystem (fat, ext2, raw), default auto\n");
		ds_printf(" -x, --lmem       -Any valid address for the loader (default auto)\n"
		          " -f, --file       -ISO image file path\n"
		          " -j, --jmp        -Boot mode:\n"
		          "                      0 = from executable (default)\n"
		          "                      1 = from IP.BIN\n"
		          "                      2 = from truncated IP.BIN\n");
		ds_printf(" -o, --os         -Executable OS:\n"
		          "                      0 = auto (default)\n"
		          "                      1 = KallistiOS\n"
		          "                      2 = KATANA\n"
		          "                      3 = WINCE\n");
		ds_printf(" -r, --addr       -Executable memory address (default 0xac010000)\n"
		          " -b, --boot       -Executable file name (default from IP.BIN)\n");
		ds_printf(" -h, --heap       -Heap mode or memory address\n"
		          "                      0 = auto address selection (default)\n"
		          "                      1 = behind the loader\n"
		          "                      2 = ingame memory allocation (KATANA only)\n"
		          "                      3 = maple DMA buffer (keep some for devices)\n"
		          "             0x8cXXXXXX = address (specify valid address)\n");
		ds_printf(" -g, --cddamode   -CDDA emulation mode\n"
		          "                      0 = Disabled (default)\n"
		          "                      1 = DMA/DMA/TMU2\n"
		          "                      2 = DMA/DMA/TMU1\n"
		          "                      3 = SQ/PIO/TMU2\n"
		          "                      4 = SQ/PIO/TMU1\n"
		          "             0x000CPDS%d = Ch[1-2],Pos[1-2],Dst[1-2-4],Src[1-2],%d\n",
				  CDDA_MODE_EXTENDED, CDDA_MODE_EXTENDED);
		ds_printf(" -v, --vmu        -Emulate VMU on port A1.\n"
		          "                      0 = disabled (default)\n"
		          "                      1 = number for VMU dump /vmu/vmudump001.vmd\n"
		          "                    999 = max number\n");
		ds_printf(" -k, --scrhot     -Screenshots from video frame buffer\n"
		          "                      0 = disabled (default)\n"
		          "                   XXXX = bit mask for pad buttons to me pressed\n");
		ds_printf("     --region     -Hardware region (1-Japan, 2-USA, 3-EU, 4-KR, 5-AU)\n"
		          "     --test       -NAOMI game test mode\n"
		          "     --pa1        -Patch address 1\n"
		          "     --pa2        -Patch address 2\n"
		          "     --pv1        -Patch value 1\n"
		          "     --pv2        -Patch value 2\n"
		          " -P, --preset     -Preset file path\n\n"
		          "Example: %s -f /sd/game.iso\n\n", argv[0]);
		return CMD_NO_ARG;
	}

	uint32 p_addr[2]  = {(uint32)-1, (uint32)-1};
	int p_value[2] = {0, 0};
	uint32 exec_addr = (uint32)-1;
	uintptr_t loader_addr = 0;
	uint32 lex = (uint32)-1, heap = (uint32)-1;
	char *file = NULL, *bin_file = NULL, *device = NULL, *fstype = NULL;
	char *preset_file = NULL;
	int verbose = -1, use_dma = -1, emu_cdda = -1, fast_boot = -1;
	int use_irq = -1, low_level = -1, alt_read = -1, use_gpio = -1;
	int test_mode = -1, fspart = -1;
	int emu_async = -1, boot_mode = -1, bin_type = -1;
	int cdda_mode = -1, emu_vmu = -1, region = -1;
	int scr_hotkey = -1, bleem = -1;
	isoldr_info_t *info;

	struct cfg_option options[] = {
		{"verbose",   'i', NULL, CFG_BOOL,  (void *) &verbose,     0},
		{"dma",       'a', NULL, CFG_BOOL,  (void *) &use_dma,     0},
		{"device",    'd', NULL, CFG_STR,   (void *) &device,      0},
		{"fspart",    'p', NULL, CFG_INT,   (void *) &fspart,      0},
		{"fstype",    't', NULL, CFG_STR,   (void *) &fstype,      0},
		{"memory",    'x', NULL, CFG_ULONG, (void *) &lex,         0},
		{"addr",      'r', NULL, CFG_ULONG, (void *) &exec_addr,   0},
		{"file",      'f', NULL, CFG_STR,   (void *) &file,        0},
		{"async",     'e', NULL, CFG_INT,   (void *) &emu_async,   0},
		{"cdda",      'c', NULL, CFG_BOOL,  (void *) &emu_cdda,    0},
		{"cddamode",  'g', NULL, CFG_INT,   (void *) &cdda_mode,   0},
		{"heap",      'h', NULL, CFG_ULONG, (void *) &heap,        0},
		{"jmp",       'j', NULL, CFG_INT,   (void *) &boot_mode,   0},
		{"os",        'o', NULL, CFG_INT,   (void *) &bin_type,    0},
		{"boot",      'b', NULL, CFG_STR,   (void *) &bin_file,    0},
		{"fast",      's', NULL, CFG_BOOL,  (void *) &fast_boot,   0},
		{"irq",       'q', NULL, CFG_BOOL,  (void *) &use_irq,     0},
		{"vmu",       'v', NULL, CFG_INT,   (void *) &emu_vmu,     0},
		{"low",       'l', NULL, CFG_BOOL,  (void *) &low_level,   0},
		{"scrhotkey", 'k', NULL, CFG_INT,   (void *) &scr_hotkey,  0},
		{"bleem",     'u', NULL, CFG_INT,   (void *) &bleem,       0},
		{"altread",   'y', NULL, CFG_BOOL,  (void *) &alt_read,    0},
		{"gpio",     '\0', NULL, CFG_BOOL,  (void *) &use_gpio,    0},
		{"region",   '\0', NULL, CFG_INT,   (void *) &region,      0},
		{"test",     '\0', NULL, CFG_BOOL,  (void *) &test_mode,   0},
		{"pa1",      '\0', NULL, CFG_ULONG, (void *) &p_addr[0],   0},
		{"pa2",      '\0', NULL, CFG_ULONG, (void *) &p_addr[1],   0},
		{"pv1",      '\0', NULL, CFG_INT,   (void *) &p_value[0],  0},
		{"pv2",      '\0', NULL, CFG_INT,   (void *) &p_value[1],  0},
		{"preset",    'P', NULL, CFG_STR,   (void *) &preset_file, 0},
		CFG_END_OF_LIST
	};

	CMD_DEFAULT_ARGS_PARSER(options);

	if(file == NULL) {
		isoldr_error("Too few arguments (ISO file) \n");
		return CMD_ERROR;
	}

	info = isoldr_get_info(file, test_mode > -1 ? 1 : 0);

	if(info == NULL) {
		return CMD_ERROR;
	}
	loader_addr = isoldr_apply_preset(info, preset_file);

	if(loader_addr == (uintptr_t)-1) {
		return CMD_ERROR;
	}

	if(device != NULL && strncasecmp(device, "auto", 4)) {

		strcpy(info->fs_dev, device);
		info->fs_dev[strlen(info->fs_dev)] = '\0';

	}
	else if(lex == (uint32)-1 && !strncasecmp(file, "/pc/", 4)) {
		lex = ISOLDR_DEFAULT_ADDR_HIGH;
		ds_printf("DS_WARNING: Using dc-load as file system, forced loader address: 0x%08lx\n", lex);
	}

	if(fstype != NULL && strncasecmp(fstype, "auto", 4)) {
		strcpy(info->fs_type, fstype);
		info->fs_type[strlen(info->fs_type)] = '\0';
	}

	if(fspart > -1 && fspart < 4) {
		info->fs_part = fspart;
	}

	if(bin_file != NULL) {
		isoldr_set_boot_file(info, file, bin_file);
	}

	if(bin_type > -1) {
		info->exec.type = bin_type;
	}

	if(exec_addr != (uint32)-1) {
		info->exec.addr = exec_addr;
	}

	if(boot_mode > -1) {
		info->boot_mode = boot_mode;
	}
	if(emu_async > -1) {
		info->emu_async = emu_async;
	}
	if(use_dma > -1) {
		info->use_dma = use_dma;
	}
	if(fast_boot > -1) {
		info->fast_boot = fast_boot;
	}
	if(heap != (uint32)-1) {
		info->heap = heap;
	}
	if(use_irq > -1) {
		info->use_irq = use_irq;
	}
	if(emu_vmu > -1) {
		info->emu_vmu = emu_vmu;
	}
	if(low_level > -1) {
		info->syscalls = low_level;
	}
	if(scr_hotkey > -1) {
		info->scr_hotkey = scr_hotkey;
	}
	if(bleem > -1) {
		info->bleem = bleem;
	}
	if(alt_read > -1) {
		info->alt_read = alt_read;
	}
	if(use_gpio > -1) {
		info->use_gpio = use_gpio;
	}
	if(region > -1) {
		info->region = region;
	}

	if(cdda_mode > -1) {
		info->emu_cdda = cdda_mode;
	}
	else if(emu_cdda > -1) {
		info->emu_cdda  = (CDDA_MODE_EXTENDED | CDDA_MODE_SRC_DMA |
			CDDA_MODE_DST_DMA | CDDA_MODE_POS_TMU2 | CDDA_MODE_CH_FIXED);
	}

	for(int i = 0; i < 2; i++) {
		if(p_addr[i] != (uint32)-1) {
			info->patch_addr[i] = p_addr[i];
			info->patch_value[i] = p_value[i];
		}
	}

	if(lex == (uint32)-1) {
		lex = loader_addr;
	}

	if(verbose > -1) {

		ds_printf("Params size: %d\n", sizeof(isoldr_info_t));

		ds_printf("\n--- Executable info ---\n"
		          "Name: %s\n"
		          "OS: %d\n"
		          "Size: %d Kb\n"
		          "LBA: %d\n"
		          "Address: 0x%08lx\n"
		          "Boot mode: %d\n",
		          info->exec.file,
		          info->exec.type,
		          info->exec.size/1024,
		          info->exec.lba,
		          info->exec.addr,
		          info->boot_mode);

		ds_printf("--- ISO info ---\n"
		          "File: %s (%s)\n"
		          "Format: %d\n"
		          "LBA: %d (%d)\n"
		          "Sector size: %d\n",
		          info->image_file,
		          info->image_second,
		          info->image_type,
		          info->track_lba[0],
		          info->track_lba[1],
		          info->sector_size);

		ds_printf("--- Loader info ---\n"
		          "Device: %s\n"
		          "Type: %s\n"
				  "Partition: %d\n"
		          "Address: 0x%08lx\n"
		          "DMA: %d\n"
		          "IRQ: %d\n"
		          "Heap: 0x%08lx\n"
				  "Bypass pre-read: %d\n",
		          info->fs_dev,
		          info->fs_dev[0] != '\0' ? info->fs_type : "normal",
		          info->fs_part,
		          lex,
		          info->use_dma,
		          info->use_irq,
		          info->heap,
				  info->alt_read);

		ds_printf("Emu async: %d\n"
		          "Emu CDDA: 0x%08lx\n"
		          "Emu VMU: %d\n"
		          "Syscalls: 0x%08lx\n"
		          "Bleem: 0x%08lx\n"
				  "GPIO: %d\n\n",
		          info->emu_async,
		          info->emu_cdda,
		          info->emu_vmu,
		          info->syscalls,
		          info->bleem,
				  info->use_gpio);
	}

	isoldr_exec(info, lex);

	return CMD_ERROR;
}
