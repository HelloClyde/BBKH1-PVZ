/* Independent compiler-image oracle plus real particle-system lifecycle. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/PvzpParticle.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/PvzpCommon.h"
#include "graphics/RewardEffects.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <algorithm>
extern "C" volatile uint32_t pvzh1_reward_effect_diagnostic[12];
namespace {
void Check(bool x){if(!x)throw std::runtime_error("Reward replay oracle/lifecycle mismatch");}
unsigned Read(std::ifstream& f){unsigned n;f.read(reinterpret_cast<char*>(&n),4);Check(bool(f));return n;}
uint32_t Quant(uint32_t v){return std::min(255u,((v&255)+8)/16*16)|(std::min(255u,(((v>>8)&255)+8)/16*16)<<8)|(std::min(255u,(((v>>16)&255)+8)/16*16)<<16);}
uint32_t Blend(uint32_t d,uint32_t s,unsigned mode){if(!s)return d;if(mode)return 0xff000000u|std::min(255u,(d&255)+(s&255))|(std::min(255u,((d>>8)&255)+((s>>8)&255))<<8)|(std::min(255u,((d>>16)&255)+((s>>16)&255))<<16);if(s>>24==255)return s;unsigned a=s>>24;return 0xff000000u|((s&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}
}
extern "C" void pvzh1_reward_effect_probe(){
 using namespace Sexy;H1RewardEffectsPrepare();Check(pvzh1_reward_effect_diagnostic[0]>0&&pvzh1_reward_effect_diagnostic[2]==0);
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();
 std::ifstream f(getenv("PVZ_REWARD_REFERENCE"),std::ios::binary);Check(bool(f));Check(Read(f)==0x57524548&&Read(f)==960&&Read(f)==544&&Read(f)==2);
 unsigned cases=0,maxError[3]{};
 for(unsigned kind=0;kind<2;++kind){Check(Read(f)==kind);unsigned frames=Read(f);
  for(unsigned frame=0;frame<frames;++frame){unsigned age=Read(f),np=Read(f);struct Pass{unsigned mode;std::vector<uint32_t> pixels;};std::vector<Pass> passes;
   for(unsigned n=0;n<np;++n){unsigned mode=Read(f);Check(Read(f)==960*544);passes.push_back({mode,std::vector<uint32_t>(960*544)});f.read(reinterpret_cast<char*>(passes.back().pixels.data()),960*544*4);Check(bool(f));}
   int at=std::max(0,int(age)-3);auto curve=[&](float from,float to){return PvzpAnimateCurveFloat(0,350,at,from,to,PvzpCurves::CURVE_EASE_OUT);};
   int minx=kind?0:std::max(0,int(std::floor(curve(0,240)))-2),maxx=kind?480:std::min(480,int(std::ceil(curve(480,240)))+2);
   int miny=kind?0:std::max(0,int(std::floor(curve(0,200.f*272/600)))-2),maxy=kind?272:std::min(272,int(std::ceil(curve(272,200.f*272/600)))+2);
   for(unsigned placement=0;placement<6;++placement){int cx=(placement==1||placement==4)?minx:(placement==2||placement==5)?maxx:(minx+maxx)/2,cy=(placement==1||placement==5)?miny:(placement==2||placement==4)?maxy:(miny+maxy)/2;
    h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+placement*0x38124du)&0xffffffu);
    std::vector<uint32_t> expected(bits,bits+480*272),unquantized(expected);Graphics g(screen);if(placement==3)g.SetClipRect(61,33,550,401);
    auto clip=H1GetPhysicalScreen(screen)->MapRect(g.mClipRect).Intersection(Rect(0,0,480,272));
    for(const auto& p:passes)for(int y=clip.mY;y<clip.mY+clip.mHeight;++y)for(int x=clip.mX;x<clip.mX+clip.mWidth;++x){int sx=x-cx+480,sy=y-cy+272;if(sx<0||sy<0||sx>=960||sy>=544)continue;auto s=p.pixels[sy*960+sx];auto i=y*480+x;expected[i]=Blend(expected[i],p.mode?Quant(s):s,p.mode);unquantized[i]=Blend(unquantized[i],s,p.mode);}
    Check(H1RewardEffectsTestDraw(kind,age,&g,cx,cy));Check(std::equal(expected.begin(),expected.end(),bits));
    for(unsigned i=0;i<480*272;++i)for(unsigned channel=0;channel<3;++channel){unsigned shift=channel*8;unsigned error=std::abs(int((bits[i]>>shift)&255)-int((unquantized[i]>>shift)&255));maxError[channel]=std::max(maxError[channel],error);}++cases;
   }
   Graphics guard(screen);Check(!H1RewardEffectsTestDraw(kind,age,&guard,minx-1,miny));
  }
 }
 Check(maxError[0]<=8&&maxError[1]<=8&&maxError[2]<=8);
 PlayerInfo profile;profile.mName="REWARD_TEST";profile.mLevel=2;auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);gLawnApp->StartPlaying();gLawnApp->mBoard->mBoardFadeOutCounter=600;
 auto* effect=gLawnApp->AddPvzpParticle(530,300,0,PARTICLE_SEED_PACKET_PICKUP);Check(effect);auto* emitter=effect->mParticleHolder->mEmitters.DataArrayGet(unsigned(effect->mEmitterList.mHead->mValue));
 for(unsigned i=0;i<121;++i)effect->Update();Check(emitter->mSystemAge==0);Graphics g(screen);effect->Draw(&g);Check(pvzh1_reward_effect_diagnostic[6]==121);
 effect->mDontUpdate=true;effect->Update();effect->Draw(&g);Check(pvzh1_reward_effect_diagnostic[6]==121);effect->mDontUpdate=false;
 effect->OverrideColor(nullptr,Color(255,0,0));effect->Draw(&g);Check(pvzh1_reward_effect_diagnostic[7]>0&&emitter->mSystemAge==121);effect->ParticleSystemDie();
 auto* stars=H1RewardStarburst(gLawnApp,530,300,0);Check(stars);for(unsigned i=0;i<101;++i)stars->Update();Check(stars->mDead);
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=previous;pvzh1_reward_effect_shutdown();Check(pvzh1_reward_effect_diagnostic[0]==0);
 unsigned bad=0;if(const char* directory=getenv("PVZ_REWARD_BAD_DIR"))for(auto name:{"missing.h1re","truncated.h1re","bad-crc.h1re","bad-index.h1re","bad-row.h1re","bad-lz-offset.h1re"}){auto p=std::filesystem::path(directory)/name;Check(!H1RewardEffectsTestLoad(p.string().c_str()));Check(pvzh1_reward_effect_diagnostic[0]==0);++bad;}
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"reward-effect-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<cases<<",\"frames\":212,\"placements_and_clips\":6,\"ROI_outside_guard_cases\":212,\"RGB8_max_error_BGR\":["<<maxError[0]<<","<<maxError[1]<<","<<maxError[2]<<"],\"no_live_particle_physics\":true,\"pause_and_tint_fallback\":true,\"starburst_lifetime\":true,\"missing_or_malformed_files\":"<<bad<<",\"shutdown_bytes\":0}\n";
}
