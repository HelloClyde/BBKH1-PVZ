/* Reduced RGBA pages preserve original physical sanding and raster pixels. */
#include "LawnApp.h"
#include "PvzpLib/PvzpCommon.h"
#include "graphics/GLImage.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/DirtyCanvas.h"
#include <memory>
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace {void Check(bool v){if(!v)throw std::runtime_error("Reduced RGBA page pixels or sanding differ");}}
extern "C" void pvzh1_rgba_storage_pages_probe(){
 using namespace Sexy;unsigned cases=0;size_t compared=0;
 auto* target=H1GetPhysicalScreen(gLawnApp->mGLInterface->GetScreenImage())->Target();auto* pixels=target->GetBits();h1DirtyCanvas.enabled=false;
 for(const char* name:{"reanim/credits_circlefade","images/seedchooser_background","data/houseofterror16outline"}){
  std::unique_ptr<GLImage> dense(gLawnApp->GetImage(name,false)),paged(gLawnApp->GetImage(name,false));Check(dense&&paged);
  const uint32_t* original=nullptr;const uint32_t*const* pages=nullptr;uint32_t w=0,h=0,pw=0,ph=0;
  Check(H1ImageCachePrepareStorageTexture(dense.get(),&original,&w,&h));
  PvzpSandImageIfNeeded(dense.get());PvzpSandImageIfNeeded(paged.get());
  Check(H1ImageCachePrepareStorageRGBPages(paged.get(),&pages,&pw,&ph)&&pw==w&&ph==h);
  for(size_t i=0;i<size_t(w)*h;++i)Check(original[i]==pages[i>>10][i&1023]);compared+=size_t(w)*h;
  for(int tint:{255,113})for(float angle:{0.f,.17f}){
   std::vector<uint32_t> expected;
   for(unsigned frame=0;frame<7;++frame){
    for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
    H1RenderPlanTestEnable(frame!=0);SexyTransform2D matrix;matrix.Scale(.4528302f,.45333333f);matrix.RotateRad(angle);matrix.Translate(21.5f,9.25f);
    H1DrawTransformed(target,frame?paged.get():dense.get(),Rect(0,0,dense->mWidth,dense->mHeight),Rect(3,5,473,261),Color(91,211,255,tint),Graphics::DRAWMODE_NORMAL,matrix,true);
    if(!frame)expected.assign(pixels,pixels+480*272);else{Check(expected==std::vector<uint32_t>(pixels,pixels+480*272));++cases;}
   }
  }
  Check(!paged->mBits);
 }
 h1DirtyCanvas.enabled=true;H1RenderPlanTestEnable(true);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"rgba-storage-pages-verification.json");
 out<<"{\"passed\":true,\"physical_pixels_with_original_sanding\":"<<compared<<",\"whole_frame_comparisons\":"<<cases<<",\"maximum_data_page_bytes\":4096,\"dense_materialization\":false}\n";
}
