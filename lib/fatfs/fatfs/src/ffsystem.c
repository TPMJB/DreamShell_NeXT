/* DreamShell FatFs allocation hooks. VFS locking is in dc.c. */
#include <stdlib.h>
#include "ff.h"
void *ff_memalloc(UINT size) { return malloc(size); }
void ff_memfree(void *ptr) { free(ptr); }
