#include "LawnApp.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImage.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/SoftwareRaster.h"
#include "h1_resources.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <cstdio>
using namespace Sexy;
extern "C" size_t pvzh1_cache_memory_pressure(size_t);
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" uint64_t pvzh1_image_pixel_bytes_loaded();
extern "C" volatile uint32_t pvzh1_storage_texture_diagnostic[8];
namespace {
void Check(bool b,const char *s){if(!b)throw std::runtime_error(s);}
uint64_t Hash(const uint32_t *p,size_t n){uint64_t h=1469598103934665603ull;while(n--){auto x=*p++;for(int s=0;s<32;s+=8){h^=(x>>s)&255;h*=1099511628211ull;}}return h;}
const uint32_t colors[]={0xffff0000,0xff00ff00,0xff0000ff,0xffffff00,0xffff00ff,0xff00ffff,0xffef7012,0xff72a4da,0xff501c87};
void Drain(){for(unsigned i=0;i<4096&&pvzh1_cache_memory_pressure((size_t)-1);++i){}}
void Write(const std::filesystem::path &p){
 std::ofstream file(p,std::ios::binary);const uint32_t header[]={0x32493148,99,75,4,0,44*34*4,44,34};file.write((const char*)header,sizeof(header));
 for(int y=0;y<34;++y)for(int x=0;x<44;++x){int col=0,row=0;while(col<2&&x>=(col+1)*33*44/99)++col;while(row<2&&y>=(row+1)*25*34/75)++row;auto c=colors[row*3+col];unsigned char rgba[]={(unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c,255};file.write((char*)rgba,4);}
 Check((bool)file,"synthetic H1I2 write failed");
}
struct Case {int kind;Color color;int mode;};
uint64_t Draw(MemoryImage *source,const Rect& rect,Case c){
 MemoryImage target(gLawnApp);target.Create(128,96);target.SetImageMode(false,false);Graphics g(&target);g.SetColor(Color(11,22,33));g.FillRect(0,0,128,96);
 SexyTransform2D matrix;Rect clip(0,0,128,96);
 if(c.kind==1)matrix.Translate(-13,-7);
 else if(c.kind==2){matrix.Scale(-1,1);matrix.Translate(85,20);}
 else if(c.kind==3){matrix.RotateDeg(22);matrix.Translate(50,38);}
 else if(c.kind==4){matrix.Translate(-4,27);clip=Rect(8,30,30,20);}
 else if(c.kind==5){matrix.Scale(1.7f,.65f);matrix.Translate(15,10);}
 else matrix.Translate(20,18);
 H1DrawTransformed(&target,source,rect,clip,c.color,c.mode,matrix,false);
 return Hash(target.GetBits(),128u*96u);
}
}
extern "C" void pvzh1_reduced_render_probe(){
 static bool done=false;if(done)return;done=true;
 const std::string previous=h1_resources_get_root();const auto root=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"contrast-resources";std::filesystem::create_directories(root);Write(root/"nine.h1i");h1_resources_set_root(root.string().c_str());
 unsigned cases=0;uint64_t aggregate=0;
 {
  MemoryImage reduced(gLawnApp);Check(H1ImageCacheDescribe(&reduced,"nine",false,0xffffff),"synthetic reduced source was not described");Check(reduced.mWidth==99&&reduced.mHeight==75&&H1ImageCacheIsReduced(&reduced),"logical dimensions were replaced by storage dimensions");
  const Case inputs[]={{0,Color::White,0},{1,Color::White,0},{2,Color::White,0},{3,Color::White,0},{4,Color::White,0},{5,Color::White,0},{0,Color(120,200,70,112),0},{2,Color(180,70,230,91),1}};
  for(int cell=0;cell<9;++cell){
   MemoryImage flat(gLawnApp);flat.Create(33,25);flat.SetImageMode(false,false);auto p=flat.GetBits();for(int i=0;i<33*25;++i)p[i]=colors[cell];flat.BitsChanged();
   for(auto c:inputs){const auto expected=Draw(&flat,Rect(0,0,33,25),c),actual=Draw(&reduced,Rect(cell%3*33,cell/3*25,33,25),c);Check(expected==actual,"reduced 3x3 cell crossed boundaries or changed mirror/rotate/clip output");aggregate^=actual;++cases;}
  }
  Check(!reduced.mBits,"reduced raster path expanded logical GetBits");const uint32_t *view=nullptr;uint32_t w=0,h=0;Check(H1ImageCachePrepareStorageTexture(&reduced,&view,&w,&h)&&w==44&&h==34,"dense physical view dimensions changed");
  const auto bytes=pvzh1_image_pixel_bytes_loaded();for(int i=0;i<12;++i)Draw(&reduced,Rect(33,25,33,25),inputs[i%8]);Check(pvzh1_image_pixel_bytes_loaded()==bytes,"warm reduced cells reread source pixels");Drain();const uint32_t *held=nullptr;Check(H1ImageCachePrepareStorageTexture(&reduced,&held,&w,&h)&&held==view,"current reduced view was evicted");
  pvzh1_cache_complete_draw_batch();Drain();Check(H1ImageCachePrepareStorageTexture(&reduced,&held,&w,&h)&&pvzh1_image_pixel_bytes_loaded()==bytes+44*34*4,"completed source did not reload once after reclaim");
  Check(!reduced.mBits,"reduced source materialized during pressure reload");
 }
 h1_resources_set_root(previous.c_str());
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"reduced-render-verification.json");out<<"{\"test_only\":true,\"production_SWTri_and_storage_loader\":true,\"logical_size\":[99,75],\"storage_size\":[44,34],\"cells\":9,\"cases\":"<<cases<<",\"screen_hash_aggregate\":"<<aggregate<<",\"mirror_rotate_negative_clip_fade_additive_equal\":true,\"warm_reads_zero\":true,\"current_pointer_pin_and_old_reclaim\":true,\"logical_GetBits_expansion\":0,\"all_passed\":true}\n";
 std::fprintf(stderr,"REDUCED 9-CELL REAL SAMPLER %u CASES PASSED\n",cases);
}
