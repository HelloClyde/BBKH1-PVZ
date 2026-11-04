#include "Lawn/Widget/GameButton.h"
/* Host hardware services for the complete original game. Mouse/key scripts are
 * delivered through the same platform Input.cpp as H1; no gameplay state is
 * manufactured here. The native display and PCM devices become file sinks. */
#include "h1_host.h"
#include "LawnApp.h"
#include "PvzpLib/PvzpList.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/SoftwareDisplay.h"
#include "widget/Dialog.h"
#include "Lawn/Board.h"
#include "Lawn/Widget/ContinueDialog.h"
#include "Lawn/Widget/NewOptionsDialog.h"
#include "Lawn/Widget/GameButton.h"
#include "widget/DialogButton.h"
#include "Lawn/CutScene.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/Projectile.h"
#include "Lawn/Coin.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/ReanimAtlas.h"
#include "PvzpLib/EffectSystem.h"
#include "h1_input.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <deque>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <malloc.h>
#endif

volatile uint32_t pvzp_diagnostic[32];
extern "C" void pvzh1_reanim_atlas_end_frame(void*);
extern "C" void pvzh1_reanim_atlas_cache_stats(uint64_t*,size_t*);
extern "C" uint64_t pvzh1_image_pixel_bytes_loaded();
extern "C" volatile uint32_t pvzh1_selector_definition_diagnostic[10];
extern "C" volatile uint32_t pvzh1_alpha_palette_diagnostic[8];
extern "C" volatile uint32_t pvzh1_rgba_font_palette_diagnostic[8];
extern "C" volatile uint32_t pvzh1_rgba_font_page_diagnostic[8];
extern "C" volatile uint32_t pvzh1_rgba_raster_page_diagnostic[8];
extern "C" volatile uint32_t pvzh1_storage_texture_diagnostic[8];
extern "C" volatile uint32_t pvzh1_physical_screen_diagnostic[8];
extern "C" void pvzh1_host_probe_fonts();
extern "C" void pvzh1_host_probe_widgets();
extern "C" void pvzh1_host_probe_triangles();
extern "C" void pvzh1_host_probe_selector();
extern "C" void pvzh1_host_probe_atlas_entry();
extern "C" void pvzh1_host_probe_allocators();
extern "C" void pvzh1_export_definitions();
extern "C" void pvzh1_export_fonts();
namespace {
using Clock=std::chrono::steady_clock;
Clock::time_point start=Clock::now();
std::filesystem::path output;
std::ofstream logFile,states;
bool opened=false,shutdownRequested=false;
unsigned frame=0,maxFrames=300,captureEvery=60;
int lastScene=-1,pointerX=400,pointerY=300;
uint64_t pcmSamples=0,pcmNonzero=0;
FILE *pcmFile=nullptr;
uint64_t heapPeak=0,heapCurrent=0,processPeak=0;
struct Event {int kind,key,x,y;};
struct Action {int scene=-1;unsigned after=0;std::string command;int a=0,b=0;bool done=false;};
std::vector<Action> actions;
std::deque<Event> events;
unsigned sceneStart[16]={0};bool seenScene[16]={false};
std::string queuedCapture;
bool autoPlantSeen=false,autoShotSeen=false,autoHitSeen=false;
unsigned atlasActiveChecks=0,atlasHandleChecks=0,atlasRebuildChecks=0;

unsigned Number(const char *name,unsigned fallback){const char *p=getenv(name);return p?(unsigned)strtoul(p,nullptr,10):fallback;}
void MeasureMemory(){
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};counters.cb=sizeof(counters);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),sizeof(counters)))
        processPeak=std::max(processPeak,(uint64_t)counters.PeakPagefileUsage);
    _HEAPINFO info{};uint64_t used=0;int status;
    while((status=_heapwalk(&info))==_HEAPOK)if(info._useflag==_USEDENTRY)used+=info._size;
    if(status==_HEAPEND){heapCurrent=used;heapPeak=std::max(heapPeak,used);}
#endif
}
void SaveFrame(const uint32_t *pixels,int width,int height,const std::string& name){
    std::filesystem::path path=output/(name+".ppm");FILE *f=fopen(path.string().c_str(),"wb");
    if(!f)return;fprintf(f,"P6\n%d %d\n255\n",width,height);
    std::vector<unsigned char> row((size_t)width*3);
    for(int y=0;y<height;++y){for(int x=0;x<width;++x){uint32_t p=pixels[y*width+x];row[x*3]=(unsigned char)(p>>16);row[x*3+1]=(unsigned char)(p>>8);row[x*3+2]=(unsigned char)p;}fwrite(row.data(),1,row.size(),f);}
    fclose(f);
}
void State(const char *event){
    if(std::strcmp(event,"first-plant")==0)pvzh1_host_probe_allocators();
    auto cache=Sexy::H1ImageCacheGetStats();
    uint64_t freedAtlases=0;size_t freedAtlasBytes=0;
    pvzh1_reanim_atlas_cache_stats(&freedAtlases,&freedAtlasBytes);
    const Board *liveBoard=gLawnApp?gLawnApp->mBoard:nullptr;
    const unsigned scene=gLawnApp?(unsigned)gLawnApp->mGameScene:pvzp_diagnostic[7];
    const unsigned updates=gLawnApp?gLawnApp->mUpdateCount:pvzp_diagnostic[8];
    const unsigned level=liveBoard?liveBoard->mLevel:pvzp_diagnostic[9];
    const unsigned sun=liveBoard?liveBoard->mSunMoney:pvzp_diagnostic[10];
    const unsigned wave=liveBoard?liveBoard->mCurrentWave:pvzp_diagnostic[11];
    const unsigned counter=liveBoard?liveBoard->mMainCounter:pvzp_diagnostic[12];
    const unsigned profile=gLawnApp&&gLawnApp->mPlayerInfo?gLawnApp->mPlayerInfo->mLevel:pvzp_diagnostic[13];
    states<<"{\"event\":\""<<event<<"\",\"frame\":"<<frame<<",\"wall_ms\":"
        <<std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count()
        <<",\"scene\":"<<scene<<",\"updates\":"<<updates
        <<",\"level\":"<<level<<",\"sun\":"<<sun
        <<",\"wave\":"<<wave<<",\"board_counter\":"<<counter
        <<",\"profile_level\":"<<profile<<",\"heap_sampled_peak\":"<<heapPeak
        <<",\"heap_sampled_current\":"<<heapCurrent
        <<",\"process_commit_peak\":"<<processPeak<<",\"source_cache_bytes\":"<<cache.residentBytes
        <<",\"source_cache_peak\":"<<cache.peakBytes<<",\"image_load_failures\":"<<cache.loadFailures
        <<",\"source_cache_budget\":"<<cache.budgetBytes<<",\"image_loads\":"<<cache.loads<<",\"image_evictions\":"<<cache.evictions
        <<",\"image_pixel_payload_bytes_loaded\":"<<pvzh1_image_pixel_bytes_loaded()
        <<",\"pcm_samples\":"<<pcmSamples<<",\"pcm_nonzero\":"<<pcmNonzero;
    states<<",\"atlas_releases\":"<<freedAtlases<<",\"atlas_pixel_bytes_released\":"<<freedAtlasBytes
          <<",\"atlas_active_checks\":"<<atlasActiveChecks<<",\"atlas_handle_checks\":"<<atlasHandleChecks
          <<",\"atlas_rebuild_checks\":"<<atlasRebuildChecks;
    states<<",\"global_allocator_pools\":[";
    for(int i=0;i<gNumGlobalAllocators;++i){
        const auto& pool=gGlobalAllocators[i];size_t blocks=0;for(void *p=pool.mBlockList;p;p=*(void**)p)++blocks;
        if(i)states<<',';states<<"{\"item_size\":"<<pool.mItemSize<<",\"grow\":"<<pool.mGrowCount<<",\"live\":"<<pool.mTotalItems
            <<",\"capacity\":"<<blocks*pool.mGrowCount<<",\"bytes\":"<<blocks*((size_t)pool.mItemSize*pool.mGrowCount+sizeof(void*))<<'}';
    }
    states<<']';
    states<<",\"selector_definition_cache\":[";
    for(int i=0;i<10;++i){if(i)states<<',';states<<pvzh1_selector_definition_diagnostic[i];}
    states<<"],\"alpha_palette\":[";
    for(int i=0;i<8;++i){if(i)states<<',';states<<pvzh1_alpha_palette_diagnostic[i];}
    states<<"],\"rgba_font_palette\":[";
    for(int i=0;i<8;++i){if(i)states<<',';states<<pvzh1_rgba_font_palette_diagnostic[i];}
    states<<"],\"rgba_font_pages\":[";
    for(int i=0;i<8;++i){if(i)states<<',';states<<pvzh1_rgba_font_page_diagnostic[i];}
    states<<"],\"rgba_raster_pages\":[";
    for(int i=0;i<8;++i){if(i)states<<',';states<<pvzh1_rgba_raster_page_diagnostic[i];}
    states<<"],\"storage_textures\":[";
    for(int i=0;i<8;++i){if(i)states<<',';states<<pvzh1_storage_texture_diagnostic[i];}
    states<<"],\"physical_screen\":[";
    for(int i=0;i<8;++i){if(i)states<<',';states<<pvzh1_physical_screen_diagnostic[i];}
    const unsigned selectorIndex=(unsigned)ReanimationType::REANIM_SELECTOR_SCREEN;
    states<<"],\"selector_definition_loaded\":"<<((gReanimatorDefArray&&gReanimatorDefCount>selectorIndex&&gReanimatorDefArray[selectorIndex].mTracks.tracks)?"true":"false");
    if(gLawnApp){
        states<<",\"game_mode\":"<<(int)gLawnApp->mGameMode;
        struct ImageUsage {size_t bytes;Sexy::MemoryImage *image;};
        std::vector<ImageUsage> imageUsage;
        struct RGBUsage {Sexy::MemoryImage *image;Sexy::H1ImageCacheRGBStats stats;};
        std::vector<RGBUsage> rgbUsage;
        struct FontPageUsage {Sexy::MemoryImage *image;Sexy::H1ImageCacheFontPageStats stats;};
        std::vector<FontPageUsage> fontPageUsage;
        struct StorageUsage {Sexy::MemoryImage *image;Sexy::H1ImageCacheStorageStats stats;};
        std::vector<StorageUsage> storageUsage;
        std::map<Sexy::MemoryImage*,const char*> atlasOwners;
        for(unsigned i=0;gReanimatorDefArray&&i<gReanimatorDefCount;++i){
            auto *atlas=gReanimatorDefArray[i].mReanimAtlas;
            if(atlas&&atlas->mMemoryImage)atlasOwners.emplace(atlas->mMemoryImage.get(),gReanimationParamArray[i].mReanimFileName);
        }
        size_t imageBytes=0,dynamicBytes=0,paletteBytes=0,sourceBytes=0,atlasBytes=0;
        for(Sexy::MemoryImage *image:gLawnApp->mMemoryImageSet){
            const size_t area=(size_t)std::max(0,image->mWidth)*std::max(0,image->mHeight);
            const size_t palettes=(image->mColorIndices?area:0)+(image->mColorTable?1024:0);
            Sexy::H1ImageCacheRGBStats rgb{};Sexy::H1ImageCacheGetRGBStats(image,&rgb);
            if(rgb.eligible||rgb.loads||rgb.argbLoads)rgbUsage.push_back({image,rgb});
            Sexy::H1ImageCacheFontPageStats fontPages{};Sexy::H1ImageCacheGetFontPageStats(image,&fontPages);
            if(fontPages.loads||fontPages.argbLoads)fontPageUsage.push_back({image,fontPages});
            Sexy::H1ImageCacheStorageStats storage{};
            if(Sexy::H1ImageCacheGetStorageStats(image,&storage)&&(storage.loads||storage.argbLoads))storageUsage.push_back({image,storage});
            const size_t bytes=(image->mBits?area*4+4:0)+palettes+(image->mNativeAlphaData?(image->mColorTable?256:area)*4:0)
                +(image->mRLAlphaData?area:0)+(image->mRLAdditiveData?area:0);
            const size_t totalBytes=bytes+rgb.bytes+rgb.tableBytes+fontPages.bytes+fontPages.tableBytes+storage.bytes;
            imageBytes+=totalBytes;paletteBytes+=palettes;
            if(atlasOwners.count(image))atlasBytes+=totalBytes;
            if(Sexy::H1ImageCacheContains(image))sourceBytes+=totalBytes;else dynamicBytes+=totalBytes;
            if(totalBytes)imageUsage.push_back({totalBytes,image});
        }
        std::sort(imageUsage.begin(),imageUsage.end(),[](const ImageUsage&a,const ImageUsage&b){return a.bytes>b.bytes;});
        states<<",\"image_buffer_bytes\":"<<imageBytes<<",\"dynamic_image_bytes\":"<<dynamicBytes
              <<",\"source_image_bytes\":"<<sourceBytes<<",\"palette_bytes\":"<<paletteBytes
              <<",\"reanim_atlas_bytes\":"<<atlasBytes
              <<",\"image_objects\":"<<gLawnApp->mMemoryImageSet.size()<<",\"largest_images\":[";
        for(size_t i=0;i<std::min<size_t>(12,imageUsage.size());++i){
            auto& usage=imageUsage[i];if(i)states<<',';
            states<<"{\"bytes\":"<<usage.bytes<<",\"w\":"<<usage.image->mWidth<<",\"h\":"<<usage.image->mHeight
                  <<",\"source\":"<<(Sexy::H1ImageCacheContains(usage.image)?"true":"false")
                  <<",\"path\":"<<std::quoted(usage.image->mFilePath);
            auto atlas=atlasOwners.find(usage.image);
            if(atlas!=atlasOwners.end())states<<",\"atlas\":"<<std::quoted(atlas->second);
            states<<'}';
        }
        states<<']';
        states<<",\"reduced_sources\":[";
        for(size_t i=0;i<storageUsage.size();++i){
            const auto& entry=storageUsage[i];const auto& s=entry.stats;if(i)states<<',';
            states<<"{\"path\":"<<std::quoted(entry.image->mFilePath)<<",\"bytes\":"<<s.bytes
                <<",\"logical_width\":"<<s.logicalWidth<<",\"logical_height\":"<<s.logicalHeight
                <<",\"storage_width\":"<<s.storageWidth<<",\"storage_height\":"<<s.storageHeight
                <<",\"loads\":"<<s.loads<<",\"opaque_blits\":"<<s.blits<<",\"logical_argb_loads\":"<<s.argbLoads<<'}';
        }
        states<<']';
        states<<",\"paged_rgb_sources\":[";
        for(size_t i=0;i<rgbUsage.size();++i){
            const auto& entry=rgbUsage[i];const auto& rgb=entry.stats;if(i)states<<',';
            states<<"{\"path\":"<<std::quoted(entry.image->mFilePath)<<",\"pages\":"<<rgb.pages<<",\"bytes\":"<<rgb.bytes
                <<",\"table_bytes\":"<<rgb.tableBytes<<",\"max_page\":"<<rgb.maxPageBytes
                <<",\"loads\":"<<rgb.loads<<",\"blits\":"<<rgb.blits<<",\"argb_loads\":"<<rgb.argbLoads
                <<",\"argb_resident\":"<<(entry.image->mBits?"true":"false")
                <<",\"rl_resident\":"<<((entry.image->mRLAlphaData||entry.image->mRLAdditiveData)?"true":"false")<<'}';
        }
        states<<']';
        states<<",\"paged_font_sources\":[";
        for(size_t i=0;i<fontPageUsage.size();++i){
            const auto& entry=fontPageUsage[i];const auto& font=entry.stats;if(i)states<<',';
            states<<"{\"path\":"<<std::quoted(entry.image->mFilePath)<<",\"pages\":"<<font.pages<<",\"bytes\":"<<font.bytes
                <<",\"table_bytes\":"<<font.tableBytes<<",\"max_page\":"<<font.maxPageBytes
                <<",\"loads\":"<<font.loads<<",\"blits\":"<<font.blits<<",\"argb_loads\":"<<font.argbLoads
                <<",\"argb_resident\":"<<(entry.image->mBits?"true":"false")
                <<",\"rl_resident\":"<<((entry.image->mRLAlphaData||entry.image->mRLAdditiveData)?"true":"false")<<'}';
        }
        states<<']';
        if(gLawnApp->mBoard){
            Board *board=gLawnApp->mBoard;
            states<<",\"board_updates\":"<<board->mBoardUpdateCounter;
            if(board->mCutScene)states<<",\"cutscene_time\":"<<board->mCutScene->mCutsceneTime;
            states<<",\"total_waves\":"<<board->mNumWaves<<",\"paused\":"<<(board->mPaused?"true":"false")<<",\"plants\":[";
            bool comma=false;
            for(Plant *plant:board->mPlants)if(!plant->mDead){
                if(comma)states<<',';comma=true;
                states<<"{\"type\":"<<(int)plant->mSeedType<<",\"row\":"<<plant->mRow<<",\"col\":"<<plant->mPlantCol<<",\"hp\":"<<plant->mPlantHealth<<'}';
            }
            states<<"],\"zombies\":[";comma=false;
            for(Zombie *zombie:board->mZombies)if(!zombie->mDead){
                if(comma)states<<',';comma=true;
                states<<"{\"type\":"<<(int)zombie->mZombieType<<",\"row\":"<<zombie->mRow<<",\"x\":"<<zombie->mPosX
                      <<",\"hp\":"<<zombie->mBodyHealth<<",\"max_hp\":"<<zombie->mBodyMaxHealth<<'}';
            }
            unsigned shots=0,suns=0;
            for(Projectile *shot:board->mProjectiles)if(!shot->mDead)++shots;
            for(Coin *coin:board->mCoins)if(!coin->mDead&&coin->IsSun())++suns;
            states<<"],\"projectiles\":"<<shots<<",\"sun_pickups\":"<<suns;
        }
    }
    states<<",\"dialogs\":[";
    bool first=true;
    if(gLawnApp)for(auto& entry:gLawnApp->mDialogMap){
        if(!first)states<<',';first=false;
        states<<"{\"id\":"<<entry.first<<",\"x\":"<<entry.second->mX<<",\"y\":"<<entry.second->mY
              <<",\"w\":"<<entry.second->mWidth<<",\"h\":"<<entry.second->mHeight;
        if(auto *dialog=dynamic_cast<ContinueDialog*>(entry.second))states<<",\"restart_label\":"<<std::quoted(dialog->mNewGameButton->mLabel);
        if(auto *dialog=dynamic_cast<NewOptionsDialog*>(entry.second))states<<",\"restart_label\":"<<std::quoted(dialog->mRestartButton->mLabel);
        states<<'}';
    }
    states<<"]}\n";
    states.flush();
}
uint64_t HandleHash(ReanimatorDefinition& definition,ReanimAtlas *restore){
    uint64_t hash=1469598103934665603ull;
    for(int t=0;t<definition.mTracks.count;++t){
        auto& track=definition.mTracks.tracks[t];
        for(int f=0;f<track.mTransforms.count;++f){
            Sexy::Image *image=track.mTransforms.mTransforms[f].mImage;
            auto *encoded=restore?restore->GetEncodedReanimAtlas(image):nullptr;
            uintptr_t value=(uintptr_t)(encoded?encoded->mOriginalImage:image);
            for(unsigned b=0;b<sizeof(value);++b){hash^=(value>>(b*8))&255;hash*=1099511628211ull;}
        }
    }
    return hash;
}
uint64_t PixelHash(Sexy::MemoryImage *image){
    uint64_t hash=1469598103934665603ull;
    const uint32_t *pixels=image->GetBits();
    for(size_t i=0;i<(size_t)image->mWidth*image->mHeight;++i){hash^=pixels[i];hash*=1099511628211ull;}
    return hash;
}
void AtlasFrameComplete(void*){
    if(!gLawnApp||!gLawnApp->mEffectSystem||!gLawnApp->mEffectSystem->mReanimationHolder)return;
    struct Audit {unsigned index;ReanimAtlas *atlas;uint64_t handles,pixels;bool active,rebuild;};
    std::vector<Audit> checks;
    for(unsigned i=0;gReanimatorDefArray&&i<gReanimatorDefCount;++i){
        auto& definition=gReanimatorDefArray[i];auto *atlas=definition.mReanimAtlas;if(!atlas)continue;
        bool active=false;
        for(Reanimation *animation:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)
            if(animation->mDefinition==&definition)active=true;
        const bool rebuild=!active&&!atlasRebuildChecks&&atlas->mMemoryImage&&
            (size_t)atlas->mMemoryImage->mWidth*atlas->mMemoryImage->mHeight>=4096;
        checks.push_back({i,atlas,HandleHash(definition,atlas),rebuild?PixelHash(atlas->mMemoryImage.get()):0,active,rebuild});
    }
    pvzh1_reanim_atlas_end_frame(nullptr);
    for(auto& check:checks){
        auto& definition=gReanimatorDefArray[check.index];
        if(check.active){
            if(definition.mReanimAtlas!=check.atlas)abort();
            ++atlasActiveChecks;
        }else{
            if(definition.mReanimAtlas||HandleHash(definition,nullptr)!=check.handles)abort();
            ++atlasHandleChecks;
            if(check.rebuild){
                ReanimationCreateAtlas(&definition,(ReanimationType)check.index);
                if(!definition.mReanimAtlas||PixelHash(definition.mReanimAtlas->mMemoryImage.get())!=check.pixels)abort();
                ++atlasRebuildChecks;pvzp_log_line("HOST ATLAS AUDIT: original atlas rebuilt with identical pixels");
            }
        }
    }
    if(!checks.empty()&&std::any_of(checks.begin(),checks.end(),[](const Audit&a){return !a.active;})){
        MeasureMemory();State("atlas-cache-trim");
    }
}
void LoadScript(){
    const char *name=getenv("PVZ_HOST_SCRIPT");if(!name)return;
    std::ifstream file(name);std::string line;
    while(std::getline(file,line)){
        if(line.empty()||line[0]=='#')continue;
        std::istringstream in(line);std::string type;Action action;in>>type;
        if(type=="frame")in>>action.after;
        else if(type=="scene")in>>action.scene>>action.after;
        else{pvzp_log_line(("Invalid host script: "+line).c_str());continue;}
        in>>action.command>>action.a>>action.b;
        actions.push_back(action);
    }
}
void Schedule(){
    for(Action& action:actions){
        if(action.done)continue;
        bool due=action.scene<0?frame>=action.after:
            action.scene<16&&seenScene[action.scene]&&(int)pvzp_diagnostic[7]==action.scene&&frame>=sceneStart[action.scene]+action.after;
        if(!due)continue;action.done=true;
        std::ostringstream entry;entry<<"HOST INPUT frame="<<frame<<" scene="<<pvzp_diagnostic[7]<<" "<<action.command<<" "<<action.a<<" "<<action.b;
        pvzp_log_line(entry.str().c_str());
        if(action.command=="click"){
            events.push_back({3,0,action.a,action.b});events.push_back({4,0,action.a,action.b});
        }else if(action.command=="down")events.push_back({3,0,action.a,action.b});
        else if(action.command=="up")events.push_back({4,0,action.a,action.b});
        else if(action.command=="move")events.push_back({5,0,action.a,action.b});
        else if(action.command=="key"){events.push_back({1,action.a,0,0});events.push_back({2,action.a,0,0});}
        else if(action.command=="capture")queuedCapture="script-"+std::to_string(frame);
        else if(action.command=="quit")shutdownRequested=true;
    }
}
void Click(int x,int y){events.push_back({3,0,x,y});events.push_back({4,0,x,y});}
void Key(int key){events.push_back({1,key,0,0});events.push_back({2,key,0,0});}
/* Optional test player: inspect the real board and send normal platform input.
 * No resource, cooldown, entity, timer, profile or game-state field is written. */
void AutoPilot(){
    static unsigned lastAction=0,pauseFrame=0,hitFrame=0;static int pausePhase=0;
    if(!Number("PVZ_HOST_AUTOPLAY",0)||!gLawnApp)return;
    if(gLawnApp->GetDialog(Dialogs::DIALOG_CONTINUE)){
        if(events.empty()&&frame>=lastAction+30){Key(H1_KEY_ENTER);lastAction=frame;State("resume-save-input");pvzp_log_line("HOST AUTO: accept original saved-game continue dialog");}
        return;
    }
    if(!gLawnApp->mBoard)return;
    Board *board=gLawnApp->mBoard;
    if(Number("PVZ_HOST_STOP_AFTER_WIN",0)&&autoPlantSeen&&gLawnApp->mGameScene==GameScenes::SCENE_PLAYING&&board->mLevel==2){
        static unsigned reached=0;if(!reached){reached=frame;State("level2-after-first-win");}
        if(frame>=reached+90)shutdownRequested=true;
        return;
    }
    if(gLawnApp->mGameScene!=GameScenes::SCENE_PLAYING||gLawnApp->mGameMode!=GameMode::GAMEMODE_ADVENTURE||board->mLevel!=1)return;
    unsigned plants=0,shots=0;bool hit=false;
    for(Plant *plant:board->mPlants)if(!plant->mDead)++plants;
    for(Projectile *shot:board->mProjectiles)if(!shot->mDead)++shots;
    for(Zombie *zombie:board->mZombies)if(!zombie->mDead&&zombie->mBodyHealth<zombie->mBodyMaxHealth)hit=true;
    if(plants&&!autoPlantSeen){autoPlantSeen=true;queuedCapture="first-real-plant";State("first-plant");}
    if(shots&&!autoShotSeen){autoShotSeen=true;queuedCapture="first-real-projectile";State("first-projectile");}
    if(hit&&!autoHitSeen){autoHitSeen=true;hitFrame=frame;queuedCapture="first-real-hit";State("first-hit");}
    if(!events.empty()||frame<lastAction+4)return;
    if(pausePhase==1){
        if(frame>=pauseFrame+12){State("pause-held");Key(H1_KEY_ENTER);lastAction=frame;pausePhase=2;pvzp_log_line("HOST AUTO: resume via original options Enter");}
        return;
    }
    if(!gLawnApp->mDialogMap.empty())return;
    if(pausePhase==2){pausePhase=3;State("resumed");}
    if(autoHitSeen&&pausePhase==0&&frame>=hitFrame+2){
        if(Number("PVZ_HOST_AUTOPLAY_TOUCH_PAUSE",0))Click(board->mX+board->mMenuButton->mX+board->mMenuButton->mWidth/2,board->mY+std::max(0,board->mMenuButton->mY)+10);
        else Key(H1_KEY_ESCAPE);
        lastAction=pauseFrame=frame;pausePhase=1;queuedCapture="original-pause-menu";
        pvzp_log_line("HOST AUTO: open original options after observed real zombie damage");return;
    }
    for(Coin *coin:board->mCoins)if(!coin->mDead&&!coin->mIsBeingCollected&&(coin->IsSun()||coin->IsLevelAward())){
        Click(board->mX+(int)coin->mPosX+coin->mWidth/2,board->mY+(int)coin->mPosY+coin->mHeight/2);
        lastAction=frame;
        if(coin->IsLevelAward()){State("level-award-input");queuedCapture="real-level-award";pvzp_log_line("HOST AUTO: click original level reward");}
        else pvzp_log_line("HOST AUTO: click existing sun pickup");
        return;
    }
    if(!board->mSeedBank)return;
    SeedPacket *packet=nullptr;
    for(int i=0;i<board->mSeedBank->mNumPackets;++i){
        SeedPacket& candidate=board->mSeedBank->mSeedPackets[i];
        if(candidate.mPacketType==SeedType::SEED_PEASHOOTER&&candidate.CanPickUp()){packet=&candidate;break;}
    }
    if(!packet)return;
    for(int col=0;col<6;++col){
        bool occupied=false;
        for(Plant *plant:board->mPlants)if(!plant->mDead&&plant->mRow==2&&plant->mPlantCol==col)occupied=true;
        if(occupied)continue;
        Click(board->mX+board->mSeedBank->mX+packet->mX+packet->mWidth/2,board->mY+board->mSeedBank->mY+packet->mY+packet->mHeight/2);
        Click(board->mX+board->GridToPixelX(col,2)+35,board->mY+board->GridToPixelY(col,2)+35);
        lastAction=frame;
        std::ostringstream message;message<<"HOST AUTO: click pea packet and real grid col="<<col<<" row=2";
        pvzp_log_line(message.str().c_str());return;
    }
}
void WaveHeader(){
    if(!pcmFile)return;
    uint32_t bytes=(uint32_t)std::min<uint64_t>(pcmSamples*2,0xfffffff0u),riff=bytes+36;
    uint16_t format=1,channels=1,align=2,bits=16;uint32_t fmt=16,rate=16000,byteRate=32000;
    fseek(pcmFile,0,SEEK_SET);fwrite("RIFF",1,4,pcmFile);fwrite(&riff,4,1,pcmFile);fwrite("WAVEfmt ",1,8,pcmFile);
    fwrite(&fmt,4,1,pcmFile);fwrite(&format,2,1,pcmFile);fwrite(&channels,2,1,pcmFile);fwrite(&rate,4,1,pcmFile);
    fwrite(&byteRate,4,1,pcmFile);fwrite(&align,2,1,pcmFile);fwrite(&bits,2,1,pcmFile);fwrite("data",1,4,pcmFile);fwrite(&bytes,4,1,pcmFile);
    fseek(pcmFile,0,SEEK_END);
}
#ifdef _WIN32
LONG WINAPI Crash(EXCEPTION_POINTERS *p){
    MeasureMemory();State("crash");
    std::ofstream error(output/"crash.txt");
    error<<"exception=0x"<<std::hex<<p->ExceptionRecord->ExceptionCode<<" address="<<p->ExceptionRecord->ExceptionAddress
         <<" image_base="<<GetModuleHandleW(nullptr)<<" frame="<<std::dec<<frame<<"\n";error.flush();
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
}

extern "C" int pvzp_window_open(void){
    if(opened)return 1;opened=true;
    const char *directory=getenv("PVZ_HOST_OUTPUT");output=directory?directory:"build/host/run";
    std::filesystem::create_directories(output);
    logFile.open(output/"pvz.log",std::ios::out|std::ios::app);states.open(output/"states.jsonl");
    maxFrames=Number("PVZ_HOST_MAX_FRAMES",300);captureEvery=Number("PVZ_HOST_CAPTURE_EVERY",60);
    if(Number("PVZ_HOST_TRIM_ATLASES",0))pvzh1_graphics_set_frame_complete(AtlasFrameComplete,nullptr);
    pvzp_diagnostic[0]=0x50565031u;pvzp_diagnostic[1]=1;LoadScript();
#ifdef _WIN32
    SetUnhandledExceptionFilter(Crash);
#endif
    pvzp_log_line("HOST: complete pinned PvZ-Portable runtime; headless hardware I/O");return 1;
}
extern "C" void pvzp_window_close(void){
    if(!opened)return;pvzp_audio_close();MeasureMemory();State("closed");
    pvzp_diagnostic[1]=9;opened=false;states.close();logFile.close();
}
extern "C" uint32_t pvzp_ticks_ms(void){
    double speed=1.0;const char *value=getenv("PVZ_HOST_SPEED");if(value)speed=std::max(0.1,atof(value));
    return (uint32_t)(std::chrono::duration<double,std::milli>(Clock::now()-start).count()*speed);
}
extern "C" void pvzp_log_line(const char *line){
    if(!line)return;
    if(logFile.is_open()){logFile<<line<<'\n';logFile.flush();}
    std::fprintf(stderr,"%s\n",line);++pvzp_diagnostic[4];
    if(states.is_open()&&gLawnApp){
        const char *checkpoint=nullptr;
        if(strncmp(line,"loading 'resources'",19)==0)checkpoint="loaded-images-fonts";
        else if(strncmp(line,"loading 'stuff'",15)==0)checkpoint="loaded-system-objects";
        else if(strncmp(line,"Resource Loading Time:",22)==0)checkpoint="loading-complete";
        if(checkpoint){
            MeasureMemory();State(checkpoint);
            if(std::strcmp(checkpoint,"loaded-images-fonts")==0){pvzh1_host_probe_fonts();pvzh1_host_probe_widgets();pvzh1_host_probe_triangles();}
            if(std::strcmp(checkpoint,"loading-complete")==0){pvzh1_host_probe_selector();pvzh1_host_probe_atlas_entry();pvzh1_export_definitions();}
            if(std::strcmp(checkpoint,"loading-complete")==0)pvzh1_export_fonts();
        }
    }
}
extern "C" void pvzp_pointer_set(int x,int y){pointerX=x;pointerY=y;}
extern "C" void pvzp_present_argb(const uint32_t *pixels,int width,int height){
    if(!pixels||width<=0||height<=0){pvzp_diagnostic[3]=2;return;}
    ++frame;pvzp_diagnostic[2]=frame;
    int scene=(int)pvzp_diagnostic[7];bool changed=scene!=lastScene;
    if(changed){if(scene>=0&&scene<16&&!seenScene[scene]){seenScene[scene]=true;sceneStart[scene]=frame;}lastScene=scene;}
    if(changed||frame==1||(captureEvery&&frame%captureEvery==0)||!queuedCapture.empty()){
        MeasureMemory();State(changed?"scene":"frame");
        std::string name=queuedCapture.empty()?"frame-"+std::to_string(frame)+"-scene-"+std::to_string(scene):queuedCapture;
        SaveFrame(pixels,width,height,name);queuedCapture.clear();
    }else if(frame<=32){
        MeasureMemory();State("loading-frame");
    }
    Schedule();pvzp_audio_pump();
    if(maxFrames&&frame>=maxFrames)shutdownRequested=true;
}
extern "C" int pvzp_input_poll(int *kind,int *key,int *x,int *y){
    if(shutdownRequested&&gLawnApp&&!gLawnApp->mShutdown){pvzp_log_line("HOST: window-close requested");gLawnApp->Shutdown();}
    Schedule();AutoPilot();if(events.empty())return 0;
    Event event=events.front();events.pop_front();*kind=event.kind;*key=event.key;*x=event.x;*y=event.y;
    if(event.kind>=3)++pvzp_diagnostic[6];else ++pvzp_diagnostic[5];return 1;
}
extern "C" int pvzp_pcm_open(){
    if(pcmFile)return 1;
    if(output.empty()){const char *directory=getenv("PVZ_HOST_OUTPUT");output=directory?directory:"build/host/run";std::filesystem::create_directories(output);}
    pcmFile=fopen((output/"audio.wav").string().c_str(),"wb+");if(!pcmFile)return 0;
    unsigned char blank[44]={0};fwrite(blank,1,44,pcmFile);pvzp_diagnostic[16]=1;return 1;
}
extern "C" unsigned pvzp_pcm_writable(){
    uint64_t wanted=(uint64_t)pvzp_ticks_ms()*16+2048;
    if(wanted<=pcmSamples)return 0;
    return (unsigned)std::min<uint64_t>(wanted-pcmSamples,2048);
}
extern "C" void pvzp_pcm_write(const int16_t *samples,unsigned count){
    if(!pcmFile)return;
    fwrite(samples,2,count,pcmFile);pcmSamples+=count;
    for(unsigned i=0;i<count;++i)if(samples[i]){++pcmNonzero;++pvzp_diagnostic[17];}
}
extern "C" void pvzp_audio_close(){
    if(pcmFile){WaveHeader();fclose(pcmFile);pcmFile=nullptr;}pvzp_diagnostic[16]=0;
}
extern "C" int pvz_portable_main(void);
int main(){int result=pvz_portable_main();pvzp_window_close();return result?result:pvzp_diagnostic[3]?10+(int)pvzp_diagnostic[3]:0;}
