#include "LawnApp.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/DirtyCanvas.h"
#include "PvzpLib/PvzpParticle.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_additive_diagnostic[4];
namespace {void Check(bool v){if(!v)throw std::runtime_error("Actual reward-particle replay differs");}}
extern "C" void pvzh1_reward_glow_probe(){
    using namespace Sexy;
    auto* effect=gLawnApp->AddPvzpParticle(530,300,0,PARTICLE_SEED_PACKET_PICKUP);Check(effect);
    auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();
    unsigned age=0,cases=0;uint32_t skipped=0,newCalls=0;
    for(unsigned update:{0u,30u,40u,50u,60u,80u,150u,300u,600u,1000u,1400u}){
        while(age<update){effect->Update();++age;}
        std::vector<uint32_t> expected;
        for(bool direct:{false,true}){
            H1DirectAdditiveTestEnable(direct);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();
            for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+17)&0xffffffu);
            const auto scratch=pvzh1_additive_diagnostic[1],before=pvzh1_additive_diagnostic[0];
            Graphics g(screen);effect->Draw(&g);
            if(!direct)expected.assign(bits,bits+480*272);else{
                Check(expected==std::vector<uint32_t>(bits,bits+480*272));
                Check(pvzh1_additive_diagnostic[1]==scratch);newCalls+=pvzh1_additive_diagnostic[0]-before;
            }
        }++cases;
    }
    skipped=pvzh1_additive_diagnostic[3];Check(newCalls>0);
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"reward-glow-verification.json");
    out<<"{\"passed\":true,\"actual_original_reward_effect\":true,\"whole_frame_comparisons\":"<<cases<<",\"new_direct_triangles\":"<<newCalls<<",\"saturated_spans_skipped\":"<<skipped<<",\"new_scratch_allocations\":0,\"particles_removed\":false}\n";
}
