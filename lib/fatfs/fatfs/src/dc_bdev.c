/*
 * FatFs for the Sega Dreamcast
 *
 * This file is part of the FatFs module, a generic FAT filesystem
 * module for small embedded systems. This version has been ported and
 * optimized specifically for the Sega Dreamcast platform.
 *
 * Copyright (c) 2007-2025 Ruslan Rostovtsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <kos/dbglog.h>
#include <dc/sd.h>
#include <dc/g1ata.h>
#include <fatfs.h>
#include "partition.h"

#define MAX_PARTITIONS 4
static kos_blockdev_t sd_dev[MAX_PARTITIONS];
static kos_blockdev_t ide_dev[MAX_PARTITIONS];
static kos_blockdev_t ide_dma[MAX_PARTITIONS];

static void mount_path(char path[8], int ide, int slot) {
    if (slot) snprintf(path, 8, "/%s%d", ide ? "ide" : "sd", slot);
    else snprintf(path, 8, "/%s", ide ? "ide" : "sd");
}

static int mount_media(int ide) {
    uint8_t mbr[512] __attribute__((aligned(32)));
    kos_blockdev_t probe;
    int indices[4], mounted = 0;
    memset(&probe, 0, sizeof(probe));
    if (ide ? g1_ata_blockdev_for_device(0, &probe) : sd_blockdev_for_device(&probe))
        return -1;
    int rc = probe.init(&probe);
    uint64_t sectors = 0;
    if (!rc) {
        sectors = probe.count_blocks(&probe);
        rc = probe.read_blocks(&probe, 0, 1, mbr);
    }
    probe.shutdown(&probe);
    if (rc < 0) return -1;
    if (!sectors || sectors > UINT32_MAX) {
        dbglog(DBG_ERROR, "FATFS: this build requires 512-byte media below 2 TiB\n");
        return -1;
    }
    int count = ds_fat_partitions(mbr, sectors, indices);
    if (count < 0) {
        dbglog(DBG_ERROR, count == -2 ?
            "FATFS: GPT is not supported; use MBR primary partitions\n" :
            "FATFS: no FAT/exFAT volume found\n");
        return -1;
    }
    if (fs_fat_init()) return -1;
    for (int i = 0; i < count; ++i) {
        int part = indices[i], slot = part < 0 ? 0 : part;
        char path[8];
        kos_blockdev_t *dev = ide ? &ide_dev[slot] : &sd_dev[slot];
        kos_blockdev_t *dma = ide ? &ide_dma[slot] : NULL;
        mount_path(path, ide, slot);
        if (fs_fat_is_mounted(path)) { ++mounted; continue; }
        memset(dev, 0, sizeof(*dev));
        if (ide ? g1_ata_blockdev_for_device(0, dev) : sd_blockdev_for_device(dev))
            continue;
        if (dma) {
            memset(dma, 0, sizeof(*dma));
            if (g1_ata_blockdev_for_device(1, dma)) dma = NULL;
        }
        /* fs_fat_mount owns the block-device resources, also on mount failure. */
        if (!fs_fat_mount(path, dev, dma, part)) ++mounted;
        else {
            memset(dev, 0, sizeof(*dev));
            if (dma) memset(dma, 0, sizeof(*dma));
        }
    }
    return mounted ? 0 : -1;
}

int fs_fat_mount_sd(void) {
    sd_init_params_t params;
    memset(&params, 0, sizeof(params));
#ifdef FATFS_SD_CHECK_CRC
    params.check_crc = 1;
#endif
    params.interface = SD_IF_SCIF;
    if (!sd_init_ex(&params) && !mount_media(0)) return 0;
    params.interface = SD_IF_SCI;
    if (!sd_init_ex(&params) && !mount_media(0)) return 0;
    return -1;
}

int fs_fat_mount_ide(void) {
    if (g1_ata_init()) return -1;
    return mount_media(1);
}

static void unmount_media(int ide) {
    for (int i = 0; i < MAX_PARTITIONS; ++i) {
        char path[8];
        mount_path(path, ide, i);
        if (fs_fat_is_mounted(path)) fs_fat_unmount(path);
    }
    if (ide) { memset(ide_dev, 0, sizeof(ide_dev)); memset(ide_dma, 0, sizeof(ide_dma)); }
    else memset(sd_dev, 0, sizeof(sd_dev));
}
void fs_fat_unmount_sd(void) { unmount_media(0); }
void fs_fat_unmount_ide(void) { unmount_media(1); }
