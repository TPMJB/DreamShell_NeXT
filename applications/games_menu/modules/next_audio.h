/* Menu previews use separate CD audio, not music stored inside game data. */
#ifndef NEXT_GAMES_AUDIO_H
#define NEXT_GAMES_AUDIO_H
#include <isofs/gdi_parse.h>

static int NextPreviewFile(char *path) {
    const char *ext = strrchr(path, '.');
    if(!ext || (strcasecmp(ext, ".raw") && strcasecmp(ext, ".wav"))) return 0;
    if(FileSize(path) > 0) return 1;
    /* Optimized dumps can retain a RAW name after converting audio to WAV. */
    if(!strcasecmp(ext, ".raw")) {
        strcpy(strrchr(path, '.'), ".wav");
        if(FileSize(path) > 0) return 1;
    }
    return 0;
}

static int NextFindPreviewTrack(const char *game, char *result, size_t capacity) {
    const char *slash = strrchr(game, '/');
    const char *ext = strrchr(game, '.');
    if(!capacity) return 0;
    result[0] = 0;
    if(!slash || !ext) return 0;
    int prefix = slash - game + 1;
    if(!strcasecmp(ext, ".gdi")) {
        FILE *fp = fopen(game, "rb");
        if(!fp) return 0;
        char line[512];
        int count = fgets(line, sizeof(line), fp) ? ds_gdi_count(line) : -1;
        for(int i = 0; i < count && fgets(line, sizeof(line), fp); ++i) {
            ds_gdi_track track;
            if(!ds_gdi_parse(line, &track) || track.number != (unsigned)i + 1) break;
            /* Track 2 is the low-density audio warning, not a game preview.
             * Never feed data sectors or a shared-file offset to the player. */
            if(track.number < 4 || track.flags != 0 || track.sector_size != 2352 || track.offset) continue;
            int n = snprintf(result, capacity, "%.*s%s", prefix, game, track.name);
            if(n > 0 && (size_t)n < capacity && NextPreviewFile(result)) {
                fclose(fp);
                return 1;
            }
        }
        fclose(fp);
    } else {
        /* Legacy ISO images can have external numbered audio tracks. */
        for(int track = 4; track <= 99; ++track) {
            int n = snprintf(result, capacity, "%.*strack%02d.raw", prefix, game, track);
            if(n <= 0 || (size_t)n >= capacity) break;
            if(NextPreviewFile(result)) return 1;
        }
    }
    result[0] = 0;
    return 0;
}
#endif
