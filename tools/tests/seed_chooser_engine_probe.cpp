#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Cutscene.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/System/PlayerInfo.h"
#include "Lawn/Widget/SeedChooserScreen.h"
#include "Lawn/Widget/GameButton.h"
#include "Lawn/Widget/TitleScreen.h"
#include "widget/WidgetManager.h"
#include "graphics/ScreenLayout.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
namespace {void CheckAt(bool v,int line){if(!v){std::fprintf(stderr,"CHOOSER CHECK FAILED line %d\n",line);std::fflush(stderr);throw std::runtime_error("Centered seed chooser drawing/hit origins differ");}}}
#define Check(v) CheckAt(bool(v),__LINE__)
extern "C" void pvzh1_seed_chooser_probe(){
    PlayerInfo profile;profile.mName="CHOOSER_TEST";profile.mId=1;profile.mLevel=21;
    auto* previous=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
    gLawnApp->mWidgetManager->RemoveWidget(gLawnApp->mTitleScreen.get());gLawnApp->mTitleScreen.reset();
    gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
    if(!gLawnApp->mSeedChooserScreen)gLawnApp->ShowSeedChooserScreen();
    auto* chooser=gLawnApp->mSeedChooserScreen.get();auto* manager=gLawnApp->mWidgetManager.get();
    // Legal UI precondition: the panel has finished its intro slide. The
    // physical test separately exercises the complete original cutscene.
    chooser->Move(Sexy::H1BoardOffset,0);chooser->mSeedChooserAge=100;gLawnApp->mBoard->mSeedBank->mY=0;
    chooser->SetVisible(true);manager->BringToFront(chooser);gLawnApp->mBoard->mCutScene->StartSeedChooser();
    Check(chooser->mX==int(Sexy::H1BoardOffset));Check(chooser->mStartButton->mParentWidget==chooser);
    const SeedType seeds[]={SEED_PEASHOOTER,SEED_SUNFLOWER,SEED_CHERRYBOMB,SEED_WALLNUT,SEED_POTATOMINE,SEED_LILYPAD};
    for(auto seed:seeds){int x,y;chooser->GetSeedPositionInChooser(seed,x,y);manager->MouseMove(chooser->mX+x+25,y+35);manager->MouseDown(chooser->mX+x+25,y+35,1);manager->MouseUp(chooser->mX+x+25,y+35,1);for(unsigned i=0;i<30;++i)chooser->Update();}
    Check(chooser->mSeedsInBank==6&&!chooser->mSeedsInFlight);
    int x,y;chooser->GetSeedPositionInBank(0,x,y);
    // The left seed is outside the centered panel but must still receive its
    // original chooser-local coordinate through the actual widget manager.
    manager->MouseMove(chooser->mX+x+25,y+35);manager->MouseDown(chooser->mX+x+25,y+35,1);manager->MouseUp(chooser->mX+x+25,y+35,1);
    for(unsigned i=0;i<30;++i)chooser->Update();Check(chooser->mSeedsInBank==5);
    chooser->GetSeedPositionInChooser(SEED_PEASHOOTER,x,y);manager->MouseMove(chooser->mX+x+25,y+35);manager->MouseDown(chooser->mX+x+25,y+35,1);manager->MouseUp(chooser->mX+x+25,y+35,1);
    for(unsigned i=0;i<30;++i)chooser->Update();Check(chooser->mSeedsInBank==6);
    const int sx=chooser->mX+chooser->mStartButton->mX+78,sy=chooser->mStartButton->mY+21;
    manager->MouseMove(sx,sy);Check(chooser->mStartButton->IsMouseOver());
    manager->MouseDown(sx,sy,1);manager->MouseUp(sx,sy,1);Check(!gLawnApp->mBoard->mCutScene->mSeedChoosing);
    gLawnApp->KillSeedChooserScreen();gLawnApp->KillBoard();gLawnApp->mPlayerInfo=previous;
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"seed-chooser-verification.json");
    out<<"{\"passed\":true,\"ordinary_widget_manager_clicks\":true,\"six_seeds_selected\":true,\"left_seed_remove_and_reselect\":true,\"visible_start_button_hit\":true,\"original_chooser_ended\":true}\n";
}
