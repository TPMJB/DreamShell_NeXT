/* App transitions are serialized by the core; no background logger or heap probe. */
#include "ds.h"
#include "memory_stats.h"

#define MEMORY_LOG_LIMIT (128 * 1024)

void MemoryStatsAppEvent(const char *phase, const char *app) {
    static int session_written, warned;
    int saved_errno = errno, failed = 0;
    memory_snapshot_t sample;
    char line[384], path[NAME_MAX];
    const char *root = getenv("PATH");
    const char *header = "K-UI RAM session: bytes; available=heap_free+unclaimed; PVR separate; snapshots may miss peaks\n";
    file_t file = FILEHND_INVALID;
    ssize_t size;
    int length;
    if (MemoryStatsRead(&sample)) {
        length = snprintf(line, sizeof(line),
            "%llu %s %.64s main=%lu heap_used=%lu heap_free=%lu unclaimed=%lu available=%lu pvr_free=%lu break=%lx\n",
            (unsigned long long)timer_ms_gettime64(), phase, app ? app : "?",
            (unsigned long)sample.main_bytes, (unsigned long)sample.heap_used,
            (unsigned long)sample.heap_free, (unsigned long)sample.unclaimed,
            (unsigned long)sample.available, (unsigned long)pvr_mem_available(),
            (unsigned long)sample.program_break);
    } else {
        length = snprintf(line, sizeof(line), "%llu %s %.64s RAM snapshot unavailable\n",
            (unsigned long long)timer_ms_gettime64(), phase, app ? app : "?");
    }
    ds_printf("K-UI RAM: %s", line);
    if (!root || (strncmp(root,"/sd/",4) && strncmp(root,"/ide/",5) &&
            strncmp(root,"/pc/",4))) goto done;
    if (length <= 0 || length >= (int)sizeof(line) ||
            snprintf(path,sizeof(path),"%s/kui-memory.log",root) >= (int)sizeof(path)) {
        failed = 1; goto done;
    }
    /* FatFs needs explicit exclusive creation, and seek instead of O_APPEND. */
    file = fs_open(path, O_WRONLY);
    if (file == FILEHND_INVALID && errno == ENOENT)
        file = fs_open(path, O_WRONLY | O_CREAT | O_EXCL);
    if (file == FILEHND_INVALID) { failed = 1; goto done; }
    size = fs_total(file);
    size_t prefix = session_written ? 0 : strlen(header);
    if (size < 0 || size > MEMORY_LOG_LIMIT ||
            (size_t)size + prefix + (size_t)length > MEMORY_LOG_LIMIT ||
            fs_seek(file,0,SEEK_END) != size) { failed = 1; goto done; }
    if (prefix && fs_write(file,header,prefix) != (ssize_t)prefix) {
        failed = 1; goto done;
    }
    session_written = 1;
    if (fs_write(file,line,length) != length) failed = 1;
done:
    if (file != FILEHND_INVALID && fs_close(file)) failed = 1;
    if (failed && !warned) {
        ds_printf("K-UI RAM: log unavailable or full (128 KiB limit); console snapshots continue\n");
        warned = 1;
    }
    errno = saved_errno;
}
