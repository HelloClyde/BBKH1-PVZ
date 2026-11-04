/* Test-only deterministic poses compare the same original widget code with
 * display hooks off/on. These are renderer fixtures, not gameplay input proof. */
#include "LawnApp.h"
#include "Lawn/Widget/GameSelector.h"
#include "Lawn/Widget/GameButton.h"
#include "Lawn/Widget/AchievementsScreen.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/ScreenLayout.h"
#include "selector/SelectorOffline.h"
#include <vector>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstring>
#include <chrono>
using namespace Sexy;
namespace {
unsigned checks=0,cases=0;
void Check(bool okay,const char* message){++checks;if(!okay)throw std::runtime_error(message);}
std::vector<uint32_t> Frame(GameSelector* selector,bool enabled,unsigned seed,bool children=true){
    H1SelectorOfflineTestEnable(enabled);
    auto* proxy=H1GetPhysicalScreen(gLawnApp->mGLInterface->GetScreenImage());Check(proxy&&proxy->Active(),"physical target active");
    auto* target=proxy->Target();auto* bits=target->GetBits();
    for(unsigned i=0;i<H1ScreenWidth*H1ScreenHeight;++i)bits[i]=0xff000000u|((i*1664525u+seed*1013904223u)&0xffffffu);
    Graphics g(proxy);selector->Draw(&g);
    if(children){NewLawnButton* list[]={selector->mAchievementsButton,selector->mZombatarButton,selector->mChangeUserButton,
        selector->mSurvivalButton,selector->mZenGardenButton,selector->mPuzzleButton,selector->mMinigameButton,
        selector->mAdventureButton,selector->mOptionsButton,selector->mQuitButton,selector->mHelpButton,selector->mStoreButton,selector->mAlmanacButton};
        for(auto* button:list)if(button->mVisible){Graphics child(proxy);child.Translate(button->mX,button->mY);button->Draw(&child);}
        Graphics overlay(proxy);selector->DrawOverlay(&overlay);
    }
    return std::vector<uint32_t>(bits,bits+H1ScreenWidth*H1ScreenHeight);
}
void Pair(GameSelector* selector,unsigned seed=0){auto before=Frame(selector,false,seed);auto after=Frame(selector,true,seed);Check(before==after,"original/offline full-frame pixels differ");++cases;}
}
extern "C" void pvzh1_offline_selector_engine_probe(){
    PlayerInfo profile;profile.mName="H1TEST";profile.mLevel=1;
    PlayerInfo* old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
    gLawnApp->ShowGameSelector();auto* selector=gLawnApp->mGameSelector.get();
    auto* animation=gLawnApp->ReanimationGet(selector->mSelectorReanimID);
    animation->PlayReanim("anim_sign",REANIM_PLAY_ONCE_AND_HOLD,0,30);
    animation->mAnimTime=1;animation->mLoopCount=1;selector->mSelectorState=SELECTOR_IDLE;
    NewLawnButton* buttons[]={selector->mAdventureButton,selector->mMinigameButton,selector->mPuzzleButton,
        selector->mOptionsButton,selector->mHelpButton,selector->mQuitButton,selector->mChangeUserButton,
        selector->mStoreButton,selector->mAlmanacButton,selector->mZenGardenButton,selector->mSurvivalButton,
        selector->mZombatarButton,selector->mAchievementsButton};
    for(auto* button:buttons)button->mBtnNoDraw=false;
    selector->Update(); // Original TrackButton/attachment/cloud/leaf placement.
    Pair(selector);Check(pvzh1_selector_offline_diagnostic[4]==2,"both stable selector groups must hit");
    Check(pvzh1_selector_offline_diagnostic[5]>=7,"real visible button hooks must hit");
    Pair(selector);Check(pvzh1_selector_offline_diagnostic[6]>=1,"welcome should replay without layout");
    auto* target=H1GetPhysicalScreen(gLawnApp->mGLInterface->GetScreenImage())->Target();
    for(unsigned seed:{1u,17u,99u})Pair(selector,seed);
    // Each normal/hover/down selector child and grey locked mode preserves its
    // original Graphics end state, not just its output pixels.
    unsigned buttonHits=0;
    for(auto* button:buttons){if(!button->mVisible)continue;
        for(unsigned state=0;state<3;++state){button->mIsOver=state!=0;button->mIsDown=state==2;button->mOverAlpha=0;
            Pair(selector,state+7);
            Graphics a(gLawnApp->mGLInterface->GetScreenImage());a.Translate(button->mX,button->mY);
            Graphics b(a);H1SelectorOfflineTestEnable(false);button->Draw(&a);
            H1SelectorOfflineTestEnable(true);auto prior=pvzh1_selector_offline_diagnostic[5];button->Draw(&b);
            if(pvzh1_selector_offline_diagnostic[5]!=prior)++buttonHits;
            Check(a.mColor==b.mColor&&a.mFont==b.mFont&&a.mColorizeImages==b.mColorizeImages&&
                a.mDrawMode==b.mDrawMode&&a.mTransX==b.mTransX&&a.mTransY==b.mTransY,"button Graphics state differs");
        }
        button->mIsOver=button->mIsDown=false;
    }
    profile.mName="SECOND_PLAYER";Pair(selector);Pair(selector);
    // Uncovered animation/geometry/tint/fade states stay in original code.
    auto hit=pvzh1_selector_offline_diagnostic[4];selector->mStartingGame=true;Pair(selector);Check(pvzh1_selector_offline_diagnostic[4]>hit,"Adventure must retain cached menu drawing");selector->mStartingGame=false;
    hit=pvzh1_selector_offline_diagnostic[4];selector->mSlideCounter=1;Pair(selector);Check(pvzh1_selector_offline_diagnostic[4]==hit,"slide must fall back");selector->mSlideCounter=0;
    selector->mSelectorState=SELECTOR_OPEN;Pair(selector);Check(pvzh1_selector_offline_diagnostic[4]==hit,"open must fall back");selector->mSelectorState=SELECTOR_IDLE;
    Graphics tinted(gLawnApp->mGLInterface->GetScreenImage());tinted.SetColorizeImages(true);tinted.SetColor(Color(128,64,32));
    Check(!H1SelectorOfflineDrawGroup(selector,&tinted,animation,RENDER_GROUP_NORMAL),"parent tint must fall back");
    auto* button=selector->mOptionsButton;button->mOverAlpha=.5;Graphics fade(gLawnApp->mGLInterface->GetScreenImage());fade.Translate(button->mX,button->mY);
    Check(!H1SelectorOfflineDrawButton(button,&fade),"fractional hover fade must fall back");button->mOverAlpha=0;
    const auto warm=pvzh1_selector_offline_diagnostic[11];for(unsigned i=0;i<10;++i)Pair(selector);
    Check(pvzh1_selector_offline_diagnostic[11]==warm,"warm frames must not reopen bundle");
    // Real manager parked pages inherit an empty clip. The old manual selector
    // fixture omitted these root siblings and missed their invisible font work.
    auto* page=selector->mAchievementsWidget.get();
    auto* proxy=H1GetPhysicalScreen(gLawnApp->mGLInterface->GetScreenImage());
    unsigned pageCases=0;
    for(int y:{600,601,599,0,-500,-page->mHeight-1}){
        std::vector<uint32_t> frames[2];
        const uint32_t prior=pvzh1_selector_cull_diagnostic[0];
        for(unsigned enabled=0;enabled<2;++enabled){
            auto* pixels=proxy->Target()->GetBits();
            for(unsigned i=0;i<H1ScreenWidth*H1ScreenHeight;++i)pixels[i]=0xff000000u|((i*1664525u+17u)&0xffffffu);
            Graphics g(proxy);g.Translate(0,y);g.ClipRect(0,0,page->mWidth,page->mHeight);
            H1SelectorOfflineTestEnable(enabled);page->Draw(&g);
            frames[enabled].assign(pixels,pixels+H1ScreenWidth*H1ScreenHeight);
        }
        Check(frames[0]==frames[1],"achievement-page cull changed physical pixels");
        const bool empty=y>=600||y+page->mHeight<=0;
        Check(pvzh1_selector_cull_diagnostic[0]-prior==(empty?1u:0u),"visible achievement page was culled or hidden page was rendered");
        ++pageCases;
    }
    // Allocation rejection must preserve the old pixels and must not trigger
    // another allocation/eviction/reload loop on every unchanged menu frame.
    H1SelectorOfflineRelease(selector);H1SelectorOfflineTestRejectVisibility(true);
    Pair(selector);const uint32_t failedPlans=pvzh1_selector_offline_diagnostic[10];
    for(unsigned i=0;i<3;++i)Pair(selector,i+101);
    Check(pvzh1_selector_offline_diagnostic[10]==failedPlans,"rejected visibility plan retried on every frame");
    H1SelectorOfflineTestRejectVisibility(false);H1SelectorOfflineRelease(selector);Pair(selector);
    const uint32_t resident=pvzh1_selector_offline_diagnostic[2],groups=pvzh1_selector_offline_diagnostic[4],welcome=pvzh1_selector_offline_diagnostic[6];
    H1SelectorOfflineRelease(selector);Check(pvzh1_selector_offline_diagnostic[2]==0,"scene release must free bundle and username cache");
    std::ofstream out(std::filesystem::path(std::getenv("PVZ_HOST_OUTPUT"))/"selector-offline-engine-verification.json");
    out<<"{\"test_only\":true,\"all_passed\":true,\"checks\":"<<checks<<",\"full_frame_pixel_cases\":"<<cases
       <<",\"clipped_page_cases\":"<<pageCases<<",\"hidden_page_skips\":"<<pvzh1_selector_cull_diagnostic[0]
       <<",\"group_hits\":"<<groups<<",\"button_state_hits\":"<<buttonHits<<",\"welcome_hits\":"<<welcome
       <<",\"resident_requested_bytes\":"<<resident<<",\"released_bytes\":0,\"warm_bundle_opens\":0,\"original_attachments_preserved\":true,\"starting_retains_cache\":true,\"fallback_states\":[\"open\",\"slide\",\"parent_tint\",\"hover_fade\"],\"gameplay_input_proof\":false}\n";
    gLawnApp->mPlayerInfo=old;
}
