/* Dense board, repeated dialogs and the serialized native-test fixture. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/CutScene.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/Widget/NewUserDialog.h"
#include "Lawn/Widget/NewOptionsDialog.h"
#include "graphics/ImageLifetime.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "OptionsFeedback.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
extern void H1PreloadCurrentBoardForPlay(LawnApp*);
extern "C" void pvzh1_actor_pose_end_frame();
extern "C" volatile uint32_t pvzh1_actor_frame_lifetime_diagnostic[4];
namespace {
void Check(bool ok,const char* message){if(!ok){fprintf(stderr,"DENSE LIFETIME: %s\n",message);throw std::runtime_error(message);}}
}
extern "C" void pvzh1_image_lifetime_dense_probe(){
    using namespace Sexy;
    gLawnApp->ShowGameSelector();gLawnApp->DoCreateUserDialog();
    static_cast<NewUserDialog*>(gLawnApp->GetDialog(DIALOG_CREATEUSER))->SetName("LifeDense");
    gLawnApp->FinishCreateUserDialog(true);
    auto* profile=gLawnApp->mPlayerInfo;Check(profile&&profile->mName=="LifeDense","Player creation failed");
    profile->mLevel=6;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
    auto* board=gLawnApp->mBoard;Check(board,"Level six missing");
    for(int i=0;i<board->mSeedBank->mNumPackets;++i)
        board->mSeedBank->mSeedPackets[i].SetPacketType(SeedType(i%6),SEED_NONE);
    for(int row=0;row<5;++row)for(int col=0;col<8;++col)
        board->AddPlant(col,row,col<3?SEED_SUNFLOWER:SEED_PEASHOOTER);
    gLawnApp->StartPlaying();
    board->mCutScene->mSeedChoosing=false;board->mCutScene->mUpsellHideBoard=false;
    board->mX=130;board->mY=0;board->mSunMoney=500;
    H1PreloadCurrentBoardForPlay(gLawnApp);
    auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();
    auto draw=[&](){h1DirtyCanvas.RequireClear();h1DirtyCanvas.Begin(target);Graphics graphics(screen);graphics.Translate(board->mX,board->mY);board->Draw(&graphics);H1ImageCacheEndFrame();};
    draw();pvzh1_actor_pose_end_frame();const auto warmed=H1ImageCacheGetStats();
    const std::vector<uint32_t> expected(target->mBits.get(),target->mBits.get()+480*272);
    for(unsigned i=0;i<40;++i){draw();pvzh1_actor_pose_end_frame();Check(std::equal(expected.begin(),expected.end(),target->mBits.get()),"Unchanged board pixels changed");}
    Check(pvzh1_actor_frame_lifetime_diagnostic[1]>0,"Unused decoder reservations were retained");
    Check(H1ImageCacheGetStats().loads==warmed.loads,"Unchanged dense board reloaded image backing");
    Check(H1ImageCacheGetStats().evictions==0,"Dense board triggered automatic eviction");
    // A completed frame with no actor draws ends every decoder-bank lease.
    const auto releases=pvzh1_actor_frame_lifetime_diagnostic[1],kept=pvzh1_actor_frame_lifetime_diagnostic[3];
    pvzh1_actor_pose_end_frame();
    Check(!pvzh1_actor_frame_lifetime_diagnostic[3]&&(!kept||pvzh1_actor_frame_lifetime_diagnostic[1]>releases),"Undrawn animation family retained decoded pages");
    draw();pvzh1_actor_pose_end_frame();
    Check(std::equal(expected.begin(),expected.end(),target->mBits.get()),"Retired animation bank did not reconstruct identical pixels");
    // Freeze a legal first-play state for the physical H1. The original V4
    // serializer writes it; no native game RAM or success result is injected.
    board->TryToSaveGame();
    Check(gLawnApp->FileExists(GetSavedGameName(GAMEMODE_ADVENTURE,profile->mId)),"Original dense-board serializer failed");
    gLawnApp->WriteCurrentUserConfig();
    const auto boardStorage=H1ImageResourcesStats().residentBytes;
    for(unsigned repeat=0;repeat<3;++repeat){
        gLawnApp->DoNewOptions(false);
        auto* dialog=static_cast<NewOptionsDialog*>(gLawnApp->GetDialog(DIALOG_NEWOPTIONS));Check(dialog,"Options missing");
        h1DirtyCanvas.Begin(target);draw();Graphics graphics(screen);graphics.Translate(dialog->mX,dialog->mY);
        {H1ImageUseScope use(H1ImageOwner::Widget,dialog);dialog->Draw(&graphics);}
        H1ImageCacheEndFrame();gLawnApp->KillNewOptionsDialog();board->Pause(false);draw();
        Check(H1ImageCacheGetStats().evictions==0,"Dialog discarded active board textures");
    }
    gLawnApp->DoBackToMain();Check(!gLawnApp->mBoard&&gLawnApp->mGameScene==SCENE_MENU,"Board did not retire");
    const auto menu=H1ImageResourcesStats();
    Check(menu.residentBytes<=1024*1024,"Old dense-scene textures remained in menu");
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"image-lifetime-dense-verification.json");
    out<<"{\"passed\":true,\"plants\":40,\"stable_frame_comparisons\":40,\"stable_frame_image_reloads\":0,\"options_cycles\":3,\"automatic_evictions\":0,\"board_image_bytes\":"<<boardStorage<<",\"menu_image_bytes\":"<<menu.residentBytes<<",\"source_releases\":"<<menu.sourceReleases<<",\"original_V4_fixture_written\":true,\"native_game_RAM_written\":false}\n";
}
