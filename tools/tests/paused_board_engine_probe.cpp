#include "LawnApp.h"
#include "widget/Dialog.h"
#include "Lawn/Board.h"
#include "Lawn/Cutscene.h"
#include "Lawn/System/PlayerInfo.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/PausedBoardCache.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/MemoryImageCache.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
namespace {void Check(bool value){if(!value)throw std::runtime_error("Paused-board replay differs from original");}}
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" size_t pvzh1_cache_memory_pressure(size_t);
extern "C" void pvzh1_paused_board_probe(){
    using namespace Sexy;
    PlayerInfo profile;profile.mName="PAUSE_TEST";profile.mLevel=1;
    auto* saved=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
    gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);auto* board=gLawnApp->mBoard;Check(board);
    gLawnApp->mGameScene=GameScenes::SCENE_PLAYING;
    board->mX=Sexy::H1BoardOffset;board->mY=0;board->mCutScene->mSeedChoosing=false;board->mCutScene->mUpsellHideBoard=false;
    gLawnApp->DoDialog(DIALOG_CONTINUE,true,"TEST","BODY","",Sexy::Dialog::BUTTONS_YES_NO);board->Pause(true);
    auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();auto* bits=target->GetBits();
    auto draw=[&](){Graphics g(screen);g.Translate(board->mX,board->mY);fprintf(stderr,"G trans=%f,%f scale=%f,%f clip=%d,%d %d,%d mode=%d colorize=%d game=%d cut=%d,%d owner=%p\n",g.mTransX,g.mTransY,g.mScaleX,g.mScaleY,g.mClipRect.mX,g.mClipRect.mY,g.mClipRect.mWidth,g.mClipRect.mHeight,g.mDrawMode,g.mColorizeImages,int(gLawnApp->mGameMode),board->mCutScene->mSeedChoosing,board->mCutScene->mUpsellHideBoard,h1DirtyCanvas.owner);board->Draw(&g);};
    unsigned checks=0;
    for(unsigned state=0;state<4;++state){
        H1PausedBoardTestEnable(false);h1DirtyCanvas.Begin(target);draw();
        std::vector<uint32_t> expected(bits,bits+480*272);
        H1PausedBoardTestEnable(true);h1DirtyCanvas.Begin(target);draw();
        fprintf(stderr,"PAUSED CACHE bytes=%u paused=%d dialogs=%d board=%d,%d scene=%d\n",pvzh1_paused_board_diagnostic[0],board->mPaused,gLawnApp->GetDialogCount(),board->mX,board->mY,int(gLawnApp->mGameScene));Check(pvzh1_paused_board_diagnostic[0]==480*272*4);
        Check(expected==std::vector<uint32_t>(bits,bits+480*272));++checks;
        if(state==0){
            // The board's synchronous draw has returned. A modal may now need
            // texture memory: its paused replacement must survive source eviction.
            pvzh1_cache_complete_draw_batch();
            const auto before=H1ImageCacheGetStats().evictions;
            for(unsigned retry=0;retry<64&&H1ImageCacheGetStats().evictions==before;++retry){
                Check(pvzh1_cache_memory_pressure(4096)>0);
                Check(pvzh1_paused_board_diagnostic[0]==480*272*4);
            }
            Check(H1ImageCacheGetStats().evictions>before);
        }
        for(unsigned frame=0;frame<4;++frame){
            for(unsigned i=0;i<480*272;++i)bits[i]=0xff000000u|((i*0x164b35u+frame)&0xffffffu);
            h1DirtyCanvas.Begin(target);draw();Check(expected==std::vector<uint32_t>(bits,bits+480*272));++checks;
            // Later child widgets may repaint arbitrary pixels. The next board
            // replay must remove these, rather than accumulate stale dialogs.
            bits[(frame+10)*480+100]=0xff123456;
        }
        if(state==0)++board->mSunMoney;
        if(state==1)++board->mMainCounter;
        if(state==2)++board->mCutScene->mCutsceneTime;
        const auto captures=pvzh1_paused_board_diagnostic[2];h1DirtyCanvas.Begin(target);draw();
        if(state<3)Check(pvzh1_paused_board_diagnostic[2]>captures);
    }
    board->Pause(false);h1DirtyCanvas.Begin(target);draw();Check(!pvzh1_paused_board_diagnostic[0]);
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"paused-board-verification.json");
    out<<"{\"passed\":true,\"whole_frame_comparisons\":"<<checks<<",\"replays\":"<<pvzh1_paused_board_diagnostic[1]<<",\"pressure_reclaims_sources_before_paused_replacement\":true,\"sun_main_counter_cutscene_invalidation\":true,\"unpause_releases_pixels\":true}\n";
    gLawnApp->KillSeedChooserScreen();gLawnApp->KillBoard();gLawnApp->mPlayerInfo=saved;
}
