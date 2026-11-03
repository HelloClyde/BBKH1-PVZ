#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
volatile uint32_t pvz_runtime_diagnostic[16];
extern volatile uint32_t pvz_runtime_heap_failure[20];
extern void pvz_runtime_capture_heap_failure(size_t,size_t);
static uint32_t base,end,bottom,count,top,records[8][2],reads,top_reads;
static int change_header;
uint32_t pvz_heap_snapshot_test_read32(uintptr_t address)
{
    switch(address){
    case 0x8057aff4u:return base;case 0x8057aff0u:return end;
    case 0x8057aff8u:return bottom;case 0x8057affcu:return count;
    case 0x8057b000u:++top_reads;return top+((change_header&&top_reads>1)?4u:0u);
    default:{
        uint32_t distance=end-(uint32_t)address,index=(distance-1u)/8u;
        assert(address>=bottom&&address<end&&index<count&&index<8&&!(address&3u));
        ++reads;return records[index][(address&4u)?1:0];
    }}
}
static void setup(unsigned n,uint32_t tail)
{
    base=0x81c00000u;end=0x83c00000u;count=n;bottom=end-n*8u;top=bottom-tail;
    reads=top_reads=0;change_header=0;
    pvz_runtime_diagnostic[3]=17000000;pvz_runtime_diagnostic[4]=19000000;
}
static void record(unsigned index,uint32_t address,uint32_t bytes,int used)
{records[index][0]=address;records[index][1]=bytes|(used?1u:0u);}
static void snapshot(void){pvz_runtime_capture_heap_failure(1635204u,1635235u);}
int main(void)
{
    /* Large tail plus one interior free region. */
    setup(3,0);top=base+4096+2048+8192;
    record(0,base,4096,1);record(1,base+4096,2048,0);record(2,base+6144,8192,1);
    snapshot();assert(pvz_runtime_heap_failure[1]==1);
    assert(pvz_runtime_heap_failure[2]==17000000&&pvz_runtime_heap_failure[3]==19000000);
    assert(pvz_runtime_heap_failure[4]==1635204&&pvz_runtime_heap_failure[5]==1635235);
    assert(pvz_runtime_heap_failure[11]==bottom-top+2048);
    assert(pvz_runtime_heap_failure[12]==bottom-top&&pvz_runtime_heap_failure[13]==bottom-top-8);
    assert(pvz_runtime_heap_failure[14]==12288&&pvz_runtime_heap_failure[15]==1&&reads==6);
    /* More aggregate free memory than requested, but no sufficiently large hole. */
    setup(5,8);
    record(0,base,1048576,0);record(1,base+1048576,65536,1);
    record(2,base+1114112,1048576,0);record(3,base+2162688,65536,1);
    record(4,base+2228224,top-(base+2228224),1);
    snapshot();assert(pvz_runtime_heap_failure[1]==1);
    assert(pvz_runtime_heap_failure[11]==2097152+8&&pvz_runtime_heap_failure[13]==1048576);
    assert(pvz_runtime_heap_failure[13]<pvz_runtime_heap_failure[5]);
    /* Descriptor growth headroom is required even for an existing free block. */
    setup(2,0);record(0,base,2097152,0);record(1,base+2097152,top-base-2097152,1);
    snapshot();assert(pvz_runtime_heap_failure[1]==1&&pvz_runtime_heap_failure[11]==2097152);
    assert(pvz_runtime_heap_failure[12]==2097152&&pvz_runtime_heap_failure[13]==0);
    /* Invalid header is rejected before any descriptor dereference. */
    setup(2,8);bottom-=4;snapshot();assert(pvz_runtime_heap_failure[1]==2&&reads==0);
    setup(65537,8);snapshot();assert(pvz_runtime_heap_failure[1]==2&&reads==0);
    /* Broken record layout and a concurrently changed header are distinguished. */
    setup(1,8);record(0,base+4,top-base,1);snapshot();assert(pvz_runtime_heap_failure[1]==3);
    setup(1,8);record(0,base,top-base,1);change_header=1;snapshot();assert(pvz_runtime_heap_failure[1]==4);
    puts("heap snapshot passed: contiguous/free/fragmented/descriptor-limit/invalid/racing layouts");
    return 0;
}
