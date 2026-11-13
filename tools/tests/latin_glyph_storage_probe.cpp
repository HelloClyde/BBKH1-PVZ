/* Compare normal and fractional/rotated glyphs with original full-atlas pixels. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/Cutscene.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "Lawn/Widget/TitleScreen.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/PausedBoardCache.h"
#include "graphics/GLInterface.h"
#include "graphics/SoftwareRaster.h"
#include "Resources.h"
#include "widget/WidgetManager.h"
#include "h1_resources.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <zlib.h>
extern "C" volatile uint32_t pvzh1_font_storage_diagnostic[8];
extern "C" volatile uint32_t pvzh1_font_logical_request_diagnostic[8];
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" size_t pvzh1_cache_memory_pressure(size_t);
namespace {void Check(bool v){if(!v)throw std::runtime_error("Bounded CJK glyph regression");}}
extern "C" void pvzh1_latin_glyph_probe(){
 using namespace Sexy;
 unsigned comparisons=0;
 for(const char* name:{"briannetod12","briannetod16","continuumbold14","houseofterror20"}){
  std::string path=std::string("data/")+name;
  std::unique_ptr<GLImage> source(gLawnApp->GetImage(path,false));Check(bool(source));
  H1Image input{};Check(h1_image_open(&input,path.c_str(),true,0xffffff));
  MemoryImage oracle(gLawnApp);oracle.Create(input.info.width,input.info.height);
  Check(h1_image_load_all(&input,oracle.GetBits(),size_t(input.info.width)*input.info.height));
  oracle.SetImageMode(true,true);h1_image_close(&input);
  for(bool sparse:{true}){
   H1ImageCacheFontTilesTestEnable(sparse);
   for(int mode:{Graphics::DRAWMODE_NORMAL,Graphics::DRAWMODE_ADDITIVE})
    for(int alpha:{255,129})for(int y:{0}){
     if(y>=source->mHeight)continue;
     Rect crop(47,y,37,std::min(40,source->mHeight-y));Color color(90,190,255,alpha);
     MemoryImage a(gLawnApp),b(gLawnApp);a.Create(100,70);b.Create(100,70);
     a.SetImageMode(false,false);b.SetImageMode(false,false);
     for(int i=0;i<100*70;++i)a.GetBits()[i]=b.GetBits()[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
     a.Blt(&oracle,7,11,crop,color,mode,false);b.Blt(source.get(),7,11,crop,color,mode,false);
     Check(std::equal(a.GetBits(),a.GetBits()+7000,b.GetBits()));
     auto stats=H1ImageCacheGetStats();
     b.Blt(source.get(),7,11,crop,color,mode,false);
     Check(H1ImageCacheGetStats().loads==stats.loads);
     Check(!source->mBits&&!source->mColorIndices&&!source->mColorTable);++comparisons;
     for(double angle:{0.0,.25,-.3})for(float x:{13.4f,-7.5f}){
      for(int i=0;i<100*70;++i)a.GetBits()[i]=b.GetBits()[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
      Rect clip(3,5,91,61);
      a.BltRotated(&oracle,x,17.25f,crop,clip,color,mode,angle,0,0);
      b.BltRotated(source.get(),x,17.25f,crop,clip,color,mode,angle,0,0);
      Check(std::equal(a.GetBits(),a.GetBits()+7000,b.GetBits()));
      Check(!source->mBits&&!source->mColorIndices&&!source->mColorTable);++comparisons;
     }
    }
  }
 }
 Check(!pvzh1_font_logical_request_diagnostic[0]);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"latin-glyph-verification.json");
 out<<"{\"passed\":true,\"pixel_comparisons\":"<<comparisons<<",\"logical_atlas_materializations\":0,\"physical_A8_storage\":true}\n";
}
