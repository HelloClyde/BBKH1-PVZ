/* Observe real game updates/draws on the fixed dense-board input. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/System/SaveGame.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern void H1PreloadCurrentBoard(LawnApp*);
extern "C" volatile uint32_t pvzh1_actor_fallback_diagnostic[64];
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" void pvzh1_dense_draw_observe_begin() __attribute__((weak));
extern "C" void pvzh1_dense_render_probe(){
 PlayerInfo p;p.mName="DENSE_TRACE";p.mId=1;p.mLevel=6;auto* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&p;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;
 const auto input=std::filesystem::absolute("build/dense-plant-fixture-v3/save/userdata/game1_0.v4").string();std::fprintf(stderr,"DENSE before load %s\n",input.c_str());
 if(!LawnLoadGame(board,input)){std::fprintf(stderr,"DENSE input load returned false\n");throw std::runtime_error("Dense input load failed");}
 std::fprintf(stderr,"DENSE after load\n");
 H1PreloadCurrentBoard(gLawnApp);board->mPaused=false;gLawnApp->mGameScene=SCENE_PLAYING;board->mX=130;board->mY=0;
 auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();h1DirtyCanvas.enabled=false;
 if(pvzh1_dense_draw_observe_begin)pvzh1_dense_draw_observe_begin();
 for(unsigned u=0;u<1000;++u){if(u%250==0)std::fprintf(stderr,"DENSE update %u\n",u);board->Update();if(u%4==0){h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();Sexy::Graphics g(screen);g.Translate(board->mX,board->mY);board->Draw(&g);}}
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"dense-fallback-observation.json");out<<"{\"observed\":true,\"original_board_updates\":1000,\"original_board_draws\":250,\"fallback\":[";for(unsigned i=0;i<64;++i){if(i)out<<',';out<<pvzh1_actor_fallback_diagnostic[i];}out<<"],\"actor\":[";for(unsigned i=0;i<16;++i){if(i)out<<',';out<<pvzh1_actor_pose_diagnostic[i];}out<<"]}\n";gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
