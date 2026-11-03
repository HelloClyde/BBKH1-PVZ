#ifndef PVZ_RUNTIME_SMALL_POOL_H
#define PVZ_RUNTIME_SMALL_POOL_H

/* Application-only, nonmoving small raw allocations. No libc allocator calls.
 * SDK file handles and other native objects remain owned by the SDK. */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define PVZ_SMALL_POOL_MAX_RAW 4096u
#define PVZ_SMALL_POOL_CLASS_COUNT 56u
#define PVZ_SMALL_POOL_PAGE_BYTES 16384u

typedef struct PvzSmallPoolOwner PvzSmallPoolOwner;
/* A successful acquire must report the complete native reserved span, which
 * can exceed requested bytes under the native whole-hole policy. Accounting
 * callbacks charge/release this span ONCE per page, never once per slot. */
typedef void *(*PvzSmallPoolAcquire)(void *context,size_t requested,size_t *reserved);
typedef void (*PvzSmallPoolRelease)(void *context,void *native_memory,size_t reserved);
typedef struct PvzSmallPoolClass {
    PvzSmallPoolOwner *available,*empty;
} PvzSmallPoolClass;
typedef struct PvzSmallPoolStats {
    size_t pages,peak_pages,live_allocations,live_class_bytes;
    size_t requested_bytes,reserved_bytes,peak_reserved_bytes;
    size_t acquire_calls,release_calls,acquire_failures,allocations,reuses;
    size_t invalid_frees;
} PvzSmallPoolStats;
typedef struct PvzSmallPool {
    PvzSmallPoolClass classes[PVZ_SMALL_POOL_CLASS_COUNT];
    PvzSmallPoolAcquire acquire;
    PvzSmallPoolRelease release;
    void *context;
    PvzSmallPoolStats stats;
    unsigned initialized,busy;
} PvzSmallPool;

/* Initialize an unused/fully disposed object. Not thread safe: the application
 * serializes allocation calls. Callback reentry is rejected without mutation. */
void pvz_small_pool_init(PvzSmallPool *pool,PvzSmallPoolAcquire acquire,
    PvzSmallPoolRelease release,void *context);
/* Accepts 8/16/32-byte alignment. Unsupported sizes/alignments and refill OOM
 * return NULL with *owner=NULL, allowing the existing native fallback. A valid
 * owner is 32-byte aligned and remains stable through this live allocation. */
void *pvz_small_pool_allocate(PvzSmallPool *pool,size_t raw_size,size_t alignment,
    PvzSmallPoolOwner **owner);
/* Owner must be the handle returned for this live allocation. A page cannot
 * move; fully empty excess pages may be returned to native here. */
int pvz_small_pool_free(PvzSmallPool *pool,void *memory,PvzSmallPoolOwner *owner);
/* Release retained completely empty pages only; never touch a live pointer. */
size_t pvz_small_pool_trim(PvzSmallPool *pool);
/* Reject disposal with live allocations; otherwise return all retained pages. */
int pvz_small_pool_dispose(PvzSmallPool *pool);
void pvz_small_pool_get_stats(const PvzSmallPool *pool,PvzSmallPoolStats *stats);
/* Class capacity for an eligible request, zero when unsupported. */
size_t pvz_small_pool_capacity(size_t raw_size,size_t alignment);

#ifdef __cplusplus
}
#endif
#endif
