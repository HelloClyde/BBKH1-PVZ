#include "OptionsFeedback.h"
#include "SDL.h"
#include <cstdint>
#include "Lawn/LawnCommon.h"
#include "PvzpLib/PvzpCommon.h"
#include "Resources.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/MemoryImage.h"
#include "graphics/PausedBoardCache.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "graphics/Graphics.h"
#include "graphics/ImageFont.h"
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" size_t pvzh1_cache_memory_pressure(size_t);
/* Open count, pending, start ms, constructor ms, first presented ms, draws,
 * warmups, reserved images, failed preparations, reserved bytes, warmup ms. */
extern "C" { volatile uint32_t pvzh1_options_diagnostic[13]; }
#ifdef PVZ_NATIVE_PROFILE
// Read/write by the controlled native A/B test only. Normal launches use 0.
extern "C" { volatile uint32_t pvzh1_options_preclean_test; }
#endif
namespace {bool drawn=false;}
void H1OptionsPrepare(){
    ++pvzh1_options_diagnostic[6];const uint32_t start=SDL_GetTicks();
    pvzh1_options_diagnostic[7]=pvzh1_options_diagnostic[9]=0;
    // Small, fixed UI textures remain resident at their offline resolution.
    // Loading these while the loading screen is active avoids both FAT I/O
    // and allocator fallback after the first paused-board draw pins its data.
    for(auto* image:{Sexy::IMAGE_OPTIONS_MENUBACK,Sexy::IMAGE_OPTIONS_BACKTOGAMEBUTTON0,
                    Sexy::IMAGE_OPTIONS_BACKTOGAMEBUTTON2,Sexy::IMAGE_OPTIONS_SLIDERSLOT,
                    Sexy::IMAGE_OPTIONS_SLIDERKNOB2,Sexy::IMAGE_OPTIONS_CHECKBOX0,Sexy::IMAGE_OPTIONS_CHECKBOX1}){
        if(!image)continue;PvzpSandImageIfNeeded(image);
        auto* memory=dynamic_cast<Sexy::MemoryImage*>(image);
        if(Sexy::H1ImageCacheReserveUI(memory)){
            ++pvzh1_options_diagnostic[7];Sexy::H1ImageCacheStorageStats stats{};
            if(Sexy::H1ImageCacheGetStorageStats(memory,&stats))pvzh1_options_diagnostic[9]+=stats.bytes;
        }else ++pvzh1_options_diagnostic[8];
    }
    // Touch only the actual characters used by this dialog while the loader
    // is visible. Keep their small offline glyph blocks alongside the UI.
    Sexy::MemoryImage canvas(gLawnApp);canvas.Create(64,16);Sexy::Graphics g(&canvas);g.SetColor(Sexy::Color::White);
    auto warm=[&](Sexy::_Font* font,std::initializer_list<const char*> words){
        if(!font)return;g.SetFont(font);
        for(auto text:words)for(size_t at=0;text[at];){
            const unsigned c=static_cast<unsigned char>(text[at]);const size_t n=c<128?1:c<224?2:c<240?3:4;
            for(int y=0;y<=64;y+=16)g.DrawString(std::string_view(text+at,n),8,y);
            at+=n;
        }
        if(auto* imageFont=dynamic_cast<Sexy::ImageFont*>(font))for(const auto& layer:imageFont->mActiveLayerList)
            pvzh1_options_diagnostic[9]+=Sexy::H1ImageCacheReserveLoadedUI(dynamic_cast<Sexy::MemoryImage*>(layer.mScaledImage));
    };
    warm(Sexy::FONT_DWARVENTODCRAFT18,{"音乐","音效","图形加速","全屏显示"});
    warm(Sexy::FONT_DWARVENTODCRAFT18GREENINSET,{"重新开始关卡","主菜单","查看图鉴"});
    warm(Sexy::FONT_DWARVENTODCRAFT18BRIGHTGREENINSET,{"重新开始关卡","主菜单","查看图鉴"});
    warm(Sexy::FONT_DWARVENTODCRAFT36GREENINSET,{"返回游戏"});
    warm(Sexy::FONT_DWARVENTODCRAFT36BRIGHTGREENINSET,{"返回游戏"});
    pvzh1_options_diagnostic[10]=SDL_GetTicks()-start;
}
void H1OptionsBegin(){
    ++pvzh1_options_diagnostic[0];pvzh1_options_diagnostic[1]=1;pvzh1_options_diagnostic[2]=SDL_GetTicks();pvzh1_options_diagnostic[3]=pvzh1_options_diagnostic[4]=0;drawn=false;
    pvzh1_options_diagnostic[11]=pvzh1_options_diagnostic[12]=0;
    const uint32_t cleanupStart=SDL_GetTicks();
    bool preclean=false;
#ifdef PVZ_NATIVE_PROFILE
    preclean=pvzh1_options_preclean_test!=0;
#endif
    if(preclean&&gLawnApp&&gLawnApp->mBoard&&gLawnApp->mGameScene==SCENE_PLAYING&&!gLawnApp->GetDialogCount()){
        // No renderer is active in this input callback. Reclaim completed
        // gameplay textures before the paused-canvas allocation,
        // while retaining the already-scaled, explicitly reserved UI images.
        pvzh1_cache_complete_draw_batch();
        // The paused canvas needs about 510 KiB. Do not drain all gameplay
        // caches: thousands of tiny evictions stalled the first open.
        size_t reclaimed=0;
        for(unsigned i=0;i<256&&reclaimed<600u*1024u;++i){
            const size_t bytes=pvzh1_cache_memory_pressure(4096);
            if(!bytes)break;reclaimed+=bytes;
        }
    }
    // The allocation bridge already reclaims caches on actual allocation
    // pressure. Scanning/evicting a fixed 600 KiB here stalls every popup even
    // when its bounded canvas pages fit. UI resources remain reserved.
    pvzh1_options_diagnostic[11]=SDL_GetTicks()-cleanupStart;
}
void H1OptionsConstructed(){const uint32_t begin=SDL_GetTicks();if(gLawnApp)H1PausedBoardPrime(gLawnApp->mBoard);pvzh1_options_diagnostic[12]=SDL_GetTicks()-begin;pvzh1_options_diagnostic[3]=SDL_GetTicks()-pvzh1_options_diagnostic[2];}
void H1OptionsDrawn(){++pvzh1_options_diagnostic[5];drawn=true;}
void H1OptionsPresented(){if(drawn&&pvzh1_options_diagnostic[1]){pvzh1_options_diagnostic[4]=SDL_GetTicks()-pvzh1_options_diagnostic[2];pvzh1_options_diagnostic[1]=0;}}
