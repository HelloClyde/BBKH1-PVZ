/* Large reduced RGB background: dense oracle vs paged raster and replay. */
#include "LawnApp.h"
#include "graphics/GLImage.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/Graphics.h"
#include "graphics/DirtyCanvas.h"
#include <memory>
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
using namespace Sexy;
namespace {void Check(bool v){if(!v)throw std::runtime_error("RGB storage page pixel mismatch");}}
extern "C" void pvzh1_rgb_storage_pages_probe(){
    std::unique_ptr<GLImage> dense(gLawnApp->GetImage("images/background1unsodded",false));
    std::unique_ptr<GLImage> paged(gLawnApp->GetImage("images/background1unsodded",false));
    const uint32_t* bits=nullptr;const uint32_t*const* pages=nullptr;uint32_t w=0,h=0,pw=0,ph=0;
    Check(dense&&paged&&H1ImageCachePrepareStorageTexture(dense.get(),&bits,&w,&h));
    Check(H1ImageCachePrepareStorageRGBPages(paged.get(),&pages,&pw,&ph)&&w==pw&&h==ph);
    for(size_t i=0;i<size_t(w)*h;++i)Check(bits[i]==pages[i>>10u][i&1023u]);
    auto* proxy=H1GetPhysicalScreen(gLawnApp->mGLInterface->GetScreenImage());Check(proxy!=nullptr);
    auto* target=proxy->Target();auto* pixels=target->GetBits();
    const bool previous=h1DirtyCanvas.enabled;h1DirtyCanvas.enabled=false;
    unsigned checks=0;
    for(int cut:{0,53,127})for(int tint:{255,111}){
        Rect source(cut,0,dense->mWidth-cut,dense->mHeight),clip(0,0,480,272);
        SexyTransform2D matrix;matrix.Scale(.4528302f,.45333333f);matrix.Translate(float(-cut)*.15f,0);
        std::vector<uint32_t> expected;
        for(unsigned frame=0;frame<7;++frame){
            for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
            H1RenderPlanTestEnable(frame!=0);
            H1DrawTransformed(target,frame?paged.get():dense.get(),source,clip,Color(255,255,255,tint),Graphics::DRAWMODE_NORMAL,matrix,true);
            if(!frame)expected.assign(pixels,pixels+480*272);
            else {Check(expected==std::vector<uint32_t>(pixels,pixels+480*272));++checks;}
        }
    }
    H1RenderPlanTestEnable(true);h1DirtyCanvas.enabled=previous;
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"rgb-pages-verification.json");
    out<<"{\"passed\":true,\"all_argb_pixels_compared\":"<<w*h<<",\"whole_frame_cases\":"<<checks<<",\"render_plan_replays\":"<<pvzh1_render_plan_diagnostic[3]<<"}\n";
}
