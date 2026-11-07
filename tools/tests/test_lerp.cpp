/* Independent original expression, including signed zero and guard edges. */
#include "PvzpLib/PvzpCommon.h"
#include <bit>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
static __attribute__((noinline)) float Original(float a,float b,float f){return a+f*(b-a);}
int main(){
 const std::array<uint32_t,14> edges={0u,0x80000000u,0x3f800000u,0xbf800000u,0x3f000000u,0x3e000000u,1u,0x80000001u,0x7f7fffffu,0xff7fffffu,0x7f800000u,0xff800000u,0x7fc00001u,0x7fc12345u};
 uint32_t random=0x3e4b1287;unsigned exact=0,nans=0;
 auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
 for(unsigned sample=0;sample<1200000;++sample){
  uint32_t aa=sample<100000?edges[next()%edges.size()]:next(),bb=sample%2?aa:(sample<100000?edges[next()%edges.size()]:next()),ff=sample%3?edges[next()%edges.size()]:next();
  float a=std::bit_cast<float>(aa),b=std::bit_cast<float>(bb),f=std::bit_cast<float>(ff),expected=Original(a,b,f),actual=FloatLerp(a,b,f);
  if(std::isnan(expected)&&std::isnan(actual)){++nans;continue;}
  if(std::bit_cast<uint32_t>(expected)!=std::bit_cast<uint32_t>(actual)){std::fprintf(stderr,"lerp sample=%u a=%08x b=%08x f=%08x expected=%08x actual=%08x\n",sample,aa,bb,ff,std::bit_cast<uint32_t>(expected),std::bit_cast<uint32_t>(actual));return 1;}++exact;
 }
 std::printf("{\"passed\":true,\"bit_exact_non_NaN_cases\":%u,\"NaN_semantic_cases\":%u}\n",exact,nans);
}
