/* Compare offline font tiles and real renderer against the original dense A8. */
#include "LawnApp.h"
#include "graphics/GLImage.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/FontTileStorage.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/Graphics.h"
#include "h1_resources.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <memory>
using namespace Sexy;
namespace {void Check(bool v){if(!v)throw std::runtime_error("Font tile pixels, residency or rendering mismatch");}}
extern "C" void pvzh1_font_tiles_probe(){
    uint64_t pixels=0;size_t sparseBytes=0;unsigned frames=0;
    for(const char* name:{"h1zh09","h1zh09o","h1zh10","h1zh10o","h1zh12","h1zh14","h1zh15","h1zh15o","h1zh17"}){
        H1Image input{};const std::string logical=std::string("data/")+name;
        Check(h1_image_open(&input,logical.c_str(),true,0xffffff));
        const auto w=input.info.storage_width,h=input.info.storage_height;
        std::vector<unsigned char> original(size_t(w)*h);Check(h1_image_load_storage_alpha(&input,original.data(),original.size()));h1_image_close(&input);
        H1FontTileStorage storage;Check(storage.Open(name,w,h));uint32_t read=0;
        Check(storage.Prepare(0,0,24,24,read));if(std::string(name)=="h1zh17")sparseBytes=storage.Bytes();
        const size_t bytes=storage.Bytes();Check(storage.Prepare(0,0,24,24,read)&&!read&&bytes==storage.Bytes());
        Check(bytes<original.size()/5);
        Check(storage.Prepare(0,0,w,h,read));auto tiles=storage.View();
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)
            Check(tiles[(y>>4)*storage.Columns()+(x>>4)][(y&15)*16+(x&15)]==original[y*w+x]);
        pixels+=original.size();
    }
    H1RenderPlanTestEnable(false);
    std::unique_ptr<GLImage> image(gLawnApp->GetImage("data/h1zh17",false));Check(image!=nullptr);
    size_t drawnBytes=0;
    for(int mode:{Graphics::DRAWMODE_NORMAL,Graphics::DRAWMODE_ADDITIVE})for(int tint:{255,129,47})for(int row:{0,530,1060}){
        std::vector<uint32_t> expected;
        for(bool tiled:{false,true}){
            H1ImageCacheFontTilesTestEnable(tiled);H1ImageCacheAlphaPagesTestEnable(false);
            MemoryImage target(gLawnApp);target.Create(480,272);target.SetImageMode(false,false);
            auto* bits=target.GetBits();for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
            SexyTransform2D matrix;matrix.Scale(.4528302f,.453333333f);matrix.Translate(13.5f,11.25f-float(row)*.453333333f);
            H1DrawTransformed(&target,image.get(),Rect(47,row,235,53),Rect(5,7,410,250),Color(90,190,255,tint),mode,matrix,true);
            if(!tiled)expected.assign(bits,bits+480*272);else{
                Check(expected==std::vector<uint32_t>(bits,bits+480*272));
                H1ImageCacheStorageStats stats{};Check(H1ImageCacheGetStorageStats(image.get(),&stats));
                Check(stats.bytes<725760/5);drawnBytes=std::max(drawnBytes,stats.bytes);
            }
        }++frames;
    }
    std::ofstream out(std::filesystem::path(std::getenv("PVZ_HOST_OUTPUT"))/"font-tiles-verification.json");
    out<<"{\"passed\":true,\"all_physical_pixels_compared\":"<<pixels<<",\"whole_frame_cases\":"<<frames<<",\"largest_font_dense_a8_bytes\":725760,\"sparse_24px_region_bytes\":"<<sparseBytes<<",\"max_five_glyph_draw_bytes\":"<<drawnBytes<<",\"repeat_tile_load_bytes\":0}\n";
}
