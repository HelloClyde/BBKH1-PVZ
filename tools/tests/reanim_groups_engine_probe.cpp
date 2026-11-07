/* Whole-frame equivalence of skipping only provably empty render groups. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/ReanimTrack.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <strings.h>
#include <stdexcept>
extern "C" void pvzh1_reanim_groups_probe(){
 auto check=[](bool v,const char* why){if(!v)throw std::runtime_error(why);};unsigned names=0,comparisons=0;uint32_t random=0x3a215f01;
 for(const char* n:{"","a","at","att","attach","attacher_","attacher__","ATTACHER__UPPER","Attacher__mix","anim_idle","zombie","attacher___"}){check(H1IsAttacherTrack(n)==(strncasecmp(n,"attacher__",10)==0),"attacher tag differs");++names;}
 for(unsigned i=0;i<65536;++i){char n[32]="attacher__tail";random=random*1664525u+1013904223u;unsigned position=random%14;n[position]=char(random>>24);check(H1IsAttacherTrack(n)==(strncasecmp(n,"attacher__",10)==0),"random attacher tag differs");++names;}
 PlayerInfo p;p.mName="GROUP_ORACLE";p.mLevel=6;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;
 for(auto seed:{SEED_PEASHOOTER,SEED_SUNFLOWER,SEED_THREEPEATER,SEED_SPLITPEA,SEED_TWINSUNFLOWER,SEED_WALLNUT}){Plant::PreloadPlantResources(seed);b->AddPlant(2,1,seed);}
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();h1DirtyCanvas.enabled=false;
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){if(r->mDead)continue;r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;
  std::vector<int> saved;for(int t=0;t<r->mDefinition->mTracks.count;++t)saved.push_back(r->mTrackInstances[t].mRenderGroup);
  for(unsigned grouping=0;grouping<5;++grouping){for(int t=0;t<r->mDefinition->mTracks.count;++t)r->mTrackInstances[t].mRenderGroup=grouping==0?saved[t]:grouping==1?0:grouping==2?t%18:grouping==3?(t%2?17:-1):18;
   for(bool offline:{false,true})for(unsigned bg=0;bg<3;++bg){std::vector<uint32_t> expected;H1ActorPoseTestEnable(offline);
    for(bool fast:{false,true}){h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);Sexy::Graphics g(screen);
     if(fast)r->DrawAllRenderGroups(&g);else for(int group=RENDER_GROUP_NORMAL;group<=RENDER_GROUP_MAX;++group)r->DrawRenderGroup(&g,group);
     if(!fast)expected.assign(bits,bits+480*272);else{check(expected==std::vector<uint32_t>(bits,bits+480*272),"present render group pixels differ");++comparisons;}
    }
   }
  }for(int t=0;t<r->mDefinition->mTracks.count;++t)r->mTrackInstances[t].mRenderGroup=saved[t];
 }
 H1ActorPoseTestEnable(true);h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;pvzh1_actor_pose_shutdown();
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"reanim-groups-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"tag_comparisons\":"<<names<<"}\n";
}
