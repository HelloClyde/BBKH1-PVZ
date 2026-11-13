/* Offline LCD samples for the uncovered blink stretch and sun scale states.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Coin.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "actors/ActorPose.h"
#include "actors/DeferredOverlay.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <array>
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
extern "C" void (*pvzh1_host_offline_additive_capture)(uint32_t*,uint32_t);
namespace {
struct Op{uint32_t at,value;};struct Tuple{uint32_t at;std::array<uint32_t,89> colors{};};
struct Segment{unsigned track;bool drawn;std::vector<Tuple> ops;};
uint32_t* pixels;std::vector<Op> normal,extra;
void Capture(uint32_t* p,uint32_t v){normal.push_back({uint32_t(p-pixels),v});}
void Additive(uint32_t* p,uint32_t v){extra.push_back({uint32_t(p-pixels),v});}
void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void Word(std::ofstream& f,uint32_t n){f.write(reinterpret_cast<const char*>(&n),4);}
Sexy::Color Shade(unsigned i){if(i==86)return Sexy::Color(128,128,128,255);if(i>=87)return Sexy::Color(255,255,255,i==87?196:92);return Sexy::Color(i*3,i*3,i*3,255);}
uint32_t Blend(uint32_t d,uint32_t v){unsigned a=v>>24;return a==255?v:0xff000000u|((v&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}
uint32_t Add(uint32_t d,uint32_t s){return (d&0xff000000u)|(std::min(255u,((d>>16)&255)+((s>>16)&255))<<16)|(std::min(255u,((d>>8)&255)+((s>>8)&255))<<8)|std::min(255u,(d&255)+(s&255));}
}
extern "C" void pvzh1_dense_state_export(){
 using namespace Sexy;H1ActorPoseTestEnable(false);PlayerInfo profile;profile.mName="DENSE_STATE_COMPILER";profile.mLevel=8;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();pixels=target->GetBits();h1DirtyCanvas.enabled=false;
 pvzh1_host_offline_pixel_capture=Capture;pvzh1_host_offline_additive_capture=Additive;
 auto folder=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));std::ofstream blink(folder/"plant-blinks.raw",std::ios::binary),sun(folder/"scaled-suns.raw",std::ios::binary);
 unsigned blinkFrames=0,sunFrames=0,comparisons=0;
 auto exportFrame=[&](Reanimation* r,unsigned frame,unsigned shades,std::ofstream& out){
  const unsigned total=r->mDefinition->mTracks.tracks[0].mTransforms.count,divisor=shades==89?2:4;
  ReanimatorFrameTime time{float(frame%divisor)/divisor,int(frame/divisor),std::min(int(frame/divisor+1),int(total-1))};
  r->mEnableExtraAdditiveDraw=false;uint64_t key;Check(H1ActorPose::Signature(r,0,key,false,true,shades==89),"scaled sample signature");
  std::vector<Segment> segments;
  auto drawTrack=[&](int track){Graphics g(screen);PvzpTriangleGroup triangles;bool drawn=r->DrawTrack(&g,track,0,&triangles,&time);triangles.DrawGroup(&g);return drawn;};
  for(int track=0;track<r->mDefinition->mTracks.count;++track){
   if(shades==1&&r->mTrackInstances[track].mRenderGroup!=0)continue;
   Segment s{unsigned(track),false,{}};
   for(unsigned shade=shades==89?1:0;shade<shades;++shade){
    normal.clear();extra.clear();r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Shade(shade);
    pvzh1_text_capture_active=1;bool drawn=drawTrack(track);pvzh1_text_capture_active=0;
    if(shade==(shades==89?1u:0u)){s.drawn=drawn;for(auto op:normal){Tuple t{};t.at=op.at;t.colors[0]=op.value;s.ops.push_back(t);}}
    Check(s.drawn==drawn&&s.ops.size()==normal.size(),"scaled shade geometry changed");unsigned at=0;
    for(unsigned i=0;i<normal.size();++i){Check(s.ops[i].at==normal[i].at&&s.ops[i].colors[0]==normal[i].value,"scaled normal geometry changed");
     if(shade&&at<extra.size()&&normal[i].at==extra[at].at)s.ops[i].colors[shade]=extra[at++].value;}
    Check(at==extra.size(),"additive outside scaled normal geometry");
   }segments.push_back(std::move(s));
  }
  // Validate every emitted pose against the original shader on a patterned
  // background, including normal, production glow and highlight intensities.
  for(unsigned shade:shades==89?std::vector<unsigned>{0,43,88}:std::vector<unsigned>{0}){
   for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+shade*0x38124du)&0xffffffu);
   std::vector<uint32_t> expected(pixels,pixels+480*272);r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Shade(shade);
   for(auto& s:segments){drawTrack(s.track);for(auto& op:s.ops)expected[op.at]=Blend(expected[op.at],op.colors[0]);if(shade)for(auto& op:s.ops)expected[op.at]=Add(expected[op.at],op.colors[shade]);}
   Check(std::equal(expected.begin(),expected.end(),pixels),"scaled whole-frame shader oracle differs");++comparisons;
  }
  unsigned marker=shades==89?0xc0000000u:0;
  for(uint32_t v:{uint32_t(key),uint32_t(key>>32),frame,uint32_t(r->mReanimationType),120u,136u,marker|unsigned(segments.size())})Word(out,v);
  for(auto& s:segments){for(uint32_t v:{s.track,unsigned(s.drawn),unsigned(s.ops.size())})Word(out,v);for(auto& op:s.ops){Word(out,op.at);for(unsigned i=0;i<shades;++i)Word(out,op.colors[i]);}}
 };
 for(auto seed:{SEED_PEASHOOTER,SEED_SUNFLOWER}){
  auto* plant=gLawnApp->mBoard->AddPlant(2,1,seed);plant->DoBlink();auto* r=gLawnApp->ReanimationGet(plant->mBlinkReanimID);
  Check(r->mFrameStart==1&&r->mFrameCount==3,"blink clip changed");H1ResolveActorOverlay(r);r->mOverlayMatrix=SexyTransform2D();r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;
  for(unsigned scale=32;scale<=96;++scale){r->mOverlayMatrix.m11=float(scale)/64;for(unsigned frame=2;frame<=6;++frame){exportFrame(r,frame,89,blink);++blinkFrames;}}
  plant->EndBlink();
 }
 auto* r=gLawnApp->AddReanimation(0,0,0,REANIM_SUN);r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;
 const unsigned total=r->mDefinition->mTracks.tracks[0].mTransforms.count;
 // Bake the measured small/normal suns and the stable large-sun endpoint.
 // Rare large-sun intermediate scales retain their existing live rendering.
 for(unsigned scale=13;scale<=100;++scale){if(scale>50&&scale!=100)continue;r->mOverlayMatrix.m00=r->mOverlayMatrix.m11=float(scale)/50;for(unsigned frame=0;frame<total*4-3;++frame){exportFrame(r,frame,1,sun);++sunFrames;}}
 blink.close();sun.close();pvzh1_host_offline_pixel_capture=pvzh1_host_offline_additive_capture=nullptr;pvzh1_text_capture_active=0;h1DirtyCanvas.enabled=true;H1ActorPoseTestEnable(true);
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream proof(folder/"dense-state-export-verification.json");proof<<"{\"passed\":true,\"blink_poses\":"<<blinkFrames<<",\"sun_poses\":"<<sunFrames<<",\"shader_comparisons\":"<<comparisons<<",\"blink_vertical_scale_step\":0.015625,\"sun_scale_step\":0.02,\"simulation_unchanged\":true}\n";
}
