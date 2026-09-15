#ifndef MUSIC_HOST_DS_H
#define MUSIC_HOST_DS_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NAME_MAX 256
typedef struct { int live; } kthread_t;
kthread_t *thd_create(int,void *(*)(void *),void *);
int thd_join(kthread_t *,void **);
void thd_sleep(int);
int GetVolumeFromSettings(void);
void ds_printf(const char *,...);
#endif
