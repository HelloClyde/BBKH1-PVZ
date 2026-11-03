/* Small application allocation slabs; native sees pages rather than every
 * STL/XML/font node. Metadata, free links and ownership bits live in the page.
 * Live allocations never move. No calls to malloc/free, no kernel mutation. */
#include "small_pool.h"
#include <string.h>

#define POOL_MAGIC UINT32_C(0x50535a48)
#define PAGE_MAGIC UINT32_C(0x50475a48)
#define PAGE_ALIGNMENT 32u
#define MIN_SLOTS 8u

struct PvzSmallPoolOwner {
    uint32_t magic;
    PvzSmallPool *pool;
    PvzSmallPoolOwner *previous,*next;
    void *native_memory,*free_head;
    unsigned char *slots;
    size_t requested,reserved,stride;
    unsigned class_index,slot_count,live_count;
};

static size_t round_up(size_t size,size_t alignment)
{
    return (size+alignment-1u)&~(alignment-1u);
}
static int class_for(size_t size,size_t alignment,unsigned *index,size_t *stride)
{
    if(alignment!=8u&&alignment!=16u&&alignment!=32u)return 0;
    if(size>PVZ_SMALL_POOL_MAX_RAW)return 0;
    if(!size)size=1u;
    size=round_up(size,alignment);
    if(size<=128u){*stride=round_up(size,8u);*index=(unsigned)(*stride/8u-1u);}
    else if(size<=1024u){*stride=round_up(size,32u);*index=16u+(unsigned)((*stride-160u)/32u);}
    else {*stride=round_up(size,256u);*index=44u+(unsigned)((*stride-1280u)/256u);}
    return 1;
}
size_t pvz_small_pool_capacity(size_t size,size_t alignment)
{
    unsigned index;size_t stride;
    return class_for(size,alignment,&index,&stride)?stride:0;
}
void pvz_small_pool_init(PvzSmallPool *pool,PvzSmallPoolAcquire acquire,
    PvzSmallPoolRelease release,void *context)
{
    if(!pool)return;
    memset(pool,0,sizeof(*pool));pool->acquire=acquire;pool->release=release;
    pool->context=context;pool->initialized=POOL_MAGIC;
}
static void unlink_available(PvzSmallPool *pool,PvzSmallPoolOwner *page)
{
    PvzSmallPoolClass *group=&pool->classes[page->class_index];
    if(page->previous)page->previous->next=page->next;
    else group->available=page->next;
    if(page->next)page->next->previous=page->previous;
    page->previous=page->next=0;
}
static void link_available(PvzSmallPool *pool,PvzSmallPoolOwner *page)
{
    PvzSmallPoolClass *group=&pool->classes[page->class_index];
    page->previous=0;page->next=group->available;
    if(page->next)page->next->previous=page;
    group->available=page;
}
static unsigned char *ownership_bits(PvzSmallPoolOwner *page)
{
    return (unsigned char *)page+sizeof(*page);
}
static void release_page(PvzSmallPool *pool,PvzSmallPoolOwner *page)
{
    void *memory=page->native_memory;size_t reserved=page->reserved;
    PvzSmallPoolClass *group=&pool->classes[page->class_index];
    unlink_available(pool,page);if(group->empty==page)group->empty=0;
    --pool->stats.pages;pool->stats.requested_bytes-=page->requested;
    pool->stats.reserved_bytes-=reserved;++pool->stats.release_calls;
    page->magic=0;pool->release(pool->context,memory,reserved);
}
static PvzSmallPoolOwner *new_page(PvzSmallPool *pool,unsigned index,size_t stride)
{
    size_t bytes=PVZ_SMALL_POOL_PAGE_BYTES,reserved=0,maximum,header,slots,i;
    void *memory;uintptr_t address;PvzSmallPoolOwner *page;unsigned char *bits;
    /* Reserve a bitmap for the upper bound of slot count before final rounding.
     * 31 bytes covers a native raw base aligned to only four bytes. */
    maximum=(bytes-sizeof(*page))/stride;
    header=round_up(sizeof(*page)+(maximum+7u)/8u,PAGE_ALIGNMENT);
    if(bytes<header+PAGE_ALIGNMENT-1u+MIN_SLOTS*stride){
        maximum=MIN_SLOTS;header=round_up(sizeof(*page)+(maximum+7u)/8u,PAGE_ALIGNMENT);
        bytes=header+PAGE_ALIGNMENT-1u+MIN_SLOTS*stride;
    }
    ++pool->stats.acquire_calls;memory=pool->acquire(pool->context,bytes,&reserved);
    address=(uintptr_t)memory;
    if(!memory||address==UINT32_MAX||address==UINTPTR_MAX){++pool->stats.acquire_failures;return 0;}
    if(reserved<bytes||address>UINTPTR_MAX-(PAGE_ALIGNMENT-1u)){
        ++pool->stats.acquire_failures;++pool->stats.release_calls;
        pool->release(pool->context,memory,reserved);return 0;
    }
    address=(address+PAGE_ALIGNMENT-1u)&~(uintptr_t)(PAGE_ALIGNMENT-1u);
    slots=(bytes-(address-(uintptr_t)memory)-header)/stride;
    page=(PvzSmallPoolOwner *)address;memset(page,0,sizeof(*page));
    page->magic=PAGE_MAGIC;page->pool=pool;page->native_memory=memory;
    page->requested=bytes;page->reserved=reserved;page->stride=stride;
    page->class_index=index;page->slot_count=(unsigned)slots;
    page->slots=(unsigned char *)page+header;
    bits=ownership_bits(page);memset(bits,0,(maximum+7u)/8u);
    for(i=slots;i;i--){
        void *slot=page->slots+(i-1u)*stride;*(void **)slot=page->free_head;page->free_head=slot;
    }
    link_available(pool,page);++pool->stats.pages;
    if(pool->stats.pages>pool->stats.peak_pages)pool->stats.peak_pages=pool->stats.pages;
    pool->stats.requested_bytes+=bytes;pool->stats.reserved_bytes+=reserved;
    if(pool->stats.reserved_bytes>pool->stats.peak_reserved_bytes)
        pool->stats.peak_reserved_bytes=pool->stats.reserved_bytes;
    return page;
}
void *pvz_small_pool_allocate(PvzSmallPool *pool,size_t size,size_t alignment,PvzSmallPoolOwner **owner)
{
    unsigned index,slot;size_t stride;PvzSmallPoolOwner *page;void *memory;unsigned char *bits;
    if(owner)*owner=0;
    if(!owner||!pool||pool->initialized!=POOL_MAGIC||pool->busy||!pool->acquire||!pool->release||
       !class_for(size,alignment,&index,&stride))return 0;
    pool->busy=1;page=pool->classes[index].available;
    if(page)++pool->stats.reuses;
    else page=new_page(pool,index,stride);
    if(!page){pool->busy=0;return 0;}
    if(pool->classes[index].empty==page)pool->classes[index].empty=0;
    memory=page->free_head;page->free_head=*(void **)memory;
    slot=(unsigned)(((unsigned char *)memory-page->slots)/stride);
    bits=ownership_bits(page);bits[slot>>3]|=(unsigned char)(1u<<(slot&7u));
    ++page->live_count;if(!page->free_head)unlink_available(pool,page);
    ++pool->stats.live_allocations;pool->stats.live_class_bytes+=stride;++pool->stats.allocations;
    *owner=page;pool->busy=0;return memory;
}
int pvz_small_pool_free(PvzSmallPool *pool,void *memory,PvzSmallPoolOwner *page)
{
    uintptr_t offset;unsigned slot,mask;unsigned char *bits;int was_full;
    if(!pool||pool->initialized!=POOL_MAGIC||pool->busy||!page||!memory)return 0;
    if(page->magic!=PAGE_MAGIC||page->pool!=pool||page->class_index>=PVZ_SMALL_POOL_CLASS_COUNT||
       (uintptr_t)memory<(uintptr_t)page->slots){++pool->stats.invalid_frees;return 0;}
    offset=(uintptr_t)memory-(uintptr_t)page->slots;
    if(offset/page->stride>=page->slot_count||offset%page->stride){++pool->stats.invalid_frees;return 0;}
    slot=(unsigned)(offset/page->stride);bits=ownership_bits(page);mask=1u<<(slot&7u);
    if(!(bits[slot>>3]&mask)){++pool->stats.invalid_frees;return 0;}
    pool->busy=1;bits[slot>>3]&=(unsigned char)~mask;was_full=!page->free_head;
    *(void **)memory=page->free_head;page->free_head=memory;
    --page->live_count;--pool->stats.live_allocations;pool->stats.live_class_bytes-=page->stride;
    if(was_full)link_available(pool,page);
    if(!page->live_count){
        if(pool->classes[page->class_index].empty)release_page(pool,page);
        else pool->classes[page->class_index].empty=page;
    }
    pool->busy=0;return 1;
}
size_t pvz_small_pool_trim(PvzSmallPool *pool)
{
    size_t released=0;unsigned i;
    if(!pool||pool->initialized!=POOL_MAGIC||pool->busy)return 0;
    pool->busy=1;
    for(i=0;i<PVZ_SMALL_POOL_CLASS_COUNT;++i){
        PvzSmallPoolOwner *page=pool->classes[i].empty;
        if(page){released+=page->reserved;release_page(pool,page);}
    }
    pool->busy=0;return released;
}
int pvz_small_pool_dispose(PvzSmallPool *pool)
{
    if(!pool||pool->initialized!=POOL_MAGIC||pool->busy||pool->stats.live_allocations)return 0;
    pvz_small_pool_trim(pool);pool->initialized=0;return 1;
}
void pvz_small_pool_get_stats(const PvzSmallPool *pool,PvzSmallPoolStats *stats)
{
    if(stats){if(pool)*stats=pool->stats;else memset(stats,0,sizeof(*stats));}
}
