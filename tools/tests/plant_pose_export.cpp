/* Offline original-shader compiler for every plant's authored frames.
 * Ordered track spans retain runtime visibility and attachment ordering.
 * LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/Graphics.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include "graphics/MemoryImage.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <stdexcept>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
namespace {
struct Op{uint32_t destination,literal;};
uint32_t* surface;std::vector<Op> captured;
void Capture(uint32_t* p,uint32_t literal){if(p<surface||p>=surface+480*272)throw std::runtime_error("Plant compiler capture outside LCD");captured.push_back({uint32_t(p-surface),literal});}
void U32(std::ofstream& f,uint32_t value){f.write(reinterpret_cast<const char*>(&value),4);}
struct Segment{int track;bool drawn;std::vector<Op> ops;};
struct Output{std::ofstream raw;std::set<std::pair<uint64_t,unsigned>> keys;std::map<uint64_t,unsigned> stateKeys;unsigned frames=0,comparisons=0,actors=0;uint64_t operations=0;};
std::pair<const char*,std::vector<Sexy::Image*>> Damage(ReanimationType type){
 using namespace Sexy;
 switch(type){
  case REANIM_WALLNUT:return {"anim_face",{nullptr,IMAGE_REANIM_WALLNUT_CRACKED1,IMAGE_REANIM_WALLNUT_CRACKED2}};
  case REANIM_TALLNUT:return {"anim_idle",{nullptr,IMAGE_REANIM_TALLNUT_CRACKED1,IMAGE_REANIM_TALLNUT_CRACKED2}};
  case REANIM_GARLIC:return {"anim_face",{nullptr,IMAGE_REANIM_GARLIC_BODY2,IMAGE_REANIM_GARLIC_BODY3}};
  case REANIM_PUMPKIN:return {"Pumpkin_front",{nullptr,IMAGE_REANIM_PUMPKIN_DAMAGE1,IMAGE_REANIM_PUMPKIN_DAMAGE3}};
  default:return {nullptr,{nullptr}};
 }
}
}
extern "C" void pvzh1_plant_pose_export(){
 PlayerInfo profile;profile.mName="PLANT_FRAME_COMPILER";profile.mId=1;profile.mLevel=6;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();surface=target->GetBits();h1DirtyCanvas.enabled=false;
 const auto folder=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"plant-raw";std::filesystem::create_directories(folder);
 std::set<Reanimation*> seen;for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)seen.insert(r);
 std::map<int,std::unique_ptr<Output>> outputs;pvzh1_host_offline_pixel_capture=Capture;
 Sexy::H1RenderPlanTestEnable(false);Sexy::H1TextReplayTestEnable(false);
 for(int seed=0;seed<int(NUM_SEED_TYPES);++seed){
  Plant::PreloadPlantResources(SeedType(seed));board->AddPlant(2,1,SeedType(seed));
  for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
   if(r->mDead||seen.contains(r))continue;seen.insert(r);if(!H1ActorPose::PlantType(r->mReanimationType))continue;
   const int type=int(r->mReanimationType);auto& slot=outputs[type];if(!slot){slot=std::make_unique<Output>();slot->raw.open(folder/(std::to_string(type)+".raw"),std::ios::binary);}
   auto& out=*slot;++out.actors;
   // Filtering asks GetBits() for the original logical texture. Stabilize that
   // representation before compiling normal and filtered packets alike.
   for(int t=0;t<r->mDefinition->mTracks.count;++t)for(int f=0;f<r->mDefinition->mTracks.tracks[t].mTransforms.count;++f){auto* image=r->mDefinition->mTracks.tracks[t].mTransforms.mTransforms[f].mImage;if(uintptr_t(image)>1000)if(auto* memory=dynamic_cast<Sexy::MemoryImage*>(image))memory->GetBits();}
   for(auto* image:Damage(r->mReanimationType).second)if(auto* memory=dynamic_cast<Sexy::MemoryImage*>(image))memory->GetBits();
   // Render every track independently. Selection and attachment existence are
   // deliberately not baked into the identity; their current state chooses
   // the already rendered track at playback time.
   for(int i=0;i<r->mDefinition->mTracks.count;++i){r->mTrackInstances[i].mRenderGroup=0;r->mTrackInstances[i].mBlendCounter=0;r->mTrackInstances[i].mShakeX=r->mTrackInstances[i].mShakeY=0;}
   const int bx=120,by=136;r->mOverlayMatrix.m02=bx*1060.f/480;r->mOverlayMatrix.m12=by*600.f/272;
   const auto damage=Damage(r->mReanimationType);const int total=r->mDefinition->mTracks.tracks[0].mTransforms.count;
   for(auto* image:damage.second){if(damage.first)r->SetImageOverride(damage.first,image);
    for(auto filter:{FILTER_EFFECT_NONE,FILTER_EFFECT_WASHED_OUT,FILTER_EFFECT_LESS_WASHED_OUT,FILTER_EFFECT_WHITE}){
     r->mFilterEffect=filter;uint64_t key;
     if(!H1ActorPose::Signature(r,0,key,false,false,true))throw std::runtime_error("Plant compiler lacks source state: "+std::to_string(seed)+"/"+std::to_string(type));
     auto [where,inserted]=out.stateKeys.emplace(key,unsigned(int(filter)+1));if(!inserted&&where->second!=unsigned(int(filter)+1))throw std::runtime_error("Plant key collision");
     for(int frame=0;frame<total;++frame)for(unsigned half=0;half<2;++half){
      const unsigned id=frame*2+half;if(half&&frame+1==total)continue;if(!out.keys.insert({key,id}).second)continue;
      ReanimatorFrameTime time;time.mFraction=half*.5f;time.mAnimFrameBeforeInt=frame;time.mAnimFrameAfterInt=std::min(frame+1,total-1);
      std::vector<Segment> segments;
      auto draw=[&](bool capture){Sexy::Graphics g(screen);captured.clear();pvzh1_text_capture_active=capture;
       for(int track=0;track<r->mDefinition->mTracks.count;++track){PvzpTriangleGroup triangles;const bool drawn=r->DrawTrack(&g,track,0,&triangles,&time);triangles.DrawGroup(&g);
        if(capture){segments.push_back({track,drawn,std::move(captured)});captured.clear();}}
       pvzh1_text_capture_active=0;};
      h1DirtyCanvas.RequireClear();draw(true);
      for(unsigned background=0;background<3;++background){
       for(unsigned i=0;i<480*272;++i)surface[i]=0xff000000u|((i*0x164b35u+background*0x38124du)&0xffffffu);
       std::vector<uint32_t> expected(surface,surface+480*272);draw(false);
       for(const auto& segment:segments)for(const auto& op:segment.ops){auto& p=expected[op.destination];const uint32_t v=op.literal,a=v>>24;
        p=a==255?v:0xff000000u|((v&0xffffffu)+((((p&0xff00ffu)*a)>>8)&0xff00ffu)+((((p&0xff00u)*a)>>8)&0xff00u));}
       if(!std::equal(expected.begin(),expected.end(),surface)){
        unsigned at=0;while(at<480*272&&expected[at]==surface[at])++at;
        std::fprintf(stderr,"PLANT PIXEL MISMATCH seed=%d type=%d frame=%d half=%u filter=%d bg=%u pixel=%u expected=%08x actual=%08x\n",seed,type,frame,half,int(filter),background,at,expected[at],surface[at]);
        for(const auto& segment:segments)for(const auto& op:segment.ops)if(op.destination==at)std::fprintf(stderr,"  track=%d literal=%08x\n",segment.track,op.literal);
        throw std::runtime_error("Plant literal pixels differ from original shader");}++out.comparisons;
      }
      U32(out.raw,uint32_t(key));U32(out.raw,uint32_t(key>>32));U32(out.raw,id);U32(out.raw,type);U32(out.raw,bx);U32(out.raw,by);U32(out.raw,uint32_t(segments.size())|0x40000000u);
      for(const auto& segment:segments){U32(out.raw,uint32_t(segment.track));U32(out.raw,segment.drawn);U32(out.raw,uint32_t(segment.ops.size()));
       for(const auto& op:segment.ops){U32(out.raw,op.destination);U32(out.raw,op.literal);++out.operations;}}
      ++out.frames;
     }
    }
   }
   out.raw.flush();
  }
 }
 pvzh1_host_offline_pixel_capture=nullptr;
 std::ofstream report(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"plant-compiler-verification.json");report<<"{\"passed\":true,\"plant_definitions\":"<<int(NUM_SEED_TYPES)<<",\"three_backgrounds_per_frame\":true,\"all_authored_half_frames\":true,\"damage_overrides_and_four_filters\":true,\"types\":[";
 bool first=true;for(auto& [type,out]:outputs){out->raw.close();if(!first)report<<',';first=false;report<<"{\"type\":"<<type<<",\"source_actors\":"<<out->actors<<",\"frames\":"<<out->frames<<",\"full_frame_comparisons\":"<<out->comparisons<<",\"pixel_operations\":"<<out->operations<<",\"state_keys\":[";bool firstKey=true;for(auto [key,filter]:out->stateKeys){if(!firstKey)report<<',';firstKey=false;report<<"[\""<<std::hex<<key<<std::dec<<"\","<<filter<<']';}report<<"]}";}
 report<<"]}\n";gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
