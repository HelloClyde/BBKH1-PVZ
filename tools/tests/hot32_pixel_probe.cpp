#include "LawnApp.h"
#include "PvzpLib/PvzpCommon.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/SWTri.h"
#include "graphics/Graphics.h"
#include "graphics/ImageFont.h"
#include "graphics/GLInterface.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/RenderPlanCache.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <vector>
#include <stdexcept>
using namespace Sexy;
namespace {
std::vector<uint64_t> hashes;unsigned kernels=0,draws=0,fonts=0;
void Check(bool v,const char*m){if(!v){fprintf(stderr,"HOT32 FAILURE: %s\n",m);throw std::runtime_error(m);}}
uint64_t Hash(const uint32_t* p,size_t n){uint64_t h=1469598103934665603ull;while(n--){uint32_t x=*p++;for(unsigned s=0;s<32;s+=8){h^=(x>>s)&255;h*=1099511628211ull;}}return h;}
void Ground(MemoryImage& image){auto*p=image.GetBits();for(size_t i=0,n=(size_t)image.mWidth*image.mHeight;i<n;++i)p[i]=((i%256)<<24)|((i*0x153789u+0x314121)&0xffffffu);image.BitsChanged();}
void Add(MemoryImage& image){hashes.push_back(Hash(image.GetBits(),(size_t)image.mWidth*image.mHeight));}
void Kernels(){
 std::vector<uint32_t> tex(64*64);for(unsigned i=0;i<tex.size();++i)tex[i]=((i%256)<<24)|((i*0x153789u)&0xffffffu);const uint32_t* pages[]={tex.data(),tex.data()+1024,tex.data()+2048,tex.data()+3072};
 for(bool custom:{false,true}){SWTri_AddAllDrawTriFuncs();if(custom)Pvzp_SWTri_AddAllDrawTriFuncs();
 for(bool additive:{false,true}){if(!custom&&additive)continue;gPvzpTriangleDrawAdditive=additive;
 for(int format:{0x888,0x8888})for(bool alpha:{false,true})for(bool mod:{false,true})for(bool global:{false,true})for(bool linear:{false,true})for(bool paged:{false,true})for(unsigned geometry=0;geometry<5;++geometry){
  std::vector<uint32_t> dest(320*240+32,0x1a11ce55);for(unsigned i=0;i<320*240;++i)dest[i+16]=((i%256)<<24)|((i*0x17354bu)&0xffffffu);
  SWHelper::SWTextureInfo info{};info.pTexture=tex.data();info.pages=paged?pages:nullptr;info.pitch=64;info.endpos=(unsigned)tex.size();
  SWHelper::SWVertex v[3]={{(int)(65536+geometry*1731),2*65536,0xff0000,0xff0000,0x870000,0x070000,32768,32768},{311*65536,19*65536,0x080000,0x170000,0xff0000,0x010000,63*65536+32767,65536},{33*65536,227*65536,0x7f0000,0x030000,0x410000,0xff0000,0,62*65536+32768}};
  if(geometry==1){auto tmp=v[1];v[1]=v[2];v[2]=tmp;}if(geometry==2){v[0].y=v[1].y;}if(geometry==3){v[1].u=-32768;v[2].v=-32768;}if(geometry==4){v[0].x=159*65536;v[2].x=159*65536;}
  SWHelper::SWDiffuse diffuse{137,101,179,231};SWHelper::SWDrawTriangle(true,alpha,mod,global,v,dest.data()+16,320*4,&info,diffuse,format,linear);
  for(unsigned i=0;i<16;++i)Check(dest[i]==0x1a11ce55&&dest[dest.size()-1-i]==0x1a11ce55,"kernel wrote framebuffer guards");hashes.push_back(Hash(dest.data()+16,320*240));++kernels;
 }
 }}gPvzpTriangleDrawAdditive=false;SWTri_AddAllDrawTriFuncs();
}
void DrawModes(){
 MemoryImage source(gLawnApp);source.Create(31,23);
 for(unsigned sourceMode=0;sourceMode<3;++sourceMode){auto*p=source.GetBits();for(unsigned i=0;i<31*23;++i)p[i]=((sourceMode==0?255:i%256)<<24)|((i*0x153789u)&0xffffffu);source.BitsChanged();source.SetImageMode(sourceMode==1,sourceMode==1);if(sourceMode==2)source.mForcedMode=false;
 for(unsigned destinationMode=0;destinationMode<3;++destinationMode)for(bool linear:{false,true})for(int mode:{0,1})for(Color color:{Color::White,Color(101,179,231,137)})for(unsigned geometry=0;geometry<5;++geometry){
  MemoryImage destination(gLawnApp);destination.Create(211,143);destination.SetImageMode(destinationMode==1,destinationMode==1);if(destinationMode==2)destination.mForcedMode=false;Ground(destination);
  SexyTransform2D matrix;matrix.Scale(geometry==3?-5.5f:5.5f,4.7f);if(geometry==4)matrix.RotateDeg(17);matrix.Translate(geometry==3?190.f:geometry==1?-19.125f:17.5f,geometry==2?-31.25f:7.125f);
  H1DrawTransformed(&destination,&source,Rect(0,0,31,23),Rect(7,11,193,119),color,mode,matrix,linear);Add(destination);++draws;
 }
 }
}
void Fonts(){
 for(const char* path:{"data/BrianneTod32.txt","data/HouseofTerror28.txt","data/DwarvenTodcraft36GreenInset.txt","data/DwarvenTodcraft18GreenInset.txt"}){
  ImageFont original(gLawnApp,path);Check(original.mFontData&&original.mFontData->mInitialized,"actual font initialization failed");
  for(double scale:{.75,1.,1.5})for(bool alpha:{false,true})for(int mode:{0,1})for(Color color:{Color::White,Color(101,179,231,137)}){
   ImageFont font(original);font.mActiveLayerList.clear();font.mActiveListValid=false;font.SetScale(scale);MemoryImage target(gLawnApp);target.Create(800,240);target.SetImageMode(alpha,alpha);Ground(target);Graphics g(&target);g.SetDrawMode(mode);g.SetClipRect(13,7,731,223);int width=0;font.Prepare();font.DrawStringEx(&g,9,90,"THE ZOMBIES ARE COMING! 123 AV WA",color,nullptr,&width);font.DrawStringEx(&g,27,171,"RGBA alpha fade",Color(31,231,79,8),nullptr,nullptr);Add(target);hashes.push_back((uint32_t)width);++fonts;
  }
 }
}
}
extern "C" void pvzh1_hot32_pixel_probe(){
 H1RenderPlanTestEnable(false);Kernels();DrawModes();Fonts();
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"hot32-pixels.json");out<<"{\"passed\":true,\"kernel_cases\":"<<kernels<<",\"forced_mode_linear_draw_cases\":"<<draws<<",\"real_font_alpha_cases\":"<<fonts<<",\"hashes\":[";for(size_t i=0;i<hashes.size();++i){if(i)out<<',';out<<hashes[i];}out<<"]}\n";fprintf(stderr,"HOT32 %u KERNEL + %u DRAW + %u FONT cases\n",kernels,draws,fonts);
}
