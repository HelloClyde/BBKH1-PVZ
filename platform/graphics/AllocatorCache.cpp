/* Reclaim only completely unused original global allocation pools.
 * Pool identity, item size, grow policy and every live allocation stay intact.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "PvzpLib/PvzpList.h"
#include <cstddef>
#include <cstdint>
#include <limits>

/* Calls, pools trimmed, blocks freed, requested payload freed(saturated),
 * largest block freed, latest trim bytes, pools with live items skipped, spare. */
extern "C" { volatile uint32_t pvzh1_allocator_cache_diagnostic[8]; }
namespace {
bool trimming=false;
void Add(unsigned index,std::size_t bytes){
    const uint32_t previous=pvzh1_allocator_cache_diagnostic[index];
    pvzh1_allocator_cache_diagnostic[index]=bytes>UINT32_MAX-previous?UINT32_MAX:previous+(uint32_t)bytes;
}
}
extern "C" void pvzh1_global_allocator_trim_empty(void*){
    if(trimming)return;
    trimming=true;pvzh1_allocator_cache_diagnostic[0]=pvzh1_allocator_cache_diagnostic[0]+1;
    std::size_t released=0;
    /* Stable root-screen boundary only. mTotalItems counts every outstanding
     * allocation, including dead reanimations awaiting original deletion.
     * Empty PvzpLists may retain their allocator pointer; FreeAll preserves
     * that object and its item/grow settings so the next Alloc grows normally. */
    for(int i=0;i<gNumGlobalAllocators;++i){
        PvzpAllocator& pool=gGlobalAllocators[i];
        if(pool.mTotalItems){if(pool.mBlockList)pvzh1_allocator_cache_diagnostic[6]=pvzh1_allocator_cache_diagnostic[6]+1;continue;}
        if(!pool.mBlockList||pool.mItemSize<=0||pool.mGrowCount<=0)continue;
        const std::size_t bytes=(std::size_t)pool.mItemSize*pool.mGrowCount+sizeof(void*);
        std::size_t blocks=0;for(void *block=pool.mBlockList;block;block=*(void**)block)++blocks;
        pool.FreeAll();
        pvzh1_allocator_cache_diagnostic[1]=pvzh1_allocator_cache_diagnostic[1]+1;Add(2,blocks);Add(3,blocks*bytes);released+=blocks*bytes;
        if(bytes>pvzh1_allocator_cache_diagnostic[4])pvzh1_allocator_cache_diagnostic[4]=(uint32_t)bytes;
    }
    pvzh1_allocator_cache_diagnostic[5]=(uint32_t)released;trimming=false;
}
