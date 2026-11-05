/* Bake the original reward rays/flash and starburst at LCD density.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "PvzpLib/PvzpParticle.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/PvzpCommon.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "Common.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <algorithm>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
extern "C" void (*pvzh1_host_offline_additive_capture)(uint32_t*,uint32_t);
namespace {
constexpr unsigned W=960,H=544,N=W*H;
uint32_t* surface;
struct Pass {unsigned mode;std::vector<uint32_t> pixels;};
std::vector<Pass> passes;
unsigned normalWrites;
void Add(uint32_t* p,uint32_t value){
 if(p<surface||p>=surface+N)throw std::runtime_error("capture outside effect canvas");
 if(passes.empty()||passes.back().mode!=1)passes.push_back({1,std::vector<uint32_t>(N)});
 auto& q=passes.back().pixels[p-surface];
 q=(std::min(255u,(q&255)+(value&255)))|(std::min(255u,((q>>8)&255)+((value>>8)&255))<<8)|(std::min(255u,((q>>16)&255)+((value>>16)&255))<<16);
}
void Normal(uint32_t* p,uint32_t value){
 if(p<surface||p>=surface+N)throw std::runtime_error("normal capture outside effect canvas");
 if(passes.empty()||passes.back().mode!=0)passes.push_back({0,std::vector<uint32_t>(N)});
 auto& q=passes.back().pixels[p-surface];if(q)throw std::runtime_error("overlapping normal flash texels");q=value;++normalWrites;
}
void U16(std::ofstream& f,unsigned n){uint16_t v=n;f.write(reinterpret_cast<const char*>(&v),2);}
void U32(std::ofstream& f,unsigned n){f.write(reinterpret_cast<const char*>(&n),4);}
void Encode(std::ofstream& f,const Pass& p){
 std::vector<uint32_t> words;
 // Raw pass pixels are temporary compiler input; packing chooses palette/RLE.
 U32(f,p.mode);U32(f,N);f.write(reinterpret_cast<const char*>(p.pixels.data()),N*4);
}
uint32_t Composite(uint32_t d,uint32_t s,unsigned mode){
 if(!s)return d;
 if(mode)return (d&0xff000000u)|std::min(255u,(d&255)+(s&255))|(std::min(255u,((d>>8)&255)+((s>>8)&255))<<8)|(std::min(255u,((d>>16)&255)+((s>>16)&255))<<16);
 if(s>>24==255)return s;
 unsigned a=s>>24;return 0xff000000u|((s&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));
}
}
extern "C" void pvzh1_reward_effect_export(){
 using namespace Sexy;
 auto* device=gLawnApp->mGLInterface.get();auto saved=std::move(device->mScreenImage);const int oldWidth=device->mWidth,oldHeight=device->mHeight;device->mWidth=2120;device->mHeight=1200;
 device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,2120,1200,W,H);
 auto* screen=device->GetScreenImage();surface=H1GetPhysicalScreen(screen)->Target()->GetBits();h1DirtyCanvas.enabled=false;h1DirtyCanvas.pending=false;
 const auto out=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));std::ofstream raw(out/"reward-effects.raw",std::ios::binary);
 U32(raw,0x57524548);U32(raw,W);U32(raw,H);U32(raw,2);
 unsigned frames=0,comparisons=0;
 for(unsigned kind=0;kind<2;++kind){
  Sexy::SRand(0x48515245u+kind);
  auto* effect=gLawnApp->AddPvzpParticle(1060,600,0,kind?PARTICLE_STARBURST:PARTICLE_SEED_PACKET_PICKUP);if(!effect)throw std::runtime_error("effect creation failed");
  unsigned age=0;const unsigned end=kind?100:600,count=end*3/10+1;U32(raw,kind);U32(raw,count);
  for(unsigned frame=0;frame<count;++frame){
   unsigned tick=(frame*10+1)/3;while(age<tick){effect->Update();++age;}
   effect->OverrideScale(nullptr,kind||age==0?1.f:PvzpAnimateCurveFloat(0,400,age,1.01f,2.f,PvzpCurves::CURVE_EASE_IN_OUT));
   std::fill(surface,surface+N,0xff000000u);passes.clear();normalWrites=0;
   pvzh1_host_offline_pixel_capture=Normal;pvzh1_host_offline_additive_capture=Add;pvzh1_text_capture_active=1;
   Graphics g(screen);
   // Flush each original emitter independently so a normal pass never combines
   // two separately rounded alpha operations at the same destination.
   for(auto* node=effect->mEmitterList.mHead;node;node=node->mNext){
    auto* e=effect->mParticleHolder->mEmitters.DataArrayGet(unsigned(node->mValue));
    if(!passes.empty()&&passes.back().mode==0)passes.push_back({0,std::vector<uint32_t>(N)});
    e->Draw(&g);
   }
   pvzh1_text_capture_active=0;pvzh1_host_offline_pixel_capture=nullptr;pvzh1_host_offline_additive_capture=nullptr;
   passes.erase(std::remove_if(passes.begin(),passes.end(),[](const Pass& p){return std::none_of(p.pixels.begin(),p.pixels.end(),[](uint32_t x){return x;});}),passes.end());
   if(frame%20==0||frame==count-1)for(unsigned background=0;background<3;++background){
    for(unsigned i=0;i<N;++i)surface[i]=0xff000000u|((i*0x164b35u+background*0x38124du)&0xffffffu);
    std::vector<uint32_t> expected(surface,surface+N);effect->Draw(&g);
    for(const auto& p:passes)for(unsigned i=0;i<N;++i){auto s=p.pixels[i];if(!s)continue;auto& d=expected[i];
     if(p.mode)d=Composite(d,s,1);else if(s>>24==255)d=s;else{auto a=s>>24;d=0xff000000u|((s&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}
    }
    if(!std::equal(expected.begin(),expected.end(),surface))throw std::runtime_error("baked reward pass differs from original shader");++comparisons;
   }
   U32(raw,age);U32(raw,passes.size());for(const auto& p:passes)Encode(raw,p);++frames;
   if(frame%20==0)fprintf(stderr,"REWARD_BAKE kind=%u age=%u passes=%zu\n",kind,age,passes.size());
  }
  effect->ParticleSystemDie();
 }
 raw.close();device->mScreenImage=std::move(saved);device->mWidth=oldWidth;device->mHeight=oldHeight;h1DirtyCanvas.enabled=true;
 std::ofstream report(out/"reward-effect-export-verification.json");report<<"{\"passed\":true,\"frames\":"<<frames<<",\"sample_rate\":30,\"whole_frame_comparisons\":"<<comparisons<<",\"canvas\":[960,544],\"viewport\":[480,272],\"seeded_decorative_layout\":true,\"original_clock_and_layer_order\":true}\n";
}
