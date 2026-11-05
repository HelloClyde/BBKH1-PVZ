/* Warm current-board dependencies before play, independent of profile unlocks.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Plant.h"
#include "Lawn/Zombie.h"
#include "Lawn/SeedPacket.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/PvzpParticle.h"
#include "Startup.h"
#include "actors/ActorPose.h"
#include "graphics/EarlyLawn.h"
#include "graphics/RewardEffects.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/MemoryImage.h"
#include <set>
extern "C" void pvzh1_sound_warmup();
extern "C" void pvzh1_music_warm_game();
extern "C" {volatile uint32_t pvzh1_level_preload_diagnostic[4];}

namespace {std::set<Sexy::MemoryImage*>* warmImages=nullptr;}
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
void H1PreloadCurrentBoard(LawnApp* app){
    if(!app||!app->mBoard)return;
    H1AudioSaveScope loading;
    std::set<Sexy::MemoryImage*> images;
    struct Scope {std::set<Sexy::MemoryImage*>* previous;Scope(std::set<Sexy::MemoryImage*>* value):previous(warmImages){warmImages=value;}~Scope(){warmImages=previous;}} scope(&images);
    auto* board=app->mBoard;
    ++pvzh1_level_preload_diagnostic[0];
    pvzh1_music_warm_game();
    H1ActorPosePrepare();
    H1EarlyLawnPrepare(app->mBoard);
    pvzh1_sound_warmup();
    for(auto type:{REANIM_PUFF,REANIM_LAWN_MOWERED_ZOMBIE,REANIM_READYSETPLANT,
                   REANIM_FINAL_WAVE,REANIM_SUN,REANIM_TEXT_FADE_ON,REANIM_SODROLL,REANIM_LAWNMOWER})
        ReanimatorEnsureDefinitionLoaded(type,true);
    bool plants[static_cast<int>(SeedType::NUM_SEED_TYPES)]{};
    bool zombies[static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES)]{};
    if(board->mSeedBank)for(int i=0;i<board->mSeedBank->mNumPackets&&i<SEEDBANK_MAX;++i){
        auto& packet=board->mSeedBank->mSeedPackets[i];
        for(auto type:{packet.mPacketType,packet.mImitaterType}){
            const int n=static_cast<int>(type);if(n>=0&&n<static_cast<int>(SeedType::NUM_SEED_TYPES))plants[n]=true;
        }
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
    for(int i=0;i<static_cast<int>(SeedType::NUM_SEED_TYPES);++i)if(plants[i]){Plant::PreloadPlantResources(static_cast<SeedType>(i));++pvzh1_level_preload_diagnostic[1];}
    for(int i=0;i<static_cast<int>(ZombieType::NUM_ZOMBIE_TYPES);++i)if(zombies[i]){Zombie::PreloadZombieResources(static_cast<ZombieType>(i));++pvzh1_level_preload_diagnostic[2];}
    pvzh1_level_preload_diagnostic[3]=images.size();
    // All getters have returned. Advance their pin epoch before the optional
    // baked reward data competes with textures that the actor replay replaces.
    Sexy::H1ImageCacheEndFrame();
}
