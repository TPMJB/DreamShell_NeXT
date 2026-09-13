#ifndef SFX_HOST_H
#define SFX_HOST_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <fcntl.h>
#define NAME_MAX 256
#define SFXHND_INVALID (-1)
#define FILEHND_INVALID (-1)
#define SND_STREAM_BUFFER_MAX_ADPCM 32704
typedef int file_t;
typedef void *gzFile;
typedef intptr_t snd_stream_hnd_t;
typedef int sfxhnd_t;
typedef struct { struct {int volume, startup_enabled, sfx_enabled, click_enabled, hover_enabled;} audio; } Settings_t;
typedef void *(*callback_t)(snd_stream_hnd_t, int, int *);
void *snd_stream_get_userdata(snd_stream_hnd_t);
void snd_stream_set_userdata(snd_stream_hnd_t, void *);
int snd_stream_poll(snd_stream_hnd_t);
void snd_stream_destroy(snd_stream_hnd_t);
snd_stream_hnd_t snd_stream_alloc(callback_t, int);
void snd_stream_start_adpcm(snd_stream_hnd_t, int, int);
void snd_stream_volume(snd_stream_hnd_t, int);
void *thd_create(int, void *(*)(void *), void *);
void thd_sleep(int);
size_t gzip_get_file_size(const char *);
gzFile gzopen(const char *, const char *);
int gzread(gzFile, void *, unsigned);
int gzclose(gzFile);
size_t FileSize(const char *);
file_t fs_open(const char *, int);
int fs_close(file_t);
int fs_read(file_t, void *, size_t);
Settings_t *GetSettings(void);
int GetVolumeFromSettings(void);
sfxhnd_t snd_sfx_load(const char *);
void snd_sfx_play(sfxhnd_t, int, int);
#endif
