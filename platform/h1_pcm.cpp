/* LGPL-3.0-or-later. Verified H1 V1.41 persistent PCM ring; no Linux driver. */
#include "h1_host.h"
#include "h1_sdk.h"
#include <string.h>
#define RING_SAMPLES 8192u
struct PcmDescriptor{int16_t *pcm;uint32_t bytes,private_words[6];};
static PcmDescriptor descriptor __attribute__((aligned(32)));
static int16_t ring[RING_SAMPLES] __attribute__((aligned(32)));
typedef int(*DescFn)(PcmDescriptor*);typedef int(*DeviceFn)(void);typedef int(*SubmitFn)(int,PcmDescriptor*,int,int);
static DescFn init_desc,destroy_desc;static DeviceFn start_device,stop_device,close_device;static SubmitFn submit;
static volatile uint32_t *queue;
static unsigned used,read_position,queued;static int attempted,active,playing,initialized;
static void *service(unsigned off){return h1_runtime_entry(h1_runtime_table(H1_RUNTIME_SYS_TABLE_SLOT),off);}
static void reset_ring(){submit(0,nullptr,0,0);used=read_position=queued=0;playing=0;memset(ring,0,sizeof(ring));}
static void retire(){
    if(!playing)return;volatile uint32_t *node=(volatile uint32_t*)queue[0];uint32_t a=(uint32_t)node;
    if(!node||(a&3)||a<0x80000000u||a>=0x84000000u||node[1]!=(uint32_t)&descriptor){reset_ring();return;}
    uint32_t cursor=node[2],base=(uint32_t)ring;
    if(cursor<base||cursor>base+sizeof(ring)||(cursor&1)){reset_ring();return;}
    unsigned position=((cursor-base)/2)%RING_SAMPLES,consumed=(position+RING_SAMPLES-read_position)%RING_SAMPLES;
    if(consumed>queued){pvzp_diagnostic[18]++;reset_ring();return;}
    for(unsigned i=0;i<consumed;i++)ring[(read_position+i)%RING_SAMPLES]=0;
    read_position=position;queued-=consumed;
}
extern "C" int pvzp_pcm_open(){
    if(attempted)return active;attempted=1;
    init_desc=(DescFn)service(0x50);destroy_desc=(DescFn)service(0x54);
    submit=(SubmitFn)service(0x5c);start_device=(DeviceFn)service(0x60);
    stop_device=(DeviceFn)service(0x64);close_device=(DeviceFn)service(0x68);
    const uint32_t *code=(const uint32_t*)submit;
    if(!code||(code[0x70/4]&0xffff0000u)!=0x3c060000u||(code[0x74/4]&0xffff0000u)!=0x24c60000u||code[0x78/4]!=0x00c43021u)return 0;
    uint32_t q=((code[0x70/4]&65535u)<<16)+(int16_t)code[0x74/4];
    if((q&3)||q<0x80000000u||q>=0x84000000u)return 0;queue=(volatile uint32_t*)q;
    memset(&descriptor,0,sizeof(descriptor));memset(ring,0,sizeof(ring));descriptor.pcm=ring;descriptor.bytes=sizeof(ring);
    if(!init_desc(&descriptor))return 0;initialized=1;
    uint32_t config[9]={32000,1,4096,0,0,0,0,0,0};typedef int(*OpenFn)(uint32_t*);
    if(!((OpenFn)service(0x58))(config)){destroy_desc(&descriptor);initialized=0;return 0;}
    active=1;if(!start_device()){pvzp_audio_close();return 0;}
    pvzp_diagnostic[16]=1;return 1;
}
extern "C" unsigned pvzp_pcm_writable(){if(!active)return 0;retire();return (RING_SAMPLES-queued)/2;}
extern "C" void pvzp_pcm_write(const int16_t *samples,unsigned count){
    if(!active)return;retire();
    for(unsigned i=0;i<count&&queued+2<=RING_SAMPLES;i++){
        ring[used]=samples[i];used=(used+1)%RING_SAMPLES;ring[used]=samples[i];used=(used+1)%RING_SAMPLES;queued+=2;
        if(samples[i])pvzp_diagnostic[17]++;
    }
    if(!playing&&queued>=4096){if(submit(0,&descriptor,0,0))playing=1;else reset_ring();}
}
extern "C" void pvzp_audio_close(){
    if(active){stop_device();submit(0,nullptr,0,0);close_device();active=0;}
    if(initialized){destroy_desc(&descriptor);initialized=0;}playing=0;pvzp_diagnostic[16]=0;
}
