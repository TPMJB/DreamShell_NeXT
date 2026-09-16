/* Exercise production music code with real files and a recording AICA backend. */
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ds.h"
static FILE *MusicFopen(const char *,const char *);
#define fopen MusicFopen
#include "../../applications/gd_ripper/modules/music.c"
#undef fopen
static int file_opens, input_during_load;
static FILE *MusicFopen(const char *path,const char *mode) {
    assert(!music_mutex && music_io_mutex);
    ++file_opens;
    return fopen(path,mode);
}
void thd_pass(void) {
    char label[64];
    MenuMusicLabel(label,sizeof(label)); /* Must remain usable during slow I/O. */
    if(input_during_load) { input_during_load=0; MenuMusicCycle(); }
}
static kthread_t worker;
static int active, allocs, destroys, master=200, volume, queued, started;
static int fail_alloc, fail_poll, fail_thread;
static snd_stream_callback_t callback;
kthread_t *thd_create(int detached,void *(*fn)(void *),void *arg) {
    (void)fn; (void)arg; assert(!detached && !worker.live);
    if(fail_thread) return NULL;
    worker.live=1; return &worker;
}
int thd_join(kthread_t *t,void **result) {
    (void)result; assert(t==&worker && worker.live && !music_mutex);
    MusicStorage(1); /* Simulate the real worker's final preference flush. */
    worker.live=0; return 0;
}
void thd_sleep(int ms) { assert(ms==25); }
int GetVolumeFromSettings(void) { return master; }
void ds_printf(const char *fmt,...) { (void)fmt; }
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb,int size) {
    assert(!active && !music_mutex && size==MUSIC_BUFFER); allocs++;
    if(fail_alloc) return -1;
    active=1; callback=cb; return 3; /* Another app could own stream zero. */
}
void snd_stream_destroy(snd_stream_hnd_t h) { assert(h==3 && active && !music_mutex); active=0; destroys++; }
void snd_stream_queue_enable(snd_stream_hnd_t h) { assert(h==3); queued=1; started=0; volume=-1; }
void snd_stream_queue_disable(snd_stream_hnd_t h) { assert(h==3); queued=0; }
void snd_stream_queue_go(snd_stream_hnd_t h) { assert(h==3 && queued && started && volume>=0); }
void snd_stream_start(snd_stream_hnd_t h,unsigned rate,int stereo) {
    int count; assert(h==3 && active && queued && rate==22050 && !stereo);
    assert(callback(h,8192,&count) && count==8192);
    assert(callback(h,8192,&count) && count==8192); started=1;
}
void snd_stream_volume(snd_stream_hnd_t h,int value) { assert(h==3 && value>=0 && value<=255); volume=value; }
int snd_stream_poll(snd_stream_hnd_t h) {
    char label[64]; MenuMusicLabel(label,sizeof(label)); /* Audio can wait for DMA. */
    int count; assert(h==3 && active);
    unsigned char *p=callback(h,8192,&count);
    assert(p && count==8192 && (uintptr_t)p%32==2);
    return fail_poll ? -1 : 0;
}
static void Write(const char *path,const void *data,size_t size) {
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(data,1,size,f)==size); assert(!fclose(f));
}
static unsigned char wav[54] = {
    'R','I','F','F',46,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
    1,0,1,0,0x22,0x56,0,0,0x44,0xac,0,0,2,0,16,0,'d','a','t','a',10,0,0,0,
    1,2,3,4,5,6,7,8,9,10
};
int main(int argc,char **argv) {
    char track[512],folder[512],label[64],config[512];
    size_t off=0,len=0; unsigned rate=0;
    assert(argc==2);
    assert(MusicWav(wav,sizeof(wav),&off,&len,&rate) && off==44 && len==10 && rate==22050);
    for(size_t n=0;n<sizeof(wav);n++) assert(!MusicWav(wav,n,&off,&len,&rate));
    unsigned char bad[54]; memcpy(bad,wav,sizeof(wav));
    bad[16]=255; assert(!MusicWav(bad,sizeof(bad),&off,&len,&rate));
    memcpy(bad,wav,sizeof(wav)); bad[22]=2; assert(!MusicWav(bad,sizeof(bad),&off,&len,&rate));
    memcpy(bad,wav,sizeof(wav)); bad[34]=8; assert(!MusicWav(bad,sizeof(bad),&off,&len,&rate));
    memcpy(bad,wav,sizeof(wav)); bad[40]=0xff; assert(!MusicWav(bad,sizeof(bad),&off,&len,&rate));
    snprintf(folder,sizeof(folder),"%s/music",argv[1]); assert(!mkdir(folder,0700));
    snprintf(track,sizeof(track),"%s/menu.wav",folder);
    snprintf(config,sizeof(config),"%s/music.cfg",argv[1]);
    Write(track,wav,sizeof(wav));
    MenuMusicOpen(argv[1]); MenuMusicPoll(); assert(active && volume==30 && allocs==1);
    music.position=8;
    int count; unsigned char *p=callback(3,1024,&count);
    assert(count==1024);
    for(int i=0;i<count;i++) assert(p[i]==wav[44+(8+i)%10]);
    assert(!unlink(track)); /* Including Off -> On, never read the track again. */
    for(int i=0;i<20;i++) MenuMusicPoll();
    assert(allocs==1 && active);
    const int levels[] = {30,50,75,100,0,15};
    for(unsigned i=0;i<sizeof(levels)/sizeof(levels[0]);++i) {
        int opens=file_opens;
        MenuMusicCycle(); assert(file_opens==opens); /* UI never does storage I/O. */
        MenuMusicPoll(); assert(music.level==levels[i] && music.file);
        assert(active==!!levels[i]);
        if(active) assert(volume==master*levels[i]/100);
    }
    assert(allocs==2); /* Only a new stream, not a new WAV, after Off. */
    MenuMusicCycle(); MenuMusicPoll(); assert(volume==60);
    MenuMusicClose(); assert(!active && !worker.live && !music.file && !music.feed);
    Write(track,wav,sizeof(wav));
    MenuMusicOpen(argv[1]); MenuMusicPoll(); assert(music.level==30 && active);
    MenuMusicSuspend(1); assert(!active && !music.file); MenuMusicPoll(); assert(!active);
    MenuMusicSuspend(0); MenuMusicPoll(); assert(active);
    /* Higher levels survive a close/reopen and master volume still caps output. */
    for(int i=0;i<3;++i) { MenuMusicCycle(); MenuMusicPoll(); }
    assert(music.level==100 && volume==master);
    MenuMusicClose(); MenuMusicOpen(argv[1]); MenuMusicPoll(); assert(music.level==100);
    master=0; MenuMusicPoll(); assert(!active && music.file);
    master=255; MenuMusicPoll(); assert(volume==255);
    /* While ripping, cycles and RAM playback work without any storage access. */
    MenuMusicStorageLock(); int opens=file_opens;
    MenuMusicCycle(); MenuMusicPoll(); assert(!active && music.level==0 && music.file);
    MenuMusicCycle(); MenuMusicPoll(); assert(active && volume==255*15/100);
    assert(file_opens==opens && music.unsaved);
    MenuMusicLabel(label,sizeof(label)); assert(strchr(label,'*'));
    MenuMusicStorageUnlock(); MenuMusicPoll(); assert(!music.unsaved);
    fail_poll=1; MenuMusicPoll(); assert(!active && music.failed); fail_poll=0;
    int previous=allocs; for(int i=0;i<10;i++) MenuMusicPoll(); assert(allocs==previous);
    MenuMusicCycle(); MenuMusicPoll(); assert(active); /* Explicit retry. */
    MenuMusicClose(); fail_alloc=1; MenuMusicOpen(argv[1]); MenuMusicPoll();
    assert(!active && music.file && music.failed); fail_alloc=0;
    MenuMusicClose(); Write(track,bad,sizeof(bad)); MenuMusicOpen(argv[1]);
    previous=allocs; MenuMusicPoll(); assert(music.failed && allocs==previous);
    assert(!music.file && !music.feed);
    MenuMusicLabel(label,sizeof(label)); assert(strstr(label,"unavailable"));
    MenuMusicClose(); Write(track,wav,sizeof(wav));
    fail_thread=1; MenuMusicOpen(argv[1]); MenuMusicPoll();
    assert(music.failed && !active); fail_thread=0;
    MenuMusicCycle(); MenuMusicPoll(); assert(active && worker.live); MenuMusicClose();
    Write(config,"unexpected config\n",18); MenuMusicOpen(argv[1]); MenuMusicPoll(); assert(music.level==15);
    MenuMusicClose();
    /* A rip that starts before loading prevents even the initial config read. */
    Write(config,"75\n",3); MenuMusicOpen(argv[1]); MenuMusicStorageLock(); opens=file_opens;
    MenuMusicPoll(); assert(file_opens==opens && !active && !music.file);
    MenuMusicStorageUnlock(); input_during_load=1; MenuMusicPoll();
    assert(music.level==100 && volume==255); /* Loading cannot clobber a new input. */
    MenuMusicClose();
    MenuMusicOpen("/nonexistent/next-music-test"); MenuMusicCycle(); MenuMusicPoll();
    assert(music.unsaved); MenuMusicClose();
    RipperMusicOpen("/cd/DS"); opens=file_opens; MenuMusicCycle(); MenuMusicPoll();
    assert(file_opens==opens && music.failed && !active); MenuMusicClose();
    RipperMusicOpen("/sd-other/DS"); MenuMusicPoll(); assert(music.failed && !active); MenuMusicClose();
    RipperMusicOpen("/sd"); assert(!strcmp(music.path,"/sd/apps/launch_app")); MenuMusicClose();
    RipperMusicOpen(NULL); MenuMusicPoll(); assert(music.failed && !active); MenuMusicClose();
    RipperMusicOpen("/sd/DS"); assert(!strcmp(music.path,"/sd/DS/apps/launch_app")); MenuMusicClose();
    RipperMusicOpen("/ide/DS"); assert(!strcmp(music.path,"/ide/DS/apps/launch_app")); MenuMusicClose();
    RipperMusicOpen("/pc/DS"); assert(!strcmp(music.path,"/pc/DS/apps/launch_app")); MenuMusicClose();
    MenuMusicClose(); assert(!active && !worker.live && destroys>0);
    puts("Music UI independence, cached mute/resume, 100% volume, storage gate and cleanup passed");
    return 0;
}
