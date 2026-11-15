/* A natural camera preview must retire its pixels before playable frames. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/CutScene.h"
#include "Lawn/LawnCommon.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/Widget/NewUserDialog.h"
#include "graphics/ImageLifetime.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
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
extern "C" void pvzh1_preview_image_handoff_probe(){
    using namespace Sexy;
    gLawnApp->ShowGameSelector();gLawnApp->DoCreateUserDialog();
    static_cast<NewUserDialog*>(gLawnApp->GetDialog(DIALOG_CREATEUSER))->SetName("Handoff");
    gLawnApp->FinishCreateUserDialog(true);
    auto* profile=gLawnApp->mPlayerInfo;Check(profile&&profile->mName=="Handoff","Player creation failed");
    profile->mLevel=3;gLawnApp->KillGameSelector();gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
    auto* board=gLawnApp->mBoard;
    auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* target=H1GetPhysicalScreen(screen)->Target();
    auto draw=[&](){h1DirtyCanvas.RequireClear();h1DirtyCanvas.Begin(target);Graphics graphics(screen);graphics.Translate(board->mX,board->mY);board->Draw(&graphics);H1ImageCacheEndFrame();pvzh1_actor_pose_end_frame();};
    for(unsigned i=0;i<5000&&gLawnApp->mGameScene!=SCENE_PLAYING;++i){gLawnApp->UpdateFrames();draw();}
    Check(gLawnApp->mGameScene==SCENE_PLAYING,"Did not reach real level-three play");draw();
    const auto before=H1ImageResourcesStats();Check(before.residentBytes<1500*1024,"Preview-only images remain owned during play");const std::vector<uint32_t> expected(target->mBits.get(),target->mBits.get()+480*272);
    H1ImageResourcesReleaseOwner(H1ImageOwner::Widget,board);
    const auto released=H1ImageResourcesStats();H1PreloadCurrentBoardForPlay(gLawnApp);draw();
    const auto after=H1ImageResourcesStats();
    Check(std::equal(expected.begin(),expected.end(),target->mBits.get()),"Retiring preview board storage changed play pixels");
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"preview-storage-verification.json");
    out<<"{\"passed\":true,\"before_bytes\":"<<before.residentBytes<<",\"immediately_released_bytes\":"<<before.residentBytes-released.residentBytes<<",\"after_play_rewarm_bytes\":"<<after.residentBytes<<",\"identical_480x272_pixels\":true,\"native_performance_tested\":false}\n";
}

