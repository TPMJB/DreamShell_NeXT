/* Exercise the real SFX loader/callback/worker with bounded fake AICA requests. */
#include "sfx_shim/sfx_host.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/sfx.c"

static struct { callback_t callback; void *data; int live; } handles[4];
static void *(*worker[4])(void *);
static void *args[4];
static size_t fixture_size = 512;
static int fail_read, fail_handle, fail_thread, reads;
static Settings_t settings = {{180, 1, 1, 1, 1}};

void *snd_stream_get_userdata(snd_stream_hnd_t h) { return handles[h].data; }
void snd_stream_set_userdata(snd_stream_hnd_t h, void *p) { handles[h].data=p; }
snd_stream_hnd_t snd_stream_alloc(callback_t cb, int size) {
    assert(size == SND_STREAM_BUFFER_MAX_ADPCM);
    if(fail_handle) return -1;
    for(int i=0;i<4;i++) if(!handles[i].live) {
        handles[i].live=1; handles[i].callback=cb; return i;
    }
    return -1;
}
void snd_stream_destroy(snd_stream_hnd_t h) { assert(handles[h].live); handles[h].live=0; }
static int request(snd_stream_hnd_t h, int n) {
    int count=-1;
    unsigned char *p=handles[h].callback(h,n,&count);
    assert(count>=0 && count<=n);
    if(!p) { assert(!count); return -3; }
    for(int i=0;i<count;i++) assert(p[i]==0x5a); /* ASan catches any overrun. */
    return count;
}
void snd_stream_start_adpcm(snd_stream_hnd_t h, int rate, int stereo) {
    assert(rate==44100 && stereo==1);
    assert(request(h,128)==128); /* KOS prefill also calls the callback. */
}
int snd_stream_poll(snd_stream_hnd_t h) { return request(h,320)<0 ? -3 : 0; }
void snd_stream_volume(snd_stream_hnd_t h, int volume) { (void)h; assert(volume==180); }
void *thd_create(int detached, void *(*fn)(void *), void *arg) {
    assert(detached); if(fail_thread) return NULL;
    intptr_t h=(intptr_t)arg; worker[h]=fn; args[h]=arg; return &handles[h];
}
void thd_sleep(int ms) { assert(ms==50); }
size_t gzip_get_file_size(const char *n) { (void)n; return fixture_size; }
gzFile gzopen(const char *n, const char *m) { (void)n; (void)m; return handles; }
int gzread(gzFile f, void *p, unsigned n) { (void)f; reads++; memset(p,0x5a,n); return fail_read ? 0 : (int)n; }
int gzclose(gzFile f) { (void)f; return 0; }
size_t FileSize(const char *n) { (void)n; return fixture_size; }
file_t fs_open(const char *n,int f) { (void)n; (void)f; return 1; }
int fs_close(file_t f) { (void)f; return 0; }
int fs_read(file_t f,void *p,size_t n) { (void)f; memset(p,0x5a,n); return (int)n; }
Settings_t *GetSettings(void) { return &settings; }
int GetVolumeFromSettings(void) { return settings.audio.volume; }
sfxhnd_t snd_sfx_load(const char *n) { (void)n; return 1; }
void snd_sfx_play(sfxhnd_t h,int v,int pan) { (void)h; (void)v; (void)pan; }

int main(void) {
    assert(ds_sfx_play(DS_SFX_STARTUP)==0);
    assert(ds_sfx_play(DS_SFX_STARTUP)==0); /* Each stream owns its allocation. */
    assert(request(0,320)==320);
    assert(request(0,320)==64); /* Last short block must report its real size. */
    assert(request(0,320)==-3);
    worker[0](args[0]); worker[1](args[1]);
    for(int i=0;i<4;i++) assert(!handles[i].live);
    fail_handle=1; assert(ds_sfx_play(DS_SFX_STARTUP)==-1); fail_handle=0;
    fail_thread=1; assert(ds_sfx_play(DS_SFX_STARTUP)==-1); fail_thread=0;
    fail_read=1; assert(ds_sfx_play(DS_SFX_STARTUP)==-1); fail_read=0;
    fixture_size=513; assert(ds_sfx_play(DS_SFX_STARTUP)==-1);
    fixture_size=0; assert(ds_sfx_play(DS_SFX_STARTUP)==-1);
    fixture_size=(2<<20)+8; assert(ds_sfx_play(DS_SFX_STARTUP)==-1);
    fixture_size=512;
    int previous_reads=reads; settings.audio.startup_enabled=0;
    assert(ds_sfx_play(DS_SFX_STARTUP)==0 && reads==previous_reads);
    settings.audio.startup_enabled=1; settings.audio.volume=0;
    assert(ds_sfx_play(DS_SFX_STARTUP)==0 && reads==previous_reads);
    puts("SFX bounds, independent streams, settings and failure cleanup passed");
    return 0;
}
