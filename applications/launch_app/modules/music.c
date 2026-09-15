/* DreamShell NeXT menu music, 2026 TPMJB and contributors.
 * Own only our stream. File reads finish before playback starts. */
#include <ds.h>
#include <dc/sound/stream.h>
#include <kos/mutex.h>
#include "music.h"

#define MUSIC_LIMIT (2u * 1024u * 1024u)
#define MUSIC_BUFFER 16384
static mutex_t music_mutex = MUTEX_INITIALIZER;
static struct {
    char path[NAME_MAX];
    unsigned char *file, *pcm, *feed;
    size_t length, position;
    unsigned rate;
    int level, opened, suspended, failed, unsaved, volume;
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

static void MusicStop(void) {
    /* destroy waits for outstanding audio DMA before memory is freed. */
    if(music.stream != SND_STREAM_INVALID) snd_stream_destroy(music.stream);
    music.stream = SND_STREAM_INVALID;
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

static int MusicLoad(int volume) {
    char path[NAME_MAX];
    FILE *file;
    long size;
    size_t offset = 0, length = 0;
    unsigned rate = 0;
    if(snprintf(path,sizeof(path),"%s/music/menu.wav",music.path) >= (int)sizeof(path)) return 0;
    file = fopen(path,"rb");
    if(!file) return 0;
    if(fseek(file,0,SEEK_END) || (size=ftell(file)) < 44 ||
       size > MUSIC_LIMIT || fseek(file,0,SEEK_SET)) { fclose(file); return 0; }
    music.file = malloc((size_t)size);
    if(!music.file || fread(music.file,1,(size_t)size,file) != (size_t)size) {
        fclose(file); MusicStop(); return 0;
    }
    fclose(file);
    if(!MusicWav(music.file,(size_t)size,&offset,&length,&rate)) { MusicStop(); return 0; }
    music.feed = aligned_alloc(32,MUSIC_BUFFER+32);
    if(!music.feed) { MusicStop(); return 0; }
    music.pcm = music.file+offset; music.length = length; music.rate = rate;
    music.position = 0;
    music.stream = snd_stream_alloc(MusicFeed,MUSIC_BUFFER);
    if(music.stream == SND_STREAM_INVALID) { MusicStop(); return 0; }
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
    return snprintf(p,n,"%s/music.cfg",music.path) < (int)n;
}

static void *MusicWorker(void *unused) {
    (void)unused;
    for(;;) {
        int opened;
        mutex_lock(&music_mutex); opened = music.opened; mutex_unlock(&music_mutex);
        if(!opened) break;
        MenuMusicPoll();
        thd_sleep(25);
    }
    return NULL;
}

void MenuMusicOpen(const char *app_path) {
    char path[NAME_MAX], line[16];
    FILE *file;
    MenuMusicClose();
    mutex_lock(&music_mutex);
    snprintf(music.path,sizeof(music.path),"%s",app_path);
    music.level = 15; music.opened = 1; music.suspended = music.failed = music.unsaved = 0;
    if(ConfigPath(path,sizeof(path)) && (file=fopen(path,"rb"))) {
        if(fgets(line,sizeof(line),file)) {
            if(!strcmp(line,"0\n")) music.level = 0;
            else if(!strcmp(line,"15\n")) music.level = 15;
            else if(!strcmp(line,"30\n")) music.level = 30;
            else if(!strcmp(line,"50\n")) music.level = 50;
        }
        fclose(file);
    }
    music.worker = thd_create(0,MusicWorker,NULL);
    if(!music.worker) {
        music.failed = 1;
        ds_printf("DS_ERROR: Menu music worker could not start\n");
    }
    mutex_unlock(&music_mutex);
}

void MenuMusicClose(void) {
    kthread_t *worker;
    mutex_lock(&music_mutex);
    music.opened = 0; MusicStop();
    worker = music.worker; music.worker = NULL;
    mutex_unlock(&music_mutex);
    /* Never unload a module while its audio worker can still call back. */
    if(worker) thd_join(worker,NULL);
}

void MenuMusicCycle(void) {
    char path[NAME_MAX];
    FILE *file = NULL;
    mutex_lock(&music_mutex);
    music.level = music.level == 0 ? 15 : music.level == 15 ? 30 : music.level == 30 ? 50 : 0;
    music.failed = 0;
    music.unsaved = 1;
    /* This tiny optional preference can safely fall back to its default if
     * interrupted. FatFs rename does not replace an existing destination. */
    if(ConfigPath(path,sizeof(path)) && (file=fopen(path,"wb"))) {
        int wrote = fprintf(file,"%d\n",music.level) > 0;
        int closed = fclose(file) == 0;
        if(wrote && closed) music.unsaved = 0;
    }
    if(music.unsaved) ds_printf("DS_ERROR: Music preference could not be saved\n");
    if(!music.level) MusicStop();
    if(!music.worker && music.opened) {
        music.worker = thd_create(0,MusicWorker,NULL);
        if(!music.worker) music.failed = 1;
    }
    mutex_unlock(&music_mutex);
}

void MenuMusicSuspend(int suspend) {
    mutex_lock(&music_mutex);
    music.suspended = suspend;
    if(suspend) MusicStop();
    mutex_unlock(&music_mutex);
}

void MenuMusicPoll(void) {
    int master;
    mutex_lock(&music_mutex);
    master = GetVolumeFromSettings();
    if(master < 0) master = 230;
    if(master > 255) master = 255;
    if(!music.opened || music.suspended || !music.level || !master) MusicStop();
    else if(!music.failed) {
        if(music.stream == SND_STREAM_INVALID && !MusicLoad(master*music.level/100)) {
            music.failed = 1;
            ds_printf("DS_ERROR: Menu music unavailable; use mono PCM16 WAV, at most 2 MiB\n");
        }
        if(music.stream != SND_STREAM_INVALID) {
            int volume = master*music.level/100;
            if(music.volume != volume) {
                snd_stream_volume(music.stream,volume);
                music.volume = volume;
            }
            if(snd_stream_poll(music.stream) < 0) { MusicStop(); music.failed = 1; }
        }
    }
    mutex_unlock(&music_mutex);
}

void MenuMusicLabel(char *text, size_t size) {
    mutex_lock(&music_mutex);
    if(music.failed) snprintf(text,size,"Y Music unavailable");
    else if(!music.level) snprintf(text,size,"Y Music off%s",music.unsaved ? "*" : "");
    else snprintf(text,size,"Y Music %d%%%s",music.level,music.unsaved ? "*" : "");
    mutex_unlock(&music_mutex);
}
