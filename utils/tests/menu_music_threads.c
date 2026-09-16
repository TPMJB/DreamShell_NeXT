/* Real threads: UI input during blocked storage and audio, plus close during load. */
#define MUSIC_REAL_THREADS
#include "ds.h"
#include <assert.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sched.h>
static sem_t reading, allow_read, polling, allow_poll;
static size_t slow_read(void *,size_t,size_t,FILE *);
#define fread slow_read
#include "../../applications/launch_app/modules/music.c"
#undef fread

static size_t slow_read(void *p,size_t s,size_t n,FILE *f) {
    sem_post(&reading); assert(!sem_wait(&allow_read));
    return fread(p,s,n,f);
}
kthread_t *thd_create(int detached,void *(*fn)(void *),void *arg) {
    kthread_t *t=malloc(sizeof(*t)); assert(t && !detached);
    assert(!pthread_create(&t->thread,NULL,fn,arg)); return t;
}
int thd_join(kthread_t *t,void **result) {
    int rv=pthread_join(t->thread,result); free(t); return rv;
}
void thd_sleep(int ms) {usleep(ms*1000);}
uint64_t timer_ms_gettime64(void) { static uint64_t tick=123; return ++tick; }
void thd_pass(void) {sched_yield();}
int GetVolumeFromSettings(void) {return 200;}
void ds_printf(const char *format,...) {(void)format;}
static int streams;
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb,int size) {
    assert(cb && size==MUSIC_BUFFER); ++streams; return 3;
}
void snd_stream_destroy(snd_stream_hnd_t h) {assert(h==3);--streams;}
void snd_stream_queue_enable(snd_stream_hnd_t h) {(void)h;}
void snd_stream_queue_disable(snd_stream_hnd_t h) {(void)h;}
void snd_stream_queue_go(snd_stream_hnd_t h) {(void)h;}
void snd_stream_start(snd_stream_hnd_t h,unsigned rate,int stereo) {(void)h;(void)rate;(void)stereo;}
void snd_stream_volume(snd_stream_hnd_t h,int v) {(void)h;(void)v;}
int snd_stream_poll(snd_stream_hnd_t h) {
    assert(h==3); sem_post(&polling); assert(!sem_wait(&allow_poll)); return 0;
}
static void *close_music(void *unused) {(void)unused;MenuMusicClose();return NULL;}
static void wait_closing(void) {
    for(;;) {
        mutex_lock(&music_mutex); int opened=music.opened; mutex_unlock(&music_mutex);
        if(!opened) return;
        sched_yield();
    }
}
int main(int argc,char **argv) {
    char folder[512],path[512],label[64];
    unsigned char wav[54] = {
        'R','I','F','F',46,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0x22,0x56,0,0,0x44,0xac,0,0,2,0,16,0,'d','a','t','a',10,0,0,0,
        1,2,3,4,5,6,7,8,9,10
    };
    assert(argc==2);
    sem_init(&reading,0,0); sem_init(&allow_read,0,0);
    sem_init(&polling,0,0); sem_init(&allow_poll,0,0);
    snprintf(folder,sizeof(folder),"%s/music",argv[1]); assert(!mkdir(folder,0700));
    snprintf(path,sizeof(path),"%s/menu.wav",folder);
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(wav,1,sizeof(wav),f)==sizeof(wav));fclose(f);
    MenuMusicOpen(argv[1]); assert(!sem_wait(&reading));
    MenuMusicLabel(label,sizeof(label)); assert(strstr(label,"loading"));
    MenuMusicCycle(); /* This would deadlock with the old load under music_mutex. */
    sem_post(&allow_read); assert(!sem_wait(&polling));
    MenuMusicCycle(); MenuMusicLabel(label,sizeof(label)); assert(strstr(label,"50%"));
    pthread_t closer; assert(!pthread_create(&closer,NULL,close_music,NULL));
    wait_closing(); sem_post(&allow_poll); assert(!pthread_join(closer,NULL));
    assert(!streams && !music.file && !music.worker);
    /* Saving the last input happens on worker exit, before join/unload. */
    snprintf(path,sizeof(path),"%s/music.cfg",argv[1]); f=fopen(path,"rb"); assert(f);
    assert(fgets(label,sizeof(label),f) && !strcmp(label,"50\n")); fclose(f);
    MenuMusicOpen(argv[1]); assert(!sem_wait(&reading));
    assert(!pthread_create(&closer,NULL,close_music,NULL)); wait_closing();
    MenuMusicLabel(label,sizeof(label)); sem_post(&allow_read);
    assert(!pthread_join(closer,NULL));
    assert(!streams && !music.file && !music.feed && !music.worker);
    puts("UI stays responsive during blocked file/audio I/O; close cancels load and joins worker");
    return 0;
}
