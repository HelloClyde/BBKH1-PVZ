/* Known shadow call versus the existing public image/matrix call. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/PvzpCommon.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/CombatSprites.h"
#include "graphics/CombatSpriteFormat.h"
#include "Resources.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern void H1DirectPlantShadowTestEnable(bool);
extern "C" volatile uint32_t pvzh1_shadow_pose_diagnostic[4];
namespace {void Check(bool b,const char* message){if(!b){fprintf(stderr,"DIRECT_SHADOW: %s\n",message);throw std::runtime_error(message);}}}
extern "C" void pvzh1_direct_plant_shadow_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mName="DIRECT_SHADOW";profile.mLevel=8;
 auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);Check(H1CombatSpritesPrepare(),"combat bank preload");
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();
 h1DirtyCanvas.enabled=false;unsigned comparisons=0,used=0;
 auto fill=[&](unsigned bg){h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+bg*0x38124du)&0xffffffu);};
 auto compare=[&](auto draw,unsigned bg){
  H1DirectPlantShadowTestEnable(false);fill(bg);draw();std::vector<uint32_t> expected(bits,bits+480*272);
  H1DirectPlantShadowTestEnable(true);fill(bg);auto before=pvzh1_shadow_pose_diagnostic[2];draw();
  used+=pvzh1_shadow_pose_diagnostic[2]-before;
  if(!std::equal(expected.begin(),expected.end(),bits)){unsigned n=0;while(expected[n]==bits[n])++n;fprintf(stderr,"case=%u first=%u,%u expected=%08x actual=%08x\n",comparisons,n%480,n/480,expected[n],bits[n]);}
  Check(std::equal(expected.begin(),expected.end(),bits),"pixels differ from public image wrapper");++comparisons;
 };
 for(auto* image:{IMAGE_PLANTSHADOW,IMAGE_PLANTSHADOW2})for(auto scale:H1Combat::Scales)
  for(auto pos:{std::pair<float,float>{130,70},{137.25f,84.75f},{-13.5f,-4.25f},{1007.5f,578.25f}})
   for(auto clip:{Rect(0,0,1060,600),Rect(130,75,70,40),Rect(1,1,1058,598)})for(unsigned bg=0;bg<2;++bg){
    Graphics g(screen);g.mTransX=pos.first;g.mTransY=pos.second;g.mClipRect=clip;
    compare([&]{PvzpDrawImageCelCenterScaledF(&g,image,-3,51,0,scale.x,scale.y);},bg);
   }
 for(int level:{1,11,21,31,41}){
  profile.mLevel=level;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
  for(auto seed:{SEED_PEASHOOTER,SEED_SUNFLOWER,SEED_TALLNUT,SEED_PUMPKINSHELL,SEED_FLOWERPOT,SEED_PUFFSHROOM,SEED_SUNSHROOM,SEED_SCAREDYSHROOM,SEED_CHOMPER,SEED_CACTUS,SEED_PLANTERN,SEED_GIANT_WALLNUT}){
   auto* p=gLawnApp->mBoard->AddPlant(4,2,seed);Graphics g(screen);g.Translate(130.f+p->mX,p->mY);
   for(unsigned bg=0;bg<2;++bg)compare([&]{p->DrawShadow(&g,.25f,-.75f);},bg);
   p->Die();gLawnApp->mEffectSystem->ProcessDeleteQueue();
  }
 }
 H1DirectPlantShadowTestEnable(true);h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 Check(used>300,"known shadows did not bypass matrix wrapper");
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"direct-plant-shadow-verification.json");
 out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<comparisons<<",\"known_shadow_draws\":"<<used<<"}\n";
}
