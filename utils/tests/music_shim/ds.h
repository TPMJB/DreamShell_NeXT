#ifndef MUSIC_HOST_DS_H
#define MUSIC_HOST_DS_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NAME_MAX 256
#ifdef MUSIC_REAL_THREADS
#include <pthread.h>
typedef struct { pthread_t thread; } kthread_t;
#else
typedef struct { int live; } kthread_t;
#endif
kthread_t *thd_create(int,void *(*)(void *),void *);
int thd_join(kthread_t *,void **);
void thd_sleep(int);
void thd_pass(void);
uint64_t timer_ms_gettime64(void);
int GetVolumeFromSettings(void);
void ds_printf(const char *,...);
#endif
