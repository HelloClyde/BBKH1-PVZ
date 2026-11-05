#include "LawnApp.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/SoftwareRaster.h"
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace {void Check(bool v){if(!v)throw std::runtime_error("Paged outline-font differs from original");}}
extern "C" void pvzh1_small_rgba_font_probe(){
    using namespace Sexy;
    unsigned cases=0;size_t total=0,largest=0;
    for(const char* path:{"data/houseofterror16outline","data/houseofterror16"}){
    std::unique_ptr<GLImage> dense(gLawnApp->GetImage(path,false));
    std::unique_ptr<GLImage> paged(gLawnApp->GetImage(path,false));Check(dense&&paged);
    auto* original=dense->GetBits();const uint32_t* const* pages=nullptr;
    Check(H1ImageCachePrepareTexturePages(paged.get(),&pages));
    const size_t count=size_t(dense->mWidth)*dense->mHeight;
    for(size_t i=0;i<count;++i)Check(original[i]==pages[i>>10][i&1023]);
    total+=count;largest=std::max(largest,count*4+4);
    for(bool linear:{false,true})for(int alpha:{255,97,19}){
        std::vector<uint32_t> expected;
        for(auto* image:{dense.get(),paged.get()}){
            MemoryImage target(gLawnApp);target.Create(480,272);target.SetImageMode(false,false);auto* bits=target.GetBits();
            for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
            SexyTransform2D matrix;matrix.Scale(.4528302f,.4533333f);matrix.Translate(17.5f,12.25f);
            H1DrawTransformed(&target,image,Rect(117,0,1000,32),Rect(3,5,473,261),Color(91,211,255,alpha),Graphics::DRAWMODE_NORMAL,matrix,linear);
            if(image==dense.get())expected.assign(bits,bits+480*272);else Check(expected==std::vector<uint32_t>(bits,bits+480*272));
        }++cases;
    }
    Check(!paged->mBits);}
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"small-rgba-font-verification.json");
    out<<"{\"passed\":true,\"rgba_and_a8_fonts\":true,\"argb_pixels_compared\":"<<total<<",\"whole_frame_cases\":"<<cases<<",\"dense_allocation_avoided_bytes\":"<<largest<<",\"maximum_page_bytes\":4096}\n";
}
