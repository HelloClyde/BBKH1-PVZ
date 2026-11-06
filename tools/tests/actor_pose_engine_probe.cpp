/* Compare complete original actor draws with offline replay at bake-grid
 * positions, including parent/child ordering and clips. LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "Common.h"
#include <zlib.h>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[12];
extern "C" volatile uint32_t pvzh1_actor_page_diagnostic[8];
extern void H1ActorPosePageTestSlots(unsigned);
namespace{void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}}
extern "C" void pvzh1_actor_pose_engine_probe(){
 H1ActorPosePageTestSlots(2); // Exercise reader pointers across frequent LRU evictions.
 PlayerInfo profile;profile.mName="POSE_TEST";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
 board->AddPlant(2,1,SeedType::SEED_PEASHOOTER);board->AddPlant(3,2,SeedType::SEED_SUNFLOWER);board->AddZombieInRow(ZombieType::ZOMBIE_NORMAL,2,0);
 auto* sod=gLawnApp->mEffectSystem->mReanimationHolder->AllocReanimation(0,0,0,ReanimationType::REANIM_SODROLL);
 Check(pvzh1_actor_pose_diagnostic[0]>0,"pose pack failed to load");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();h1DirtyCanvas.enabled=false;
 unsigned comparisons=0,replays=0,guarded=0,screenCrops=0;
 for(auto* actor:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
  if(actor->mDead||(actor->mReanimationType!=REANIM_PEASHOOTER&&actor->mReanimationType!=REANIM_SUNFLOWER&&actor->mReanimationType!=REANIM_ZOMBIE&&actor!=sod))continue;
  const bool isSod=actor==sod;actor->mOverlayMatrix.m02=isSod?0:265;actor->mOverlayMatrix.m12=isSod?0:300;
  for(float phase:{0.f,.125f,.25f,.5f,.75f,1.f}){
   // Pick an authored half-frame so original and quantized replay coincide.
   const int half=int(phase*(actor->mFrameCount-1)*2);actor->mAnimTime=float(half)/(2*(actor->mFrameCount-1));
   for(bool clip:{false,true})for(unsigned background=0;background<3;++background){std::vector<uint32_t> expected;
    for(bool replay:{false,true}){H1ActorPoseTestEnable(replay);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();
     for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+background*0x38124du)&0xffffffu);
     Sexy::Graphics g(screen);if(clip)g.ClipRect(245,275,100,100);
     auto before=pvzh1_actor_pose_diagnostic[5];actor->DrawRenderGroup(&g,0);
     if(!replay)expected.assign(bits,bits+480*272);else{if(expected!=std::vector<uint32_t>(bits,bits+480*272)){
       for(unsigned i=0;i<480*272;++i)if(expected[i]!=bits[i]){fprintf(stderr,"pose mismatch type=%d base=%d phase=%f framehalf=%d clip=%d pixel=%u expected=%08x actual=%08x replay=%u miss=%u guard=%u\n",int(actor->mReanimationType),actor->mFrameBasePose,phase,half,clip,i,expected[i],bits[i],pvzh1_actor_pose_diagnostic[5]-before,pvzh1_actor_pose_diagnostic[2],pvzh1_actor_pose_diagnostic[3]);break;}Check(false,"pose replay pixels differ");}replays+=pvzh1_actor_pose_diagnostic[5]-before;++comparisons;}
    }
   }
  }
  Sexy::Graphics g(screen);actor->mColorOverride=Sexy::Color(50,100,150,128);Check(!H1ActorPoseDraw(actor,&g,0),"color override guard failed");actor->mColorOverride=Sexy::Color::White;++guarded;
  if(actor->mReanimationType==REANIM_SUNFLOWER||actor->mReanimationType==REANIM_ZOMBIE||actor==sod){
   const float baseX=actor->mOverlayMatrix.m02,baseY=actor->mOverlayMatrix.m12;h1DirtyCanvas.RequireClear();std::fill(bits,bits+480*272,0xff102030u);
   Check(H1ActorPoseDraw(actor,&g,0),"reference bitmap replay missed");std::vector<uint32_t> sprite(bits,bits+480*272);
   for(auto delta:{Sexy::Point(-150,-150),Sexy::Point(350,-150),Sexy::Point(-150,130),Sexy::Point(350,130)}){
    actor->mOverlayMatrix.m02=baseX+delta.mX*1060.f/480;actor->mOverlayMatrix.m12=baseY+delta.mY*600.f/272;std::fill(bits,bits+480*272,0xff102030u);
    Check(H1ActorPoseDraw(actor,&g,0),"LCD boundary unnecessarily used triangle fallback");std::vector<uint32_t> expected(480*272,0xff102030u);
    for(int y=0;y<272;++y)for(int x=0;x<480;++x){int tx=x+delta.mX,ty=y+delta.mY;if(tx>=0&&tx<480&&ty>=0&&ty<272)expected[ty*480+tx]=sprite[y*480+x];}
    Check(expected==std::vector<uint32_t>(bits,bits+480*272),"LCD bitmap crop differs");++screenCrops;
   }actor->mOverlayMatrix.m02=baseX;actor->mOverlayMatrix.m12=baseY;
  }
 }
 Check(replays>=comparisons/3,"pose path was bypassed");
 pvzh1_actor_pose_shutdown();Check(!pvzh1_actor_pose_diagnostic[0],"pose shutdown leaked");
 const std::string resourceFolder=Sexy::GetResourceFolder();std::ifstream input(Sexy::GetResourcePath("actor-poses.h1ap"),std::ios::binary);
 std::vector<char> encoded((std::istreambuf_iterator<char>(input)),{}),original=encoded;
 if(encoded.size()>=32&&!std::memcmp(encoded.data(),"H1PZ",4)){
  auto word=[&](unsigned at){uint32_t n;std::memcpy(&n,encoded.data()+at,4);return n;};
  original.resize(word(8));for(unsigned i=0;i<word(12);++i){auto offset=word(32+i*12),length=word(36+i*12);uLongf n=std::min(size_t(4096),original.size()-i*4096);
   Check(uncompress((Bytef*)original.data()+i*4096,&n,(const Bytef*)encoded.data()+32+word(24)+offset,length)==Z_OK,"independent actor fixture decode failed");}
 }
 Check(pvzh1_actor_page_diagnostic[7]>0&&!pvzh1_actor_page_diagnostic[4]&&pvzh1_actor_page_diagnostic[1]<=8192,"paged cache exceeded two-slot stress limit");
 const auto temp=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"bad-assets";std::filesystem::create_directory(temp);Sexy::SetResourceFolder(temp.string());
 unsigned failures=0;
 auto rejected=[&](){auto before=pvzh1_actor_pose_diagnostic[7];H1ActorPosePrepare();Check(!pvzh1_actor_pose_diagnostic[0]&&pvzh1_actor_pose_diagnostic[7]==before+1,"malformed pack was accepted or leaked");
  H1ActorPosePrepare();Check(pvzh1_actor_pose_diagnostic[7]==before+1,"failed pack retried each draw");pvzh1_actor_pose_shutdown();++failures;};
 rejected();
 for(int corruption=0;corruption<5;++corruption){auto bytes=original;
  if(corruption==0)bytes.resize(4);
  if(corruption==1)bytes[0]^=1;
  if(corruption==2)bytes.back()^=1;
  if(corruption==3){uint32_t packet;std::memcpy(&packet,bytes.data()+64+12,4);bytes[packet+4]=0;bytes[packet+5]=0;uint32_t crc=crc32(0,reinterpret_cast<const Bytef*>(bytes.data()+64),bytes.size()-64);std::memcpy(bytes.data()+40,&crc,4);}
  if(corruption==4){uint32_t entries;std::memcpy(&entries,bytes.data()+16,4);bool changed=false;for(unsigned i=0;i<entries;++i){uint32_t packet;std::memcpy(&packet,bytes.data()+64+i*24+12,4);if(bytes[packet+6]==1){uint32_t bad=0xffffffffu;std::memcpy(bytes.data()+packet+24,&bad,4);changed=true;break;}}Check(changed,"missing shared flash reference fixture");uint32_t crc=crc32(0,reinterpret_cast<const Bytef*>(bytes.data()+64),bytes.size()-64);std::memcpy(bytes.data()+40,&crc,4);}
  {std::ofstream f(temp/"actor-poses.h1ap",std::ios::binary);f.write(bytes.data(),bytes.size());}rejected();
 }
 for(unsigned fault=0;fault<3;++fault){auto bytes=encoded;if(fault==0)bytes.resize(40);if(fault==1)bytes.back()^=1;if(fault==2)bytes[8]^=16;
  {std::ofstream f(temp/"actor-poses.h1ap",std::ios::binary);f.write(bytes.data(),bytes.size());}rejected();}
 H1ActorPosePageTestSlots(32);
 Sexy::SetResourceFolder(resourceFolder);h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"actor-pose-engine-verification.json");out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"pose_replays\":"<<replays<<",\"color_override_guards\":"<<guarded<<",\"LCD_bitmap_boundary_cases\":"<<screenCrops<<",\"missing_or_malformed_packs\":"<<failures<<",\"shutdown_bytes\":0}\n";
}
