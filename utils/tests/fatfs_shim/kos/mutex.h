#ifndef FAT_TEST_MUTEX_H
#define FAT_TEST_MUTEX_H
#include <pthread.h>
typedef pthread_mutex_t mutex_t;
#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
static inline void fat_test_unlock(mutex_t **m) { pthread_mutex_unlock(*m); }
#define mutex_lock_scoped(m) mutex_t *guard __attribute__((cleanup(fat_test_unlock))) = (m); pthread_mutex_lock(guard)
#endif
