/* Production reader versus the already verified original-shader oracle.
 * LGPL-3.0-or-later. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/GLInterface.h"
#include "graphics/EarlyLawn.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <set>
#include <string>
#include <stdexcept>
#include <algorithm>
void H1ActorPosePageTestSlots(unsigned);
namespace {
struct Op{int32_t x,y;uint32_t color,kind;};
struct Segment{unsigned track;std::vector<Op> ops;};
struct Sample{unsigned frame;std::vector<Segment> segments;};
void Check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
uint32_t Blend(uint32_t d,uint32_t v,unsigned kind){
 if(kind==1)return(d&0xff000000u)|(std::min(255u,((d>>16)&255)+((v>>16)&255))<<16)|(std::min(255u,((d>>8)&255)+((v>>8)&255))<<8)|std::min(255u,(d&255)+(v&255));
 unsigned a=v>>24;if(a==255)return v;
 if(kind==2){unsigned inv=256-a;return 0xff000000u|((((d&0xff00ffu)*inv+(v&0xff00ffu)*a)>>8)&0xff00ffu)|((((d&0xff00u)*inv+(v&0xff00u)*a)>>8)&0xff00u);}
 return 0xff000000u|((v&0xffffffu)+((((d&0xff00ffu)*a)>>8)&0xff00ffu)+((((d&0xff00u)*a)>>8)&0xff00u));
}
std::vector<Sample> Samples(const std::string&path,unsigned count){
 std::set<unsigned> selected;for(unsigned i=0;i<7;++i)selected.insert(i*(count-1)/6);
 std::ifstream f(path,std::ios::binary);Check(bool(f),"raw oracle open");std::vector<Sample> result;
 for(unsigned frame=0;frame<count;++frame){uint32_t h[7];Check(bool(f.read((char*)h,28)),"raw header");bool keep=selected.contains(h[2]);Sample sample{h[2],{}};
  for(unsigned track=0;track<(h[6]&0x3fffffffu);++track){uint32_t row[3];Check(bool(f.read((char*)row,12)),"raw track");
   if(keep){Segment s;s.track=row[0];s.ops.resize(row[2]);Check(bool(f.read((char*)s.ops.data(),size_t(row[2])*16)),"raw operations");sample.segments.push_back(std::move(s));}
   else f.seekg(uint64_t(row[2])*16,std::ios::cur);
  }if(keep)result.push_back(std::move(sample));
 }return result;
}
}
extern "C" void pvzh1_extra_actor_replay_probe(){
 using namespace Sexy;
 H1ActorPoseTestEnable(false);H1EarlyLawnTestEnable(false);PlayerInfo profile;profile.mName="ALL_ACTOR_REPLAY";profile.mLevel=6;auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);H1ActorPoseTestEnable(true);H1ExtraPoseTestIdleEnable(false);H1ExtraPoseTestAllEnable(true);
 auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();auto*pixels=target->GetBits();h1DirtyCanvas.enabled=false;
 std::ifstream list(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"raw-inputs.txt");std::string line;unsigned types=0,comparisons=0;
 if(auto* slots=getenv("PVZ_HOST_EXTRA_PAGE_SLOTS"))H1ActorPosePageTestSlots(std::stoul(slots));
 while(std::getline(list,line)){auto split=line.find('|');unsigned type=std::stoul(line.substr(0,split));std::string path=line.substr(split+1);
  if(auto* only=getenv("PVZ_HOST_EXTRA_ONLY_TYPE"))if(type!=std::stoul(only))continue;
  if(auto* from=getenv("PVZ_HOST_EXTRA_FROM_TYPE"))if(type<std::stoul(from))continue;
  auto*r=gLawnApp->AddReanimation(0,0,0,ReanimationType(type));Check(H1ExtraPosePrepare(ReanimationType(type)),"extra reader prepare");
  unsigned frames=r->mDefinition->mTracks.tracks[0].mTransforms.count*2-1;auto samples=Samples(path,frames);
  for(auto&sample:samples){r->mFrameStart=sample.frame/2;r->mFrameCount=r->mFrameStart+1<int((frames+1)/2)?2:1;r->mAnimTime=(sample.frame&1)*.5f;
   for(unsigned selection=0;selection<2;++selection){for(int t=0;t<r->mDefinition->mTracks.count;++t)r->mTrackInstances[t].mRenderGroup=selection&&t%3==0?1:0;
    for(unsigned position=0;position<3;++position){int ox=position==0?120:position==1?-20:430,oy=position==0?136:position==1?10:260;
     r->mOverlayMatrix.m02=ox*1060.f/480;r->mOverlayMatrix.m12=oy*600.f/272;
     for(unsigned bg=0;bg<3;++bg){h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);
      std::vector<uint32_t> expected(pixels,pixels+480*272);
      for(auto&segment:sample.segments)if(r->mTrackInstances[segment.track].mRenderGroup==0)for(auto&op:segment.ops){int x=op.x+ox,y=op.y+oy;if(x>=0&&x<480&&y>=0&&y<272)expected[y*480+x]=Blend(expected[y*480+x],op.color,op.kind);}
      Graphics g(screen);bool replay=H1ActorPoseDraw(r,&g,0);
      if(!replay||expected!=std::vector<uint32_t>(pixels,pixels+480*272)){unsigned at=0;while(at<480*272&&expected[at]==pixels[at])++at;fprintf(stderr,"EXTRA_REPLAY_FAIL type=%u half=%u selection=%u pos=%u bg=%u handled=%d at=%u expected=%08x actual=%08x\n",type,sample.frame,selection,position,bg,int(replay),at,at<480*272?expected[at]:0,at<480*272?pixels[at]:0);throw std::runtime_error("extra replay differs from original shader oracle");}++comparisons;
     }
    }
   }
  }++types;fprintf(stderr,"EXTRA_REPLAY_PASS type=%u cases=%u\n",type,comparisons);r->ReanimationDie();
 }
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream report(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"extra-actor-replay-verification.json");report<<"{\"passed\":true,\"types\":"<<types<<",\"whole_frame_comparisons\":"<<comparisons<<",\"all_reader_calls_used_baked_path\":true,\"visibility_and_screen_edge_clipping\":true}\n";
}
