/*-----------------------------------------------------------------------*/
/*  FatFs OS-abstraction overrides for kyblRTOS                          */
/*                                                                       */
/*  Replaces the stock ffsystem.c. Memory goes through FreeRTOS heap_4   */
/*  so 'mem' and heap-stats commands see all FS allocations.             */
/*  Mutex hooks are stubs because FF_FS_REENTRANT = 0 — kyblFS wraps    */
/*  every call with a single RTOS mutex, which is simpler and avoids    */
/*  a priority-inversion surface inside FatFs itself.                   */
/*-----------------------------------------------------------------------*/

#include "ff.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ── Memory ──────────────────────────────────────────────────────────── */
void *ff_memalloc(UINT msize) {
    return pvPortMalloc((size_t)msize);
}

void ff_memfree(void *mblock) {
    vPortFree(mblock);
}

/* ── Mutex (unused when FF_FS_REENTRANT = 0, but keep for linkage) ──── */
#if FF_FS_REENTRANT
int ff_mutex_create(int vol) {
    (void)vol;
    return 1;
}
void ff_mutex_delete(int vol) { (void)vol; }
int  ff_mutex_take  (int vol) { (void)vol; return 1; }
void ff_mutex_give  (int vol) { (void)vol; }
#endif
