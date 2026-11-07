/* Bounded LZ4 blocks and precomputed RGB565 motion deltas. LGPL-3.0-or-later. */
#include "IntroFrames.h"
#include <string.h>
#define WORDS (480u*272u)
int H1IntroUnpack(const uint8_t* input,unsigned size,uint16_t* output){
 unsigned at=0,out=0;uint8_t* bytes=(uint8_t*)output;
 while(at<size){unsigned token=input[at++],literal=token>>4,match=token&15u;
  if(literal==15){unsigned extra;do{if(at>=size)return 0;extra=input[at++];if(extra>WORDS*2-literal)return 0;literal+=extra;}while(extra==255);}
  if(literal>size-at||literal>WORDS*2-out)return 0;
  memcpy(bytes+out,input+at,literal);at+=literal;out+=literal;
  if(at==size)return out==WORDS*2;
  if(size-at<2)return 0;
  unsigned distance=input[at]|((unsigned)input[at+1]<<8);at+=2;if(!distance||distance>out)return 0;
  if(match==15){unsigned extra;do{if(at>=size)return 0;extra=input[at++];if(extra>WORDS*2-match)return 0;match+=extra;}while(extra==255);}
  if(out>WORDS*2-4||match>WORDS*2-out-4)return 0;
  match+=4;
  if(distance==1){memset(bytes+out,bytes[out-1],match);out+=match;}
  else {unsigned end=out+match;
   /* Each word is complete before its destination is written, including
      overlapping matches. Explicit builtins permit unaligned word copies
      under the freestanding MIPS build without violating C aliasing. */
   if(distance>=4)while(end-out>=4){uint32_t word;
    __builtin_memcpy(&word,bytes+out-distance,4);
    __builtin_memcpy(bytes+out,&word,4);out+=4;
   }
   while(end-out>=4){
    bytes[out]=bytes[out-distance];++out;bytes[out]=bytes[out-distance];++out;
    bytes[out]=bytes[out-distance];++out;bytes[out]=bytes[out-distance];++out;
   }while(out<end){bytes[out]=bytes[out-distance];++out;}}
 }
 return out==WORDS*2;
}
void H1IntroReconstruct(int shift,const uint16_t* delta,const uint16_t* before,uint16_t* current){
 const int left=shift>0?shift:0,right=shift<0?480+shift:480;
 for(unsigned row=0;row<272;++row){unsigned at=row*480;
  for(int x=0;x<left;++x)current[at+x]=delta[at+x];
  for(int x=left;x<right;++x)current[at+x]=delta[at+x]^before[at+x-shift];
  for(int x=right;x<480;++x)current[at+x]=delta[at+x];
 }
}
#include "IntroColours.h"
void H1IntroExpand(const uint16_t* pixels,uint32_t* out){
 for(unsigned i=0;i<WORDS;++i){unsigned c=pixels[i];out[i]=0xff000000u|low[c&255]|high[c>>8];}
}
void H1IntroReconstructExpand(int shift,const uint16_t* delta,const uint16_t* before,uint16_t* current,uint32_t* out){
 const int left=shift>0?shift:0,right=shift<0?480+shift:480;
 for(unsigned row=0;row<272;++row){unsigned at=row*480;
  for(int x=0;x<left;++x){unsigned c=delta[at+x];current[at+x]=c;out[at+x]=0xff000000u|low[c&255]|high[c>>8];}
  for(int x=left;x<right;++x){unsigned c=delta[at+x]^before[at+x-shift];current[at+x]=c;out[at+x]=0xff000000u|low[c&255]|high[c>>8];}
  for(int x=right;x<480;++x){unsigned c=delta[at+x];current[at+x]=c;out[at+x]=0xff000000u|low[c&255]|high[c>>8];}
 }
}
/* Match the native window's 0x00RRGGBB surface contract with volatile stores.
   No intermediate ARGB image or subsequent full-screen copy is required. */
void H1IntroPresent565(const uint16_t* pixels,volatile uint32_t* out){

 for(unsigned i=0;i<WORDS;i+=4){unsigned a=pixels[i],b=pixels[i+1],c=pixels[i+2],d=pixels[i+3];
  out[i]=low[a&255]|high[a>>8];out[i+1]=low[b&255]|high[b>>8];
  out[i+2]=low[c&255]|high[c>>8];out[i+3]=low[d&255]|high[d>>8];
 }
}
