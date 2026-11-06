/* Compare the original float GL_REPEAT sampler with the integer white-alpha
 * specialization, using the unchanged game's complete animated pool meshes. */
#include "LawnApp.h"
#include "Lawn/System/PoolEffect.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_white_repeat_diagnostic[3];
namespace {void Check(bool v){if(!v)throw std::runtime_error("Pool repeated-alpha sampler differs from original");}}
extern "C" void pvzh1_pool_repeat_probe(){
    using namespace Sexy;
    Check(gLawnApp->mPoolEffect!=nullptr);auto& pool=*gLawnApp->mPoolEffect;
    auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();
    uint64_t pixels=0,differences=0;unsigned cases=0,maxDelta=0;
    for(bool night:{false,true})for(unsigned time:{0u,1u,37u,299u,799u,1599u,3199u,316799u})for(int pan:{-150,0,100}){
        pool.mPoolCounter=time;pool.UpdateWaterEffect();std::vector<uint32_t> reference;
        for(bool fast:{false,true}){
            H1WhiteRepeatTestEnable(fast);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();
            for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
            Graphics g(screen);g.Translate(pan,0);pool.PoolEffectDraw(&g,night);
            if(!fast)reference.assign(bits,bits+480*272);
            else for(unsigned i=0;i<480*272;++i){
                ++pixels;if(bits[i]!=reference[i])++differences;
                for(unsigned shift=0;shift<32;shift+=8){int delta=int((bits[i]>>shift)&255)-int((reference[i]>>shift)&255);
                    unsigned d=unsigned(delta<0?-delta:delta);if(d>maxDelta)maxDelta=d;Check(d<=1);}
            }
        }++cases;
    }
    Check(pvzh1_white_repeat_diagnostic[0]>0);Check(pvzh1_white_repeat_diagnostic[1]>0);
    H1WhiteRepeatTestEnable(true);
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"pool-repeat-verification.json");
    out<<"{\"passed\":true,\"actual_original_pool_meshes\":true,\"day_and_night\":true,\"whole_frame_cases\":"<<cases
       <<",\"pixels_compared\":"<<pixels<<",\"different_pixels\":"<<differences<<",\"maximum_channel_delta\":"<<maxDelta
       <<",\"fast_triangles\":"<<pvzh1_white_repeat_diagnostic[0]<<",\"fast_pixels\":"<<pvzh1_white_repeat_diagnostic[1]
       <<",\"original_animation_clock_and_mesh_unchanged\":true}\n";
}
