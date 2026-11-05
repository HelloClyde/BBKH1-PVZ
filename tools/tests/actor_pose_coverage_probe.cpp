/* Exercise original attached-actor updates, reporting actual replay coverage. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/EffectSystem.h"
#include "PvzpLib/Reanimator.h"
#include "actors/ActorPose.h"
#include "actors/ActorPoseFormat.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[12];
extern "C" void pvzh1_actor_pose_coverage_probe(){
 PlayerInfo p;p.mName="COVERAGE";p.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;
 gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* b=gLawnApp->mBoard;auto* pea=b->AddPlant(2,1,SEED_PEASHOOTER);auto* sun=b->AddPlant(3,2,SEED_SUNFLOWER);
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();h1DirtyCanvas.enabled=false;
 std::ofstream o(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"actor-pose-coverage.json");o<<"{\"head_linear_matrices\":[";bool first=true;
 for(int update=0;update<160;++update){
  if(update==20){pea->DoBlink();sun->DoBlink();}
  if(update==40){pea->EndBlink();sun->EndBlink();}
  pea->UpdateReanim();sun->UpdateReanim();gLawnApp->mEffectSystem->Update();gLawnApp->mEffectSystem->ProcessDeleteQueue();h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();
  const auto before=pvzh1_actor_pose_diagnostic[5];
  for(auto* plant:{pea,sun}){Sexy::Graphics g(screen);g.Translate(plant->mX+130,plant->mY);plant->Draw(&g);}
  if(update>=60&&pvzh1_actor_pose_diagnostic[5]-before!=3)throw std::runtime_error("first blink permanently disabled ordinary pose replay");
  if(update%20==0){auto* head=gLawnApp->ReanimationGet(pea->mHeadReanimID);if(!first)o<<",";first=false;auto& m=head->mOverlayMatrix;o<<"["<<m.m00<<","<<m.m01<<","<<m.m10<<","<<m.m11<<"]";}
 }
 o<<"],\"diagnostics\":[";for(int i=0;i<12;++i){if(i)o<<",";o<<pvzh1_actor_pose_diagnostic[i];}o<<"]}\n";
 h1DirtyCanvas.enabled=true;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
