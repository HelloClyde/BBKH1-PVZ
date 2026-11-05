// Original renderer versus the production offline-frame reader and replay.
#include "LawnApp.h"
#include "Lawn/Widget/GameSelector.h"
#include "PvzpLib/Reanimator.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/Graphics.h"
#include "graphics/ScreenLayout.h"
#include "selector/SelectorOffline.h"
#include "Common.h"
#include <vector>
#include <cstring>
#include <stdexcept>
#include <filesystem>
#include <fstream>
using namespace Sexy;
extern "C" void pvzh1_adventure_probe(){
    auto* screen=gLawnApp->mGLInterface->GetScreenImage();auto* physical=H1GetPhysicalScreen(screen);if(!physical)throw std::runtime_error("No physical screen");
    gLawnApp->mGameSelector=std::make_unique<GameSelector>(gLawnApp);auto* selector=gLawnApp->mGameSelector.get();
    selector->mSelectorState=SELECTOR_IDLE;selector->mStartingGame=true;selector->mLoading=false;selector->mSlideCounter=0;
    auto* hand=gLawnApp->AddReanimation(-70,10,0,REANIM_ZOMBIE_HAND);hand->mLoopType=REANIM_PLAY_ONCE_AND_HOLD;
    for(int i=0;i<hand->mDefinition->mTracks.count;i++)if(!strncasecmp(hand->mDefinition->mTracks.tracks[i].mName,"rock",4))hand->mTrackInstances[i].mIgnoreClipRect=true;
    auto* target=physical->Target();const unsigned count=H1ScreenWidth*H1ScreenHeight;unsigned checks=0;
    auto background=[&](unsigned seed){auto* p=target->GetBits();for(unsigned i=0;i<count;i++)p[i]=0xff000000u|((i*0x164b35u+seed*0x213141u)&0xffffffu);};
    for(unsigned update=0;update<=450;update++){
        if(update)hand->Update();if(update%4&&update!=450)continue;selector->mStartingGameCounter=update;
        for(unsigned seed:{1u,17u,231u}){
            background(seed);{Graphics g(screen);g.SetLinearBlend(true);g.SetClipRect(0,0,H1LogicalWidth,BOARD_HEIGHT-40);hand->Draw(&g);}
            std::vector<uint32_t> expected(target->GetBits(),target->GetBits()+count);background(seed);
            {Graphics g(screen);g.SetLinearBlend(true);g.SetClipRect(0,0,H1LogicalWidth,BOARD_HEIGHT-40);if(!H1SelectorOfflineDrawHand(selector,&g,hand))throw std::runtime_error("Offline hand fallback");}
            if(memcmp(expected.data(),target->GetBits(),count*4))throw std::runtime_error("Offline hand pixels differ");++checks;
        }
    }
    std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"adventure-verification.json");
    std::ifstream file(GetResourcePath("h1menu/adventure.h1mb"),std::ios::binary);unsigned char codec[4]{};file.seekg(52);file.read((char*)codec,4);if(!file)throw std::runtime_error("Missing adventure codec header");
    out<<"{\"passed\":true,\"poses\":114,\"whole_frame_pixel_cases\":"<<checks<<",\"original_450_updates_preserved\":true,\"runtime_codec\":\""<<(codec[0]?"zlib":"raw")<<"\"}\n";
}
