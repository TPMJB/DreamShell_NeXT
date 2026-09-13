#ifndef FAT_TEST_SD_H
#define FAT_TEST_SD_H
#include <kos/blockdev.h>
#define SD_IF_SCIF 0
#define SD_IF_SCI 1
typedef struct { int interface; int check_crc; } sd_init_params_t;
int sd_init_ex(const sd_init_params_t *params);
int sd_blockdev_for_device(kos_blockdev_t *dev);

#endif
