/* Reuse unchanged paused-board pixels; SPDX-License-Identifier: LGPL-3.0-or-later. */
#include "PausedBoardCache.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Cutscene.h"
#include "graphics/Graphics.h"
#include "PhysicalScreen.h"
#include "DirtyCanvas.h"
#include "PixelCopy.h"
#include "graphics/GLInterface.h"
#include <cstring>
#include <new>
#include <algorithm>
extern "C" {volatile unsigned pvzh1_paused_board_diagnostic[4];}
namespace {
constexpr unsigned Pixels=Sexy::H1ScreenWidth*Sexy::H1ScreenHeight,PagePixels=1024,Pages=(Pixels+1023)/1024;
struct Key {Board* board;Sexy::MemoryImage* target;int main,level,sun,wave,cutscene[9],fade,stop;
    bool mustache,future,pinata,dance,daisy,sukhbir,superMower;};
Key current{};uint32_t* pages[Pages]{};bool valid=false,busy=false,pending=false,enabled=true;
void Drop(){for(auto& p:pages){delete[] p;p=nullptr;}valid=pending=false;current={};pvzh1_paused_board_diagnostic[0]=0;}
Sexy::MemoryImage* Target(Board* b,Sexy::Graphics* g,Key& key){
    if(!enabled||!b||!g||!b->mPaused||b->mApp->mGameMode!=GAMEMODE_ADVENTURE||b->mApp->mGameScene!=GameScenes::SCENE_PLAYING||
       b->mApp->GetDialogCount()==0||b->mX!=int(Sexy::H1BoardOffset)||b->mY||!b->mCutScene||
       b->mCutScene->mSeedChoosing||b->mCutScene->mUpsellHideBoard||g->mTransX!=b->mX||g->mTransY||
       g->mScaleX!=1||g->mScaleY!=1||g->mDrawMode!=Sexy::Graphics::DRAWMODE_NORMAL||g->mColorizeImages||
       g->mClipRect.mX>0||g->mClipRect.mY>0||g->mClipRect.mX+g->mClipRect.mWidth<int(Sexy::H1LogicalWidth)||
       g->mClipRect.mY+g->mClipRect.mHeight<int(Sexy::H1LogicalHeight))return nullptr;
    auto* screen=Sexy::H1GetPhysicalScreen(dynamic_cast<Sexy::GLImage*>(g->mDestImage));
    if(!screen||!screen->Active())return nullptr;
    auto* target=screen->Target();if(target!=h1DirtyCanvas.owner||!target->mBits)return nullptr;
    std::memset(&key,0,sizeof(key));key.board=b;key.target=target;key.main=b->mMainCounter;key.level=b->mLevel;key.sun=b->mSunMoney;key.wave=b->mCurrentWave;
    const auto* c=b->mCutScene.get();const int clocks[]={c->mCutsceneTime,c->mSodTime,c->mGraveStoneTime,c->mReadySetPlantTime,c->mFogTime,c->mBossTime,c->mCrazyDaveTime,c->mLawnMowerTime,c->mCrazyDaveCountDown};
    std::memcpy(key.cutscene,clocks,sizeof(clocks));key.fade=b->mBoardFadeOutCounter;key.stop=b->mTimeStopCounter;
    key.mustache=b->mMustacheMode;key.future=b->mFutureMode;key.pinata=b->mPinataMode;key.dance=b->mDanceMode;
    key.daisy=b->mDaisyMode;key.sukhbir=b->mSukhbirMode;key.superMower=b->mSuperMowerMode;
    return target;
}
bool Same(const Key& a,const Key& b){return !std::memcmp(&a,&b,sizeof(Key));}
}
void H1PausedBoardPrime(Board* b){
    if(!b||!b->mApp->mGLInterface)return;
    Sexy::Graphics g(b->mApp->mGLInterface->GetScreenImage());
    g.mTransX=b->mX;g.mTransY=b->mY;
    Key key{};if(!Target(b,&g,key))return;
    if(valid&&Same(key,current))return;
    // Input runs after the previous complete presentation. Its untouched
    // canvas is already the exact image to freeze under the modal dialog.
    Drop();current=key;pending=true;H1PausedBoardCapture(b,&g);
}
bool H1PausedBoardReplay(Board* b,Sexy::Graphics* g){
    if(busy)return false;Key key{};auto* target=Target(b,g,key);
    if(!target){if(valid||pending)Drop();return false;}
    if(valid&&Same(key,current)){
        busy=true;
        const uint64_t identity[3]={0x504155534544ull,(uint64_t)(uintptr_t)b,(uint64_t)key.main};
        h1DirtyCanvas.Root(target,identity,sizeof(identity));
        H1CanvasKnownDraw drawing(target,0,0,Sexy::H1ScreenWidth,Sexy::H1ScreenHeight);
        auto* out=target->mBits.get();
        for(unsigned i=0;i<Pages;++i)H1CopyPixels(out+i*PagePixels,pages[i],std::min(PagePixels,Pixels-i*PagePixels));
        target->BitsChanged();Sexy::H1ScreenNoteDraw(static_cast<Sexy::GLImage*>(g->mDestImage));
        ++pvzh1_paused_board_diagnostic[1];busy=false;return true;
    }
    Drop();current=key;pending=true;return false;
}
void H1PausedBoardCapture(Board* b,Sexy::Graphics* g){
    if(!pending||busy)return;Key key{};auto* target=Target(b,g,key);
    if(!target||!Same(key,current)){Drop();return;}
    busy=true;bool ok=true;
    for(unsigned i=0;i<Pages;++i){const unsigned n=std::min(PagePixels,Pixels-i*PagePixels);
        pages[i]=new(std::nothrow) uint32_t[n];if(!pages[i]){ok=false;break;}
        std::memcpy(pages[i],target->mBits.get()+i*PagePixels,n*4);}
    if(!ok)Drop();else{valid=true;pending=false;pvzh1_paused_board_diagnostic[0]=Pixels*4;++pvzh1_paused_board_diagnostic[2];}
    busy=false;
}
void H1PausedBoardForget(Board* b){if(current.board==b&&!busy)Drop();}
extern "C" void pvzh1_paused_board_shutdown(){if(!busy)Drop();}
extern "C" size_t pvzh1_paused_board_release_pressure(size_t){
    if(busy||!valid)return 0;const auto bytes=Pixels*4;Drop();++pvzh1_paused_board_diagnostic[3];return bytes;
}
#ifdef PVZ_HOST_RUNNER
void H1PausedBoardTestEnable(bool value){Drop();enabled=value;}
#endif
