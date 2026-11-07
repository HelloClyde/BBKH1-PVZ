/* Bounded standalone native codec timings; no game state or clock writes. */
#include "h1_sdk.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../vendor/tinyh264/src/h264bsd_decoder.h"
volatile uint32_t intro_probe[32];
static uint32_t colours[1024];
static unsigned word(const uint8_t *p){return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24);}
static uint32_t rgb(uint8_t y,int r,int g,int b){int c=298*((int)y-16)+128;return 0xff000000u|(colours[((c+r)>>8)+320]<<16)|(colours[((c+g)>>8)+320]<<8)|colours[((c+b)>>8)+320];}
int pvz_portable_main(void){
 intro_probe[0]=1;intro_probe[1]=*(volatile uint32_t*)0xb0000000u;intro_probe[2]=*(volatile uint32_t*)0xb0000010u;
 FILE *f=fopen("/PVZ/first-intro.h1iv","rb");if(!f){intro_probe[0]=2;return 2;}
 uint32_t head[16];if(fread(head,4,16,f)!=16){fclose(f);return 3;}
 uint8_t *input=malloc(head[8]+32);uint32_t *pixels=malloc(480*272*4),*copy=malloc(480*272*4);storage_t *d=calloc(1,sizeof(*d));
 if(!input||!pixels||!copy||!d){intro_probe[0]=4;return 4;}
 if(fread(input,1,head[8],f)!=head[8]){fclose(f);return 5;}fclose(f);memset(input+head[8],0,32);
 for(int i=0;i<1024;++i){int c=i-320;colours[i]=c<0?0:c>255?255:c;}
 unsigned at=0;for(unsigned i=0;i<150;++i)at+=4+word(input+at);
 intro_probe[3]=h264bsdInit(d,1);intro_probe[4]=h1_raw_tick_80hz();
 for(unsigned i=0;i<16;++i){unsigned n=word(input+at);at+=4;u8 *p=0;u32 w=0,h=0;uint32_t t=h1_raw_tick_80hz();
  unsigned status=h264bsdDecode(d,input+at,n,&p,&w,&h);intro_probe[6]+=h1_raw_tick_80hz()-t;at+=n;if(status!=1||w!=480||h!=272){intro_probe[0]=6;break;}
  t=h1_raw_tick_80hz();const u8 *u=p+480*272,*v=u+240*136;
  for(unsigned y=0;y<272;y+=2)for(unsigned x=0;x<480;x+=2){unsigned a=y*480+x,c=(y/2)*240+x/2;int z=(int)u[c]-128,e=(int)v[c]-128,r=409*e,g=-100*z-208*e,b=516*z;pixels[a]=rgb(p[a],r,g,b);pixels[a+1]=rgb(p[a+1],r,g,b);pixels[a+480]=rgb(p[a+480],r,g,b);pixels[a+481]=rgb(p[a+481],r,g,b);}
  intro_probe[7]+=h1_raw_tick_80hz()-t;t=h1_raw_tick_80hz();memcpy(copy,pixels,480*272*4);intro_probe[8]+=h1_raw_tick_80hz()-t;intro_probe[9]=i+1;
  if(i==15){uint32_t hash=2166136261u;for(unsigned k=0;k<480*272*3/2;++k)hash=(hash^p[k])*16777619u;intro_probe[11]=hash;}
 }
 intro_probe[5]=h1_raw_tick_80hz();intro_probe[10]=copy[12345];h264bsdShutdown(d);free(d);free(input);free(pixels);free(copy);intro_probe[0]=0x600d;return 0;
}
