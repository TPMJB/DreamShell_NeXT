#ifndef MUSIC_HOST_STREAM_H
#define MUSIC_HOST_STREAM_H
typedef int snd_stream_hnd_t;
typedef void *(*snd_stream_callback_t)(snd_stream_hnd_t,int,int *);
#define SND_STREAM_INVALID -1
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t,int);
void snd_stream_destroy(snd_stream_hnd_t);
void snd_stream_start(snd_stream_hnd_t,unsigned,int);
void snd_stream_queue_enable(snd_stream_hnd_t);
void snd_stream_queue_disable(snd_stream_hnd_t);
void snd_stream_queue_go(snd_stream_hnd_t);
void snd_stream_volume(snd_stream_hnd_t,int);
int snd_stream_poll(snd_stream_hnd_t);
#endif
