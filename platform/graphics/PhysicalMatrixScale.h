/* Exact narrow replacement for a left diagonal scale of an affine matrix.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef PVZH1_PHYSICAL_MATRIX_SCALE_H
#define PVZH1_PHYSICAL_MATRIX_SCALE_H
#include <stdint.h>
namespace Sexy {
inline uint32_t H1ScaleFloatBits(float value) {
    uint32_t bits; __builtin_memcpy(&bits,&value,sizeof(bits)); return bits;
}
inline bool H1ScaleNormalOrZero(uint32_t bits) {
    const uint32_t magnitude=bits&0x7fffffffu;
    const uint32_t exponent=magnitude&0x7f800000u;
    return magnitude==0 || (exponent!=0 && exponent!=0x7f800000u);
}
/* Failure never changes matrix. Generic multiplication starts each sum at +0,
 * so a zero product (including -0) must become +0. No fast-math is required. */
inline bool H1TryScaleAffineMatrix(float *matrix,float sx,float sy) {
    if(H1ScaleFloatBits(matrix[6])!=0 || H1ScaleFloatBits(matrix[7])!=0 ||
       H1ScaleFloatBits(matrix[8])!=0x3f800000u)return false;
    const uint32_t bx=H1ScaleFloatBits(sx),by=H1ScaleFloatBits(sy);
    if((bx&0x80000000u)||(by&0x80000000u) || bx==0 || by==0 ||
       !H1ScaleNormalOrZero(bx)||!H1ScaleNormalOrZero(by))return false;
    for(unsigned i=0;i<6;++i)if(!H1ScaleNormalOrZero(H1ScaleFloatBits(matrix[i])))return false;
    float output[6];
    for(unsigned i=0;i<6;++i) {
        output[i]=matrix[i]*(i<3?sx:sy);
        const uint32_t bits=H1ScaleFloatBits(output[i]);
        if(!H1ScaleNormalOrZero(bits))return false;
        if((bits&0x7fffffffu)==0)output[i]=0.0f;
    }
    for(unsigned i=0;i<6;++i)matrix[i]=output[i];
    return true;
}
}
#endif
