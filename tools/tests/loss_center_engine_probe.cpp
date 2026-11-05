/* Real failure-animation construction at the H1 screen anchor. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CutScene.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/ScreenLayout.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <cmath>
#include <stdexcept>
namespace {void Check(bool x){if(!x)throw std::runtime_error("Loss screen is not anchored to the H1 center");}}
extern "C" void pvzh1_loss_center_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mName="LOSS_LAYOUT";profile.mLevel=2;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto* b=gLawnApp->mBoard;auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* pixels=target->GetBits();auto out=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));unsigned cases=0;
 auto verify=[&](Reanimation* r,const char* name){
  Check(r&&std::abs(r->mOverlayMatrix.m02+b->mX-float(H1BoardOffset))<.001f&&std::abs(r->mOverlayMatrix.m12+b->mY)<.001f);
  r->SetFramesForLayer("ZombiesWon");r->mAnimTime=.95f;r->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;
  h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();for(unsigned i=0;i<480*272;++i)pixels[i]=0xff202020u;Graphics g(screen);g.Translate(b->mX,b->mY);r->Draw(&g);
  std::ofstream f(out/(std::string(name)+".ppm"),std::ios::binary);f<<"P6\n480 272\n255\n";for(unsigned i=0;i<480*272;++i){char rgb[3]={char(pixels[i]>>16),char(pixels[i]>>8),char(pixels[i])};f.write(rgb,3);}++cases;r->mDead=true;
 };
 for(int x:{130,220,-120}){b->mX=x;b->mY=0;gLawnApp->mGameScene=SCENE_ZOMBIES_WON;b->mCutScene->mCutsceneTime=6000;b->mCutScene->UpdateZombiesWon();verify(gLawnApp->ReanimationGet(b->mCutScene->mZombiesWonReanimID),(std::string("loss-cutscene-")+std::to_string(x)).c_str());}
 b->mX=130;gLawnApp->mGameMode=GAMEMODE_PUZZLE_I_ZOMBIE_1;gLawnApp->mGameScene=SCENE_PLAYING;b->ZombiesWon(nullptr);
 Reanimation* alternate=nullptr;for(auto* r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)if(!r->mDead&&r->mReanimationType==REANIM_ZOMBIES_WON)alternate=r;verify(alternate,"loss-alternate");
 gLawnApp->mGameMode=GAMEMODE_ADVENTURE;gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
 std::ofstream report(out/"loss-center-verification.json");report<<"{\"passed\":true,\"actual_cutscene_and_alternate_constructors\":true,\"cases\":"<<cases<<",\"board_camera_x\":[130,220,-120],\"authored_canvas_left_on_screen\":130,\"authored_center_logical\":530,\"screen_center_x\":240,\"resolution\":[480,272]}\n";
}
