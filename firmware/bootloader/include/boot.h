#ifndef NEXT_BOOT_H
#define NEXT_BOOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BOOT_PATH_MAX 256
#define BOOT_CORE_MAX (8u * 1024u * 1024u)
#define BOOT_READ_CHUNK 32768u

typedef enum { BOOT_RAW, BOOT_SCRAMBLED, BOOT_GZIP } boot_format_t;
typedef enum {
    BOOT_OK, BOOT_OPEN, BOOT_SIZE, BOOT_MEMORY, BOOT_READ,
    BOOT_SHORT_READ, BOOT_CHANGED, BOOT_CLOSE, BOOT_CANCELLED,
    BOOT_GZIP_ERROR, BOOT_DESCRAMBLE
} boot_error_t;
typedef enum { BOOT_READING, BOOT_DECODING, BOOT_COMPLETE } boot_stage_t;
typedef bool (*boot_progress_t)(boot_stage_t stage, uint32_t count,
                                uint32_t total, void *arg);
typedef struct {
    uint8_t *data;
    uint32_t size, count;
    boot_error_t error;
} boot_image_t;

boot_error_t boot_load(const char *path, boot_format_t format,
                       boot_progress_t progress, void *arg, boot_image_t *image);
const char *boot_error_message(boot_error_t error);
int descramble(uint8_t *source, uint8_t *dest, uint32_t size);

typedef struct {
    char order[40];
    char core_path[BOOT_PATH_MAX];
    char fallback_path[BOOT_PATH_MAX];
    unsigned delay_seconds;
    bool autoboot, diagnostics;
} boot_config_t;

void boot_config_defaults(boot_config_t *config);
/* A malformed file leaves defaults intact and returns its first bad line. */
unsigned boot_config_parse(const char *data, size_t size, boot_config_t *config);
int boot_device_rank(const boot_config_t *config, const char *device);
bool boot_core_path_valid(const char *path);
boot_format_t boot_path_format(const char *path);

/* A countdown is explicitly disarmed by input, failure, or manual entry. */
typedef struct { bool armed; uint64_t deadline; } boot_countdown_t;
void boot_countdown_start(boot_countdown_t *timer, uint64_t now,
                          const boot_config_t *config, bool manual, bool have_core);
bool boot_countdown_due(boot_countdown_t *timer, uint64_t now, bool input);

#endif
