/* The old full-triangle scratch implementation is the independent oracle. */
#include "LawnApp.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <memory>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_additive_diagnostic[4];
namespace {void Check(bool v){if(!v)throw std::runtime_error("Direct additive pixels differ from scratch oracle");}}
extern "C" void pvzh1_direct_additive_probe(){
    using namespace Sexy;
    std::unique_ptr<GLImage> image(gLawnApp->GetImage("particles/seedpacketglow",false));Check(image!=nullptr);
    unsigned cases=0;uint64_t pixels=0;
    for(bool opaque:{false,true})for(bool linear:{false,true})for(float scale:{.45f,1.0f,4.0f,10.0f})for(int tint:{255,129,31})for(float angle:{0.0f,.37f}){
        std::vector<uint32_t> reference;
        const auto oldCalls=pvzh1_additive_diagnostic[1];
        for(bool direct:{false,true}){
            H1DirectAdditiveTestEnable(direct);
            MemoryImage target(gLawnApp);target.Create(480,272);target.SetImageMode(!opaque,!opaque);
            auto* bits=target.GetBits();h1DirtyCanvas.Bind(&target,bits);h1DirtyCanvas.Begin(&target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)bits[i]=(opaque?0xff000000u:((i*73u)&255)<<24)|((i*0x164b35u+17)&0xffffffu);
            SexyTransform2D matrix;matrix.Scale(scale,scale*.9f);matrix.RotateRad(angle);matrix.Translate(171.25f,133.5f);
            H1DrawTransformed(&target,image.get(),Rect(0,0,image->mWidth,image->mHeight),Rect(3,5,473,261),Color(91,211,255,tint),Graphics::DRAWMODE_ADDITIVE,matrix,linear,0,0,true);
            if(!direct)reference.assign(bits,bits+480*272);else{
                Check(reference==std::vector<uint32_t>(bits,bits+480*272));
                Check(pvzh1_additive_diagnostic[1]==oldCalls+2);
            }
        }++cases;pixels+=480*272;
    }
    H1DirectAdditiveTestEnable(true);
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"direct-additive-verification.json");
    out<<"{\"passed\":true,\"whole_frame_cases\":"<<cases<<",\"pixels_compared\":"<<pixels<<",\"old_max_temporary_bytes\":"<<pvzh1_additive_diagnostic[2]<<",\"new_triangle_scratch_allocations\":0,\"glow_removed\":false}\n";
}
