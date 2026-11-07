/* The aligned ordinary-frame decoder must match the complete original draw. */
#define pvzh1_actor_pose_engine_probe pvzh1_actor_pose_original_probe
#include "actor_pose_engine_probe.cpp"
#undef pvzh1_actor_pose_engine_probe
#include "actors/ActorPoseFormat.h"
#include <set>
extern "C" volatile uint32_t pvzh1_normal_actor_diagnostic[4];
extern "C" void pvzh1_actor_normal_probe(){
 pvzh1_actor_pose_original_probe();
 Check(pvzh1_normal_actor_diagnostic[1]>50,"ordinary aligned path was bypassed");
 PlayerInfo profile;profile.mName="NORMAL_TEST";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;board->AddPlant(2,1,SEED_PEASHOOTER);board->AddPlant(3,2,SEED_SUNFLOWER);
 const auto folder=Sexy::GetResourceFolder();std::ifstream f(Sexy::GetResourcePath("actor-normal.h1ap"),std::ios::binary);std::vector<uint32_t> words(462976/4);f.read(reinterpret_cast<char*>(words.data()),words.size()*4);Check(f.gcount()==462976,"normal fixture missing");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();h1DirtyCanvas.enabled=false;
 unsigned frames=0,comparisons=0;std::set<unsigned> covered;
 for(auto* actor:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
  if(actor->mDead||(actor->mReanimationType!=REANIM_PEASHOOTER&&actor->mReanimationType!=REANIM_SUNFLOWER))continue;
  actor->mOverlayMatrix.m02=265;actor->mOverlayMatrix.m12=300;std::set<int> groups;
  for(int t=0;t<actor->mDefinition->mTracks.count;++t)if(actor->mTrackInstances[t].mRenderGroup>=0)groups.insert(actor->mTrackInstances[t].mRenderGroup);
  for(int group:groups){uint64_t key;Check(H1ActorPose::Signature(actor,group,key),"source signature rejected");
   for(unsigned i=0;i<words[4];++i){const auto* e=words.data()+16+i*6;if((uint64_t(e[1])<<32|e[0])!=key)continue;
    unsigned half=e[2];actor->mFrameStart=half/2;actor->mFrameCount=std::min(2,actor->mDefinition->mTracks.tracks[0].mTransforms.count-actor->mFrameStart);actor->mLoopType=REANIM_LOOP;actor->mAnimTime=(half&1)?.5:0;
    ReanimatorFrameTime time;actor->GetFrameTime(&time);Check(time.mAnimFrameBeforeInt==int(half/2)&&time.mFraction==(half&1)*.5f,"authored frame selection differs");
    for(unsigned bg=0;bg<3;++bg){std::vector<uint32_t> expected;
     for(bool replay:{false,true}){H1ActorPoseTestEnable(replay);h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned p=0;p<480*272;++p)bits[p]=0xff000000u|((p*0x164b35u+bg*0x38124du)&0xffffffu);
      Sexy::Graphics g(screen);auto before=pvzh1_normal_actor_diagnostic[1];actor->DrawRenderGroup(&g,group);
      if(!replay)expected.assign(bits,bits+480*272);else{Check(pvzh1_normal_actor_diagnostic[1]>before,"aligned authored frame missed");Check(expected==std::vector<uint32_t>(bits,bits+480*272),"aligned authored frame pixels differ");++comparisons;}
     }
    }covered.insert(i);++frames;
   }
  }
 }
 Check(covered.size()==211,"not every aligned packet was exercised");pvzh1_actor_pose_shutdown();Check(!pvzh1_normal_actor_diagnostic[0]&&!pvzh1_actor_pose_diagnostic[0],"normal cache leaked");
 const auto temp=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"normal-faults";std::filesystem::create_directory(temp);
 for(const char* name:{"pea-transitions.h1ap","zombie-hit.h1ap","sod-roll.h1ap","sun-poses.h1ap","plant-shadows.h1ap"})std::filesystem::create_hard_link(std::filesystem::path(folder)/name,temp/name);
 unsigned faults=0;Sexy::SetResourceFolder(temp.string());
 const int faultCount=words[15]==52?8:4;
 for(int fault=0;fault<faultCount;++fault){if(fault){auto bad=words;if(fault==1)bad[0]^=1;if(fault==2)bad.back()^=1;if(fault==3)bad[16+3]|=1;
   const unsigned packet=words[16+3];
   if(fault==4)bad[(packet+28)/4]=0x7fffffffu;
   if(fault==5)bad[(packet+28)/4]=65536;
   if(fault==6)reinterpret_cast<uint16_t*>(bad.data())[(packet+8)/2]=32767;
   if(fault==7)reinterpret_cast<uint16_t*>(bad.data())[(packet+6)/2]=20;
   if(fault>=3)bad[10]=crc32(0,reinterpret_cast<const Bytef*>(bad.data()+16),(bad.size()-16)*4);
   std::ofstream out(temp/"actor-normal.h1ap",std::ios::binary);out.write(reinterpret_cast<char*>(bad.data()),bad.size()*4);}
  Check(!H1ActorPosePrepare(),"missing/corrupt required normal bank was accepted");Check(pvzh1_actor_pose_diagnostic[0]>0&&!pvzh1_normal_actor_diagnostic[0],"normal bank failure leaked its pixels");pvzh1_actor_pose_shutdown();++faults;
 }
 Sexy::SetResourceFolder(folder);h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"actor-normal-engine-verification.json");out<<"{\"passed\":true,\"unique_packets\":"<<covered.size()<<",\"authored_frames\":"<<frames<<",\"whole_frame_comparisons\":"<<comparisons<<",\"required_asset_failures_rejected\":"<<faults<<",\"shutdown_bytes\":0}\n";
}
