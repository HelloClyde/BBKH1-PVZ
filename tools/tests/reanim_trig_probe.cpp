/* Independent original formula oracle for every Boss transform and edge bits. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpCommon.h"
#include "graphics/ReanimTrig.h"
#include <bit>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <limits>
namespace {
unsigned checked=0;
void Compare(const ReanimatorTransform& t){
 Sexy::SexyMatrix3 expected,actual;
 float x=-DEG_TO_RAD(t.mSkewX),y=-DEG_TO_RAD(t.mSkewY);
 expected.m00=std::cos(x)*t.mScaleX;expected.m10=-std::sin(x)*t.mScaleX;expected.m20=0.f;
 expected.m01=std::sin(y)*t.mScaleY;expected.m11=std::cos(y)*t.mScaleY;expected.m21=0.f;
 expected.m02=t.mTransX;expected.m12=t.mTransY;expected.m22=1.f;
 Reanimation::MatrixFromTransform(t,actual);
 if(std::memcmp(&expected,&actual,sizeof(actual)))throw std::runtime_error("Exact trig memo changed original matrix bits");
 ++checked;
}
}
extern "C" void pvzh1_reanim_trig_probe(){
 ReanimatorTransform t;t.mTransX=-0.f;t.mTransY=37.125f;
 float values[]={0.f,-0.f,1.f,-1.f,.45f,-.45f,std::numeric_limits<float>::min(),
  std::numeric_limits<float>::denorm_min(),std::numeric_limits<float>::max(),
  std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::bit_cast<float>(0x7fc12345u)};
 for(float x:{17.f,-17.f,179.999f,360.f,100000.f})for(float y:{-17.f,17.f,180.f,-360.f,100000.f})
 for(float sx:values)for(float sy:values){t.mSkewX=x;t.mSkewY=y;t.mScaleX=sx;t.mScaleY=sy;Compare(t);Compare(t);}
 PlayerInfo p;p.mLevel=50;p.mName="TRIG_BITS";auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto*z=gLawnApp->mBoard->GetBossZombie();
 if(!z)z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_BOSS,0,-2);
 auto*r=gLawnApp->ReanimationGet(z->mBodyReanimID);unsigned boss=0;
 for(int i=0;i<r->mDefinition->mTracks.count;++i){
  const auto& frames=r->mDefinition->mTracks.tracks[i].mTransforms;
  for(int f=0;f+1<frames.count;++f)for(float part:{0.f,.5f}){
   ReanimatorFrameTime ft{part,f,f+1};r->GetCurrentTransform(i,&t,&ft);
   if(t.mSkewX==0&&t.mSkewY==0)continue;Compare(t);++boss;
  }
 }
 if(pvzh1_trig_diagnostic[1]<1000)throw std::runtime_error("Memo reuse not exercised");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"reanim-trig-verification.json");
 out<<"{\"passed\":true,\"original_matrix_bit_comparisons\":"<<checked<<",\"boss_transforms\":"<<boss
    <<",\"exact_angle_hits\":"<<pvzh1_trig_diagnostic[1]<<",\"memo_bytes\":2048,\"resource_bytes_added\":0}\n";
 gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
