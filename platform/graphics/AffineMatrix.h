/* Exact affine subset of the original 3x3 products. LGPL-3.0-or-later. */
#pragma once
#include "misc/SexyMatrix.h"
#include <bit>
#include <cstdint>

inline float H1AffineFiniteProduct(float a,float b){
 const uint32_t x=std::bit_cast<uint32_t>(a),y=std::bit_cast<uint32_t>(b),ax=x&0x7fffffffu,ay=y&0x7fffffffu;
 if(!ax||!ay)return std::bit_cast<float>((x^y)&0x80000000u);
 if(ax==0x3f800000u)return std::bit_cast<float>(y^(x&0x80000000u));
 if(ay==0x3f800000u)return std::bit_cast<float>(x^(y&0x80000000u));
 return a*b;
}
inline float H1AffineAdd(float a,float b){
 const uint32_t x=std::bit_cast<uint32_t>(a),y=std::bit_cast<uint32_t>(b),ax=x&0x7fffffffu,ay=y&0x7fffffffu;
 if(!ax&&ay<=0x7f800000u)return ay?b:std::bit_cast<float>(x&y&0x80000000u);
 if(!ay&&ax<=0x7f800000u)return a;
 return a+b;
}
inline float H1AffineFirstProduct(float a,float b,bool leadingZero){
 const float p=H1AffineFiniteProduct(a,b);
 return leadingZero?H1AffineAdd(0.f,p):p;
}
inline float H1AffineBottom(const Sexy::SexyMatrix3& l,const Sexy::SexyMatrix3& r,unsigned col,bool leadingZero){
 const uint32_t a=std::bit_cast<uint32_t>(l.m20)^std::bit_cast<uint32_t>(r.m[0][col]);
 const uint32_t b=std::bit_cast<uint32_t>(l.m21)^std::bit_cast<uint32_t>(r.m[1][col]);
 return std::bit_cast<float>(leadingZero?0:a&b&std::bit_cast<uint32_t>(r.m[2][col])&0x80000000u);
}
__attribute__((noinline,optimize("Os"))) inline bool H1MultiplyAffine(
 Sexy::SexyMatrix3& out,const Sexy::SexyMatrix3& left,const Sexy::SexyMatrix3& right,bool leadingZero=false){
 const Sexy::SexyMatrix3* matrices[]={&left,&right};
 for(const auto* m:matrices){
  if((std::bit_cast<uint32_t>(m->m20)&0x7fffffffu)||(std::bit_cast<uint32_t>(m->m21)&0x7fffffffu)||std::bit_cast<uint32_t>(m->m22)!=0x3f800000u)return false;
  for(unsigned row=0;row<2;++row)for(unsigned col=0;col<3;++col)
   if((std::bit_cast<uint32_t>(m->m[row][col])&0x7f800000u)==0x7f800000u)return false;
 }
 // The common attachment case is two translations. Keep the same two final
 // additions and the original intermediate signed zeros, without multiplying
 // their identity linear components through the software floating point ABI.
 bool translations=true;
 for(const auto* m:matrices)translations=translations&&
  std::bit_cast<uint32_t>(m->m00)==0x3f800000u&&std::bit_cast<uint32_t>(m->m11)==0x3f800000u&&
  !std::bit_cast<uint32_t>(m->m01)&&!std::bit_cast<uint32_t>(m->m10);
 if(translations){
  Sexy::SexyMatrix3 result=left;
  const uint32_t zeroSign=leadingZero?0:(std::bit_cast<uint32_t>(right.m02)&std::bit_cast<uint32_t>(right.m12)&0x80000000u);
  float x=right.m02,y=right.m12;
  if(!(std::bit_cast<uint32_t>(x)&0x7fffffffu))x=std::bit_cast<float>(zeroSign);
  if(!(std::bit_cast<uint32_t>(y)&0x7fffffffu))y=std::bit_cast<float>(zeroSign);
  result.m02=H1AffineAdd(x,left.m02);result.m12=H1AffineAdd(y,left.m12);
  result.m20=H1AffineBottom(left,right,0,leadingZero);result.m21=H1AffineBottom(left,right,1,leadingZero);out=result;return true;
 }
 Sexy::SexyMatrix3 result;
 for(unsigned row=0;row<2;++row){
  for(unsigned col=0;col<2;++col){
   float sum=H1AffineAdd(H1AffineFirstProduct(left.m[row][0],right.m[0][col],leadingZero),H1AffineFiniteProduct(left.m[row][1],right.m[1][col]));
   const uint32_t sign=(std::bit_cast<uint32_t>(left.m[row][2])^std::bit_cast<uint32_t>(right.m[2][col]))&0x80000000u;
   result.m[row][col]=H1AffineAdd(sum,std::bit_cast<float>(sign));
  }
  result.m[row][2]=H1AffineAdd(H1AffineAdd(H1AffineFirstProduct(left.m[row][0],right.m02,leadingZero),H1AffineFiniteProduct(left.m[row][1],right.m12)),left.m[row][2]);
 }
 result.m20=H1AffineBottom(left,right,0,leadingZero);result.m21=H1AffineBottom(left,right,1,leadingZero);result.m22=1.f;out=result;return true;
}
