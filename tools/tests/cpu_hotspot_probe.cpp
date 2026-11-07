/* Exact numeric and original-query oracles for measured H1 hotspots. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpCommon.h"
#include "BoardLookup.h"
#include "actors/ActorPose.h"
#include "graphics/ConstantTransform.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/GLInterface.h"
#include <array>
#include <bit>
#include <set>
#include <vector>
#include <limits>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
namespace {
void Check(bool okay,const char* why){if(!okay)throw std::runtime_error(why);}
std::array<float,7> Numbers(const ReanimatorTransform& t){return {t.mTransX,t.mTransY,t.mSkewX,t.mSkewY,t.mScaleX,t.mScaleY,t.mAlpha};}
float OriginalLerp(float a,float b,float f){return a+f*(b-a);}
ReanimatorFrameTime OriginalFrameTime(const Reanimation& r){
 const bool full=r.mLoopType==REANIM_PLAY_ONCE_FULL_LAST_FRAME||r.mLoopType==REANIM_LOOP_FULL_LAST_FRAME||r.mLoopType==REANIM_PLAY_ONCE_FULL_LAST_FRAME_AND_HOLD;
 const int count=full?r.mFrameCount:r.mFrameCount-1;const float position=r.mFrameStart+r.mAnimTime*count,before=floor(position);
 ReanimatorFrameTime out{position-before,FloatRoundToInt(before),0};
 if(out.mAnimFrameBeforeInt>=r.mFrameStart+r.mFrameCount-1){out.mAnimFrameBeforeInt=r.mFrameStart+r.mFrameCount-1;out.mAnimFrameAfterInt=out.mAnimFrameBeforeInt;}else out.mAnimFrameAfterInt=out.mAnimFrameBeforeInt+1;
 return out;
}
void Same(const std::array<float,7>& a,const std::array<float,7>& b){for(unsigned i=0;i<7;++i)Check(std::bit_cast<uint32_t>(a[i])==std::bit_cast<uint32_t>(b[i]),"constant interpolation changed float bits");}
}
extern "C" void pvzh1_cpu_hotspot_probe(){
 unsigned numeric=0,constant=0,queries=0,clips=0,matrices=0,increments=0,frameTimes=0;
 {
  Reanimation actor;
  for(int count:{1,2,7,29,53,120,1024})for(float rate:{0.f,-0.f,1.f,-1.f,7.125f,12.f,35.f,67.89f,1000.f,std::numeric_limits<float>::denorm_min()}){
   actor.mFrameCount=count;actor.mAnimRate=rate;const double expected=SECONDS_PER_UPDATE*rate/count;
   for(unsigned repeat=0;repeat<8;++repeat){const double actual=H1ActorAnimIncrement(&actor);Check(std::bit_cast<uint64_t>(expected)==std::bit_cast<uint64_t>(actual),"animation rate cache changed double bits");
    for(float time:{0.f,.125f,.5f,.9999999f})Check(std::bit_cast<uint32_t>(float(double(time)+expected))==std::bit_cast<uint32_t>(float(double(time)+actual)),"animation update changed float clock");++increments;}
  }
 }
 for(float value:{0.f,-0.f,1.f,-1.f,42.5f,std::numeric_limits<float>::max(),std::numeric_limits<float>::denorm_min()})for(float f:{0.f,.125f,.5f,.75f,1.f}){
  ReanimatorTransform a,b,out;a.mTransX=a.mTransY=a.mSkewX=a.mSkewY=a.mScaleX=a.mScaleY=a.mAlpha=value;b=a;Check(H1ConstantTransform(a,b,f,out),"constant endpoint was missed");std::array<float,7> expected;expected.fill(OriginalLerp(value,value,f));Same(Numbers(out),expected);++numeric;
 }
 PlayerInfo p;p.mName="CPU_HOTSPOT_ORACLE";p.mLevel=46;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
 auto answers=[&](){std::vector<Plant*> out;for(int y=-1;y<=6;++y)for(int x=-1;x<=10;++x){out.push_back(board->GetFlowerPotAt(x,y));out.push_back(board->GetPumpkinAt(x,y));}return out;};
 auto compare=[&](){H1BoardLookupTestEnable(false);auto expected=answers();H1BoardLookupTestEnable(true);Check(answers()==expected,"support absence changed original query result");Check(answers()==expected,"repeated cached support query changed");queries+=expected.size()*2;};
 compare();auto* pot=board->AddPlant(0,0,SEED_FLOWERPOT);compare();auto* pea=board->AddPlant(0,0,SEED_PEASHOOTER);compare();auto* pumpkin=board->AddPlant(0,0,SEED_PUMPKINSHELL);compare();pumpkin->mPlantCol=1;compare();pumpkin->Die();compare();pot->Die();compare();board->AddPlant(2,1,SEED_FLOWERPOT);compare();++board->mMainCounter;compare();
 const auto saved=(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"support.v4").string();Check(LawnSaveGame(board,saved),"support save failed");for(auto* plant:board->mPlants)if(!plant->mDead&&plant->mSeedType==SEED_FLOWERPOT)plant->Die();compare();Check(LawnLoadGame(board,saved),"support reload failed");compare();
 auto* screen=Sexy::H1GetPhysicalScreen(gLawnApp->mGLInterface->GetScreenImage());
 auto boundary=[](int v,int logical,int physical){int64_t a=int64_t(v)*physical*2-logical,b=int64_t(logical)*2;return int(a>=0?(a+b-1)/b:a/b);};
 for(int x:{-20,0,1,130,1059,1060})for(int y:{-20,0,1,599,600})for(int width:{0,1,200,1060,2000})for(int height:{0,1,272,600,1000}){
  Sexy::Rect r=Sexy::Rect(x,y,width,height).Intersection(Sexy::Rect(0,0,1060,600));int l=boundary(r.mX,1060,480),t=boundary(r.mY,600,272),right=boundary(r.mX+r.mWidth,1060,480),bottom=boundary(r.mY+r.mHeight,600,272);auto actual=screen->MapRect(Sexy::Rect(x,y,width,height));Check(actual==Sexy::Rect(l,t,std::max(0,right-l),std::max(0,bottom-t)),"full-screen clip shortcut changed mapping");++clips;
 }
 std::set<int> types;for(int seed=0;seed<int(NUM_SEED_TYPES);++seed){Plant::PreloadPlantResources(SeedType(seed));board->AddPlant(4,2,SeedType(seed));
  for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)if(!r->mDead&&types.insert(int(r->mReanimationType)).second){
   const int oldStart=r->mFrameStart,oldCount=r->mFrameCount;const auto oldLoop=r->mLoopType;const float oldTime=r->mAnimTime;
   const int total=r->mDefinition->mTracks.tracks[0].mTransforms.count;
   for(int start:{0,std::min(1,total-1)})for(int count:{1,std::min(2,total-start),total-start})for(auto loop:{REANIM_LOOP,REANIM_PLAY_ONCE_AND_HOLD,REANIM_LOOP_FULL_LAST_FRAME,REANIM_PLAY_ONCE_FULL_LAST_FRAME_AND_HOLD})for(float time:{0.f,-0.f,.125f,.5f,.9999999f,1.f}){
    r->mFrameStart=start;r->mFrameCount=count;r->mLoopType=loop;r->mAnimTime=time;const auto expected=OriginalFrameTime(*r);
    for(unsigned repeat=0;repeat<3;++repeat){ReanimatorFrameTime actual;r->GetFrameTime(&actual);Check(!std::memcmp(&expected,&actual,sizeof(actual)),"frame time memo changed original float bits or frame indices");++frameTimes;}
   }
   r->mFrameStart=oldStart;r->mFrameCount=oldCount;r->mLoopType=oldLoop;r->mAnimTime=oldTime;
   for(int track=0;track<r->mDefinition->mTracks.count;++track){const auto& frames=r->mDefinition->mTracks.tracks[track].mTransforms;
    for(float phase:{0.f,.125f,.5f,.875f})for(float x:{0.f,17.5f}){
     r->mAnimTime=phase;r->mOverlayMatrix.m02=x;Sexy::SexyTransform2D expected,actual;H1ActorMatrixTestEnable(false);r->GetAttachmentOverlayMatrix(track,expected);H1ActorMatrixTestEnable(true);
     for(unsigned repeat=0;repeat<2;++repeat){r->GetAttachmentOverlayMatrix(track,actual);Check(!std::memcmp(&expected,&actual,36),"matrix memo changed original float bits");++matrices;}
    }
    for(int frame=0;frame<frames.count;++frame)for(float f:{0.f,.125f,.5f,.75f,1.f}){
     const auto& a=frames.mTransforms[frame];const auto& b=frames.mTransforms[std::min(frame+1,frames.count-1)];ReanimatorFrameTime time{f,frame,std::min(frame+1,frames.count-1)};ReanimatorTransform actual;r->GetTransformAtTime(track,&actual,&time);auto na=Numbers(a),nb=Numbers(b);std::array<float,7> expected;for(unsigned i=0;i<7;++i)expected[i]=OriginalLerp(na[i],nb[i],f);Same(Numbers(actual),expected);Check(actual.mImage==a.mImage&&actual.mFont==a.mFont&&actual.mText==a.mText,"constant curve changed resource handles");++numeric;ReanimatorTransform fast;if(H1ConstantTransform(a,b,f,fast))++constant;
    }
   }
  }
 }
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"cpu-hotspot-verification.json");out<<"{\"passed\":true,\"numeric_cases\":"<<numeric<<",\"constant_cases\":"<<constant<<",\"support_query_comparisons\":"<<queries<<",\"clip_cases\":"<<clips<<",\"matrix_comparisons\":"<<matrices<<",\"animation_increment_comparisons\":"<<increments<<",\"frame_time_comparisons\":"<<frameTimes<<",\"types\":"<<types.size()<<"}\n";
}
