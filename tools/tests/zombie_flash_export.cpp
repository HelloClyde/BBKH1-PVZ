/* Compile normal and all 25 original hit intensities with shared geometry. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "actors/ActorPose.h"
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
#include "Resources.h"
#include <cstring>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
extern "C" void (*pvzh1_host_offline_additive_capture)(uint32_t*,uint32_t);
namespace {
struct Op{uint32_t at,value;};struct Tuple{uint32_t at;std::array<uint32_t,26> values;};
struct Segment{int track;bool alternate;std::vector<Tuple> ops;};
uint32_t* surface;std::vector<Op> normal,extra;
void CaptureNormal(uint32_t* p,uint32_t v){normal.push_back({uint32_t(p-surface),v});}
void CaptureExtra(uint32_t* p,uint32_t v){extra.push_back({uint32_t(p-surface),v});}
void U32(std::ofstream& f,uint32_t n){f.write(reinterpret_cast<const char*>(&n),4);}
void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
uint32_t Add(uint32_t d,uint32_t s){return (d&0xff000000u)|(std::min(255u,((d>>16)&255)+((s>>16)&255))<<16)|(std::min(255u,((d>>8)&255)+((s>>8)&255))<<8)|std::min(255u,(d&255)+(s&255));}
}
extern "C" void pvzh1_zombie_flash_export(){
 using namespace Sexy;H1ActorPoseTestEnable(false);PlayerInfo p;p.mName="FLASH_COMPILER";p.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_NORMAL,2,0);auto* r=gLawnApp->ReanimationGet(z->mBodyReanimID);
 r->mOverlayMatrix.m02=265;r->mOverlayMatrix.m12=300;uint64_t key;Check(H1ActorPose::Signature(r,0,key),"flash source state");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();surface=target->GetBits();h1DirtyCanvas.enabled=false;
 pvzh1_host_offline_pixel_capture=CaptureNormal;pvzh1_host_offline_additive_capture=CaptureExtra;
 std::ofstream raw(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"zombie-flash.raw",std::ios::binary);unsigned poses=0,comparisons=0;uint64_t operations=0;
 for(unsigned frame=0;frame<500;++frame){
  ReanimatorFrameTime t;t.mAnimFrameBeforeInt=frame/2;t.mAnimFrameAfterInt=std::min(int(frame/2+1),r->mDefinition->mTracks.tracks[0].mTransforms.count-1);t.mFraction=(frame&1)*.5f;
  std::vector<Segment> segments;
  for(int track=0;track<r->mDefinition->mTracks.count;++track){auto& inst=r->mTrackInstances[track];if(inst.mRenderGroup!=0&&!H1ActorPose::ZombieVisibilityVaries(r->mDefinition->mTracks.tracks[track].mName))continue;Check(inst.mAttachmentID==ATTACHMENTID_NULL,"flash source has attachment");bool hasAlternate=!std::strcmp(r->mDefinition->mTracks.tracks[track].mName,"Zombie_outerarm_upper");
  for(unsigned alternate=0;alternate<=(hasAlternate?1u:0u);++alternate){inst.mImageOverride=alternate?IMAGE_REANIM_ZOMBIE_OUTERARM_UPPER2:nullptr;Segment segment{track,bool(alternate),{}};
   for(unsigned shade=1;shade<=25;++shade){
    normal.clear();extra.clear();std::fill(surface,surface+480*272,0xff000000u);h1DirtyCanvas.RequireClear();r->mEnableExtraAdditiveDraw=true;r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);
    pvzh1_text_capture_active=1;Graphics g(screen);PvzpTriangleGroup triangles;r->DrawTrack(&g,track,0,&triangles,&t);triangles.DrawGroup(&g);pvzh1_text_capture_active=0;
    if(shade==1){for(auto op:normal){Tuple tuple{};tuple.at=op.at;tuple.values[0]=op.value;segment.ops.push_back(tuple);}}
    Check(segment.ops.size()==normal.size(),"flash geometry varies by intensity");
    unsigned addAt=0;for(unsigned i=0;i<normal.size();++i){Check(normal[i].at==segment.ops[i].at&&normal[i].value==segment.ops[i].values[0],"normal geometry differs");if(addAt<extra.size()&&normal[i].at==extra[addAt].at)segment.ops[i].values[shade]=extra[addAt++].value;}
    Check(addAt==extra.size(),"flash adds outside normal geometry");
   }
   if(!segment.ops.empty()){operations+=segment.ops.size();segments.push_back(std::move(segment));}
  }inst.mImageOverride=nullptr;}
  for(unsigned shade=0;shade<=25;++shade)for(unsigned bg=0;bg<3;++bg){
   for(unsigned i=0;i<480*272;++i)surface[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);std::vector<uint32_t> expected(surface,surface+480*272);
   r->mEnableExtraAdditiveDraw=shade!=0;r->mExtraAdditiveColor=Color(shade*10,shade*10,shade*10,255);Graphics g(screen);PvzpTriangleGroup triangles;
   for(int track=0;track<r->mDefinition->mTracks.count;++track)if(r->mTrackInstances[track].mRenderGroup==0)r->DrawTrack(&g,track,0,&triangles,&t);triangles.DrawGroup(&g);
   for(auto& segment:segments){const auto& instance=r->mTrackInstances[segment.track];if(instance.mRenderGroup!=0||bool(instance.mImageOverride)!=segment.alternate)continue;for(auto& op:segment.ops){auto& d=expected[op.at];uint32_t v=op.values[0],a=v>>24;d=a==255?v:0xff000000u|((v&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}if(shade)for(auto& op:segment.ops)expected[op.at]=Add(expected[op.at],op.values[shade]);}
   Check(expected==std::vector<uint32_t>(surface,surface+480*272),"flash whole-frame differs");++comparisons;
  }
  for(uint32_t v:{uint32_t(key),uint32_t(key>>32),frame,21u,120u,136u,0x80000000u|uint32_t(segments.size())})U32(raw,v);
  for(auto& segment:segments){for(uint32_t v:{uint32_t(segment.track),uint32_t(segment.alternate),uint32_t(segment.ops.size())})U32(raw,v);for(auto& op:segment.ops){U32(raw,op.at);for(auto v:op.values)U32(raw,v);}}
  ++poses;
 }
 raw.close();r->mEnableExtraAdditiveDraw=false;pvzh1_host_offline_pixel_capture=nullptr;pvzh1_host_offline_additive_capture=nullptr;H1ActorPoseTestEnable(true);h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"zombie-flash-export-verification.json");out<<"{\"passed\":true,\"poses\":"<<poses<<",\"intensities\":26,\"whole_frame_comparisons\":"<<comparisons<<",\"shared_geometry_operations\":"<<operations<<",\"gameplay_changed\":false}\n";
}
