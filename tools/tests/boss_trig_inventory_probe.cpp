#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpCommon.h"
#include <bit>
#include <fstream>
#include <filesystem>
#include <cstdint>
extern "C" void pvzh1_boss_trig_inventory_probe(){
 PlayerInfo p;p.mLevel=50;p.mName="BOSS_TRIG";auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto*z=gLawnApp->mBoard->GetBossZombie();
 if(!z)z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_BOSS,0,-2);auto*r=gLawnApp->ReanimationGet(z->mBodyReanimID);
 struct Slot{uint32_t angle=0;bool valid=false;} slots[128];
 unsigned calls=0,hits=0,equal=0,tracks=0,zero=0;
 r->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;r->mFrameCount=2;
 unsigned frames=r->mDefinition->mTracks.tracks[0].mTransforms.count;
 for(unsigned frame=0;frame+1<frames;++frame)for(float part:{0.f,.5f}){
  r->mFrameStart=frame;r->mAnimTime=part;ReanimatorFrameTime ft;r->GetFrameTime(&ft);
  for(int i=0;i<r->mDefinition->mTracks.count;++i){
   ReanimatorTransform t;r->GetCurrentTransform(i,&t,&ft);
   if(t.mFrame<0||t.mAlpha<=0||(!t.mImage&&!t.mFont))continue;
   ++tracks;if(t.mSkewX==0&&t.mSkewY==0){++zero;continue;}
   uint32_t a=std::bit_cast<uint32_t>(-DEG_TO_RAD(t.mSkewX)),b=std::bit_cast<uint32_t>(-DEG_TO_RAD(t.mSkewY));equal+=a==b;
   for(auto key:{a,b}){auto& s=slots[(key^(key>>7)^(key>>14))&127];++calls;if(s.valid&&s.angle==key)++hits;else s={key,true};}
  }
 }
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-trig-inventory.json");
 out<<"{\"frames\":"<<frames<<",\"visible_tracks\":"<<tracks<<",\"zero_skew_tracks\":"<<zero<<",\"equal_nonzero_skew_tracks\":"<<equal<<",\"nonzero_angle_lookups\":"<<calls<<",\"exact_128_slot_hits\":"<<hits<<"}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
