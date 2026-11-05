#include "module_abi.h"
static const char path[]="B:\\" "\xd3\xa6\xd3\xc3" "\\" "\xca\xfd\xbe\xdd" "\\" "\xd3\xce\xcf\xb7" "\\PVZ\\data\\font-tiles.h1ft";
static const uint32_t table_crc[16]={0x0u,0x1db71064u,0x3b6e20c8u,0x26d930acu,0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu};

int h1_module_entry(const h1_exec_api*api,h1_exec_call*call){
 if(!api||!call||call->arg_size||call->output_capacity<40||call->scratch_capacity<1024)return -1;
 uint32_t table=api->runtime_table(0x83c00008u);
 void*(*open)(const char*,const char*)=(void*)api->runtime_entry(table,0);
 int(*close)(void*)=(void*)api->runtime_entry(table,4);
 unsigned(*read)(void*,unsigned,unsigned,void*)=(void*)api->runtime_entry(table,8);
 uint32_t*out=(void*)call->output;for(unsigned i=0;i<10;++i)out[i]=0;out[0]=0x43525056;
 void*f=open(path,"rb");if(!f||(uintptr_t)f==0xffffffffu){call->output_size=40;return -2;}
 uint32_t crc=0xffffffffu,bytes=0,begin=api->tick_80hz();unsigned n;
 while((n=read(call->scratch,1,call->scratch_capacity,f))){
  for(unsigned i=0;i<n;++i){crc^=call->scratch[i];crc=(crc>>4)^table_crc[crc&15];crc=(crc>>4)^table_crc[crc&15];}
  bytes+=n;if(api->poll_cancel()){out[9]=1;break;}
 }
 out[1]=bytes;out[2]=crc^0xffffffffu;out[3]=api->tick_80hz()-begin;out[4]=close(f);call->output_size=40;return out[9]?-3:0;
}
