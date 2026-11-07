/* Original production-plant glow: shared geometry, every legal intensity. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "actors/ActorPose.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include <array>
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
extern "C" void (*pvzh1_host_offline_additive_capture)(uint32_t*,uint32_t);
namespace {
constexpr unsigned Shades=89;
struct Op{uint32_t at,value;};struct Tuple{uint32_t at;std::array<uint32_t,Shades> values;};
struct Segment{int track;bool drawn;std::vector<Tuple> ops;};
uint32_t* surface;std::vector<Op> normal,extra;
void CaptureNormal(uint32_t* p,uint32_t v){normal.push_back({uint32_t(p-surface),v});}
void CaptureExtra(uint32_t* p,uint32_t v){extra.push_back({uint32_t(p-surface),v});}
void U32(std::ofstream& f,uint32_t n){f.write(reinterpret_cast<const char*>(&n),4);}
void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
Sexy::Color Shade(unsigned i){if(i==86)return Sexy::Color(128,128,128,255);if(i>=87)return Sexy::Color(255,255,255,i==87?196:92);return Sexy::Color(i*3,i*3,i*3,255);}
uint32_t Add(uint32_t d,uint32_t s){return (d&0xff000000u)|(std::min(255u,((d>>16)&255)+((s>>16)&255))<<16)|(std::min(255u,((d>>8)&255)+((s>>8)&255))<<8)|std::min(255u,(d&255)+(s&255));}
}
extern "C" void pvzh1_sunflower_glow_export(){
 using namespace Sexy;H1ActorPoseTestEnable(false);H1RenderPlanTestEnable(false);H1TextReplayTestEnable(false);
 PlayerInfo p;p.mName="SUNFLOWER_GLOW_COMPILER";p.mLevel=6;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* plant=gLawnApp->mBoard->AddPlant(2,1,SEED_SUNFLOWER);auto* r=gLawnApp->ReanimationGet(plant->mBodyReanimID);
 r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;for(int i=0;i<r->mDefinition->mTracks.count;++i)r->mTrackInstances[i].mRenderGroup=0;
 uint64_t key;Check(H1ActorPose::Signature(r,0,key,true,true,true),"sunflower glow source identity");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();surface=target->GetBits();h1DirtyCanvas.enabled=false;
 pvzh1_host_offline_pixel_capture=CaptureNormal;pvzh1_host_offline_additive_capture=CaptureExtra;
 const auto folder=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));std::ofstream raw(folder/"sunflower-glow.raw",std::ios::binary);unsigned poses=0,comparisons=0;uint64_t operations=0;const unsigned total=r->mDefinition->mTracks.tracks[0].mTransforms.count;
 for(unsigned frame=0;frame<total*2-1;++frame){
  ReanimatorFrameTime t{(frame&1)*.5f,int(frame/2),std::min(int(frame/2+1),int(total-1))};std::vector<Segment> segments;
  for(int track=0;track<r->mDefinition->mTracks.count;++track){Segment segment{track,false,{}};
   for(unsigned shade=1;shade<Shades;++shade){
    normal.clear();extra.clear();h1DirtyCanvas.RequireClear();r->mEnableExtraAdditiveDraw=true;r->mExtraAdditiveColor=Shade(shade);
    pvzh1_text_capture_active=1;Graphics g(screen);PvzpTriangleGroup triangles;const bool drawn=r->DrawTrack(&g,track,0,&triangles,&t);triangles.DrawGroup(&g);pvzh1_text_capture_active=0;
    if(shade==1){segment.drawn=drawn;for(auto op:normal){Tuple tuple{};tuple.at=op.at;tuple.values[0]=op.value;segment.ops.push_back(tuple);}}
    Check(segment.ops.size()==normal.size()&&segment.drawn==drawn,"glow geometry changes with intensity");
    unsigned addAt=0;for(unsigned i=0;i<normal.size();++i){Check(normal[i].at==segment.ops[i].at&&normal[i].value==segment.ops[i].values[0],"glow changes normal pixels");if(addAt<extra.size()&&normal[i].at==extra[addAt].at)segment.ops[i].values[shade]=extra[addAt++].value;}
    Check(addAt==extra.size(),"glow adds outside captured normal geometry");
   }operations+=segment.ops.size();segments.push_back(std::move(segment));
  }
  for(unsigned shade=0;shade<Shades;++shade)for(unsigned bg=0;bg<3;++bg){
   for(unsigned i=0;i<480*272;++i)surface[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);std::vector<uint32_t> expected(surface,surface+480*272);
   r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Shade(shade);Graphics g(screen);PvzpTriangleGroup triangles;
   for(int track=0;track<r->mDefinition->mTracks.count;++track)r->DrawTrack(&g,track,0,&triangles,&t);triangles.DrawGroup(&g);
   for(auto& segment:segments){for(auto& op:segment.ops){auto& d=expected[op.at];uint32_t v=op.values[0],a=v>>24;d=a==255?v:0xff000000u|((v&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}if(shade)for(auto& op:segment.ops)expected[op.at]=Add(expected[op.at],op.values[shade]);}
   Check(expected==std::vector<uint32_t>(surface,surface+480*272),"sunflower glow whole-frame differs");++comparisons;
  }
  for(uint32_t v:{uint32_t(key),uint32_t(key>>32),frame,uint32_t(REANIM_SUNFLOWER),120u,136u,0xc0000000u|uint32_t(segments.size())})U32(raw,v);
  for(auto& segment:segments){for(uint32_t v:{uint32_t(segment.track),uint32_t(segment.drawn),uint32_t(segment.ops.size())})U32(raw,v);for(auto& op:segment.ops){U32(raw,op.at);for(auto v:op.values)U32(raw,v);}}
  ++poses;
 }
 raw.close();r->mEnableExtraAdditiveDraw=false;pvzh1_host_offline_pixel_capture=nullptr;pvzh1_host_offline_additive_capture=nullptr;H1ActorPoseTestEnable(true);h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(folder/"sunflower-glow-export-verification.json");out<<"{\"passed\":true,\"poses\":"<<poses<<",\"intensities\":"<<Shades<<",\"whole_frame_comparisons\":"<<comparisons<<",\"shared_geometry_operations\":"<<operations<<",\"production_and_hit_gray_steps\":3,\"includes_imitater_limit_128\":true,\"includes_highlight_alpha_196_and_92\":true,\"gameplay_changed\":false}\n";
}
