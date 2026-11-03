#include "LawnApp.h"
#include "Resources.h"
#include "graphics/ImageFont.h"
#include "graphics/MemoryImage.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImageCache.h"
#include "h1_resources.h"
#include "misc/ResourceManager.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <set>
#include <stdexcept>
#include <cstring>
#include <new>
#include <cstdlib>
#include <iomanip>
static bool measure=false;static unsigned largeArrays=0;
void *operator new(std::size_t n){void *p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();if(measure&&n==900508)++largeArrays;return p;}
void *operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void *p)noexcept{std::free(p);}void operator delete[](void *p)noexcept{std::free(p);}
void operator delete(void *p,std::size_t)noexcept{std::free(p);}void operator delete[](void *p,std::size_t)noexcept{std::free(p);}
using namespace Sexy;
namespace {
void Check(bool x,const char *text){if(!x){std::fprintf(stderr,"CHECK FAILED: %s\n",text);std::exit(4);}}
uint64_t Hash(const void *data,size_t bytes){uint64_t h=1469598103934665603ull;auto p=(const unsigned char*)data;while(bytes--){h^=*p++;h*=1099511628211ull;}return h;}
struct Source {MemoryImage *image;std::vector<uint32_t> pixels;uint32_t rgb;bool alpha,trans,forced;};
void Set(Source& s,bool palette,int rgbOverride){
 auto *i=s.image;size_t area=s.pixels.size();uint32_t rgb=rgbOverride<0?s.rgb:(uint32_t)rgbOverride;
 i->mNativeAlphaData.reset();i->mRLAlphaData.reset();i->mRLAdditiveData.reset();i->mColorIndices.reset();i->mColorTable.reset();
 if(palette){
  auto indices=std::make_unique<uchar[]>(area);auto table=std::make_unique<uint32_t[]>(256);
  for(size_t p=0;p<area;p++)indices[p]=(uchar)(s.pixels[p]>>24);
  for(unsigned a=0;a<256;a++)table[a]=(a<<24)|rgb;
  i->mBits.reset();i->mColorIndices=std::move(indices);i->mColorTable=std::move(table);
 }else{
  auto bits=std::make_unique<uint32_t[]>(area+1);
  for(size_t p=0;p<area;p++)bits[p]=(s.pixels[p]&0xff000000u)|rgb;
  bits[area]=MEMORYCHECK_ID;i->mBits=std::move(bits);
 }
 i->mHasAlpha=s.alpha;i->mHasTrans=s.trans;i->mForcedMode=s.forced;i->mBitsChanged=false;
}
struct Case {const char *name;double scale;int pointDelta;Color color;bool multi,alphaDest;int sourceRGB;};
uint64_t Render(ImageFont *loaded,const Case& c,unsigned& allocations){
 ImageFont font(*loaded);font.mActiveLayerList.clear();font.mActiveListValid=false;
 MemoryImage image(gLawnApp);image.Create(800,240);image.SetImageMode(c.alphaDest,c.alphaDest);Graphics g(&image);
 g.SetColor(Color(23,37,59,c.alphaDest?90:255));g.FillRect(0,0,800,240);
 largeArrays=0;measure=true;font.SetPointSize(loaded->mPointSize+c.pointDelta);font.SetScale(c.scale);font.Prepare();
 int width=0;font.DrawStringEx(&g,10,90,"Zombie 123 AV WA fi!?",c.color,nullptr,&width);
 if(c.multi){font.DrawStringEx(&g,10,160,"Bri32",Color(50,200,80,255),nullptr,nullptr);font.DrawStringEx(&g,280,160,"alpha",Color(230,80,180,80),nullptr,nullptr);}
 measure=false;allocations=largeArrays;
 uint64_t h=Hash(image.GetBits(),800u*240u*4u);return h^(uint64_t)(unsigned)width;
}
}
extern "C" void pvzh1_alpha_palette_probe(){
 ImageFont first(gLawnApp,"data/BrianneTod32.txt"),second(gLawnApp,"data/BrianneTod32Black.txt");
 std::vector<ImageFont*> fonts={&first,&second};
 std::set<MemoryImage*> seen;std::vector<Source> sources;
 for(auto *font:fonts){Check(font&&font->mFontData,"real Font32 absent");for(auto& layer:font->mFontData->mFontLayerList){
  MemoryImage *image=layer.mImage;if(!image||!seen.insert(image).second)continue;
  H1ImageInfo info;Check(h1_image_probe(image->mFilePath.c_str(),1,&info)&&info.channels==1&&info.flags==1,"Font32 layer is not original A8");
  auto bits=image->GetBits();size_t area=(size_t)image->mWidth*image->mHeight;uint32_t rgb=bits[0]&0xffffffu;
  for(size_t p=0;p<area;p++)Check((bits[p]&0xffffffu)==rgb,"A8 source RGB not constant");
  sources.push_back({image,std::vector<uint32_t>(bits,bits+area),rgb,image->mHasAlpha,image->mHasTrans,image->mForcedMode});
 }}
 Check(sources.size()==2,"expected real Font32 base and outline A8 images");
 Case cases[]={ {"normal",1,0,Color::White,false,false,-1},{"color",1,0,Color(220,90,40),false,false,-1},{"alpha",1,0,Color(220,90,40,90),false,false,-1},{"multicolor",1,0,Color::White,true,false,-1},{"alpha-dest",1,0,Color(120,200,210,160),true,true,-1},{"alpha-color",1,0,Color::White,false,false,0x2468ac},{"scale075",.75,0,Color::White,true,false,-1},{"scale150",1.5,0,Color::White,false,false,-1},{"point35",1,3,Color::White,false,false,-1} };
 const char *dir=getenv("PVZ_HOST_OUTPUT");std::ofstream out(std::filesystem::path(dir?dir:".")/"alpha-palette-verification.json");
 out<<"{\"test_only\":true,\"production_loader\":true,\"real_font_layers\":2,\"layer_dimensions\":["<<sources[0].image->mWidth<<','<<sources[0].image->mHeight<<"],\"cases\":[";
 bool comma=false;unsigned noExpand=0,expanded=0;
 for(size_t f=0;f<fonts.size();f++)for(const Case& c:cases){
  for(auto& s:sources)Set(s,false,c.sourceRGB);unsigned a=0,b=0;auto before=Render(fonts[f],c,a);
  for(auto& s:sources){auto path=s.image->mFilePath;Check(H1ImageCacheDescribe(s.image,path,true,c.sourceRGB<0?(int)s.rgb:c.sourceRGB),"real A8 descriptor reload failed");}
  auto after=Render(fonts[f],c,b);Check(before==after,"real production A8 loader Font32 pixels differ");
  bool retained=true,loadedPalette=false;for(auto& s:sources){retained&=!s.image->mBits;loadedPalette|=s.image->mColorIndices&&s.image->mColorTable;}retained&=loadedPalette;
  if(c.scale==1&&c.pointDelta==0)Check(retained&&b==0,"1x Font32 materialized ARGB");
  if(retained)noExpand++;else expanded++;
  if(comma)out<<',';comma=true;out<<"{\"font\":"<<f<<",\"case\":"<<std::quoted(c.name)<<",\"baseline_hash\":"<<before<<",\"palette_hash\":"<<after<<",\"palette_retained\":"<<(retained?"true":"false")<<",\"source_sized_argb_allocations\":"<<b<<",\"equal\":true}";
 }
 for(auto& s:sources)Set(s,false,-1);
 out<<"],\"no_expansion_cases\":"<<noExpand<<",\"expanded_cases\":"<<expanded<<",\"all_pixels_equal\":true,\"all_1x_no_expansion\":true,\"all_passed\":true}\n";
}
