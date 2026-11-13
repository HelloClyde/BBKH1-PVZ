/* Bake both authored 20-update shooting transitions, rather than omitting
 * their blends. SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpCommon.h"
#include "PvzpLib/EffectSystem.h"
#include "actors/ActorPose.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
namespace{struct Op{uint32_t destination,literal;};std::vector<Op> ops;uint32_t* surface;
void Capture(uint32_t* p,uint32_t value){if(p<surface||p>=surface+480*272)throw std::runtime_error("blend capture range");ops.push_back({uint32_t(p-surface),value});}
void U32(std::ofstream& f,uint32_t n){f.write(reinterpret_cast<const char*>(&n),4);}}
extern "C" void pvzh1_pea_layered_export(){
 using namespace Sexy;H1ActorPoseTestEnable(false);H1RenderPlanTestEnable(false);H1TextReplayTestEnable(false);PlayerInfo p;p.mName="BLEND_COMPILER";p.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* plant=gLawnApp->mBoard->AddPlant(2,1,SEED_PEASHOOTER);auto* head=gLawnApp->ReanimationGet(plant->mHeadReanimID);head->mOverlayMatrix.m02=265;head->mOverlayMatrix.m12=300;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();surface=target->GetBits();h1DirtyCanvas.enabled=false;pvzh1_host_offline_pixel_capture=Capture;
 std::ofstream raw(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"pea-layered.raw",std::ios::binary);unsigned count=0,comparisons=0;uint64_t operations=0;
 auto clear=[&](){for(int i=0;i<head->mDefinition->mTracks.count;++i){head->mTrackInstances[i].mBlendCounter=0;head->mTrackInstances[i].mBlendTime=0;}head->mDead=false;head->mLoopCount=0;};
 auto capture=[&](uint32_t frame){
  struct Segment{int track;bool drawn;std::vector<Op> pixels;};std::vector<Segment> segments;
  ReanimatorFrameTime time;head->GetFrameTime(&time);
  for(int track=0;track<head->mDefinition->mTracks.count;++track){
   h1DirtyCanvas.RequireClear();ops.clear();pvzh1_text_capture_active=1;Graphics g(screen);PvzpTriangleGroup triangles;
   bool drawn=head->DrawTrack(&g,track,0,&triangles,&time);triangles.DrawGroup(&g);pvzh1_text_capture_active=0;
   segments.push_back({track,drawn,ops});
  }
  for(unsigned bg=0;bg<3;++bg){for(unsigned i=0;i<480*272;++i)surface[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);std::vector<uint32_t> expected(surface,surface+480*272);
   Graphics g(screen);head->Draw(&g);
   for(auto&segment:segments)for(auto op:segment.pixels){auto&d=expected[op.destination];unsigned a=op.literal>>24;d=a==255?op.literal:0xff000000u|((op.literal&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}
   if(expected!=std::vector<uint32_t>(surface,surface+480*272))throw std::runtime_error("layered head replay differs");++comparisons;
  }
  uint64_t key;if(!H1ActorPose::Signature(head,0,key,true,false,true))throw std::runtime_error("layered blend identity rejected");
  for(uint32_t v:{uint32_t(key),uint32_t(key>>32),frame,4u,120u,136u,0x40000000u|uint32_t(segments.size())})U32(raw,v);
  for(auto&segment:segments){for(uint32_t v:{uint32_t(segment.track),uint32_t(segment.drawn),uint32_t(segment.pixels.size())})U32(raw,v);
   for(auto op:segment.pixels){U32(raw,uint32_t(int(op.destination%480)-120));U32(raw,uint32_t(int(op.destination/480)-136));U32(raw,op.literal);U32(raw,0);}operations+=segment.pixels.size();
  }++count;
 };

 for(unsigned oldHalf=58;oldHalf<=106;++oldHalf){clear();head->mAnimRate=18;head->SetFramesForLayer("anim_head_idle");head->mAnimTime=float(oldHalf-58)/48;head->StartBlend(20);head->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;head->mAnimRate=35;head->SetFramesForLayer("anim_shooting");
  for(unsigned step=0;step<20;++step){capture(0x80000000u|(oldHalf<<8)|step);head->Update();}
 }
 for(unsigned oldHalf:{132u,156u})for(unsigned newHalf=58;newHalf<=106;++newHalf)for(unsigned step=0;step<20;++step){clear();head->mAnimRate=35;head->SetFramesForLayer("anim_shooting");head->mAnimTime=float(oldHalf-108)/48;head->StartBlend(20);head->SetFramesForLayer("anim_head_idle");head->mLoopType=REANIM_LOOP;head->mAnimRate=18;head->mAnimTime=float(newHalf-58)/48;
  for(int i=0;i<head->mDefinition->mTracks.count;++i)if(head->mTrackInstances[i].mBlendCounter)head->mTrackInstances[i].mBlendCounter=20-step;
  capture((oldHalf==156?0x90000000u:0xa0000000u)|(newHalf<<8)|step);
 }
 raw.close();pvzh1_host_offline_pixel_capture=nullptr;H1ActorPoseTestEnable(true);h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"pea-layered-export-verification.json");out<<"{\"passed\":true,\"blended_poses\":"<<count<<",\"whole_frame_comparisons\":"<<comparisons<<",\"ordered_pixel_operations\":"<<operations<<",\"original_blend_updates\":20,\"blend_updates_omitted\":0}\n";
}
