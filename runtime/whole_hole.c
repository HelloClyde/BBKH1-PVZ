/* V1.41's allocator rejects all requests if descriptor tail headroom is <8,
 * even when an existing free record fits. This guarded app allocation entry
 * keeps tail payload appends above 64KiB for firmware-internal allocations.
 * Existing-hole splits consume only an 8-byte descriptor and may use headroom
 * down to an 8KiB floor. Otherwise it takes ONE WHOLE fitting hole.
 * It never splits, moves descriptors, changes heap boundaries, or patches ROM.
 * The original firmware free() remains the owner of the complete native span.
 * This follows the SDK's task-context semaphore contract; IRQs stay enabled.
 * Pinned kernel SHA256:
 * d05786e442f9aad62a8d0a0cb4f6d786bdc7c2fa353a7a2b152c9ed9f01b40ef.
 */
#include <stddef.h>
#include <stdint.h>

#define HEAP_BASE_WORD   0x8057aff4u
#define HEAP_END_WORD    0x8057aff0u
#define HEAP_BOTTOM_WORD 0x8057aff8u
#define HEAP_COUNT_WORD  0x8057affcu
#define HEAP_TOP_WORD    0x8057b000u
#define HEAP_SEM_WORD    0x8057b004u
#define NATIVE_HEAP_BASE 0x81c00000u
#define NATIVE_HEAP_END  0x83c00000u
#define RECORD_LIMIT     65536u
#define SDK_PAYLOAD_RESERVE 65536u
#define SDK_DESCRIPTOR_FLOOR 8192u

/* magic, attempts, successes, last status, user request, raw request,
 * rounded raw request, ACTUAL full native span, cumulative span-minus-raw
 * waste (saturated), selected record index, semaphore error, tail bytes.
 * Successes counts whole-hole reuse only. Status: 1 whole-hole success,
 * 2 ABI guard, 3 semaphore, 4 heap header, 5 records, 7 no fit,
 * 8 changed metadata, 9 request, 10 ordinary SDK success, 11 SDK failure. */
volatile uint32_t pvz_runtime_hole_diagnostic[12];

#ifdef PVZ_WHOLE_HOLE_TEST
extern uint32_t pvz_whole_hole_test_read32(uintptr_t);
extern void pvz_whole_hole_test_write32(uintptr_t,uint32_t);
extern unsigned pvz_whole_hole_test_lock(uintptr_t);
extern void pvz_whole_hole_test_unlock(uintptr_t);
extern void *pvz_whole_hole_test_native_allocate(size_t);
#define READ32(a) pvz_whole_hole_test_read32((uintptr_t)(a))
#define WRITE32(a,v) pvz_whole_hole_test_write32((uintptr_t)(a),(v))
#else
#define READ32(a) (*(volatile const uint32_t *)(uintptr_t)(a))
#define WRITE32(a,v) (*(volatile uint32_t *)(uintptr_t)(a)=(v))
#endif

static int compatible_firmware(void)
{
    static const uint32_t words[][2]={
        {0x800dc790u,0x27bdffd8u}, /* raw allocator */
        {0x800dc7c8u,0x01491023u}, {0x800dc7ccu,0x28420008u},
        {0x800dc7d0u,0x14400031u}, {0x800dc8b0u,0xae020004u},
        {0x800db980u,0x27bdffe0u}, /* raw free reads descriptor length */
        {0x800db9f0u,0x8e020004u}, {0x800db9fcu,0x00439024u},
        {0x800dba04u,0xae120004u},
        {0x800dc9a8u,0x8c84b004u}, /* malloc wrapper, same semaphore */
        {0x800dc9b4u,0x0c003521u}, {0x800dc9bcu,0x0c0371e4u},
        {0x800dc9ccu,0x0c003566u},
        {0x800dbb64u,0x8c84b004u}, /* free wrapper */
        {0x800dbb70u,0x0c003521u}, {0x800dbb78u,0x0c036e60u},
        {0x800dbb84u,0x0c003566u},
        {0x8000d484u,0x3c028058u}, /* OSSemPend rejects ISR/invalid event */
        {0x8000d4e4u,0x24020003u}, {0x8000d508u,0xae020004u},
        {0x8000d598u,0x27bdffe0u}, {0x8000d5b8u,0x24020003u}
    };
    uint32_t table=READ32(0x83c00010u);
    unsigned int i;
    if((table&3u)||table<0x80004000u||table>0x83bffff0u)return 0;
    if(READ32(table+8u)!=0x800dc998u||READ32(table+12u)!=0x800dbb54u)return 0;
    for(i=0;i<sizeof(words)/sizeof(words[0]);++i)
        if(READ32(words[i][0])!=words[i][1])return 0;
    return 1;
}
static unsigned heap_lock(uintptr_t semaphore)
{
#ifdef PVZ_WHOLE_HOLE_TEST
    return pvz_whole_hole_test_lock(semaphore);
#else
    typedef void (*Pend)(void *,unsigned,unsigned char *);
    unsigned char error=255;
    ((Pend)0x8000d484u)((void *)semaphore,0,&error);
    return error;
#endif
}
static void heap_unlock(uintptr_t semaphore)
{
#ifdef PVZ_WHOLE_HOLE_TEST
    pvz_whole_hole_test_unlock(semaphore);
#else
    typedef unsigned (*Post)(void *);
    (void)((Post)0x8000d598u)((void *)semaphore);
#endif
}
static void *ordinary_allocate(size_t requested)
{
#ifdef PVZ_WHOLE_HOLE_TEST
    return pvz_whole_hole_test_native_allocate(requested);
#else
    /* The semaphore is ALREADY held, exactly as in SDK wrapper 800dc998. */
    typedef void *(*Allocate)(unsigned);
    return ((Allocate)0x800dc790u)((unsigned)requested);
#endif
}

void *pvz_runtime_native_allocate(size_t requested,size_t native_requested)
{
    uint32_t base,end,bottom,count,top,cursor,i,need,selected=0,selected_address=0,span=0,index=0;
    uint32_t first_span=0,tail;
    uintptr_t semaphore;
    unsigned error;
    void *result=0;
    pvz_runtime_hole_diagnostic[0]=0x48314846u; /* H1HF */
    ++pvz_runtime_hole_diagnostic[1];
    pvz_runtime_hole_diagnostic[3]=9;
    pvz_runtime_hole_diagnostic[4]=(uint32_t)requested;
    pvz_runtime_hole_diagnostic[5]=(uint32_t)native_requested;
    pvz_runtime_hole_diagnostic[6]=0;pvz_runtime_hole_diagnostic[7]=0;
    pvz_runtime_hole_diagnostic[9]=UINT32_MAX;
    pvz_runtime_hole_diagnostic[10]=0;pvz_runtime_hole_diagnostic[11]=0;
    if(!native_requested||native_requested>UINT32_MAX-3u||
       native_requested>NATIVE_HEAP_END-NATIVE_HEAP_BASE)return 0;
    need=((uint32_t)native_requested+3u)&~3u;
    pvz_runtime_hole_diagnostic[6]=need;
    pvz_runtime_hole_diagnostic[3]=2;
    if(!compatible_firmware())return 0;
    semaphore=(uintptr_t)READ32(HEAP_SEM_WORD);
    if((semaphore&3u)||semaphore<0x80004000u||semaphore>0x81bffff0u)return 0;
    /* OS_EVENT type 3 is the semaphore type checked by the original Pend. */
    if((READ32(semaphore)&255u)!=3u)return 0;
    error=heap_lock(semaphore);
    pvz_runtime_hole_diagnostic[10]=error;
    if(error){pvz_runtime_hole_diagnostic[3]=3;return 0;}

    base=READ32(HEAP_BASE_WORD);end=READ32(HEAP_END_WORD);
    bottom=READ32(HEAP_BOTTOM_WORD);count=READ32(HEAP_COUNT_WORD);top=READ32(HEAP_TOP_WORD);
    pvz_runtime_hole_diagnostic[3]=4;
    if(base!=NATIVE_HEAP_BASE||end!=NATIVE_HEAP_END||
       (bottom&3u)||(top&3u)||count>RECORD_LIMIT||
       bottom!=end-count*8u||top<base||top>bottom||
       (!count&&top!=base)||(count&&top-base<count*4u))goto done;
    tail=bottom-top;pvz_runtime_hole_diagnostic[11]=tail;
    /* If even a worst-case tail append leaves the reserve, ordinary SDK
       allocation is safe without an additional full descriptor scan. */
    if(tail>SDK_PAYLOAD_RESERVE&&need<=tail-SDK_PAYLOAD_RESERVE&&
       tail-SDK_PAYLOAD_RESERVE-need>=8u)goto ordinary;

    cursor=base;pvz_runtime_hole_diagnostic[3]=5;
    for(i=0;i<count;++i){
        uint32_t record=end-(i+1u)*8u;
        uint32_t address=READ32(record),flags=READ32(record+4u),size=flags&~1u;
        if(!size||(size&3u)||address!=cursor||size>top-cursor)goto done;
        if(!(flags&1u)&&size>=need&&!first_span)first_span=size;
        if(!(flags&1u)&&size>=need&&(!selected||size<span)){
            selected=record;selected_address=address;span=size;index=i;
        }
        cursor+=size;
    }
    if(cursor!=top)goto done;
    /* The SDK uses FIRST fit, not best fit. An exact fit needs no descriptor;
       splitting the first fitting hole consumes eight bytes of tail. */
    if(first_span&&((first_span==need&&tail>=8u)||
       (first_span>need&&tail>=SDK_DESCRIPTOR_FLOOR+8u)))goto ordinary;
    if(!selected){pvz_runtime_hole_diagnostic[3]=7;goto done;}
    /* Full scan completed before ANY heap write, including records following
       the chosen best fit. The same native semaphore serializes alloc/free. */
    pvz_runtime_hole_diagnostic[3]=8;
    if(base!=READ32(HEAP_BASE_WORD)||end!=READ32(HEAP_END_WORD)||
       bottom!=READ32(HEAP_BOTTOM_WORD)||count!=READ32(HEAP_COUNT_WORD)||
       top!=READ32(HEAP_TOP_WORD)||semaphore!=READ32(HEAP_SEM_WORD)||
       span!=READ32(selected+4u)||selected_address!=READ32(selected))goto done;
    result=(void *)(uintptr_t)selected_address;
    WRITE32(selected+4u,span|1u); /* Only heap mutation: take the WHOLE hole. */
    pvz_runtime_hole_diagnostic[3]=1;++pvz_runtime_hole_diagnostic[2];
    pvz_runtime_hole_diagnostic[7]=span;pvz_runtime_hole_diagnostic[9]=index;
    {
        uint32_t wasted=span-(uint32_t)native_requested;
        uint32_t before=pvz_runtime_hole_diagnostic[8];
        pvz_runtime_hole_diagnostic[8]=wasted>UINT32_MAX-before?UINT32_MAX:before+wasted;
    }
    goto done;
ordinary:
    pvz_runtime_hole_diagnostic[3]=8;
    if(base!=READ32(HEAP_BASE_WORD)||end!=READ32(HEAP_END_WORD)||
       bottom!=READ32(HEAP_BOTTOM_WORD)||count!=READ32(HEAP_COUNT_WORD)||
       top!=READ32(HEAP_TOP_WORD)||semaphore!=READ32(HEAP_SEM_WORD))goto done;
    result=ordinary_allocate(native_requested);
    if(result&&(uintptr_t)result!=UINT32_MAX){
        pvz_runtime_hole_diagnostic[3]=10;
        pvz_runtime_hole_diagnostic[7]=need;
    }else{
        pvz_runtime_hole_diagnostic[3]=11;
        result=0;
    }
done:
    heap_unlock(semaphore);
    return result;
}
