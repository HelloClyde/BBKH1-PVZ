#ifdef PVZH1_WIDESCREEN
#include <cstdint>
extern "C" volatile uint32_t pvzh1_selector_retained_diagnostic[4];
#endif
/* Exact whole-canvas/LCD comparison with controlled menu poses. */
#include "LawnApp.h"
#include "Lawn/Widget/GameSelector.h"
#include "Lawn/Widget/GameButton.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/DirtyPresentation.h"
#include "selector/SelectorOffline.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdio>
using namespace Sexy;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
extern "C" void pvzh1_dirty_menu_probe(){
 PlayerInfo player;player.mName="H1test";player.mLevel=1;
 auto* priorPlayer=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&player;
 gLawnApp->ShowGameSelector();auto* selector=gLawnApp->mGameSelector.get();
 auto* animation=gLawnApp->ReanimationGet(selector->mSelectorReanimID);
 animation->PlayReanim("anim_sign",REANIM_PLAY_ONCE_AND_HOLD,0,30);
 animation->mAnimTime=1;animation->mLoopCount=1;selector->mSelectorState=SELECTOR_IDLE;
 auto* device=gLawnApp->mGLInterface.get();auto saved=std::move(device->mScreenImage);
 std::vector<std::vector<uint32_t>> frames,lcdFrames;
 unsigned cases=0,limited=0,useful=0;uint32_t skipped=0;
 for(bool enabled:{false,true}){
  H1SelectorOfflineRelease(selector);h1DirtyCanvas.enabled=enabled;
  device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,H1LogicalWidth,H1LogicalHeight,H1ScreenWidth,H1ScreenHeight);
  auto* screen=device->GetScreenImage();auto* target=H1ScreenTarget(screen);
  std::vector<uint32_t> lcd(480*272,0x89753521);H1PresentationMap map;H1DirtyPresentation presenter;
  for(unsigned frame=0;frame<48;++frame){
   player.mName=frame<20?"H1test":"SECOND_PLAYER";
   selector->mSlideCounter=(frame==31)?1:0;
   const int backgroundTrack=animation->FindTrackIndex("SelectorScreen_BG_Right");
   animation->mTrackInstances[backgroundTrack].mTrackColor=(frame>=28&&frame<=36)?Color(111,153,207):Color::White;
   for(unsigned i=0;i<6;++i){auto* cloud=gLawnApp->ReanimationGet(selector->mCloudReanimID[i]);cloud->mOverlayMatrix.m02=(float)(int((frame*19+i*109)%1100)-150);cloud->mOverlayMatrix.m12=20.f+i*18;}
#ifdef PVZH1_WIDESCREEN
   // Move the actual attached flowers across fresh tiles and static layer
   // boundaries; this must exercise restoration of the foreground prefix.
   for(unsigned i=0;i<3;++i){auto* flower=gLawnApp->ReanimationGet(selector->mFlowerReanimID[i]);
       flower->mOverlayMatrix.m02=float(int((frame*97+i*199)%H1LogicalWidth)-900);
       flower->mOverlayMatrix.m12=float(int((frame*67+i*131)%500)-400);}
#endif
   auto* hover=selector->mOptionsButton;hover->mIsOver=(frame%3)!=0;hover->mIsDown=(frame%3)==2;hover->mOverAlpha=0;
   Graphics g(screen);selector->Draw(&g);
   NewLawnButton* buttons[]={selector->mAchievementsButton,selector->mZombatarButton,selector->mChangeUserButton,selector->mSurvivalButton,selector->mZenGardenButton,selector->mPuzzleButton,selector->mMinigameButton,selector->mAdventureButton,selector->mOptionsButton,selector->mQuitButton,selector->mHelpButton,selector->mStoreButton,selector->mAlmanacButton};
   for(auto* button:buttons)if(button->mVisible){Graphics child(screen);child.Translate(button->mX,button->mY);button->Draw(&child);}
   Graphics overlay(screen);selector->DrawOverlay(&overlay);
   {H1CanvasProtectedPixels read(target);const auto* pixels=target->GetBits();
    const int px=int((frame*67)%850)-25,py=int((frame*71)%650)-25;
    if(!enabled){frames.emplace_back(pixels,pixels+H1ScreenWidth*H1ScreenHeight);H1PresentMapped(lcd.data(),pixels,H1ScreenWidth,H1ScreenHeight,px,py,map);lcdFrames.push_back(lcd);}
    else {
     Check(frames[frame]==std::vector<uint32_t>(pixels,pixels+H1ScreenWidth*H1ScreenHeight),"menu canvas differs from full redraw");
     const uint32_t* mask=h1DirtyCanvas.Presentation(pixels);
     if(mask){++limited;unsigned dirty=0;for(unsigned i=0;i<H1DirtyCanvas::Rows;++i)dirty+=__builtin_popcount(mask[i]);if(dirty<H1DirtyCanvas::Rows*(H1ScreenWidth/H1TileWidth))++useful;}
     Check(presenter.Present(lcd.data(),pixels,H1ScreenWidth,H1ScreenHeight,px,py,map,mask),"menu presentation fallback");
     if(lcd!=lcdFrames[frame]){fprintf(stderr,"DIRTY MENU mismatch frame %u\n",frame);throw std::runtime_error("limited LCD differs from full frame presentation");}++cases;
    }
   }
   h1DirtyCanvas.Begin(target);target->BitsChanged();
  }
  skipped=presenter.skippedBlocks;presenter.Reset();device->mScreenImage.reset();
 }
 Check(limited>20&&useful>20,"menu never used a limited damage mask");
#ifdef PVZH1_WIDESCREEN
 extern volatile uint32_t unused;
 Check(pvzh1_selector_retained_diagnostic[0]>20,"wide menu retention path was never active");
 Check(pvzh1_selector_retained_diagnostic[2]>0,"moving attachments never initialized a foreground prefix");
 fprintf(stderr,"MENU RETAINED frames=%u tile_initializations=%u attachment_prefixes=%u\n",pvzh1_selector_retained_diagnostic[0],pvzh1_selector_retained_diagnostic[1],pvzh1_selector_retained_diagnostic[2]);
#endif
 device->mScreenImage=std::move(saved);h1DirtyCanvas.enabled=true;gLawnApp->mPlayerInfo=priorPlayer;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"dirty-menu-verification.json");
 out<<"{\"passed\":true,\"whole_canvas_and_LCD_cases\":"<<cases<<",\"limited_presentations\":"<<limited<<",\"useful_masks\":"<<useful<<",\"skipped_blocks\":"<<skipped<<",\"cloud_motion\":true,\"button_states\":true,\"username_change\":true,\"slide_fallback\":true,\"cursor_edges\":true}\n";
}
