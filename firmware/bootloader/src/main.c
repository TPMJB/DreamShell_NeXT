/**
 * DreamShell bootloader
 * Main
 * (c)2011-2026 SWAT <http://www.dc-swat.ru>
 */

#include "main.h"
#include "fs.h"
#include <fatfs.h>
#include <dc/sd.h>

static void early_init(void) {
	/* hardware_sys_mode() is not ready yet at KOS_INIT_EARLY time */
	if((((*(volatile uint32_t *)0xA05F74B0) >> 4) & 0x0F) == HW_TYPE_RETAIL) {
		g1_ata_select_device(G1_ATA_MASTER);
	}
}

KOS_INIT_EARLY(early_init);
KOS_INIT_FLAGS(INIT_IRQ | INIT_FS_ROMDISK | INIT_FS_PTY | INIT_CONTROLLER | INIT_CDROM);

pvr_init_params_t params = {
	/* Enable opaque and translucent with size 16 and polygons with size 32 */
	{ PVR_BINSIZE_16, PVR_BINSIZE_0, PVR_BINSIZE_32, PVR_BINSIZE_0, PVR_BINSIZE_0 },
	/* Vertex buffer size 512K */
	512 * 1024,

	/* No DMA */
	0,

	/* No FSAA */
	0,

	/* Translucent Autosort enabled. */
	0,

	/* Extra OPBs */
	3,

	/* Vertex buffer double-buffering enabled */
	0
};

const char title[] = "DreamShell NeXT boot v"VERSION;


int FileExists(const char *fn) {
    file_t f;
    
    f = fs_open(fn, O_RDONLY);
	
    if(f == FILEHND_INVALID) {
       return 0; 
    }
    
    fs_close(f);
    return 1;
}

int DirExists(const char *dir) {
    file_t f;
    
    f = fs_open(dir, O_DIR | O_RDONLY);
	
    if(f == FILEHND_INVALID) {
       return 0; 
    }
    
    fs_close(f);
    return 1;
}

int flashrom_get_region_only() {

	int start, size;
	uint8 region[6] = { 0 };
	region[2] = *(uint8 *)0xa021a002;

	/* Find the partition */
	if(flashrom_info(FLASHROM_PT_SYSTEM, &start, &size) < 0) {
		dbglog(DBG_ERROR, "%s: can't find partition %d\n", __func__, FLASHROM_PT_SYSTEM);
	} else {
		/* Read the first 5 characters of that partition */
		if(flashrom_read(start, region, 5) < 0) {
			dbglog(DBG_ERROR, "%s: can't read partition %d\n", __func__, FLASHROM_PT_SYSTEM);
		}
	}

	if(region[2] == 0x58 || region[2] == 0x30) {
		spiral_color = 0x44ed1800;
		return FLASHROM_REGION_JAPAN;
	} else if(region[2] == 0x59 || region[2] == 0x31) {
		spiral_color = 0x44ed1800;
		return FLASHROM_REGION_US;
	} else if(region[2] == 0x5A || region[2] == 0x32) {
		spiral_color = 0x443370d4;
		return FLASHROM_REGION_EUROPE;
	} else {
		spiral_color = 0x44000000;
		dbglog(DBG_ERROR, "%s: Unknown region code %02x\n", __func__, region[2]);
		return FLASHROM_REGION_UNKNOWN;
	}
}

volatile int start_pressed = 0;
uint32 boot_detect_ms;

static void start_callback(void) {
	start_pressed = 1;
}

static void show_boot_message(void) {
	dbglog(DBG_INFO, "         %s\n", title);
	dbglog(DBG_INFO, "         by TPMJB - based on SWAT DreamShell\n");
	dbglog(DBG_INFO, "         FAT16 / FAT32 / exFAT\n");
	dbglog(DBG_INFO, "  !!! Press START to enter the boot menu !!!\n\n");
}


uint32 boot_detect_devices(bool rescan) {
	uint64 started = timer_ms_gettime64();
	if(rescan) {
		fs_fat_unmount_sd();
		sd_shutdown();
		/* IDE must be connected before power-on. Refresh its volumes without
		   flushing or resetting a possibly absent device on the shared G1 bus. */
		fs_fat_unmount_ide();
	}
	int ide = InitIDE();
	int sd = InitSDCard();
	if(ide && sd && !DirExists("/brd")) InitRomdisk();
	return (uint32)(timer_ms_gettime64() - started);
}

int main(int argc, char **argv) {

	dbglog_set_level(DBG_KDEBUG);
	cont_btn_callback(0, CONT_START, (cont_btn_callback_t)start_callback);

	if(vid_check_cable() == CT_VGA) {
		vid_set_mode(DM_640x480_VGA, PM_RGB565);
	}
#ifndef __NAOMI__
	else if(flashrom_get_region_only() == FLASHROM_REGION_EUROPE) {
		vid_set_mode(DM_640x480_PAL_IL, PM_RGB565);
	}
	else {
		vid_set_mode(DM_640x480_NTSC_IL, PM_RGB565);
	}
#endif

	dbgio_dev_select("fb");
	show_boot_message();
	boot_detect_ms = boot_detect_devices(false);
	menu_init();
	menu_autoboot();
	dbgio_disable();

	pvr_init(&params);
	pvr_set_bg_color(192.0/255.0, 192.0/255.0, 192.0/255.0);
	spiral_init();

	menu_graphics_init();

	while(1) {
		menu_update();
		pvr_wait_ready();
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_TR_POLY);
		spiral_frame();
		menu_frame();
		pvr_list_finish();
		pvr_scene_finish();
	}

	return 0;
}
