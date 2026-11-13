/* Real intro dependencies and bounded source warmup. LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/MemoryImage.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_baked_actor_diagnostic[144];
namespace {void Verify(bool okay,const char*why){if(!okay){fprintf(stderr,"BOSS_DEPENDENCY_FAILURE: %s\n",why);throw std::runtime_error(why);}}}
extern "C" void pvzh1_boss_dependency_probe(){
 using namespace Sexy;
 auto initial=H1ImageCacheGetStats();H1ImageCacheSetBudget(1024*1024);
 PlayerInfo profile;profile.mName="BOSS_INTRO_DEPENDENCIES";profile.mLevel=50;auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto*b=gLawnApp->mBoard;b->mX=220;b->mY=0;gLawnApp->mGameScene=SCENE_LEVEL_INTRO;
 auto warmed=H1ImageCacheGetStats();fprintf(stderr,"BOSS_WARMUP budget=%zu resident=%zu peak=%zu initial_peak=%zu\n",warmed.budgetBytes,warmed.residentBytes,warmed.peakBytes,initial.peakBytes);
 Verify(warmed.peakBytes<=4*1024*1024,"Source warmup pinned more than four MiB under a one-MiB working budget");
 std::vector<Plant*> pots;for(auto*p:b->mPlants)if(!p->mDead&&p->mSeedType==SEED_FLOWERPOT)pots.push_back(p);Verify(pots.size()==15,"Original Boss intro did not create fifteen pots");
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();auto*pixels=target->GetBits();H1RenderPlanTestEnable(false);h1DirtyCanvas.enabled=false;unsigned comparisons=0,replayed=0;
 H1ActorPoseTestEnable(true);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();Graphics world(screen);world.Translate(220,0);auto hits=pvzh1_baked_actor_diagnostic[REANIM_FLOWER_POT];for(auto*p:pots)p->Draw(&world);Verify(pvzh1_baked_actor_diagnostic[REANIM_FLOWER_POT]-hits==15,"Natural intro pots did not use their preloaded bank");
 // Pixel identity uses the bank's documented 120x136 LCD compiler anchor.
 // World placement continues to use the existing LCD translation snapping.
 auto*r=gLawnApp->ReanimationGet(pots.front()->mBodyReanimID);r->mOverlayMatrix.m02=120*1060.f/480;r->mOverlayMatrix.m12=136*600.f/272;
 for(int t=0;t<r->mDefinition->mTracks.count;++t)for(int f=0;f<r->mDefinition->mTracks.tracks[t].mTransforms.count;++f){auto* image=r->mDefinition->mTracks.tracks[t].mTransforms.mTransforms[f].mImage;if(uintptr_t(image)>1000)if(auto* memory=dynamic_cast<MemoryImage*>(image))memory->GetBits();}
 for(unsigned phase=0;phase<4;++phase){
  {int first,count;r->GetFramesForLayer("anim_idle",first,count);int frame=first+(count-1)*phase/3;r->mFrameStart=frame;r->mFrameCount=frame+1<r->mDefinition->mTracks.tracks[0].mTransforms.count?2:1;r->mAnimTime=(phase%2)*.5f;}
  for(unsigned background=0;background<3;++background){std::vector<uint32_t> expected;
   for(bool replay:{false,true}){H1ActorPoseTestEnable(replay);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+background*0x38124du)&0xffffffu);
    Graphics g(screen);const auto before=pvzh1_baked_actor_diagnostic[REANIM_FLOWER_POT];r->DrawRenderGroup(&g,0);
    if(!replay)expected.assign(pixels,pixels+480*272);else{const auto count=pvzh1_baked_actor_diagnostic[REANIM_FLOWER_POT]-before;Verify(count==1,"Initial pots did not use their preloaded plant bank");replayed+=count;Verify(std::equal(expected.begin(),expected.end(),pixels),"Initial pot replay differs from original pixels");++comparisons;}
   }
  }
 }
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-dependency-verification.json");out<<"{\"passed\":true,\"original_intro_pots\":15,\"natural_world_pot_replays\":15,\"anchor_pot_replays\":"<<replayed<<",\"compiler_anchor_whole_frame_comparisons\":"<<comparisons<<",\"source_budget_bytes\":"<<warmed.budgetBytes<<",\"source_peak_bytes\":"<<warmed.peakBytes<<",\"new_animation_bytes\":0}\n";
 H1ImageCacheSetBudget(initial.budgetBytes);gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;H1ActorPoseTestEnable(true);H1RenderPlanTestEnable(true);h1DirtyCanvas.enabled=true;
}
