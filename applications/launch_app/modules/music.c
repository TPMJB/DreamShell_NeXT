/* K-UI menu music, 2026 TPMJB and contributors.
 * Own only our stream. File reads finish before playback starts. */
#include <ds.h>
#include <dc/sound/stream.h>
#include <kos/mutex.h>
#include "music.h"

#define MUSIC_LIMIT (2u * 1024u * 1024u)
#define MUSIC_BUFFER 16384
static const char *const music_tracks[] = {
    "menu.wav", "neon-circuit.wav", "orbital-drift.wav",
    "midnight-vector.wav", "chrome-horizon.wav"
};
#define MUSIC_TRACKS (sizeof(music_tracks) / sizeof(music_tracks[0]))
static uint32_t music_random;
static unsigned music_previous = MUSIC_TRACKS;

/* A private generator leaves games' rand() state alone. Pick once per visit;
 * muting, retries and drive activity never advance the playlist. */
static unsigned MusicChoose(void) {
    uint64_t now = timer_ms_gettime64();
    music_random ^= (uint32_t)now ^ (uint32_t)(now >> 32) ^ 0x9e3779b9u;
    music_random ^= music_random << 13;
    music_random ^= music_random >> 17;
    music_random ^= music_random << 5;
    unsigned choice = music_random % MUSIC_TRACKS;
    if(choice == music_previous) choice = (choice + 1) % MUSIC_TRACKS;
    music_previous = choice;
    return choice;
}
/* UI state only: never hold this mutex across storage or audio-driver calls. */
static mutex_t music_mutex = MUTEX_INITIALIZER;
/* GD Ripper holds this during drive work; RAM playback never needs it. */
static mutex_t music_io_mutex = MUTEX_INITIALIZER;
/* Verification's service thread and app close can both join the audio worker. */
static mutex_t music_lifecycle_mutex = MUTEX_INITIALIZER;
static struct {
    char path[NAME_MAX];
    unsigned char *file, *pcm, *feed;
    size_t length, position;
    unsigned rate, track;
    int level, opened, suspended, failed, unsaved, volume, initialized, loading, waiting;
    unsigned revision, save_revision;
    snd_stream_hnd_t stream;
    kthread_t *worker;
} music = {.stream = SND_STREAM_INVALID};

static unsigned le16(const unsigned char *p) { return p[0] | ((unsigned)p[1] << 8); }
static unsigned le32(const unsigned char *p) { return le16(p) | (le16(p + 2) << 16); }

/* Bound every chunk before touching it, including its optional pad byte.
 * Deliberately accept just PCM16 mono: no decoder or stereo buffer overhead. */
static int MusicWav(const unsigned char *p, size_t n, size_t *offset,
                    size_t *length, unsigned *rate) {
    size_t at = 12, end;
    int format = 0, data = 0;
    if(n < 44 || n > MUSIC_LIMIT || memcmp(p,"RIFF",4) || memcmp(p+8,"WAVE",4)) return 0;
    if(le32(p+4) != n-8) return 0;
    end = n;
    while(at < end) {
        size_t count, step;
        if(end-at < 8) return 0;
        count = le32(p+at+4);
        if(count > end-at-8) return 0;
        step = 8 + count + (count & 1);
        if(step > end-at) return 0;
        if(!memcmp(p+at,"fmt ",4)) {
            const unsigned char *f = p+at+8;
            if(format || count < 16 || le16(f) != 1 || le16(f+2) != 1 ||
               le16(f+12) != 2 || le16(f+14) != 16) return 0;
            *rate = le32(f+4);
            if(*rate < 8000 || *rate > 44100 || le32(f+8) != *rate*2) return 0;
            format = 1;
        } else if(!memcmp(p+at,"data",4)) {
            if(data || count < 4 || (count & 1)) return 0;
            *offset = at+8; *length = count; data = 1;
        }
        at += step;
    }
    return format && data;
}

static void MusicStopStream(void) {
    /* destroy waits for outstanding audio DMA before memory is freed. */
    if(music.stream != SND_STREAM_INVALID) snd_stream_destroy(music.stream);
    music.stream = SND_STREAM_INVALID;
}

static void MusicStop(void) {
    MusicStopStream();
    free(music.file); free(music.feed);
    music.file = music.pcm = music.feed = NULL;
    music.length = music.position = 0;
}

static void *MusicFeed(snd_stream_hnd_t stream, int requested, int *received) {
    size_t count, copied = 0;
    (void)stream;
    *received = 0;
    if(requested <= 0 || !music.length) return NULL;
    /* This pinned KOS implementation passes BYTES despite the header's old
     * sample terminology. Always provide whole PCM frames across the loop. */
    count = (size_t)requested > MUSIC_BUFFER ? MUSIC_BUFFER : (size_t)requested;
    count &= ~(size_t)3;
    while(copied < count) {
        size_t part = music.length - music.position;
        if(part > count-copied) part = count-copied;
        memcpy(music.feed+2+copied, music.pcm+music.position, part);
        music.position = (music.position+part) % music.length;
        copied += part;
    }
    *received = (int)count;
    /* Force KOS's protected copy into its DMA buffer. Returning an aligned
     * reusable buffer would let the next callback overwrite in-flight DMA. */
    return music.feed+2;
}

static int MusicLoadTrack(unsigned track) {
    char path[NAME_MAX];
    FILE *file;
    long size;
    size_t offset = 0, length = 0;
    unsigned rate = 0;
    if(snprintf(path,sizeof(path),"%s/music/%s",music.path,music_tracks[track]) >= (int)sizeof(path)) return 0;
    file = fopen(path,"rb");
    if(!file) return 0;
    if(fseek(file,0,SEEK_END) || (size=ftell(file)) < 44 ||
       size > MUSIC_LIMIT || fseek(file,0,SEEK_SET)) { fclose(file); return 0; }
    music.file = malloc((size_t)size);
    if(!music.file) { fclose(file); return 0; }
    /* Yield between small reads on serial SD; this never holds the UI mutex. */
    for(size_t at = 0; at < (size_t)size;) {
        size_t count = (size_t)size-at;
        if(count > 4096) count = 4096;
        if(fread(music.file+at,1,count,file) != count) {
            fclose(file); MusicStop(); return 0;
        }
        at += count;
        thd_pass();
        mutex_lock(&music_mutex);
        int cancelled = !music.opened || music.suspended || !music.level;
        mutex_unlock(&music_mutex);
        if(cancelled) { fclose(file); MusicStop(); return -1; }
    }
    fclose(file);
    if(!MusicWav(music.file,(size_t)size,&offset,&length,&rate)) { MusicStop(); return 0; }
    music.feed = aligned_alloc(32,MUSIC_BUFFER+32);
    if(!music.feed) { MusicStop(); return 0; }
    music.pcm = music.file+offset; music.length = length; music.rate = rate;
    music.position = 0;
    return 1;
}

static int MusicLoad(void) {
    int result = MusicLoadTrack(music.track);
    /* An older installation or a removed track can still play menu.wav.
     * Cancellation must not trigger another read. Only one WAV is retained. */
    if(result == 0 && music.track != 0) result = MusicLoadTrack(0);
    return result;
}

static int MusicStart(int volume) {
    music.stream = snd_stream_alloc(MusicFeed,MUSIC_BUFFER);
    if(music.stream == SND_STREAM_INVALID) return 0;
    /* Queue the start so even the first sample respects the selected volume. */
    snd_stream_queue_enable(music.stream);
    snd_stream_start(music.stream,music.rate,0);
    snd_stream_volume(music.stream,volume);
    music.volume = volume;
    snd_stream_queue_go(music.stream);
    snd_stream_queue_disable(music.stream);
    return 1;
}

static int ConfigPath(char *p, size_t n) {
    return music.path[0] && snprintf(p,n,"%s/music.cfg",music.path) < (int)n;
}

/* Called only by the audio worker. The state mutex is released before all I/O. */
static void MusicStorage(int closing) {
    char path[NAME_MAX], line[16];
    FILE *file;
    int initialized, level, failed, dirty, valid = 0;
    unsigned revision;
    if(mutex_trylock(&music_io_mutex)) return;
    mutex_lock(&music_mutex);
    initialized = music.initialized;
    mutex_unlock(&music_mutex);
    if(!initialized && !closing) {
        level = 15;
        if(ConfigPath(path,sizeof(path)) && (file=fopen(path,"rb"))) {
            if(fgets(line,sizeof(line),file)) {
                const int levels[] = {0,15,30,50,75,100};
                for(size_t i = 0; i < sizeof(levels)/sizeof(levels[0]); ++i) {
                    char expected[16];
                    snprintf(expected,sizeof(expected),"%d\n",levels[i]);
                    if(!strcmp(line,expected)) level = levels[i];
                }
            }
            fclose(file);
        }
        mutex_lock(&music_mutex);
        if(!music.revision) music.level = level; /* Preserve input during loading. */
        music.initialized = 1;
        mutex_unlock(&music_mutex);
    }
    mutex_lock(&music_mutex);
    level = music.level; revision = music.revision; failed = music.failed;
    dirty = music.unsaved && revision != music.save_revision;
    mutex_unlock(&music_mutex);
    if(dirty) {
        if(ConfigPath(path,sizeof(path)) && (file=fopen(path,"wb"))) {
            int wrote = fprintf(file,"%d\n",level) > 0;
            int closed = fclose(file) == 0;
            valid = wrote && closed;
        }
        mutex_lock(&music_mutex);
        music.save_revision = revision;
        if(music.revision == revision) music.unsaved = !valid;
        mutex_unlock(&music_mutex);
        if(!valid) ds_printf("DS_ERROR: Music preference could not be saved\n");
    }
    if(!closing && level && !failed && !music.file) {
        mutex_lock(&music_mutex); music.loading = 1; mutex_unlock(&music_mutex);
        valid = MusicLoad();
        mutex_lock(&music_mutex);
        music.loading = 0;
        if(valid >= 0) music.failed = !valid;
        mutex_unlock(&music_mutex);
        if(!valid) ds_printf("DS_ERROR: Music unavailable; use mono PCM16 WAV, at most 2 MiB\n");
    }
    mutex_unlock(&music_io_mutex);
}

static void *MusicWorker(void *unused) {
    (void)unused;
    for(;;) {
        int run;
        mutex_lock(&music_mutex);
        run = music.opened && !music.suspended;
        mutex_unlock(&music_mutex);
        if(!run) break;
        MenuMusicPoll();
        thd_sleep(25);
    }
    MusicStorage(1); /* Flush the last selection before the module is unloaded. */
    return NULL;
}

static void MusicStartWorker(void) {
    music.worker = thd_create(0,MusicWorker,NULL);
    if(!music.worker) {
        mutex_lock(&music_mutex); music.failed = 1; mutex_unlock(&music_mutex);
        ds_printf("DS_ERROR: Music worker could not start\n");
    }
}

static void MusicJoin(void) {
    if(music.worker) { thd_join(music.worker,NULL); music.worker = NULL; }
    MusicStop();
}

/* The lifecycle lock serializes joins/restarts; the worker never takes it. */
void MenuMusicOpen(const char *app_path) {
    mutex_lock(&music_lifecycle_mutex);
    mutex_lock(&music_mutex); music.opened = 0; mutex_unlock(&music_mutex);
    MusicJoin();
    mutex_lock(&music_mutex);
    snprintf(music.path,sizeof(music.path),"%s",app_path ? app_path : "");
    music.level = 15; music.opened = 1;
    music.suspended = music.unsaved = music.loading = music.waiting = 0;
    music.failed = music.initialized = !app_path;
    music.revision = music.save_revision = 0;
    music.track = MusicChoose();
    mutex_unlock(&music_mutex);
    MusicStartWorker();
    mutex_unlock(&music_lifecycle_mutex);
}

void MenuMusicClose(void) {
    mutex_lock(&music_lifecycle_mutex);
    mutex_lock(&music_mutex); music.opened = 0; mutex_unlock(&music_mutex);
    MusicJoin();
    mutex_unlock(&music_lifecycle_mutex);
}

void MenuMusicCycle(void) {
    mutex_lock(&music_mutex);
    music.level = music.level == 0 ? 15 : music.level == 15 ? 30 :
        music.level == 30 ? 50 : music.level == 50 ? 75 : music.level == 75 ? 100 : 0;
    if(music.path[0]) music.failed = 0;
    music.unsaved = 1;
    ++music.revision;
    mutex_unlock(&music_mutex);
    mutex_lock(&music_lifecycle_mutex);
    if(music.opened && !music.suspended && !music.worker) MusicStartWorker();
    mutex_unlock(&music_lifecycle_mutex);
}

void MenuMusicSuspend(int suspend) {
    mutex_lock(&music_lifecycle_mutex);
    mutex_lock(&music_mutex);
    music.suspended = suspend;
    mutex_unlock(&music_mutex);
    if(suspend) MusicJoin();
    else if(music.opened && !music.worker) MusicStartWorker();
    mutex_unlock(&music_lifecycle_mutex);
}

void MenuMusicStorageLock(void) { mutex_lock(&music_io_mutex); }
void MenuMusicStorageUnlock(void) { mutex_unlock(&music_io_mutex); }

void MenuMusicPoll(void) {
    int master, level, run, failed;
    mutex_lock(&music_mutex);
    run = music.opened && !music.suspended;
    mutex_unlock(&music_mutex);
    if(!run) return;
    MusicStorage(0);
    master = GetVolumeFromSettings();
    if(master < 0) master = 230;
    if(master > 255) master = 255;
    mutex_lock(&music_mutex);
    level = music.level; failed = music.failed;
    music.waiting = level && !failed && !music.file;
    run = music.opened && !music.suspended;
    mutex_unlock(&music_mutex);
    /* Off releases the stream but retains the track until app close/suspend. */
    if(!run || !level || !master || failed) MusicStopStream();
    else if(music.file) {
        int volume = master*level/100;
        if(music.stream == SND_STREAM_INVALID && !MusicStart(volume)) failed = 1;
        if(music.stream != SND_STREAM_INVALID) {
            if(music.volume != volume) {
                snd_stream_volume(music.stream,volume);
                music.volume = volume;
            }
            if(snd_stream_poll(music.stream) < 0) { MusicStopStream(); failed = 1; }
        }
        if(failed) {
            mutex_lock(&music_mutex); music.failed = 1; mutex_unlock(&music_mutex);
        }
    }
}

void MenuMusicLabel(char *text, size_t size) {
    mutex_lock(&music_mutex);
    if(music.failed) snprintf(text,size,"Y Music unavailable");
    else if(!music.level) snprintf(text,size,"Y Music off%s",music.unsaved ? "*" : "");
    else if(music.loading) snprintf(text,size,"Y Music loading");
    else if(music.waiting) snprintf(text,size,"Y Music queued");
    else snprintf(text,size,"Y Music %d%%%s",music.level,music.unsaved ? "*" : "");
    mutex_unlock(&music_mutex);
}
