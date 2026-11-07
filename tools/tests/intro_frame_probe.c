/* Standalone owned-memory test of the exact runtime frame decoder. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "module_abi.h"
#include "h1_memory.h"
void* memcpy(void* d,const void* s,size_t n){return h1_memcpy(d,s,n);}
void* memset(void* d,int c,size_t n){return h1_memset(d,c,n);}
#include "../../platform/video/IntroFrames.c"
static unsigned word(const uint8_t* p){return p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static uint32_t hash(const uint8_t* p,unsigned n){uint32_t h=2166136261u;for(unsigned i=0;i<n;++i)h=(h^p[i])*16777619u;return h;}
int h1_module_entry(const h1_exec_api* api,h1_exec_call* call){
 if(!api||!call||call->output_capacity<64)return -1;
 uint32_t* o=(uint32_t*)(void*)call->output;memset(o,0,64);o[0]=0x49465232;call->output_size=64;
 uint32_t mt=api->runtime_table(0x83c00010u),ft=api->runtime_table(0x83c00008u);
 typedef void*(*Alloc)(unsigned);typedef void(*Free)(void*);typedef void*(*Open)(const char*,const char*);typedef unsigned(*Read)(void*,unsigned,unsigned,void*);typedef int(*Close)(void*);
 Alloc alloc=(Alloc)(uintptr_t)api->runtime_entry(mt,8);Free release=(Free)(uintptr_t)api->runtime_entry(mt,12);Open open=(Open)(uintptr_t)api->runtime_entry(ft,0);Read read=(Read)(uintptr_t)api->runtime_entry(ft,8);Close close=(Close)(uintptr_t)api->runtime_entry(ft,4);
 uint8_t* arena=alloc(4u*1024u*1024u);if(!arena)return -2;
 uint8_t* input=arena;uint16_t* seed=(uint16_t*)(arena+2*1024*1024);uint16_t* before=seed+480*272;uint16_t* current=before+480*272;uint16_t* delta=current+480*272;uint32_t* argb=(uint32_t*)(delta+480*272);
 const char path[]="B:\\\xd3\xa6\xd3\xc3\\\xca\xfd\xbe\xdd\\\xd3\xce\xcf\xb7\\PVZ\\intro-frames-benchmark.h1ib";
 void* file=open(path,"rb");if(!file){release(arena);return -3;}o[6]=read(input,1,2*1024*1024,file);close(file);
 if(o[6]<24||word(input)!=0x42493148||word(input+4)!=2||word(input+12)!=4){release(arena);return -4;}
 unsigned seedBytes=word(input+8);if(seedBytes>o[6]-24||!H1IntroUnpack(input+24,seedBytes,seed)){release(arena);return -5;}o[12]=word(input+16);o[11]=hash((uint8_t*)seed,480*272*2);
 o[3]=api->tick_80hz();
 for(unsigned epoch=0;epoch<32;++epoch){uint32_t t=api->tick_80hz();int cancelled=api->poll_cancel();o[5]+=api->tick_80hz()-t;if(cancelled){o[9]=1;break;}
  t=api->tick_80hz();memcpy(before,seed,480*272*2);o[13]+=api->tick_80hz()-t;unsigned at=24+seedBytes;
  for(unsigned i=0;i<4;++i){if(at+8>o[6]){o[9]=2;break;}int shift=(int32_t)word(input+at);unsigned n=word(input+at+4);at+=8;
   t=api->tick_80hz();if(shift< -3||shift>3||n>o[6]-at||!H1IntroUnpack(input+at,n,delta)){o[9]=3;break;}o[14]+=api->tick_80hz()-t;at+=n;
   t=api->tick_80hz();H1IntroReconstructExpand(shift,delta,before,current,argb);o[15]+=api->tick_80hz()-t;uint16_t* swap=before;before=current;current=swap;++o[2];
  }if(o[9])break;
 }
 o[4]=api->tick_80hz();o[8]=hash((uint8_t*)argb,480*272*4);o[7]=(unsigned)((uint8_t*)(argb+480*272)-arena);release(arena);return o[9]?-6:0;
}
