/* Real reduced A8 sampler: paged vs dense storage, including failure cleanup. */
#include "LawnApp.h"
#include "graphics/GLImage.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/Graphics.h"
#include "graphics/NativeDisplay.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <memory>
using namespace Sexy;
namespace {void Check(bool v){if(!v)throw std::runtime_error("A8 page equivalence or cleanup failed");}}
extern "C" void pvzh1_alpha_pages_probe(){
    // CJK fonts now use H1F glyph packs, not image atlases. Exercise the
    // remaining real A8 atlas rather than a removed historical font image.
    std::unique_ptr<GLImage> image(gLawnApp->GetImage("data/briannetod32outline",true));Check(image!=nullptr);
    H1RenderPlanTestEnable(false);
    uint32_t w=0,h=0;const unsigned char* dense=nullptr;
    H1ImageCacheAlphaPagesTestEnable(false);Check(H1ImageCachePrepareStorageAlpha(image.get(),&dense,&w,&h));
    std::vector<unsigned char> reference(dense,dense+size_t(w)*h);
    H1ImageCacheAlphaPagesTestEnable(true);
    const unsigned char*const* pages=nullptr;Check(H1ImageCachePrepareStorageAlphaPages(image.get(),&pages,&w,&h));
    for(size_t i=0;i<reference.size();++i)Check(pages[i>>10][i&1023]==reference[i]);
    unsigned cases=0;
    for(int mode:{Graphics::DRAWMODE_NORMAL,Graphics::DRAWMODE_ADDITIVE})for(int tint:{255,129,47})for(int y:{0,8,24}){
        std::vector<uint32_t> expected;
        for(bool paged:{false,true}){
            H1ImageCacheAlphaPagesTestEnable(paged);
            MemoryImage target(gLawnApp);target.Create(480,272);target.SetImageMode(false,false);
            auto* bits=target.GetBits();for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
            SexyTransform2D matrix;matrix.Scale(.45f,.453333333f);matrix.Translate(13.5f,11.25f);
            H1DrawTransformed(&target,image.get(),Rect(37,y,600,24),Rect(5,7,410,250),Color(90,190,255,tint),mode,matrix,true);
            if(!paged)expected.assign(bits,bits+480*272);else Check(expected==std::vector<uint32_t>(bits,bits+480*272));
        }++cases;
    }
    unsigned failures=0;
    for(int point:{0,1,17,40}){
        H1ImageCacheAlphaPagesTestEnable(true);H1ImageCacheAlphaTestFailure(point,-1);
        Check(!H1ImageCachePrepareStorageAlphaPages(image.get(),&pages,&w,&h));Check(!pages);
        H1ImageCacheStorageStats stats{};Check(H1ImageCacheGetStorageStats(image.get(),&stats)&&!stats.bytes);
        H1ImageCacheAlphaTestFailure(-1,-1);++failures;
    }
    H1ImageCacheAlphaPagesTestEnable(true);H1ImageCacheAlphaTestFailure(-1,5);
    Check(!H1ImageCachePrepareStorageAlphaPages(image.get(),&pages,&w,&h));Check(!pages);++failures;
    H1ImageCacheAlphaTestFailure(-1,-1);Check(H1ImageCachePrepareStorageAlphaPages(image.get(),&pages,&w,&h));
    {
        std::unique_ptr<GLImage> other(gLawnApp->GetImage("images/sod1row",false));Check(other!=nullptr);
        NativeDisplay display;display.mRGBBits=32;display.mRedBits=display.mGreenBits=display.mBlueBits=8;
        display.mRedMask=0xff0000;display.mGreenMask=0xff00;display.mBlueMask=0xff;
        display.mRedShift=16;display.mGreenShift=8;display.mBlueShift=0;
        const size_t area=size_t(other->mWidth)*other->mHeight;
        const auto initial=H1ImageCacheGetStats().residentBytes;
        Check(other->GetNativeAlphaData(&display)!=nullptr);
        Check(H1ImageCacheGetStats().residentBytes==initial+area*8+4);
        Check(other->GetRLAlphaData()!=nullptr);
        Check(H1ImageCacheGetStats().residentBytes==initial+area*9+4);
    }
    std::ofstream out(std::filesystem::path(std::getenv("PVZ_HOST_OUTPUT"))/"alpha-pages-verification.json");
    out<<"{\"passed\":true,\"all_alpha_bytes_compared\":"<<reference.size()<<",\"whole_frame_cases\":"<<cases<<",\"injected_failures_cleaned\":"<<failures<<",\"nested_getter_accounting_exact\":true}\n";
}
