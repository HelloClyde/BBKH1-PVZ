/* Compile every remaining authored actor through the original shaders.
 * LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpCommon.h"
#include "PvzpLib/EffectSystem.h"
#include "actors/ActorPose.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/GLInterface.h"
#include "graphics/EarlyLawn.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/MemoryImage.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/TextReplay.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <iomanip>
extern "C" uint32_t pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t);
extern "C" void (*pvzh1_host_offline_additive_capture)(uint32_t*,uint32_t);
extern "C" void (*pvzh1_host_offline_fill_capture)(Sexy::MemoryImage*,int,int,int,int,uint32_t,int);
namespace {
constexpr int Width=960,Height=544,BX=470,BY=264;
struct Op {int32_t x,y;uint32_t literal,additive;};
struct Segment {int track;bool drawn;std::vector<Op> ops;};
uint32_t* surface;std::vector<Op> captured;
void Capture(uint32_t*p,uint32_t v,bool add){if(p<surface||p>=surface+Width*Height)throw std::runtime_error("Extra actor capture out of bounds");int n=int(p-surface);captured.push_back({n%Width-BX,n/Width-BY,v,unsigned(add)});}
void Normal(uint32_t*p,uint32_t v){Capture(p,v,false);}
void Additive(uint32_t*p,uint32_t v){Capture(p,v,true);}
void Fill(Sexy::MemoryImage*image,int x,int y,int w,int h,uint32_t v,int mode){
 if(image->mBits.get()!=surface)throw std::runtime_error("Unexpected offscreen fill");
 for(int py=y;py<y+h;++py)for(int px=x;px<x+w;++px)captured.push_back({px-BX,py-BY,v,mode==Sexy::Graphics::DRAWMODE_NORMAL?2u:1u});
}
void Word(std::ofstream&f,uint32_t v){f.write(reinterpret_cast<const char*>(&v),4);}
uint32_t Add(uint32_t d,uint32_t s){return(d&0xff000000u)|(std::min(255u,((d>>16)&255)+((s>>16)&255))<<16)|(std::min(255u,((d>>8)&255)+((s>>8)&255))<<8)|std::min(255u,(d&255)+(s&255));}
bool ExtraSignature(Reanimation*r,uint64_t&key){
 // The existing per-track visitor accepts plants/pole. Replace only the
 // admission type in this offline fixture, then emit the real type word.
 auto type=r->mReanimationType;r->mReanimationType=REANIM_POLEVAULTER;
 unsigned word=0;key=14695981039346656037ull;
 bool ok=H1ActorPose::VisitSignature(r,0,[&](uint32_t v){H1ActorPose::Mix(key,word++?v:uint32_t(type));},false,false,true);
 r->mReanimationType=type;return ok;
}
bool Dedicated(ReanimationType t){return H1ActorPose::PlantType(t)||t==REANIM_ZOMBIE||t==REANIM_SUN||t==REANIM_SODROLL||t==REANIM_SELECTOR_SCREEN;}
}
extern "C" void pvzh1_extra_actor_export(){
 using namespace Sexy;
 H1ActorPoseTestEnable(false);H1EarlyLawnTestEnable(false);H1RenderPlanTestEnable(false);H1TextReplayTestEnable(false);h1DirtyCanvas.enabled=false;
 PlayerInfo profile;profile.mName="EXTRA_ACTOR_COMPILER";profile.mLevel=6;auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto*device=gLawnApp->mGLInterface.get();int oldWidth=device->mWidth,oldHeight=device->mHeight;
 auto saved=std::move(device->mScreenImage);device->mWidth=2120;device->mHeight=1200;
 device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,2120,1200,Width,Height);
 auto*screen=device->GetScreenImage();surface=H1GetPhysicalScreen(screen)->Target()->GetBits();
 const auto dir=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));std::filesystem::create_directories(dir/"actor-raw");
 std::ofstream report(dir/"extra-actor-export-verification.json");report<<"{\"types\":[";bool first=true;unsigned types=0,failures=0;
 pvzh1_host_offline_pixel_capture=Normal;pvzh1_host_offline_additive_capture=Additive;pvzh1_host_offline_fill_capture=Fill;
 for(unsigned type=0;type<gReanimatorDefCount;++type){
  if(Dedicated(ReanimationType(type)))continue;
  if(auto*from=getenv("PVZ_HOST_EXTRA_FROM_TYPE");from&&type<unsigned(atoi(from)))continue;
  if(auto*only=getenv("PVZ_HOST_EXTRA_ONLY_TYPE");only&&type!=unsigned(atoi(only)))continue;
  try{
  auto*r=gLawnApp->AddReanimation(0,0,0,ReanimationType(type));
  for(int t=0;t<r->mDefinition->mTracks.count;++t){auto&track=r->mTrackInstances[t];track.mRenderGroup=0;track.mBlendCounter=0;track.mShakeX=track.mShakeY=0;
   for(int f=0;f<r->mDefinition->mTracks.tracks[t].mTransforms.count;++f){auto*image=r->mDefinition->mTracks.tracks[t].mTransforms.mTransforms[f].mImage;if(uintptr_t(image)>1000)if(auto*memory=dynamic_cast<MemoryImage*>(image))memory->GetBits();}}
  r->mOverlayMatrix.m02=BX*1060.f/480;r->mOverlayMatrix.m12=BY*600.f/272;
  uint64_t key;if(!ExtraSignature(r,key))throw std::runtime_error("Extra signature admission: "+std::to_string(type));
  std::ofstream raw(dir/"actor-raw"/(std::to_string(type)+".raw"),std::ios::binary);unsigned frames=0,comparisons=0;uint64_t operations=0;
  const int total=r->mDefinition->mTracks.tracks[0].mTransforms.count;
  for(int frame=0;frame<total;++frame)for(unsigned half=0;half<2;++half){
   if(half&&frame+1==total)continue;
   ReanimatorFrameTime time;time.mAnimFrameBeforeInt=frame;time.mAnimFrameAfterInt=std::min(frame+1,total-1);time.mFraction=half*.5f;
   std::vector<Segment> segments;
   auto draw=[&](bool capture){Graphics g(screen);captured.clear();pvzh1_text_capture_active=capture;
    for(int track=0;track<r->mDefinition->mTracks.count;++track){PvzpTriangleGroup triangles;bool drawn=r->DrawTrack(&g,track,0,&triangles,&time);triangles.DrawGroup(&g);if(capture){segments.push_back({track,drawn,std::move(captured)});captured.clear();}}
    pvzh1_text_capture_active=0;};
   draw(true);
   for(unsigned bg=0;bg<3;++bg){for(unsigned i=0;i<Width*Height;++i)surface[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);
    std::vector<uint32_t> expected(surface,surface+Width*Height);draw(false);
    for(const auto&segment:segments)for(const auto&op:segment.ops){auto&d=expected[(op.y+BY)*Width+op.x+BX];const auto v=op.literal,a=v>>24;
     if(op.additive==2){unsigned inv=256-a;d=a==255?v:0xff000000u|((((d&0xff00ffu)*inv+(v&0xff00ffu)*a)>>8)&0xff00ffu)|((((d&0xff00u)*inv+(v&0xff00u)*a)>>8)&0xff00u);}
     else d=op.additive?Add(d,v):a==255?v:0xff000000u|((v&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));}
    if(!std::equal(expected.begin(),expected.end(),surface)){unsigned at=0;while(at<Width*Height&&expected[at]==surface[at])++at;fprintf(stderr,"EXTRA_DIFF type=%u frame=%d half=%u bg=%u pixel=%u expected=%08x actual=%08x segments=%zu\n",type,frame,half,bg,at,expected[at],surface[at],segments.size());throw std::runtime_error("Extra shader mismatch: "+std::to_string(type)+"/"+std::to_string(frame));}++comparisons;
   }
   Word(raw,uint32_t(key));Word(raw,uint32_t(key>>32));Word(raw,frame*2+half);Word(raw,type);Word(raw,BX);Word(raw,BY);Word(raw,segments.size()|0x40000000u);
   for(const auto&segment:segments){Word(raw,segment.track);Word(raw,segment.drawn);Word(raw,segment.ops.size());raw.write(reinterpret_cast<const char*>(segment.ops.data()),segment.ops.size()*sizeof(Op));operations+=segment.ops.size();}++frames;
  }
  raw.close();if(!first)report<<',';first=false;++types;
  report<<"{\"type\":"<<type<<",\"key\":\""<<std::hex<<key<<std::dec<<"\",\"frames\":"<<frames<<",\"comparisons\":"<<comparisons<<",\"operations\":"<<operations<<'}';report.flush();
  fprintf(stderr,"EXTRA_BAKED type=%u frames=%u operations=%llu\n",type,frames,(unsigned long long)operations);
  r->ReanimationDie();H1ImageCacheEndFrame();
  }catch(const std::exception&error){pvzh1_text_capture_active=0;++failures;if(!first)report<<',';first=false;report<<"{\"type\":"<<type<<",\"passed\":false,\"error\":"<<std::quoted(error.what())<<'}';report.flush();fprintf(stderr,"EXTRA_FAILED type=%u error=%s\n",type,error.what());}
 }
 report<<"],\"passed\":"<<(failures?"false":"true")<<",\"failed_types\":"<<failures<<",\"types_count\":"<<types<<",\"sample_canvas\":[960,544],\"display_density\":[480,272],\"normal_and_additive_original_shaders\":true}\n";
 pvzh1_host_offline_pixel_capture=pvzh1_host_offline_additive_capture=nullptr;pvzh1_host_offline_fill_capture=nullptr;
 device->mScreenImage=std::move(saved);device->mWidth=oldWidth;device->mHeight=oldHeight;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
