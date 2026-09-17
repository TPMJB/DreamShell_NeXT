#ifndef MUSIC_HOST_MUTEX_H
#define MUSIC_HOST_MUTEX_H
#ifdef MUSIC_REAL_THREADS
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define mutex_trylock pthread_mutex_trylock
#else
#include <assert.h>
typedef int mutex_t;
#define MUTEX_INITIALIZER 0
static inline void mutex_lock(mutex_t *m) { assert(!*m); *m=1; }
static inline int mutex_trylock(mutex_t *m) { if(*m) return -1; *m=1; return 0; }
static inline void mutex_unlock(mutex_t *m) { assert(*m); *m=0; }
#endif
#endif
