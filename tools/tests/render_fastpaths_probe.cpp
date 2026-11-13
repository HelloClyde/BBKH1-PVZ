#include "LawnApp.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/SWTri.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/MemoryImageCache.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <cstdio>
using namespace Sexy;
extern "C" volatile uint32_t pvzh1_axis_span_diagnostic[8];
namespace {
void Check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
uint32_t ReferencePixel(uint32_t p,uint32_t tex,bool alpha){unsigned a=alpha?tex>>24:255;if(a<=8)return p;if(a>=240)return tex|0xff000000u;unsigned trb=(((tex&0xff00ffu)*a)>>8)&0xff00ffu,tg=(((tex&0x00ff00u)*a)>>8)&0x00ff00u;unsigned inv=255-a,prb=(((p&0xff00ffu)*inv)>>8)&0xff00ffu,pg=(((p&0x00ff00u)*inv)>>8)&0x00ff00u;return 0xff000000u|((trb|tg)+(prb|pg));}
void Reset(MemoryImage& image){auto bits=image.GetBits();for(size_t i=0,n=(size_t)image.mWidth*image.mHeight;i<n;++i)bits[i]=0xff000000u|((uint32_t)i*0x125b75u&0xffffffu);image.BitsChanged();}
}
extern "C" void pvzh1_render_fastpath_probe(){
 unsigned direct=0,draws=0;std::vector<uint32_t> texture(512*4);for(size_t i=0;i<texture.size();++i)texture[i]=((i%256)<<24)|((i*2246822519u)&0xffffffu);SWHelper::SWTextureInfo info{};info.pTexture=texture.data();info.pitch=512;info.endpos=(unsigned)texture.size();info.reducedClamp=true;info.clampLeft=1;info.clampRight=511;info.clampTop=1;info.clampBottom=3;
 for(int step:{32768,65536,98304,-65536})for(bool alpha:{false,true})for(unsigned v:{0u,65536u,3u*65536u}){unsigned u=(step<0?450u:20u)*65536u+123;std::vector<uint32_t>a(264,0xff739ac1),b=a;for(unsigned i=0,s=u;i<256;++i,s+=(unsigned)step)a[i+4]=ReferencePixel(a[i+4],info.ReadClamped(s>>16,v>>16),alpha);Check(H1FastReducedSpan(b.data()+4,256,&info,u,v,step,0,alpha),"eligible horizontal span rejected");if(a!=b){for(unsigned i=0;i<a.size();++i)if(a[i]!=b[i]){std::fprintf(stderr,"SPAN MISMATCH step=%d alpha=%u v=%u i=%u expected=%08x actual=%08x\n",step,(unsigned)alpha,v,i,a[i],b[i]);break;}}Check(a==b,"horizontal span differs from original 0888 alpha/opaque/clamp rounding");++direct;}
 std::vector<uint32_t> sentinel(32,0xaabbccdd);auto before=sentinel;Check(!H1FastReducedSpan(sentinel.data(),7,&info,65536,65536,65536,0,true),"tiny span should use fallback");Check(!H1FastReducedSpan(sentinel.data(),16,&info,0,65536,65536,0,true),"x clamp should use fallback");Check(!H1FastReducedSpan(sentinel.data(),16,&info,65536,65536,65536,65536,true),"out-of-cell rotated UV should use general sampler");Check(sentinel==before,"rejected span wrote destination");direct+=3;
 auto device=gLawnApp->mGLInterface.get();GLImage right(device),left(device),center(device);Check(H1ImageCacheDescribe(&right,"reanim/selectorscreen_bg_right",true,0xffffff),"right descriptor missing");Check(H1ImageCacheDescribe(&left,"reanim/selectorscreen_bg_left",true,0xffffff),"left descriptor missing");Check(H1ImageCacheDescribe(&center,"reanim/selectorscreen_bg_center",true,0xffffff),"center descriptor missing");
 for(auto size: {std::pair<int,int>(360,270),std::pair<int,int>(800,600)}){
  MemoryImage a(gLawnApp),b(gLawnApp);a.Create(size.first,size.second);b.Create(size.first,size.second);a.SetImageMode(false,false);b.SetImageMode(false,false);
  for(int kind=0;kind<9;++kind){Reset(a);Reset(b);for(bool fast:{false,true}){H1FastReducedSpanTestEnable(fast);auto destination=fast?&b:&a;for(auto image:{&right,&left,&center}){SexyTransform2D matrix;if(kind==5){matrix.RotateDeg(17);matrix.Scale(.45f,.45f);}else if(kind==4)matrix.Scale(-.45f,.45f);else matrix.Scale(kind==7?1.0f:.45f,kind==7?1.0f:.45f);matrix.Translate(kind==4?310.25f:kind==2?-33.5f:kind==1?17.125f:0,kind==3?-71.25f:kind==1?11.75f:0);Rect rect=kind==6?Rect(11,9,image->mWidth-22,image->mHeight-18):Rect(0,0,image->mWidth,image->mHeight);Rect clip=kind==3?Rect(13,27,size.first-41,size.second-63):Rect(0,0,size.first,size.second);H1DrawTransformed(destination,image,rect,clip,kind==8?Color(141,219,72,173):Color::White,0,matrix,false);}}Check(!std::memcmp(a.GetBits(),b.GetBits(),(size_t)size.first*size.second*4),"actual reduced menu BG/clip/mirror/overlap pixels differ");++draws;}
 }
 H1FastReducedSpanTestEnable(true);Check(pvzh1_axis_span_diagnostic[1]>24&&pvzh1_axis_span_diagnostic[2]>10000,"real BG draw never used optimized span");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"render-fastpaths-verification.json");out<<"{\"all_passed\":true,\"direct_span_checks\":"<<direct<<",\"actual_menu_BG_pixel_oracles\":"<<draws<<",\"opaque_and_full_256_alpha_range\":true,\"clipped_mirrored_rotated_fallback_tinted_overlap\":true,\"sizes\":[[360,270],[800,600]],\"accepted_spans\":"<<pvzh1_axis_span_diagnostic[1]<<",\"accepted_pixels\":"<<pvzh1_axis_span_diagnostic[2]<<"}\n";
 std::fprintf(stderr,"RENDER FASTPATH %u DIRECT + %u ACTUAL BG PIXEL ORACLES PASSED\n",direct,draws);
}
