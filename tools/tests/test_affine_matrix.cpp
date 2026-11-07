#include "graphics/AffineMatrix.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

static Sexy::SexyMatrix3 Original(const Sexy::SexyMatrix3& a,const Sexy::SexyMatrix3& b,bool zero){
 Sexy::SexyMatrix3 result;
 for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){
  if(zero){float x=0;for(unsigned k=0;k<3;++k)x+=a.m[i][k]*b.m[k][j];result.m[i][j]=x;}
  else result.m[i][j]=a.m[i][0]*b.m[0][j]+a.m[i][1]*b.m[1][j]+a.m[i][2]*b.m[2][j];
 }return result;
}
int main(){
 uint32_t random=0x12a92e7du;unsigned cases=0;
 auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
 const std::array<float,12> edges={0.f,-0.f,1.f,-1.f,.5f,-.5f,10000.f,-10000.f,std::numeric_limits<float>::denorm_min(),-std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::max(),-std::numeric_limits<float>::max()};
 for(unsigned sample=0;sample<600000;++sample){
  Sexy::SexyMatrix3 a,b;
  for(auto* m:{&a,&b}){
   m->m20=m->m21=0.f;m->m22=1.f;
   for(unsigned row=0;row<2;++row)for(unsigned col=0;col<3;++col){
    uint32_t bits=next();if((bits&0x7f800000u)==0x7f800000u)bits^=0x00800000u;
    m->m[row][col]=sample<100000?edges[next()%edges.size()]:std::bit_cast<float>(bits);
   }
   if(sample>=200000&&(sample<400000||sample>=500000)){m->m00=m->m11=1.f;m->m01=m->m10=0.f;}
   if(sample>=400000){m->m20=std::bit_cast<float>(next()&0x80000000u);m->m21=std::bit_cast<float>(next()&0x80000000u);}
  }
  for(bool zero:{false,true}){
   const auto expected=Original(a,b,zero);Sexy::SexyMatrix3 actual;
   if(!H1MultiplyAffine(actual,a,b,zero)||std::memcmp(&expected,&actual,sizeof(actual))){
    for(unsigned i=0;i<9;++i)if(std::bit_cast<uint32_t>(expected.m[i/3][i%3])!=std::bit_cast<uint32_t>(actual.m[i/3][i%3]))std::fprintf(stderr,"sample=%u leading_zero=%d element=%u expected=%08x actual=%08x\n",sample,zero,i,std::bit_cast<uint32_t>(expected.m[i/3][i%3]),std::bit_cast<uint32_t>(actual.m[i/3][i%3]));
    return 1;
   }
   auto alias=a;H1MultiplyAffine(alias,alias,b,zero);if(std::memcmp(&expected,&alias,sizeof(alias)))return 2;
   alias=b;H1MultiplyAffine(alias,a,alias,zero);if(std::memcmp(&expected,&alias,sizeof(alias)))return 3;
   cases+=3;
  }
 }
 Sexy::SexyMatrix3 a,b,out;a.LoadIdentity();b.LoadIdentity();unsigned guards=0;
 for(uint32_t bits:{0x3f800000u,0x7fc00001u,0x7f800000u}){a.m20=std::bit_cast<float>(bits);if(H1MultiplyAffine(out,a,b))return 4;++guards;}
 a.LoadIdentity();a.m00=std::numeric_limits<float>::infinity();if(H1MultiplyAffine(out,a,b))return 5;++guards;
 std::printf("{\"passed\":true,\"bit_exact_products_and_alias_cases\":%u,\"non_affine_or_nonfinite_guards\":%u}\n",cases,guards);
}
