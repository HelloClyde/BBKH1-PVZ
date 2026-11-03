/* Known-aligned, nonoverlapping pixel runs; LGPL-3.0-or-later. */
#pragma once
#include <cstdint>
#include <cstddef>

// Menu and render-plan runs contain whole uint32_t pixels. Avoid the generic
// byte/alignment/prefetch dispatch for each of their many short spans.
static __attribute__((always_inline)) inline void H1CopyPixels(uint32_t* out,const uint32_t* in,std::size_t count){
    while(count>=8){
        out[0]=in[0];out[1]=in[1];out[2]=in[2];out[3]=in[3];
        out[4]=in[4];out[5]=in[5];out[6]=in[6];out[7]=in[7];
        out+=8;in+=8;count-=8;
    }
    while(count--)*out++=*in++;
}
