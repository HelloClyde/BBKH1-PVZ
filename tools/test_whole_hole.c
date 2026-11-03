#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern void *pvz_runtime_native_allocate(size_t,size_t);
extern volatile uint32_t pvz_runtime_hole_diagnostic[12];
static const uint32_t code[][2]={
 {0x800dc790u,0x27bdffd8u},{0x800dc7c8u,0x01491023u},{0x800dc7ccu,0x28420008u},
 {0x800dc7d0u,0x14400031u},{0x800dc8b0u,0xae020004u},{0x800db980u,0x27bdffe0u},
 {0x800db9f0u,0x8e020004u},{0x800db9fcu,0x00439024u},{0x800dba04u,0xae120004u},
 {0x800dc9a8u,0x8c84b004u},{0x800dc9b4u,0x0c003521u},{0x800dc9bcu,0x0c0371e4u},
 {0x800dc9ccu,0x0c003566u},{0x800dbb64u,0x8c84b004u},{0x800dbb70u,0x0c003521u},
 {0x800dbb78u,0x0c036e60u},{0x800dbb84u,0x0c003566u},{0x8000d484u,0x3c028058u},
 {0x8000d4e4u,0x24020003u},{0x8000d508u,0xae020004u},{0x8000d598u,0x27bdffe0u},
 {0x8000d5b8u,0x24020003u}};
enum { RECORDS=1024 };
static uint32_t base,end,bottom,count,top,record[RECORDS][2],sem;
static unsigned locks,unlocks,writes,locked,lock_error,bad_code,bad_table,bad_type;
static unsigned count_reads,change_count,record_reads;
static unsigned native_calls;

uint32_t pvz_whole_hole_test_read32(uintptr_t address)
{
 unsigned i;
 switch(address){
 case 0x83c00010u:return bad_table?0:0x802aac4cu;
 case 0x802aac54u:return 0x800dc998u;
 case 0x802aac58u:return 0x800dbb54u;
 case 0x8057b004u:return sem;
 case 0x80580000u:return bad_type?2:3;
 case 0x8057aff4u:assert(locked);return base;
 case 0x8057aff0u:assert(locked);return end;
 case 0x8057aff8u:assert(locked);return bottom;
 case 0x8057affcu:assert(locked);return count+((change_count&&++count_reads>1)?1:0);
 case 0x8057b000u:assert(locked);return top;
 default:break;
 }
 for(i=0;i<sizeof(code)/sizeof(code[0]);++i)
  if(address==code[i][0])return code[i][1]^(bad_code&&i==0?1u:0u);
 assert(locked&&address>=bottom&&address<end&&!(address&3u));
 i=(end-(uint32_t)address-1u)/8u;assert(i<count&&i<RECORDS);++record_reads;
 return record[i][(address&4u)?1:0];
}
void pvz_whole_hole_test_write32(uintptr_t address,uint32_t value)
{
 unsigned i=(end-(uint32_t)address-1u)/8u;
 assert(locked&&i<count&&i<RECORDS&&address==end-8u*(i+1u)+4u);
 assert(!(record[i][1]&1u)&&value==(record[i][1]|1u));
 ++writes;record[i][1]=value;
}
unsigned pvz_whole_hole_test_lock(uintptr_t semaphore)
{
 assert(semaphore==sem&&!locked);++locks;
 if(lock_error)return lock_error;
 locked=1;return 0;
}
void pvz_whole_hole_test_unlock(uintptr_t semaphore)
{assert(semaphore==sem&&locked);++unlocks;locked=0;}
/* Model of the pinned raw SDK allocation: unconditional 8B headroom, FIRST
 * fitting hole, optional split descriptor, otherwise bump plus descriptor. */
void *pvz_whole_hole_test_native_allocate(size_t requested)
{
 unsigned i,j;uint32_t need=((uint32_t)requested+3u)&~3u,result;
 assert(locked);++native_calls;
 if(bottom-top<8u)return 0;
 for(i=0;i<count;i++){
  uint32_t size=record[i][1];
  if((size&1u)||size<need)continue;
  result=record[i][0];record[i][1]=need|1u;
  if(size>need){
   assert(count<RECORDS);for(j=count;j>i+1;j--)memcpy(record[j],record[j-1],sizeof(record[j]));
   record[i+1][0]=result+need;record[i+1][1]=size-need;++count;bottom-=8;
  }
  return (void *)(uintptr_t)result;
 }
 if(bottom-top<need+8u)return 0;
 assert(count<RECORDS);result=top;
 record[count][0]=top;record[count][1]=need|1u;
 ++count;bottom-=8;top+=need;return (void *)(uintptr_t)result;
}
static void setup(unsigned n,unsigned tail)
{
 base=0x81c00000u;end=0x83c00000u;count=n;bottom=end-n*8u;top=bottom-tail;
 memset(record,0,sizeof(record));memset((void *)pvz_runtime_hole_diagnostic,0,48);
 sem=0x80580000u;locks=unlocks=writes=locked=lock_error=bad_code=bad_table=bad_type=0;
 count_reads=change_count=record_reads=native_calls=0;
}
static void part(unsigned i,uint32_t address,uint32_t bytes,int used)
{assert(i<RECORDS);record[i][0]=address;record[i][1]=bytes|(used?1u:0u);}
static void standard(void)
{
 setup(4,4);part(0,base,512,0);part(1,base+512,64,1);
 part(2,base+576,256,0);part(3,base+832,top-base-832,1);
}
static void valid_layout(void)
{
 uint32_t cursor=base;unsigned i;
 assert(bottom==end-count*8u&&top<=bottom);
 for(i=0;i<count;i++){assert(record[i][0]==cursor);cursor+=record[i][1]&~1u;}
 assert(cursor==top);
}
/* Model of the observed original free: address lookup, mark entire descriptor
 * free, merge adjacent holes, then remove free tail records and retreat top. */
static void original_free_model(uint32_t address)
{
 unsigned i,j;
 for(i=0;i<count&&record[i][0]!=address;++i){}
 assert(i<count);
 record[i][1]&=~1u;
 for(i=0;i+1<count;){
  if(!(record[i][1]&1u)&&!(record[i+1][1]&1u)){
   record[i][1]+=record[i+1][1];
   for(j=i+1;j+1<count;j++)memcpy(record[j],record[j+1],sizeof(record[j]));
   --count;
  }else ++i;
 }
 while(count&&!(record[count-1][1]&1u)){top=record[count-1][0];--count;}
 bottom=end-count*8u;valid_layout();
}
static void no_write(unsigned status)
{assert(!writes&&!locked&&pvz_runtime_hole_diagnostic[3]==status);}
int main(void)
{
 void *p;uint32_t before[RECORDS][2],oldtop,oldbottom,oldcount;unsigned trial;
 standard();memcpy(before,record,sizeof(record));oldtop=top;oldbottom=bottom;oldcount=count;
 p=pvz_runtime_native_allocate(108,139);
 assert((uintptr_t)p==base+576&&locks==1&&unlocks==1&&writes==1&&!locked);
 assert(top==oldtop&&bottom==oldbottom&&count==oldcount&&record[2][1]==257);
 assert(record[0][1]==before[0][1]&&record[1][1]==before[1][1]&&record[3][1]==before[3][1]);
 assert(pvz_runtime_hole_diagnostic[4]==108&&pvz_runtime_hole_diagnostic[5]==139);
 assert(pvz_runtime_hole_diagnostic[6]==140&&pvz_runtime_hole_diagnostic[7]==256);
 assert(pvz_runtime_hole_diagnostic[8]==117&&pvz_runtime_hole_diagnostic[9]==2);
 valid_layout();original_free_model((uint32_t)(uintptr_t)p);
 assert(!memcmp(record,before,sizeof(record))&&top==oldtop&&count==oldcount);

 /* Freeing whole spans can merge into neighbours and restore native tail
    headroom without inventing split descriptors. */
 standard();p=pvz_runtime_native_allocate(108,256);
 assert((uintptr_t)p==base+576&&pvz_runtime_hole_diagnostic[8]==0);
 original_free_model(base+512);assert(count==3&&bottom-top==12);
 original_free_model((uint32_t)(uintptr_t)p);assert(count==2&&record[0][1]==832);
 original_free_model(record[1][0]);assert(!count&&top==base&&bottom==end);

 standard();p=pvz_runtime_native_allocate(600,631);assert(!p);no_write(7);
 standard();top=bottom-8;record[3][1]=((top-base-832)|1u);
 p=pvz_runtime_native_allocate(108,139);assert(p&&!native_calls);assert(writes==1);
 standard();bad_code=1;p=pvz_runtime_native_allocate(108,139);assert(!p&&!locks);no_write(2);
 standard();bad_table=1;p=pvz_runtime_native_allocate(108,139);assert(!p&&!locks);no_write(2);
 standard();bad_type=1;p=pvz_runtime_native_allocate(108,139);assert(!p&&!locks);no_write(2);
 standard();sem=0;p=pvz_runtime_native_allocate(108,139);assert(!p&&!locks);no_write(2);
 standard();lock_error=2;p=pvz_runtime_native_allocate(108,139);assert(!p&&locks==1&&!unlocks);no_write(3);
 standard();bottom-=4;p=pvz_runtime_native_allocate(108,139);assert(!p&&!record_reads);no_write(4);
 standard();base+=4;p=pvz_runtime_native_allocate(108,139);assert(!p&&!record_reads);no_write(4);
 standard();count=65537;p=pvz_runtime_native_allocate(108,139);assert(!p&&!record_reads);no_write(4);
 setup(0,200000);p=pvz_runtime_native_allocate(108,139);assert(!p&&!native_calls);no_write(4);
 setup(1,200000);top=base;p=pvz_runtime_native_allocate(108,139);assert(!p&&!native_calls);no_write(4);
 setup(0,0);top=base;p=pvz_runtime_native_allocate(108,139);assert((uintptr_t)p==base&&native_calls==1);valid_layout();
 /* A malformed record AFTER the best fit must prevent the only heap write. */
 standard();record[3][0]+=4;p=pvz_runtime_native_allocate(108,139);assert(!p);no_write(5);
 standard();record[2][1]|=2;p=pvz_runtime_native_allocate(108,139);assert(!p);no_write(5);
 standard();change_count=1;p=pvz_runtime_native_allocate(108,139);assert(!p);no_write(8);
 standard();p=pvz_runtime_native_allocate(0,0);assert(!p&&!locks);no_write(9);
 standard();p=pvz_runtime_native_allocate(1,UINT32_MAX);assert(!p&&!locks);no_write(9);
 /* Ample tail: normal native allocation remains first choice. */
 setup(1,200000);part(0,base,top-base,1);
 p=pvz_runtime_native_allocate(108,139);assert(p&&native_calls==1&&!writes);
 assert(pvz_runtime_hole_diagnostic[3]==10&&bottom-top==200000-140-8);valid_layout();
 /* A small app allocation must not bump through the 64KiB reserve. */
 setup(1,65600);part(0,base,top-base,1);
 p=pvz_runtime_native_allocate(108,139);assert(!p&&!native_calls);no_write(7);
 assert(bottom-top==65600);
 /* SDK FIRST fit determines whether its split crosses the floor. */
 standard();top=bottom-65544;record[3][1]=(top-base-832)|1u;
 p=pvz_runtime_native_allocate(108,139);assert((uintptr_t)p==base&&native_calls==1&&!writes);
 assert(bottom-top==65536);valid_layout();
 standard();top=bottom-65540;record[3][1]=(top-base-832)|1u;
 p=pvz_runtime_native_allocate(108,139);assert((uintptr_t)p==base&&native_calls==1&&!writes);
 assert(bottom-top==65532);valid_layout();
 standard();top=bottom-8200;record[3][1]=(top-base-832)|1u;
 p=pvz_runtime_native_allocate(108,139);assert((uintptr_t)p==base&&native_calls==1&&!writes);
 assert(bottom-top==8192);valid_layout();
 standard();top=bottom-8196;record[3][1]=(top-base-832)|1u;
 p=pvz_runtime_native_allocate(108,139);assert((uintptr_t)p==base+576&&!native_calls&&writes==1);
 assert(bottom-top==8196);valid_layout();
 /* FIRST exact-fit needs no descriptor and may use ordinary SDK safely. */
 setup(2,65540);part(0,base,140,0);part(1,base+140,top-base-140,1);
 p=pvz_runtime_native_allocate(108,139);assert((uintptr_t)p==base&&native_calls==1&&!writes);
 assert(bottom-top==65540);valid_layout();
 /* Critical integration case: a large app request cannot consume the tail,
 * leaving room for firmware fopen's independent 522B native allocation. */
 setup(1,100000);part(0,base,top-base,1);
 p=pvz_runtime_native_allocate(200000,200031);assert(!p&&!native_calls);no_write(7);
 locked=1;p=pvz_whole_hole_test_native_allocate(522);locked=0;
 assert(p&&native_calls==1&&bottom-top==100000-524-8);valid_layout();
 standard();top=bottom-65540;record[3][1]=(top-base-832)|1u;
 p=pvz_runtime_native_allocate(108,139);assert(p&&native_calls==1);
 locked=1;p=pvz_whole_hole_test_native_allocate(522);locked=0;
 assert(p&&native_calls==2&&bottom-top==65540-8-524-8);valid_layout();
 /* Regression for Run15's 383 small allocations: split existing payload,
    consuming 3KiB of descriptors rather than swallowing large whole holes. */
 setup(2,65540);part(0,base,4u*1024u*1024u,0);
 part(1,base+4u*1024u*1024u,top-base-4u*1024u*1024u,1);
 for(trial=0;trial<383;trial++){
  p=pvz_runtime_native_allocate(108,139);
  assert((uintptr_t)p==base+trial*140u&&pvz_runtime_hole_diagnostic[7]==140);
 }
 assert(native_calls==383&&!writes&&!pvz_runtime_hole_diagnostic[2]);
 assert(!pvz_runtime_hole_diagnostic[8]&&bottom-top==65540-383*8);
 locked=1;p=pvz_whole_hole_test_native_allocate(522);locked=0;
 assert(p&&bottom-top==65540-384*8);valid_layout();
 /* Independent policy oracle over varying exact/split/no-fit sizes and all
    four important tail-floor edges; every resulting layout remains valid. */
 for(trial=0;trial<2000;trial++){
  static const unsigned tails[]={4,8,8188,8192,8196,8200,8204,65532,65536,65540,65544,65600,70000,200000};
  unsigned tail=tails[trial%14],left=((trial*73)%512+1)*4,right=((trial*41)%512+1)*4;
  unsigned raw=(trial*113)%3000+1,need=(raw+3)&~3u,first=0,best=0;
  int normal;
  setup(4,tail);part(0,base,left,0);part(1,base+left,64,1);
  part(2,base+left+64,right,0);part(3,base+left+64+right,top-base-left-64-right,1);
  if(left>=need)first=left;else if(right>=need)first=right;
  if(left>=need)best=left;
  if(right>=need&&(!best||right<best))best=right;
  normal=(tail>=65536+need+8)||(first&&((first==need&&tail>=8)||(first>need&&tail>=8200)));
  p=pvz_runtime_native_allocate(raw>31?raw-31:1,raw);
  if(normal){assert(p&&native_calls==1&&!writes&&pvz_runtime_hole_diagnostic[7]==need);}
  else if(best){assert(p&&!native_calls&&writes==1&&pvz_runtime_hole_diagnostic[7]==best);}
  else{assert(!p&&!native_calls&&!writes);}
  assert(!locked&&locks==1&&unlocks==1);valid_layout();
 }
 puts("whole-hole passed: best-fit/full-span/free-coalescing, guards, SDK first-fit/bump prediction, preserved descriptor headroom");
 return 0;
}
