/* Bake measured walk/eat transitions, all hit shades and damaged hats in one
 * ordered packet. Authored 20-update transitions are retained in full.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "actors/ActorPose.h"
#include "actors/ActorPoseFormat.h"
#include "actors/DenseStateFormat.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "Resources.h"
#include <array>
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
extern "C" void (*pvzh1_host_offline_additive_capture)(uint32_t*,uint32_t);
namespace {
struct Op{uint32_t at,value;};struct Tuple{uint32_t at;std::array<uint32_t,26> values{};};
struct Segment{unsigned track,variant;std::vector<Tuple> ops;};
uint32_t* surface;std::vector<Op> normal,extra;
void Capture(uint32_t* p,uint32_t v){normal.push_back({uint32_t(p-surface),v});}
void Additive(uint32_t* p,uint32_t v){extra.push_back({uint32_t(p-surface),v});}
void Word(std::ofstream& f,uint32_t n){f.write(reinterpret_cast<const char*>(&n),4);}
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
uint32_t Blend(uint32_t d,uint32_t v){unsigned a=v>>24;return a==255?v:0xff000000u|((v&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}
uint32_t Add(uint32_t d,uint32_t s){return (d&0xff000000u)|(std::min(255u,((d>>16)&255)+((s>>16)&255))<<16)|(std::min(255u,((d>>8)&255)+((s>>8)&255))<<8)|std::min(255u,(d&255)+(s&255));}
}
extern "C" void pvzh1_zombie_blend_export(){
 using namespace Sexy;using namespace H1ActorPose;H1ActorPoseTestEnable(false);
 PlayerInfo profile;profile.mName="ZOMBIE_BLEND_COMPILER";profile.mLevel=8;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_NORMAL,2,0);auto* r=gLawnApp->ReanimationGet(z->mBodyReanimID);r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();surface=target->GetBits();h1DirtyCanvas.enabled=false;
 pvzh1_host_offline_pixel_capture=Capture;pvzh1_host_offline_additive_capture=Additive;
 const auto folder=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));std::ofstream raw(folder/"zombie-blends.raw",std::ios::binary);
 unsigned poses=0,comparisons=0;uint64_t ops=0;bool prototype=getenv("PVZ_BLEND_PROTOTYPE")!=nullptr;
 for(unsigned direction=0;direction<2;++direction){const unsigned first=direction?276:88,last=direction?354:180;
  for(unsigned source=first;source<=(prototype?first:last);++source)for(unsigned bucket=0;bucket<(direction?unsigned(std::size(ZombieWalkRates)):1u);++bucket){
   r->mEnableExtraAdditiveDraw=false;r->mDead=false;r->mLoopCount=0;r->mLoopType=REANIM_LOOP;
   for(int i=0;i<r->mDefinition->mTracks.count;++i){r->mTrackInstances[i].mBlendCounter=0;r->mTrackInstances[i].mImageOverride=nullptr;}
   r->mAnimRate=12;r->SetFramesForLayer(direction?"anim_eat":"anim_walk");r->mAnimTime=float(source-first)/float((r->mFrameCount-1)*2);
   r->StartBlend(20);r->mAnimRate=direction?ZombieWalkRates[bucket]:36;r->SetFramesForLayer(direction?"anim_walk":"anim_eat");
   uint64_t key;Check(Signature(r,0,key,true,true),"zombie blended source signature");
   for(unsigned step=0;step<20;++step){
    ReanimatorFrameTime t;r->GetFrameTime(&t);std::vector<Segment> segments;
    auto draw=[&](int track){Graphics g(screen);PvzpTriangleGroup triangles;r->DrawTrack(&g,track,0,&triangles,&t);triangles.DrawGroup(&g);};
    for(int track=0;track<r->mDefinition->mTracks.count;++track){auto& inst=r->mTrackInstances[track];const char* name=r->mDefinition->mTracks.tracks[track].mName;const bool hat=ZombieHatTrack(name);
     if(inst.mRenderGroup!=0&&!ZombieVisibilityVaries(name)&&!hat)continue;
     bool arm=!std::strcmp(name,"Zombie_outerarm_upper"),bucketHat=!std::strcmp(name,"anim_bucket");unsigned variants=hat?3:arm?2:1;
     const int savedGroup=inst.mRenderGroup;if(hat)inst.mRenderGroup=0;
     for(unsigned variant=0;variant<variants;++variant){
      inst.mImageOverride=hat?(variant==1?(bucketHat?IMAGE_REANIM_ZOMBIE_BUCKET2:IMAGE_REANIM_ZOMBIE_CONE2):variant==2?(bucketHat?IMAGE_REANIM_ZOMBIE_BUCKET3:IMAGE_REANIM_ZOMBIE_CONE3):nullptr):variant?IMAGE_REANIM_ZOMBIE_OUTERARM_UPPER2:nullptr;
      Segment s{unsigned(track),hat?(variant<<8):variant,{}};
      for(unsigned shade=1;shade<=25;++shade){normal.clear();extra.clear();r->mEnableExtraAdditiveDraw=true;r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);
       pvzh1_text_capture_active=1;draw(track);pvzh1_text_capture_active=0;
       if(shade==1)for(auto op:normal){Tuple tuple{};tuple.at=op.at;tuple.values[0]=op.value;s.ops.push_back(tuple);}
       Check(s.ops.size()==normal.size(),"blend shade changes geometry");unsigned at=0;
       for(unsigned i=0;i<normal.size();++i){Check(normal[i].at==s.ops[i].at&&normal[i].value==s.ops[i].values[0],"blend changes normal pixels");if(at<extra.size()&&normal[i].at==extra[at].at)s.ops[i].values[shade]=extra[at++].value;}
       Check(at==extra.size(),"blend additive geometry outside normal");
      }
      // Check each track/variant at the first and last blend step. Remaining
      // samples also check all 25 shade geometries while being exported.
      if(step==0||step==19)for(unsigned shade:{0u,13u,25u}){
       for(unsigned i=0;i<480*272;++i)surface[i]=0xff000000u|((i*0x164b35u+shade*0x38124du)&0xffffffu);
       std::vector<uint32_t> expected(surface,surface+480*272);r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);draw(track);
       for(auto& op:s.ops)expected[op.at]=Blend(expected[op.at],op.values[0]);if(shade)for(auto& op:s.ops)expected[op.at]=Add(expected[op.at],op.values[shade]);
       Check(std::equal(expected.begin(),expected.end(),surface),"blended shader pixels differ");++comparisons;
      }
      ops+=s.ops.size();if(!s.ops.empty())segments.push_back(std::move(s));
     }inst.mImageOverride=nullptr;inst.mRenderGroup=savedGroup;
    }
    for(uint32_t v:{uint32_t(key),uint32_t(key>>32),ZombieBlendFrame(source,step,direction,bucket),uint32_t(REANIM_ZOMBIE),120u,136u,0x80000000u|unsigned(segments.size())})Word(raw,v);
    for(auto& s:segments){for(uint32_t v:{s.track,s.variant,unsigned(s.ops.size())})Word(raw,v);for(auto& op:s.ops){Word(raw,op.at);for(auto v:op.values)Word(raw,v);}}
    ++poses;r->Update();
   }
  }
 }
 raw.close();pvzh1_host_offline_pixel_capture=pvzh1_host_offline_additive_capture=nullptr;pvzh1_text_capture_active=0;h1DirtyCanvas.enabled=true;H1ActorPoseTestEnable(true);
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream proof(folder/"zombie-blend-export-verification.json");proof<<"{\"passed\":true,\"prototype\":"<<(prototype?"true":"false")<<",\"poses\":"<<poses<<",\"shader_comparisons\":"<<comparisons<<",\"ordered_operations\":"<<ops<<",\"original_blend_updates\":20,\"hit_intensities\":26,\"hat_damage_variants\":6}\n";
}
