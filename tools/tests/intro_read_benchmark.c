/* Read-only native B-drive throughput, cooperative and bounded. */
#include "module_abi.h"
typedef void*(*Open)(const char*,const char*);
typedef unsigned(*Read)(void*,unsigned,unsigned,void*);
typedef int(*Close)(void*);
int h1_module_entry(const h1_exec_api* api,h1_exec_call* call){
 if(!api||!call||call->output_capacity<44||call->scratch_capacity<16384)return -1;
 const uint32_t table=api->runtime_table(0x83c00008u);
 if(table<0x80004000u||table>=0x83c00000u)return -2;
 Open open=(Open)(uintptr_t)api->runtime_entry(table,0);
 Read read=(Read)(uintptr_t)api->runtime_entry(table,8);
 Close close=(Close)(uintptr_t)api->runtime_entry(table,4);
 const char path[]="B:\\\xd3\xa6\xd3\xc3\\\xca\xfd\xbe\xdd\\\xd3\xce\xcf\xb7\\PVZ\\plant-poses.h1pp";
 uint32_t* out=(uint32_t*)(void*)call->output;for(unsigned i=0;i<11;++i)out[i]=0;
 out[0]=0x49524442;out[1]=api->tick_80hz();void* file=open(path,"rb");out[2]=api->tick_80hz();
 if(!file)return -3;
 while(out[4]<4u*1024u*1024u){
  if(!(out[4]%262144u)){uint32_t tick=api->tick_80hz();int cancelled=api->poll_cancel();out[9]+=api->tick_80hz()-tick;++out[10];if(cancelled){out[7]=1;break;}}
  unsigned n=read(call->scratch,1,16384,file);out[4]+=n;
  if(n!=16384){out[8]=1;break;}
 }
 out[3]=api->tick_80hz();out[5]=close(file);out[6]=api->tick_80hz();call->output_size=44;return 0;
}
