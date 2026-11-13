/* Read-only observations of the pinned H1/Y100 V1.41 native allocator.
 * Kernel SHA256 d05786e442f9aad62a8d0a0cb4f6d786bdc7c2fa353a7a2b152c9ed9f01b40ef.
 * Audited allocator 800dc790/800dc998, free 800db980/800dbb54, init800dc91c:
 * descriptors descend from heapEnd, eight bytes {address, length|used_bit0}.
 * No locks, allocation, logging, or writes to firmware memory are performed.
 * The double header check detects changes; this is not an atomic OS snapshot.
 */
#include "runtime.h"

/* magic, status(1=checked,2=bad header,3=bad record,4=header changed),
 * user current, user peak, user request, native request,
 * heap base/end, current top, descriptor bottom/count,
 * total free bytes (holes+tail), largest free region, max native alloc request,
 * allocated raw bytes, free-record count, tail bytes, descriptor bytes,
 * free-record bytes, visited records. */
volatile uint32_t pvz_runtime_heap_failure[20];

#define HEAP_BASE_WORD   0x8057aff4u
#define HEAP_END_WORD    0x8057aff0u
#define HEAP_BOTTOM_WORD 0x8057aff8u
#define HEAP_COUNT_WORD  0x8057affcu
#define HEAP_TOP_WORD    0x8057b000u
#define HEAP_RECORD_LIMIT 65536u
#ifdef PVZ_HEAP_SNAPSHOT_TEST
extern uint32_t pvz_heap_snapshot_test_read32(uintptr_t address);
#define READ32(address) pvz_heap_snapshot_test_read32((uintptr_t)(address))
#else
#define READ32(address) (*(volatile const uint32_t *)(uintptr_t)(address))
#endif

void pvz_runtime_heap_snapshot_into(volatile uint32_t *target,size_t requested,size_t native_requested)
{
    uint32_t base=READ32(HEAP_BASE_WORD),end=READ32(HEAP_END_WORD);
    uint32_t bottom=READ32(HEAP_BOTTOM_WORD),count=READ32(HEAP_COUNT_WORD),top=READ32(HEAP_TOP_WORD);
    uint32_t cursor=base,holes=0,largest_hole=0,used=0,free_records=0,i,tail,largest;
    for(i=0;i<20;i++)target[i]=0;
    target[0]=0x48314853u;
    target[2]=pvz_runtime_diagnostic[3];
    target[3]=pvz_runtime_diagnostic[4];
    target[4]=(uint32_t)requested;
    target[5]=(uint32_t)native_requested;
    target[6]=base;target[7]=end;
    target[8]=top;target[9]=bottom;
    target[10]=count;
    /* Never derive a dereference from unchecked allocator metadata. */
    if(base<0x81000000u||end>0x83c00000u||base>=end||
       (base&3u)||(end&3u)||(bottom&3u)||(top&3u)||
       count>HEAP_RECORD_LIMIT||count>(end-base)/8u||
       bottom!=end-count*8u||top<base||top>bottom){
        target[1]=2;return;
    }
    target[17]=count*8u;
    for(i=0;i<count;i++){
        const uint32_t record=end-(i+1u)*8u;
        uint32_t address=READ32(record),flags=READ32(record+4u),size=flags&~1u;
        target[19]=i+1u;
        if(!size||(size&3u)||address!=cursor||size>top-cursor){
            target[1]=3;return;
        }
        cursor+=size;
        if(flags&1u)used+=size;
        else {holes+=size;++free_records;if(size>largest_hole)largest_hole=size;}
    }
    if(cursor!=top){target[1]=3;return;}
    tail=bottom-top;largest=tail>largest_hole?tail:largest_hole;
    target[11]=holes+tail;
    target[12]=largest;
    /* h1_alloc unconditionally requires one descriptor of tail headroom,
       even when reusing an exact-fit free record (800dc7c8..800dc7d0). */
    target[13]=tail<8u?0u:(largest_hole>tail-8u?largest_hole:tail-8u);
    target[14]=used;
    target[15]=free_records;
    target[16]=tail;
    target[18]=holes;
    if(base!=READ32(HEAP_BASE_WORD)||end!=READ32(HEAP_END_WORD)||
       bottom!=READ32(HEAP_BOTTOM_WORD)||count!=READ32(HEAP_COUNT_WORD)||top!=READ32(HEAP_TOP_WORD)){
        target[1]=4;return;
    }
    target[1]=1;
}

void pvz_runtime_capture_heap_failure(size_t requested,size_t native_requested)
{
    pvz_runtime_heap_snapshot_into(pvz_runtime_heap_failure,requested,native_requested);
}
