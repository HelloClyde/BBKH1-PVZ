/* Warm current-board dependencies before play, independent of profile unlocks.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/SeedPacket.h"
#include "Lawn/CutScene.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpParticle.h"
#include "Startup.h"
#include "OptionsFeedback.h"
#include "BoardLookup.h"
#include "actors/ActorPose.h"
#include "actors/CachedFrames.h"
#include "graphics/EarlyLawn.h"
#include "graphics/RewardEffects.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/MemoryImage.h"
#include "video/IntroMovie.h"
#include <set>
#include <stdexcept>
extern "C" void pvzh1_sound_warmup();
extern "C" void pvzh1_sound_warm_intro();
extern "C" void pvzh1_music_warm_game();
extern "C" void pvzh1_music_warm_intro();
extern "C" {volatile uint32_t pvzh1_level_preload_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_board_replay_retirement_diagnostic[3];}
extern "C" volatile uint32_t pvzh1_actor_pose_diagnostic[16];
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" void pvzh1_selector_definition_trim(void*);

void H1RetireBoardReplay(){
    // Only the confirmed leave/quit paths call this, after drawing returns and
    // before the original serializer allocates its payload. Actor/board state
    // and active definitions remain alive for the unchanged V4 serializer.
    pvzh1_cache_complete_draw_batch();
    H1CachedFramesRelease();
    ++pvzh1_board_replay_retirement_diagnostic[0];
    pvzh1_board_replay_retirement_diagnostic[1]=pvzh1_actor_pose_diagnostic[0];
    pvzh1_actor_pose_shutdown();pvzh1_early_lawn_shutdown();pvzh1_reward_effect_shutdown();
    pvzh1_board_replay_retirement_diagnostic[2]=pvzh1_actor_pose_diagnostic[0];
}

namespace {std::set<Sexy::MemoryImage*>* warmImages=nullptr;}
namespace {Board* supportBoard=nullptr;int supportCounter=0;unsigned supportMask=0;bool supportDirty=true;
#ifdef PVZ_HOST_RUNNER
bool supportEnabled=true;
#endif
}
void H1BoardSupportChanged(){supportDirty=true;}
bool H1BoardMayHaveSupport(Board* board,SeedType type){
#ifdef PVZ_HOST_RUNNER
 if(!supportEnabled)return true;
#endif
 if(type!=SEED_FLOWERPOT&&type!=SEED_PUMPKINSHELL)return true;
 if(supportDirty||supportBoard!=board||supportCounter!=board->mMainCounter){
  supportMask=0;for(auto* p:board->mPlants)if(!p->mDead){if(p->mSeedType==SEED_FLOWERPOT)supportMask|=1;if(p->mSeedType==SEED_PUMPKINSHELL)supportMask|=2;}
  supportBoard=board;supportCounter=board->mMainCounter;supportDirty=false;
 }return supportMask&(type==SEED_FLOWERPOT?1u:2u);
}
#ifdef PVZ_HOST_RUNNER
void H1BoardLookupTestEnable(bool enabled){supportEnabled=enabled;}
#endif
namespace {
void WarmImage(Sexy::Image* value){
    if(!warmImages||uintptr_t(value)<=1000)return;
    auto* image=dynamic_cast<Sexy::MemoryImage*>(value);
    if(!image||!Sexy::H1ImageCacheContains(image)||!warmImages->insert(image).second)return;
    PvzpSandImageIfNeeded(image);
    const uint32_t* bits=nullptr;const uint32_t* const* pages=nullptr;uint32_t width=0,height=0;
    if(!Sexy::H1ImageCachePrepareStorageRGBPages(image,&pages,&width,&height)&&
       !Sexy::H1ImageCachePrepareStorageTexture(image,&bits,&width,&height)&&!Sexy::H1ImageCacheIsReduced(image))image->GetBits();
}
}
void H1PrewarmDefinitionImages(int type){
    if(!warmImages||type<0||unsigned(type)>=gReanimatorDefCount)return;
    const auto& definition=gReanimatorDefArray[type];
    for(int t=0;t<definition.mTracks.count;++t){const auto& track=definition.mTracks.tracks[t];
        for(int f=0;f<track.mTransforms.count;++f)WarmImage(track.mTransforms.mTransforms[f].mImage);
    }
}
static void PreloadCurrentBoard(LawnApp* app,bool enteringPlay){
    if(!app||!app->mBoard)return;
    // Board's original constructor has already destroyed the menu's effect
    // instances. Retire its now-unreferenced definition before the synchronous
    // level preload, rather than waiting for the next screen draw afterwards.
    pvzh1_selector_definition_trim(nullptr);
    H1BoardSupportChanged();
    Sexy::H1ImageCacheReleaseUI();
    H1AudioSaveScope loading;
    std::set<Sexy::MemoryImage*> images;
    struct Scope {std::set<Sexy::MemoryImage*>* previous;Scope(std::set<Sexy::MemoryImage*>* value):previous(warmImages){warmImages=value;}~Scope(){warmImages=previous;}} scope(&images);
    auto* board=app->mBoard;
    ++pvzh1_level_preload_diagnostic[0];
    pvzh1_level_loading_step(10);
    if(app->mGameMode==GAMEMODE_INTRO&&H1IntroMovieRequired()){pvzh1_music_warm_intro();pvzh1_sound_warm_intro();}
    else pvzh1_music_warm_game();
    pvzh1_level_loading_step(25);
    // The decorative first-player battle is baked into target-size frames.
    // Keep original CutScene clock/audio but don't warm unused actor assets.
    if(H1IntroMoviePrepare(board)){pvzh1_level_loading_step(100);return;}
    // The baked actor bundle has no useful preview poses. Before selection,
    // keep this memory available to the original intro/chooser definitions.
    // Early lawns need their baked sod animation before play, and continued
    // games already have real packet types, so those still prepare here.
    bool packetsChosen=board->mSeedBank&&board->mSeedBank->mNumPackets>0;
    if(packetsChosen)for(int i=0;i<board->mSeedBank->mNumPackets&&i<SEEDBANK_MAX;++i)
        packetsChosen&=int(board->mSeedBank->mSeedPackets[i].mPacketType)>=0;
    const bool prepareActors=!board->ChooseSeedsOnCurrentLevel()||packetsChosen;
    pvzh1_level_loading_step(35);
    pvzh1_sound_warmup();
    pvzh1_level_loading_step(45);
    for(auto type:{REANIM_PUFF,REANIM_LAWN_MOWERED_ZOMBIE,REANIM_READYSETPLANT,
                   REANIM_FINAL_WAVE,REANIM_SUN,REANIM_TEXT_FADE_ON,REANIM_SODROLL,REANIM_LAWNMOWER})
        ReanimatorEnsureDefinitionLoaded(type,true);
    bool plants[static_cast<int>(SeedType::NUM_SEED_TYPES)]{};
    bool filtered[static_cast<int>(SeedType::NUM_SEED_TYPES)]{};
    bool zombies[static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)]{};
    if(board->mSeedBank)for(int i=0;i<board->mSeedBank->mNumPackets&&i<SEEDBANK_MAX;++i){
        auto& packet=board->mSeedBank->mSeedPackets[i];
        for(auto type:{packet.mPacketType,packet.mImitaterType}){
            const int n=static_cast<int>(type);if(n>=0&&n<static_cast<int>(SeedType::NUM_SEED_TYPES))plants[n]=true;
        }
        if(int(packet.mImitaterType)>=0&&int(packet.mImitaterType)<int(NUM_SEED_TYPES))filtered[int(packet.mImitaterType)]=true;
    }
    for(auto* plant:board->mPlants)if(!plant->mDead){
        for(auto type:{plant->mSeedType,plant->mImitaterType}){
            const int n=int(type);if(n>=0&&n<int(SeedType::NUM_SEED_TYPES))plants[n]=true;
        }
        if(int(plant->mImitaterType)>=0&&int(plant->mImitaterType)<int(NUM_SEED_TYPES))filtered[int(plant->mImitaterType)]=true;
    }
    if(app->mGameMode==GAMEMODE_ADVENTURE&&app->IsFirstTimeAdventureMode()){
        const int award=int(app->GetAwardSeedForLevel(board->mLevel));
        if(award>=0&&award<int(SeedType::NUM_SEED_TYPES))plants[award]=true;
    }
    for(auto effect:{PARTICLE_AWARD_PICKUP_ARROW,PARTICLE_SEED_PACKET,PARTICLE_SEED_PACKET_PICKUP,PARTICLE_SEED_PACKET_FLASH}){
        const int index=int(effect);if(index<0||index>=gParticleDefCount)continue;
        const auto& definition=gParticleDefArray[index];
        for(int i=0;i<definition.mEmitterDefCount;++i)WarmImage(definition.mEmitterDefs[i].mImage);
    }
    // Both authored waves and current-level allowed types cover previews and
    // challenge-spawned enemies, without consulting the player's unlock list.
    // PickZombieWaves initializes only active waves and the prefix before the
    // INVALID terminator. The remaining array storage is not authored content.
    for(int wave=0;wave<board->mNumWaves&&wave<MAX_ZOMBIE_WAVES;++wave)
        for(auto type:board->mZombiesInWave[wave]){
            if(type==ZombieType::ZOMBIE_INVALID)break;
            const int n=static_cast<int>(type);if(n>=0&&n<static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES))zombies[n]=true;
        }
    for(int i=0;i<static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)&&i<100;++i)zombies[i]|=board->mZombieAllowed[i];
    // The upstream flag-zombie preloader warms its body but not this separate
    // attached animation, otherwise first flag spawn still opens image files.
    if(zombies[int(ZombieType::ZOMBIE_FLAG)])ReanimatorEnsureDefinitionLoaded(REANIM_FLAG,true);
    bool keepTypes[NUM_REANIMS]{},keepFilters[NUM_REANIMS]{};
    for(int i=0;i<int(NUM_SEED_TYPES);++i)if(plants[i]){const int type=int(gPlantDefs[i].mReanimationType);if(type>=0&&type<NUM_REANIMS){keepTypes[type]=true;keepFilters[type]|=filtered[i];}}
    H1PlantPoseDiscardUnneeded(keepTypes,keepFilters);
    H1CachedFramesPrepare(plants,filtered,packetsChosen);
    // Definition parsers need temporary contiguous arrays. Finish every
    // original definition before immutable pixel banks occupy those holes.
    for(int i=0;i<static_cast<int>(SeedType::NUM_SEED_TYPES);++i)if(plants[i]){Plant::PreloadPlantResources(static_cast<SeedType>(i));++pvzh1_level_preload_diagnostic[1];}
    for(int i=0;i<static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES);++i)if(zombies[i]){Zombie::PreloadZombieResources(static_cast<ZombieType>(i));++pvzh1_level_preload_diagnostic[2];}
    // Every definition/image getter above has returned. No borrowed pixel
    // pointer survives here; end their pin epoch before baked banks compete
    // with replaceable source textures for the same native heap.
    Sexy::H1ImageCacheEndFrame();
    pvzh1_level_loading_step(65);
    // StartPlaying runs while the original scene still says INTRO. Its sod
    // replay may already have retired naturally, so gameplay preparation must
    // not require or reload that finished animation.
    const bool earlySod=!enteringPlay&&app->mGameScene!=SCENE_PLAYING&&app->IsFirstTimeAdventureMode()&&
        (board->mLevel==1||board->mLevel==2||board->mLevel==4);
    if(prepareActors&&!H1ActorPosePrepare(keepTypes[REANIM_PEASHOOTER],true,earlySod))
        throw std::runtime_error("H1 offline actor bank preload failed");
    if(!H1EarlyLawnPrepare(app->mBoard))
        throw std::runtime_error("H1 offline lawn preload failed");
    for(int i=0;i<static_cast<int>(SeedType::NUM_SEED_TYPES);++i)if(plants[i]&&!H1PlantPosePrepare(gPlantDefs[i].mReanimationType,filtered[i]))
        throw std::runtime_error("H1 offline plant bank preload failed");
    if(prepareActors&&keepTypes[REANIM_SUNFLOWER]&&!H1SunflowerGlowPrepare())
        throw std::runtime_error("H1 offline sunflower glow preload failed");
    pvzh1_level_preload_diagnostic[3]=images.size();
    pvzh1_level_loading_step(95);
    // All getters have returned. Advance their pin epoch before the optional
    // baked reward data competes with textures that the actor replay replaces.
    Sexy::H1ImageCacheEndFrame();
    // Actor-definition loading has a larger temporary peak. Reserve the UI
    // only after that work and its pixel pins have completed.
    H1OptionsPrepare();
}
void H1PreloadCurrentBoard(LawnApp* app){PreloadCurrentBoard(app,false);}
void H1PreloadCurrentBoardForPlay(LawnApp* app){PreloadCurrentBoard(app,true);}
