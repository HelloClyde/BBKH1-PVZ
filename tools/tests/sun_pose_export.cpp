/* Build-time inspection of real actor instances and their animation clips. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Coin.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <cstring>
#include "actors/ActorPoseFormat.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <set>
#include <stdexcept>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
namespace {
struct Op{uint32_t destination,literal;};
uint32_t* surface;std::vector<Op> captured;
void Capture(uint32_t* p,uint32_t literal){
 if(p<surface||p>=surface+480*272)throw std::runtime_error("offscreen capture");
 captured.push_back({uint32_t(p-surface),literal});
}
void U32(std::ofstream& f,uint32_t n){f.write(reinterpret_cast<const char*>(&n),4);}
}
extern "C" void pvzh1_sun_pose_export(){
 PlayerInfo profile;profile.mName="POSE_COMPILER";profile.mLevel=2;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* board=gLawnApp->mBoard;board->AddCoin(200,200,CoinType::COIN_SUN,CoinMotion::COIN_MOTION_FROM_SKY);
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"actor-poses-info.json");out<<"[\n";bool first=true;
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
  auto type=r->mReanimationType;
  if(r->mDead||type!=ReanimationType::REANIM_SUN)continue;
  if(!first)out<<",\n";first=false;
  out<<"{\"type\":"<<int(type)<<",\"tracks\":"<<r->mDefinition->mTracks.count<<",\"frames\":"<<r->mDefinition->mTracks.tracks[0].mTransforms.count
     <<",\"active_start\":"<<r->mFrameStart<<",\"active_count\":"<<r->mFrameCount<<",\"base_pose\":"<<r->mFrameBasePose
     <<",\"overlay\":["<<r->mOverlayMatrix.m00<<","<<r->mOverlayMatrix.m01<<","<<r->mOverlayMatrix.m10<<","<<r->mOverlayMatrix.m11<<","<<r->mOverlayMatrix.m02<<","<<r->mOverlayMatrix.m12<<"],\"clips\":[";
  bool tagFirst=true;
  for(int i=0;i<r->mDefinition->mTracks.count;++i){const char* tag=r->mDefinition->mTracks.tracks[i].mName;if(std::strncmp(tag,"anim_",5))continue;
   int start,count;r->GetFramesForLayer(tag,start,count);if(!tagFirst)out<<",";tagFirst=false;out<<"["<<std::quoted(tag)<<","<<start<<","<<count<<"]";}
  out<<"],\"track_state\":[";
  for(int i=0;i<r->mDefinition->mTracks.count;++i){const auto& t=r->mTrackInstances[i];if(i)out<<",";out<<"["<<t.mRenderGroup<<","<<(t.mAttachmentID!=AttachmentID::ATTACHMENTID_NULL)<<","<<(t.mImageOverride!=nullptr)<<","<<t.mBlendCounter<<","<<t.mShakeOverride<<","<<t.mShakeX<<","<<t.mShakeY<<","<<t.mTrackColor.ToInt()<<"]";}
  out<<"]}";
 }
 out<<"\n]\n";out.close();
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();surface=target->GetBits();
 h1DirtyCanvas.enabled=false;pvzh1_host_offline_pixel_capture=Capture;
 std::ofstream raw(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"sun-poses.raw",std::ios::binary);
 unsigned poses=0,comparisons=0;uint64_t operations=0;
 for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations){
  if(r->mDead||r->mReanimationType!=ReanimationType::REANIM_SUN)continue;
  if(r->mReanimationType==ReanimationType::REANIM_ZOMBIE)continue; // Per-track flash compiler also supplies the normal pose.
  auto type=r->mReanimationType;std::set<int> frames,groups;
  for(int i=0;i<r->mDefinition->mTracks.count;++i)if(r->mTrackInstances[i].mRenderGroup>=0)groups.insert(r->mTrackInstances[i].mRenderGroup);
  auto add=[&](int first,int count){for(int i=first;i<first+count;++i)frames.insert(i);};
  add(0,r->mDefinition->mTracks.tracks[0].mTransforms.count);
  const int bx=type==ReanimationType::REANIM_SODROLL?0:120,by=type==ReanimationType::REANIM_SODROLL?0:136;
  r->mOverlayMatrix.m02=bx*1060.f/480;r->mOverlayMatrix.m12=by*600.f/272;
  for(int group:groups){uint64_t key;if(!H1ActorPose::Signature(r,group,key))throw std::runtime_error("unsupported source actor");
   for(int frame:frames)for(int half=0;half<4;++half){
    if(frame==r->mDefinition->mTracks.tracks[0].mTransforms.count-1&&half)continue;
    ReanimatorFrameTime time;time.mFraction=half*.25f;time.mAnimFrameBeforeInt=frame;time.mAnimFrameAfterInt=std::min(frame+1,r->mDefinition->mTracks.tracks[0].mTransforms.count-1);
    struct Segment{int end;bool drawn;std::vector<Op> ops;};std::vector<Segment> segments;
    auto draw=[&](bool capture){
     PvzpTriangleGroup triangles;Sexy::Graphics g(screen);captured.clear();pvzh1_text_capture_active=capture;
     for(int track=0;track<r->mDefinition->mTracks.count;++track){auto& t=r->mTrackInstances[track];if(t.mRenderGroup!=group)continue;
      bool drawn=r->DrawTrack(&g,track,group,&triangles,&time);
      if(t.mAttachmentID!=AttachmentID::ATTACHMENTID_NULL){triangles.DrawGroup(&g);if(capture){segments.push_back({track,drawn,std::move(captured)});captured.clear();}}
     }triangles.DrawGroup(&g);if(capture)segments.push_back({-1,false,std::move(captured)});pvzh1_text_capture_active=0;
    };
    h1DirtyCanvas.RequireClear();draw(true);
    // Compare captured ordered literals with original shader output on three
    // different backgrounds. Attachment children are independently compiled.
    for(unsigned background=0;background<3;++background){
     for(unsigned i=0;i<480*272;++i)surface[i]=0xff000000u|((i*0x164b35u+background*0x38124du)&0xffffffu);
     std::vector<uint32_t> expected(surface,surface+480*272);draw(false);
     for(const auto& s:segments)for(const auto& op:s.ops){auto& pixel=expected[op.destination];const auto literal=op.literal;const auto alpha=literal>>24;
      if(alpha==255)pixel=literal;else{unsigned inverse=alpha;pixel=0xff000000u|((literal&0xffffffu)+((((pixel&0xff00ffu)*inverse)>>8)&0xff00ffu)+((((pixel&0xff00u)*inverse)>>8)&0xff00u));}}
     if(!std::equal(expected.begin(),expected.end(),surface))throw std::runtime_error("captured pose differs from original shader");++comparisons;
    }
    U32(raw,uint32_t(key));U32(raw,uint32_t(key>>32));U32(raw,frame*4+half);U32(raw,uint32_t(type));U32(raw,bx);U32(raw,by);U32(raw,unsigned(segments.size()));
    for(const auto& s:segments){U32(raw,s.end);U32(raw,s.drawn);U32(raw,unsigned(s.ops.size()));raw.write(reinterpret_cast<const char*>(s.ops.data()),s.ops.size()*sizeof(Op));operations+=s.ops.size();}
    ++poses;
   }
  }
 }
 raw.close();pvzh1_host_offline_pixel_capture=nullptr;h1DirtyCanvas.enabled=true;
 std::ofstream verified(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"sun-pose-export-verification.json");
 verified<<"{\"passed\":true,\"poses\":"<<poses<<",\"whole_frame_comparisons\":"<<comparisons<<",\"operations\":"<<operations<<",\"sample_rate\":\"four samples per authored frame; 24 Hz at original 6 Hz animation rate\",\"resolution\":[480,272]}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
