#ifndef MUSIC_HOST_MUTEX_H
#define MUSIC_HOST_MUTEX_H
#include <assert.h>
typedef int mutex_t;
#define MUTEX_INITIALIZER 0
static inline void mutex_lock(mutex_t *m) { assert(!*m); *m=1; }
static inline void mutex_unlock(mutex_t *m) { assert(*m); *m=0; }
#endif
