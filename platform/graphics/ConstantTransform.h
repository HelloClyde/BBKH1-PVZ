/* Exact constant endpoints of the original FloatLerp. LGPL-3.0-or-later. */
#pragma once
#include "PvzpLib/Reanimator.h"
#include <bit>
#include <cstring>
inline bool H1ConstantTransform(const ReanimatorTransform& a,const ReanimatorTransform& b,float fraction,ReanimatorTransform& out){
 const uint32_t f=std::bit_cast<uint32_t>(fraction);
 if(f>0x3f800000u||std::memcmp(&a.mTransX,&b.mTransX,6*sizeof(float))||std::bit_cast<uint32_t>(a.mAlpha)!=std::bit_cast<uint32_t>(b.mAlpha))return false;
 for(float v:{a.mTransX,a.mTransY,a.mSkewX,a.mSkewY,a.mScaleX,a.mScaleY,a.mAlpha})if((std::bit_cast<uint32_t>(v)&0x7f800000u)==0x7f800000u)return false;
 auto value=[](float v){return (std::bit_cast<uint32_t>(v)&0x7fffffffu)?v:0.f;};
 out.mTransX=value(a.mTransX);out.mTransY=value(a.mTransY);out.mSkewX=value(a.mSkewX);out.mSkewY=value(a.mSkewY);out.mScaleX=value(a.mScaleX);out.mScaleY=value(a.mScaleY);out.mAlpha=value(a.mAlpha);return true;
}
