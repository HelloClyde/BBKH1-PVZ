/* Ordered original-shader literals, baked at the LCD resolution. There is no
 * texture sampling or geometry setup in replay. LGPL-3.0-or-later. */
#include "ActorPose.h"
#include "DeferredOverlay.h"
#include "ActorPoseFormat.h"
#include "DenseStateFormat.h"
#include "ActorBakePolicy.h"
#include "SDL.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CutScene.h"
#include "PvzpLib/Attachment.h"
#include "PvzpLib/EffectSystem.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/PixelCopy.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/ReanimTrack.h"
#include "Common.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <new>
#include <stdexcept>
bool H1CombatSpritesPrepare();
#include <zlib.h>
#include "TrustedResources.h"
#include "graphics/CombatSprites.h"
#include "graphics/CombatSpriteFormat.h"
#include "graphics/TextReplay.h"
#include "PvzpLib/PvzpParticle.h"
#ifdef PVZ_NATIVE_PROFILE
#include "profiler.h"
#endif
#ifdef PVZ_HOST_RUNNER
extern "C" void pvzh1_host_replay_phase(int,unsigned,bool) __attribute__((weak));
extern "C" void pvzh1_host_replay_segment(int,unsigned,unsigned,unsigned,bool,bool) __attribute__((weak));
#endif
extern "C" {volatile uint32_t pvzh1_actor_pose_diagnostic[16];}
extern "C" {volatile uint32_t pvzh1_sod_offline_diagnostic[8];}
extern "C" {volatile uint32_t pvzh1_actor_page_diagnostic[8];}
/* Actual replay work by actor type; source-image draws do not contribute. */
extern "C" {volatile uint32_t pvzh1_bank_page_hits[NUM_REANIMS],pvzh1_bank_page_misses[NUM_REANIMS],pvzh1_bank_read_bytes[NUM_REANIMS],pvzh1_bank_read_calls[NUM_REANIMS];}
extern "C" {volatile uint32_t pvzh1_bank_draw_milliseconds[NUM_REANIMS];}
#ifdef PVZH1_ACTOR_DETAIL_PROFILE
/* Per type: guards/blends, signature, packet setup, page miss, pixels,
 * attachments, destination commit, reserved. Page/attachment times are nested. */
extern "C" {volatile uint32_t pvzh1_bank_phase_ms[NUM_REANIMS*8];}
#endif
extern "C" {volatile uint32_t pvzh1_plant_pose_diagnostic[12];}
extern "C" {volatile uint32_t pvzh1_base_inverse_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_actor_fallback_diagnostic[64];}
extern "C" {volatile uint32_t pvzh1_shadow_pose_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_normal_actor_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_actor_lifetime_diagnostic[8];}
extern "C" {volatile uint32_t pvzh1_frame_time_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_linear_actor_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_active_track_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_extra_actor_diagnostic[8];volatile uint32_t pvzh1_live_actor_diagnostic[NUM_REANIMS];volatile uint32_t pvzh1_baked_actor_diagnostic[NUM_REANIMS];}
extern "C" {volatile uint32_t pvzh1_dense_actor_diagnostic[12];}
extern "C" {volatile uint32_t pvzh1_combat_sprite_diagnostic[12];}
#ifdef PVZ_NATIVE_PROFILE
extern "C" {volatile uint32_t pvzh1_resident_bank_diagnostic[NUM_REANIMS+8];}
#endif
#ifdef PVZH1_ACTOR_DETAIL_PROFILE
extern "C" {volatile uint32_t pvzh1_live_draw_calls_diagnostic[NUM_REANIMS],pvzh1_live_draw_ticks_diagnostic[NUM_REANIMS],pvzh1_live_draw_worst_diagnostic[NUM_REANIMS];}
uint32_t H1ActorLiveDrawBegin(){return SDL_GetTicks();}
void H1ActorLiveDrawEnd(Reanimation* r,uint32_t start){if(!r||unsigned(r->mReanimationType)>=NUM_REANIMS)return;unsigned type=unsigned(r->mReanimationType),elapsed=SDL_GetTicks()-start;
 ++pvzh1_live_draw_calls_diagnostic[type];pvzh1_live_draw_ticks_diagnostic[type]+=elapsed;pvzh1_live_draw_worst_diagnostic[type]=std::max((uint32_t)pvzh1_live_draw_worst_diagnostic[type],elapsed);}
#endif
namespace {
using namespace H1ActorPose;
struct Entry{uint64_t key;uint32_t frame,offset,bytes,type;};
struct EncodedPage{uint32_t offset,bytes;};
int activeBankType=-1;
#ifdef PVZH1_ACTOR_DETAIL_PROFILE
struct BankDrawScope{int previous;uint32_t started;explicit BankDrawScope(int type):previous(activeBankType),started(SDL_GetTicks()){activeBankType=type>=0&&type<NUM_REANIMS?type:-1;}~BankDrawScope(){if(activeBankType>=0)pvzh1_bank_draw_milliseconds[activeBankType]+=SDL_GetTicks()-started;activeBankType=previous;}};
#else
struct BankDrawScope{int previous;explicit BankDrawScope(int type):previous(activeBankType){activeBankType=type>=0&&type<NUM_REANIMS?type:-1;}~BankDrawScope(){activeBankType=previous;}};
#endif
#ifdef PVZH1_ACTOR_DETAIL_PROFILE
struct BankPhaseScope{
 int index;uint32_t started;
 explicit BankPhaseScope(unsigned phase):index(activeBankType<0?-1:activeBankType*8+phase),started(index<0?0:SDL_GetTicks()){}
 void End(){if(index>=0){pvzh1_bank_phase_ms[index]+=SDL_GetTicks()-started;index=-1;}}
 ~BankPhaseScope(){End();}
};
#define H1_BANK_PHASE(name,phase) BankPhaseScope name{phase}
#define H1_BANK_PHASE_END(name) name.End()
#elif defined(PVZ_HOST_RUNNER)
struct HostReplayPhase {
 unsigned phase;int type;bool active=true;
 explicit HostReplayPhase(unsigned p):phase(p),type(activeBankType){if(pvzh1_host_replay_phase)pvzh1_host_replay_phase(type,phase,true);}
 void End(){if(active&&pvzh1_host_replay_phase)pvzh1_host_replay_phase(type,phase,false);active=false;}
 ~HostReplayPhase(){End();}
};
#define H1_BANK_PHASE(name,phase) HostReplayPhase name{phase}
#define H1_BANK_PHASE_END(name) name.End()
#elif defined(PVZ_NATIVE_PROFILE)
struct ReplayNativePhase {
 unsigned stage;uint32_t started;bool active;
 explicit ReplayNativePhase(unsigned phase):stage(PVZ_PROFILE_ACTOR_GUARD+phase),started(0),active(pvz_profile_enabled(stage)){if(active)started=pvz_profile_enter(stage);}
 void End(){if(active)pvz_profile_leave(stage,started);active=false;}
 ~ReplayNativePhase(){End();}
};
#define H1_BANK_PHASE(name,phase) ReplayNativePhase name{phase}
#define H1_BANK_PHASE_END(name) name.End()
#else
#define H1_BANK_PHASE(name,phase) ((void)0)
#define H1_BANK_PHASE_END(name) ((void)0)
#endif
constexpr unsigned DecodedSlots=256,InflateArena=49152;
// Actor decoding is synchronous. Reset one decoder between independent pages
// instead of retaining the same 48 KiB workspace in every animation bank.
struct InflateWork {z_stream inflater{};uint8_t arena[InflateArena];unsigned arenaUsed=0,users=0;};
InflateWork* inflateWork=nullptr;
bool AcquireInflater();void ReleaseInflater();
constexpr unsigned TileSlots=256,TileWidth=32,TileHeight=16,TilePixels=512,TileBytes=2560;
struct TileSlot {uint8_t* bits=nullptr;uint32_t reference=UINT32_MAX,stamp=0,flags=0;};
#ifdef PVZ_HOST_RUNNER
unsigned pageSlotLimit=DecodedSlots;
#else
constexpr unsigned pageSlotLimit=DecodedSlots;
#endif
struct Cache{uint8_t** pages=nullptr;uint32_t* palette=nullptr;uint32_t** palettePages=nullptr;uint32_t* flashNormal=nullptr;uint32_t* variantColors[3]{};Entry* entries=nullptr;
 uint32_t size=0,npages=0,count=0,colors=0,flashOffset=0,flashColors=0,flashShades=0,dataOffset=0;
 uint32_t** flashTablePages=nullptr;uint32_t flashTablePageCount=0,flashTableBytes=0;uint16_t* flashReferences=nullptr;
 EncodedPage* encoded=nullptr;uint8_t** blob=nullptr;uint16_t* slotForPage=nullptr;
 FILE* storageFile=nullptr;uint32_t storageOffset=0;bool directStorageReady=false;
 uint8_t* decoded[DecodedSlots]{};uint32_t decodedPage[DecodedSlots]{},stamp[DecodedSlots]{},clock=0,encodedBytes=0,blobCount=0;
 TileSlot* tiles=nullptr;uint8_t* tileInput=nullptr;uint8_t* tileScratch=nullptr;uint32_t tileClock=0,tileAllocated=0;
  unsigned decodeLimit=128;bool streamPreferred=false,flashColumns=false,flashIndexed=false,compressed=false,rawPages=false,denseTiles=false,inflateReady=false,tried=false,busy=false,enabled=true,perTrack=false,directLiterals=false,colorVariants=false,alignedRuns=false,linearRuns=false,segmentOffsets=false,extendedRuns=false;} baseCache,sunCache,normalCache,zombieCache,sodCache,glowCache,poleIdleCache,blinkCache,zombieBlendCache;
struct PlantBanks{Cache* items[4]{};};
Cache* activeCache=&baseCache;PlantBanks* plantCaches[NUM_REANIMS]{};Cache* shadowCache=nullptr;
Cache* combatCache=nullptr;Cache* hatsCache=nullptr;
bool combatEnabled=true;
struct HitLive {PvzpParticleSystem* system=nullptr;unsigned age=0,variant=0;};
HitLive* hitLive=nullptr;unsigned hitCapacity=0;
#define cache (*activeCache)
struct CacheScope{Cache* previous;explicit CacheScope(Cache* value):previous(activeCache){activeCache=value;}~CacheScope(){activeCache=previous;}};
struct SignatureMemo{Reanimation* actor;SignatureMemo* next;uint32_t* words=nullptr;uint64_t key=0;unsigned count=0;bool perTrack;uint32_t stepRate=0;int stepFrames=0;double step=0;};
SignatureMemo* signatures[128]{};unsigned signatureBytes=0;
struct BasePoseMemo{BasePoseMemo* next;ReanimatorDefinition* definition;int track,frame;Sexy::SexyTransform2D inverse;};
BasePoseMemo* baseInverses[32]{};
struct FrameMemo {Reanimation* actor=nullptr;uint32_t time=0;int start=0,count=0,loop=0;ReanimatorFrameTime value{};};
FrameMemo (*frameTimes)[2]=nullptr;
unsigned FrameBucket(Reanimation* actor){const uintptr_t p=uintptr_t(actor);return ((p>>4)^(p>>13))&127;}
struct ActiveMemo {Reanimation* actor=nullptr;ReanimatorDefinition* definition=nullptr;ReanimatorTrackInstance* tracks=nullptr;unsigned count=0,generation=0;uint32_t mask[8]{};unsigned firstAttachment=UINT32_MAX,colorEpoch=0;uint32_t colors[14]{};};ActiveMemo (*activeTracks)[2]=nullptr;
unsigned colorEpoch=1;
extern "C" {volatile uint32_t pvzh1_dense_replay_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_color_memo_diagnostic[4];}
unsigned attachmentGeneration=1,activeReplacement=0;
#ifdef PVZ_HOST_RUNNER
bool activeTracksEnabled=true;
bool colorMemoEnabled=true;
bool extraTestAll=false;
#endif
bool ActiveTrack(Reanimation* actor,unsigned index){const auto& t=actor->mTrackInstances[index];return t.mBlendCounter>0||
 (std::bit_cast<uint32_t>(t.mShakeOverride)&0x7fffffffu)||t.mAttachmentID!=ATTACHMENTID_NULL||H1IsAttacherTrack(actor->mDefinition->mTracks.tracks[index].mName);}
bool ActiveMemoType(ReanimationType type){return H1ActorPose::PlantType(type)||type==REANIM_ZOMBIE||type==REANIM_SUN;}
#ifdef PVZ_HOST_RUNNER
bool attachmentMemoEnabled=true;
#endif
bool MemoSignature(Reanimation* actor,int group,uint64_t& key,bool blend,bool flash,bool perTrack){
 uint32_t words[SignatureWords];unsigned count=0;
 if(!VisitSignature(actor,group,[&](uint32_t word){words[count++]=word;},blend,flash,perTrack))return false;
 auto*& bucket=signatures[((uintptr_t(actor)>>4)^unsigned(perTrack))&127];SignatureMemo* found=bucket;
 while(found&&(found->actor!=actor||found->perTrack!=perTrack))found=found->next;
 if(found&&found->count==count&&!std::memcmp(found->words,words,count*4)){key=found->key;++pvzh1_plant_pose_diagnostic[8];return true;}
 key=14695981039346656037ull;for(unsigned i=0;i<count;++i)Mix(key,words[i]);++pvzh1_plant_pose_diagnostic[9];
 if(!found){found=new(std::nothrow) SignatureMemo{actor,bucket,nullptr,0,0,perTrack};if(!found)return true;bucket=found;signatureBytes+=sizeof(SignatureMemo);}
 if(found->count!=count){auto* data=new(std::nothrow) uint32_t[count];if(!data)return true;delete[] found->words;signatureBytes-=found->count*4;found->words=data;found->count=count;signatureBytes+=count*4;}
 std::memcpy(found->words,words,count*4);found->key=key;return true;
}
void DropSignatures(){for(auto*& head:signatures)while(head){auto* next=head->next;delete[] head->words;delete head;head=next;}for(auto*& head:baseInverses)while(head){auto* next=head->next;delete head;head=next;}signatureBytes=0;pvzh1_base_inverse_diagnostic[2]=pvzh1_base_inverse_diagnostic[3]=0;}
size_t DropTileFrames(Cache& c){size_t bytes=c.tileAllocated;
 if(c.tiles){for(unsigned i=0;i<TileSlots;++i)delete[] c.tiles[i].bits;delete[] c.tiles;c.tiles=nullptr;bytes+=TileSlots*sizeof(TileSlot);}c.tileAllocated=c.tileClock=0;
 if(c.tileInput){delete[] c.tileInput;c.tileInput=nullptr;bytes+=2624;}
 if(c.tileScratch){delete[] c.tileScratch;c.tileScratch=nullptr;bytes+=TileBytes;}return bytes;
}
size_t DropDecoded(Cache& c){
 if(c.busy||!c.compressed)return 0;size_t bytes=0;
 for(unsigned i=0;i<DecodedSlots;++i)if(c.decoded[i]){
  if(c.stamp[i]){c.pages[c.decodedPage[i]]=nullptr;c.slotForPage[c.decodedPage[i]]=UINT16_MAX;}
  delete[] c.decoded[i];c.decoded[i]=nullptr;c.stamp[i]=0;bytes+=4096;
 }return bytes+DropTileFrames(c);
}
bool ReserveDecoded(Cache& c){
 if(!c.compressed)return true;
 for(unsigned i=0;i<std::min(pageSlotLimit,c.decodeLimit);++i)if(!c.decoded[i]){
  c.decoded[i]=new(std::nothrow) uint8_t[4096];if(!c.decoded[i])return false;
 }return true;
}
uint32_t Resident(const Cache& c){if(!c.entries)return 0;uint32_t bytes=c.size;
 if(c.compressed){bytes=(c.storageFile?(c.directStorageReady?sizeof(FILE):16384u):c.encodedBytes+c.blobCount*sizeof(void*))+c.npages*((c.rawPages?0:sizeof(EncodedPage))+sizeof(uint16_t));for(auto* p:c.decoded)if(p)bytes+=4096;}
 bytes+=c.tileAllocated+(c.tiles?TileSlots*sizeof(TileSlot):0)+(c.tileInput?2624:0)+(c.tileScratch?TileBytes:0);
 for(auto* p:c.variantColors)if(p)bytes+=c.flashColors*4;
 return bytes+c.flashTableBytes+c.flashTablePageCount*sizeof(void*)+(c.flashReferences?c.flashColors*2:0)+c.count*sizeof(Entry)+c.colors*4+c.npages*sizeof(void*)+(c.flashNormal?c.flashColors*4:0)+(c.palettePages?((c.colors+1023)/1024)*sizeof(void*):0);}
void CountResident(){unsigned bytes=Resident(baseCache)+Resident(sunCache)+Resident(normalCache)+Resident(zombieCache)+Resident(sodCache)+Resident(glowCache)+Resident(poleIdleCache)+Resident(blinkCache)+Resident(zombieBlendCache)+signatureBytes+(combatCache?Resident(*combatCache):0)+(hatsCache?Resident(*hatsCache):0)+hitCapacity*sizeof(HitLive)+(inflateWork?sizeof(InflateWork):0)+(frameTimes?sizeof(FrameMemo)*256:0)+(activeTracks?sizeof(ActiveMemo)*256:0)+H1DeferredOverlayResident(),entries=baseCache.count+sunCache.count+normalCache.count+zombieCache.count+sodCache.count+glowCache.count+blinkCache.count+zombieBlendCache.count;
 pvzh1_actor_lifetime_diagnostic[0]=Resident(baseCache);pvzh1_actor_lifetime_diagnostic[1]=Resident(zombieCache);pvzh1_actor_lifetime_diagnostic[2]=Resident(sodCache);pvzh1_actor_lifetime_diagnostic[3]=Resident(normalCache);
 if(shadowCache){bytes+=Resident(*shadowCache);entries+=shadowCache->count;}
 #ifdef PVZ_NATIVE_PROFILE
 unsigned slot=0;for(auto* p:{&baseCache,&zombieCache,&normalCache,&sunCache,&glowCache,&sodCache,&poleIdleCache,shadowCache})pvzh1_resident_bank_diagnostic[slot++]=p?Resident(*p):0;
#endif
 for(unsigned type=0;type<NUM_REANIMS;++type){unsigned typedBytes=0;if(auto* banks=plantCaches[type])for(auto* p:banks->items)if(p){unsigned n=Resident(*p);bytes+=n;typedBytes+=n;entries+=p->count;}
#ifdef PVZ_NATIVE_PROFILE
  pvzh1_resident_bank_diagnostic[8+type]=typedBytes;
#endif
 }
 pvzh1_actor_pose_diagnostic[0]=bytes;pvzh1_actor_pose_diagnostic[1]=entries;}
struct Blend {Reanimation* actor=nullptr;unsigned from=0,time=0,use=0,clip=0;} blends[64];unsigned blendClock=0;
Board* sodBoard=nullptr;int sodStart=0;
unsigned FrameHalf(Reanimation* r){ReanimatorFrameTime t;r->GetFrameTime(&t);unsigned n=t.mAnimFrameBeforeInt*2;if(t.mAnimFrameAfterInt!=t.mAnimFrameBeforeInt){if(t.mFraction>=.75f)n=t.mAnimFrameAfterInt*2;else if(t.mFraction>=.25f)++n;}return n;}
uint16_t LE16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t LE32(const uint8_t* p){return uint32_t(LE16(p))|(uint32_t(LE16(p+2))<<16);}
void Drop(){if(cache.inflateReady)ReleaseInflater();cache.inflateReady=false;
 delete[]cache.flashReferences;cache.flashReferences=nullptr;
 DropTileFrames(cache);
 if(cache.flashTablePages){for(unsigned i=0;i<cache.flashTablePageCount;++i)delete[] cache.flashTablePages[i];delete[] cache.flashTablePages;}
 cache.flashTablePages=nullptr;cache.flashTableBytes=cache.flashTablePageCount=0;
 if(cache.storageFile)fclose(cache.storageFile);cache.storageFile=nullptr;cache.storageOffset=0;cache.directStorageReady=false;
 if(cache.compressed){for(auto*& p:cache.decoded){delete[] p;p=nullptr;}}
 if(cache.pages){if(!cache.compressed)for(unsigned i=0;i<cache.npages;++i)delete[] cache.pages[i];delete[] cache.pages;}
 if(cache.blob){for(unsigned i=0;i<cache.blobCount;++i)delete[] cache.blob[i];delete[] cache.blob;}
 delete[] cache.encoded;delete[] cache.slotForPage;
 cache.encoded=nullptr;cache.blob=nullptr;cache.slotForPage=nullptr;cache.compressed=cache.rawPages=cache.denseTiles=false;
 cache.encodedBytes=cache.blobCount=cache.clock=0;cache.perTrack=cache.directLiterals=cache.colorVariants=cache.alignedRuns=cache.linearRuns=cache.segmentOffsets=cache.extendedRuns=false;cache.flashColumns=cache.flashIndexed=false;for(unsigned i=0;i<DecodedSlots;++i){cache.decodedPage[i]=0;cache.stamp[i]=0;}
 for(auto*& p:cache.variantColors){delete[] p;p=nullptr;}
 if(cache.palettePages){for(unsigned i=0;i<(cache.colors+1023)/1024;++i)delete[] cache.palettePages[i];delete[] cache.palettePages;cache.palettePages=nullptr;}
 delete[] cache.flashNormal;cache.flashNormal=nullptr;delete[] cache.palette;delete[] cache.entries;cache.pages=nullptr;cache.palette=nullptr;cache.entries=nullptr;
 cache.size=cache.npages=cache.count=cache.colors=cache.flashOffset=cache.flashColors=cache.flashShades=cache.dataOffset=0;pvzh1_actor_pose_diagnostic[0]=0;}
voidpf PageAlloc(voidpf opaque,uInt n,uInt size){auto& c=*(InflateWork*)opaque;const uint64_t bytes=uint64_t(n)*size;unsigned at=(c.arenaUsed+7)&~7u;
 if(bytes>InflateArena-at)return nullptr;c.arenaUsed=at+(unsigned)bytes;return c.arena+at;}
void PageFree(voidpf,voidpf){}
bool AcquireInflater(){
 if(inflateWork){++inflateWork->users;return true;}
 auto* w=new(std::nothrow) InflateWork;if(!w)return false;
 w->inflater.zalloc=PageAlloc;w->inflater.zfree=PageFree;w->inflater.opaque=w;
 if(H1ResourceInflateInit(&w->inflater)!=Z_OK){delete w;return false;}
 w->users=1;inflateWork=w;return true;
}
void ReleaseInflater(){if(inflateWork&&!--inflateWork->users){inflateEnd(&inflateWork->inflater);delete inflateWork;inflateWork=nullptr;}}
void BlobRead(unsigned offset,unsigned bytes,uint8_t* out){
 if(cache.storageFile){
  // Metadata benefits from sequential stdio buffering. Sparse replay pages
  // do not: each seek otherwise rereads a whole 16 KiB buffer for <=4 KiB.
  // Once metadata is finished, retire that buffer and use positioned reads.
  if(!cache.directStorageReady){
   if(fflush(cache.storageFile)||setvbuf(cache.storageFile,nullptr,_IONBF,0))throw std::runtime_error("Cannot prepare offline actor stream");
   cache.directStorageReady=true;CountResident();
  }
  const int fd=fileno(cache.storageFile);
  if(lseek(fd,off_t(cache.storageOffset)+offset,SEEK_SET)<0)throw std::runtime_error("Cannot seek offline actor page");
  unsigned done=0;while(done<bytes){const auto n=read(fd,out+done,bytes-done);
   if(n<=0||size_t(n)>bytes-done)throw std::runtime_error("Unable to read offline actor page");done+=unsigned(n);}
  if(activeBankType>=0){pvzh1_bank_read_bytes[activeBankType]+=bytes;++pvzh1_bank_read_calls[activeBankType];}return;
 }
 while(bytes){unsigned n=std::min(bytes,4096-(offset&4095));std::memcpy(out,cache.blob[offset>>12]+(offset&4095),n);out+=n;offset+=n;bytes-=n;}}
void CloseLoadFile(FILE*f){if(f!=cache.storageFile)fclose(f);}
const uint8_t* Page(unsigned page){
 if(!cache.compressed)return cache.pages[page];
 if(cache.pages[page]){cache.stamp[cache.slotForPage[page]]=++cache.clock;++pvzh1_actor_page_diagnostic[3];if(activeBankType>=0)++pvzh1_bank_page_hits[activeBankType];return cache.pages[page];}
 H1_BANK_PHASE(pagePhase,3);
 unsigned slot=0;for(unsigned i=1;i<std::min(pageSlotLimit,cache.decodeLimit);++i)if(cache.stamp[i]<cache.stamp[slot])slot=i;
 if(!cache.decoded[slot]){cache.decoded[slot]=new(std::nothrow) uint8_t[4096];if(!cache.decoded[slot])return nullptr;}
 if(cache.stamp[slot]){cache.pages[cache.decodedPage[slot]]=nullptr;cache.slotForPage[cache.decodedPage[slot]]=UINT16_MAX;}
 const unsigned bytes=std::min(4096u,cache.size-page*4096);bool ok=true;
 if(cache.rawPages)BlobRead(page*4096,bytes,cache.decoded[slot]);
 else{uint8_t input[4128];const auto& e=cache.encoded[page];BlobRead(e.offset,e.bytes,input);auto& z=inflateWork->inflater;
  ok=H1ResourceInflateReset(&z)==Z_OK;z.next_in=input;z.avail_in=e.bytes;z.next_out=cache.decoded[slot];z.avail_out=bytes;
  ok=ok&&inflate(&z,Z_FINISH)==Z_STREAM_END&&z.total_out==bytes;}
 if(!ok){++pvzh1_actor_page_diagnostic[4];return nullptr;}
 cache.decodedPage[slot]=page;cache.slotForPage[page]=slot;cache.pages[page]=cache.decoded[slot];cache.stamp[slot]=++cache.clock;
 ++pvzh1_actor_page_diagnostic[2];if(activeBankType>=0)++pvzh1_bank_page_misses[activeBankType];unsigned resident=0;for(auto* p:cache.decoded)if(p)resident+=4096;
 pvzh1_actor_page_diagnostic[1]=std::max((uint32_t)pvzh1_actor_page_diagnostic[1],resident);return cache.pages[page];
}
bool ReadCompressed(FILE* f,uint32_t start){
 uint8_t header[32];if(fseek(f,start,SEEK_SET)||fread(header,1,32,f)!=32)return false;
 uint32_t h[8];for(unsigned i=0;i<8;++i)h[i]=LE32(header+i*4);
 if(h[2]>512*1024*1024)return false;
 cache.compressed=true;cache.rawPages=h[1]==3;cache.size=h[2];cache.npages=h[3];cache.encodedBytes=h[5];cache.blobCount=(h[5]+4095)/4096;
 cache.pages=new(std::nothrow) uint8_t*[cache.npages]{};if(!cache.rawPages)cache.encoded=new(std::nothrow) EncodedPage[cache.npages];
 // Repeated head transitions are the measured hot stream. Keep one shared
 // compressed copy, while selected-but-unused plant banks stay file-backed.
 // Their palettes/entry tables are ready before play; no renderer fallback.
 const bool paged=cache.streamPreferred||(cache.encodedBytes>512*1024&&activeCache!=&baseCache);
 cache.slotForPage=new(std::nothrow) uint16_t[cache.npages];if(!paged)cache.blob=new(std::nothrow) uint8_t*[cache.blobCount]{};
 if(!cache.pages||(!cache.rawPages&&!cache.encoded)||!cache.slotForPage||(!paged&&!cache.blob))return false;
 for(unsigned i=0;i<cache.npages;++i){if(!cache.rawPages){uint8_t row[12];if(fread(row,1,12,f)!=12)return false;cache.encoded[i]={LE32(row),LE32(row+4)};}cache.slotForPage[i]=UINT16_MAX;}
 if(paged){cache.storageFile=f;cache.storageOffset=uint32_t(ftell(f));}
 else for(unsigned i=0;i<cache.blobCount;++i){unsigned n=std::min(4096u,cache.encodedBytes-i*4096);cache.blob[i]=new(std::nothrow) uint8_t[n];if(!cache.blob[i]||fread(cache.blob[i],1,n,f)!=n)return false;}
 if(!AcquireInflater())return false;cache.inflateReady=true;
 pvzh1_actor_page_diagnostic[0]=cache.encodedBytes;pvzh1_actor_page_diagnostic[5]=cache.size;pvzh1_actor_page_diagnostic[6]=DecodedSlots*4096;
 return true;
}
__attribute__((always_inline)) inline uint32_t FlashColor(unsigned index,unsigned shade){
 // The validated palette offset and every entry are four-byte aligned; a word
 // never crosses a 4 KiB page. H1 and the supported host runner are little endian.
 if(!shade&&cache.flashNormal)return cache.flashNormal[index];
 if(cache.flashReferences){const unsigned ref=cache.flashReferences[index];const Cache* source=(ref&0x8000u)?hatsCache:&zombieCache;
  const unsigned at=(ref&0x7fffu)*26+shade;return source->flashTablePages[at>>10][at&1023];}
 if(cache.flashTablePages){unsigned at=cache.flashColumns?shade*cache.flashColors+index:index*cache.flashShades+shade;
  if(cache.flashIndexed)return cache.palette[reinterpret_cast<const uint16_t*>(cache.flashTablePages[at>>11])[at&2047]];
  return cache.flashTablePages[at>>10][at&1023];}
 unsigned at=cache.flashOffset+(cache.flashColumns?shade*cache.flashColors+index:index*cache.flashShades+shade)*(cache.flashIndexed?2:4);
 const auto* p=Page(at>>12);if(!p)throw std::runtime_error("Invalid offline actor page");return cache.flashIndexed?cache.palette[LE16(p+(at&4095))]:LE32(p+(at&4095));
}
struct Reader{
 uint32_t offset;const uint8_t* p=nullptr;unsigned available=0;
 explicit Reader(uint32_t o):offset(o){}
 void Refresh(){const auto* page=Page(offset>>12);if(!page)throw std::runtime_error("Invalid offline actor page");p=page+(offset&4095);available=std::min(4096-(offset&4095),cache.size-offset);}
 bool Stale()const{return cache.compressed&&(!cache.pages[offset>>12]||p!=cache.pages[offset>>12]+(offset&4095));}
 uint8_t U8(){if(!available||Stale())Refresh();uint8_t v=*p++;++offset;--available;return v;}
 __attribute__((always_inline)) inline uint16_t U16(){if(!available||Stale())Refresh();
  if(available==1){uint16_t low=U8();return low|(uint16_t(U8())<<8);}
  uint16_t v=LE16(p);p+=2;offset+=2;available-=2;return v;}
 uint32_t U32(){if(available>=4&&!Stale()){uint32_t v=LE32(p);p+=4;offset+=4;available-=4;return v;}uint32_t low=U16();return low|(uint32_t(U16())<<16);}
 void Segment(bool offsets,int& track,unsigned& draw,unsigned& runs,unsigned& payload,int& x,int& y){
  const unsigned size=offsets?16:12;
  if(available>=size&&!Stale()){track=int16_t(LE16(p));draw=LE16(p+2);runs=LE32(p+4);payload=LE32(p+8);x=offsets?int16_t(LE16(p+12)):0;y=offsets?int16_t(LE16(p+14)):0;p+=size;offset+=size;available-=size;return;}
  track=int16_t(U16());draw=U16();runs=U32();payload=U32();x=offsets?int16_t(U16()):0;y=offsets?int16_t(U16()):0;
 }
 unsigned TakeSamples(unsigned count,unsigned width,const uint8_t*& out){if(!available||Stale())Refresh();unsigned n=std::min(count,available/width);out=p;p+=n*width;offset+=n*width;available-=n*width;return n;}
 void Run(bool compact,int& x,int& y,unsigned& flags,bool aligned=false,bool linear=false,int left=0){
  if(linear){const int value=std::bit_cast<int32_t>(U32());flags=U16();U16();if(value< -131040||value>131040){x=y=INT32_MAX;return;}const int delta=value-left;y=delta>=0?delta/480:-((-delta+479)/480);x=value-y*480;return;}
  const unsigned n=compact?3:aligned?8:6;
  if(available>=n&&!Stale()){
   if(compact){x=int8_t(p[0]);y=int8_t(p[1]);flags=(p[2]&127)|((p[2]&128)<<8);}
   else{x=int16_t(LE16(p));y=int16_t(LE16(p+2));flags=LE16(p+4);}
   p+=n;offset+=n;available-=n;return;
  }
  if(compact){unsigned xy=U16(),f=U8();x=int8_t(xy);y=int8_t(xy>>8);flags=(f&127)|((f&128)<<8);}else{x=int16_t(U16());y=int16_t(U16());flags=U16();if(aligned)U16();}}
 void Skip(unsigned n){if(!n)return;offset+=n;if(n<=available){p+=n;available-=n;}else{p=nullptr;available=0;}}
};
__attribute__((optimize("O3"))) const uint8_t* Tile(uint32_t reference,uint32_t& flags){
 if(!cache.tiles)cache.tiles=new TileSlot[TileSlots];
 for(unsigned i=0;i<TileSlots;++i)if(auto&t=cache.tiles[i];t.bits&&t.reference==reference){t.stamp=++cache.tileClock;flags=t.flags;++pvzh1_dense_actor_diagnostic[1];return t.bits;}
 Reader input(cache.dataOffset+reference);flags=input.U32();uint32_t base=input.U32();int dx=0,dy=0;
 if(flags&0x08000000u){unsigned shift=input.U32();dx=int8_t(shift);dy=int8_t(shift>>8);}
 if(base!=UINT32_MAX){uint32_t ignored;const uint8_t* previous=Tile(base,ignored);
  if(!cache.tileScratch)cache.tileScratch=new uint8_t[TileBytes];
  if(dx||dy){std::memset(cache.tileScratch,0,TileBytes);int first=std::max(0,-dx),last=std::min(int(TileWidth),int(TileWidth)-dx);
   for(int y=0;y<int(TileHeight);++y){int sy=y+dy;if(sy<0||sy>=int(TileHeight)||first>=last)continue;
    H1CopyPixels(reinterpret_cast<uint32_t*>(cache.tileScratch)+(y*TileWidth+first),reinterpret_cast<const uint32_t*>(previous)+(sy*TileWidth+first+dx),last-first);
    std::memcpy(cache.tileScratch+TilePixels*4+y*TileWidth+first,previous+TilePixels*4+sy*TileWidth+first+dx,last-first);}
  }else std::memcpy(cache.tileScratch,previous,TileBytes);
 }
 unsigned slot=0;for(unsigned i=1;i<TileSlots;++i)if(cache.tiles[i].stamp<cache.tiles[slot].stamp)slot=i;auto&t=cache.tiles[slot];
 if(!t.bits){t.bits=new uint8_t[TileBytes];cache.tileAllocated+=TileBytes;pvzh1_dense_actor_diagnostic[3]=std::max((uint32_t)pvzh1_dense_actor_diagnostic[3],cache.tileAllocated);}
 if(!cache.tileInput)cache.tileInput=new uint8_t[2624];unsigned bytes=flags&0x03ffffffu;
 if(bytes>2624)throw std::runtime_error("Unsupported offline tile packet size");
 unsigned left=bytes;auto*out=cache.tileInput;while(left){const uint8_t* source;unsigned n=input.TakeSamples(left,1,source);if(!n)throw std::runtime_error("Truncated offline tile");std::memcpy(out,source,n);out+=n;left-=n;}
 if(flags&0x80000000u)std::memcpy(t.bits,cache.tileInput,TileBytes);
 else{auto&z=inflateWork->inflater;bool okay=H1ResourceInflateReset(&z)==Z_OK;z.next_in=cache.tileInput;z.avail_in=bytes;z.next_out=t.bits;z.avail_out=TileBytes;
  if(!okay||inflate(&z,Z_FINISH)!=Z_STREAM_END||z.total_out!=TileBytes){++pvzh1_dense_actor_diagnostic[11];throw std::runtime_error("Unable to decode offline tile");}}
 if(flags&0x20000000u){std::memcpy(cache.tileInput,t.bits,TileBytes);auto*words=reinterpret_cast<uint32_t*>(t.bits);
  for(unsigned i=0;i<TilePixels;++i)words[i]=uint32_t(cache.tileInput[i])|(uint32_t(cache.tileInput[TilePixels+i])<<8)|(uint32_t(cache.tileInput[TilePixels*2+i])<<16)|(uint32_t(cache.tileInput[TilePixels*3+i])<<24);
  std::memcpy(t.bits+TilePixels*4,cache.tileInput+TilePixels*4,TilePixels);++pvzh1_dense_actor_diagnostic[4];}
 if(flags&0x04000000u){auto*words=reinterpret_cast<uint32_t*>(t.bits);for(unsigned i=0;i<TilePixels;++i){uint32_t v=words[i],g=((v>>8)&255)*0x00010001u;words[i]=(v&0xff00ff00u)|(((v&0xff00ffu)+g)&0xff00ffu);}++pvzh1_dense_actor_diagnostic[10];}
 if(base!=UINT32_MAX){auto*words=reinterpret_cast<uint32_t*>(t.bits);const auto*before=reinterpret_cast<const uint32_t*>(cache.tileScratch);
  if(flags&0x10000000u){for(unsigned i=0;i<TileBytes/4;++i){uint32_t a=words[i],b=before[i];words[i]=(((a&0xff00ffu)+(b&0xff00ffu))&0xff00ffu)|((((((a>>8)&0xff00ffu)+((b>>8)&0xff00ffu))&0xff00ffu))<<8);}++pvzh1_dense_actor_diagnostic[6];}
  else{for(unsigned i=0;i<TileBytes/4;++i)words[i]^=before[i];++pvzh1_dense_actor_diagnostic[5];}}
 if(flags&0x08000000u)++pvzh1_dense_actor_diagnostic[7];
 t.reference=reference;t.flags=flags;t.stamp=++cache.tileClock;++pvzh1_dense_actor_diagnostic[0];pvzh1_dense_actor_diagnostic[2]+=bytes;return t.bits;
}
__attribute__((optimize("O3"))) void DrawDenseTiles(Reader&stream,unsigned count,uint32_t*pixels,int dx,int dy,int cx,int cy,int cr,int cb){
 for(unsigned tile=0;tile<count;++tile){int x=int16_t(stream.U16())+dx,y=int16_t(stream.U16())+dy;uint32_t reference=stream.U32();
  int firstX=std::max(0,cx-x),lastX=std::min(int(TileWidth),cr-x),firstY=std::max(0,cy-y),lastY=std::min(int(TileHeight),cb-y);
  if(firstX>=lastX||firstY>=lastY)continue;uint32_t flags;const auto*bits=Tile(reference,flags);const auto*colors=reinterpret_cast<const uint32_t*>(bits);const auto*kinds=bits+TilePixels*4;
  if(flags&0x40000000u){for(int row=firstY;row<lastY;++row)H1CopyPixels(pixels+(y+row)*480+x+firstX,colors+row*TileWidth+firstX,lastX-firstX);++pvzh1_dense_actor_diagnostic[9];continue;}
  for(int row=firstY;row<lastY;++row){auto*dest=pixels+(y+row)*480+x+firstX;unsigned at=row*TileWidth+firstX;
   for(int col=firstX;col<lastX;++col,++at,++dest){unsigned kind=kinds[at];if(!kind)continue;uint32_t v=colors[at],p=*dest;
    if(kind==2){unsigned rb=(p&0xff00ffu)+(v&0xff00ffu),g=(p&0xff00u)+(v&0xff00u),carry=rb&0x1000100u;rb|=carry-(carry>>8);if(g&0x10000u)g=0xff00u;*dest=(p&0xff000000u)|(rb&0xff00ffu)|(g&0xff00u);}
    else if(v>>24==255)*dest=v;
    else if(kind==3){unsigned a=v>>24,inv=256-a;*dest=0xff000000u|((((p&0xff00ffu)*inv+(v&0xff00ffu)*a)>>8)&0xff00ffu)|((((p&0xff00u)*inv+(v&0xff00u)*a)>>8)&0xff00u);}
    else{unsigned inv=v>>24;*dest=0xff000000u|((v&0xffffffu)+((((p&0xff00ffu)*inv)>>8)&0xff00ffu)+((((p&0xff00u)*inv)>>8)&0xff00u));}
   }
  }
 }++pvzh1_dense_actor_diagnostic[8];
}
// This input has already passed packet, coordinate, literal and bounds checks
// at preload. Whole visible sprites need only the baked LCD offsets and copy
// operations; all original attachment segment boundaries remain outside it.
__attribute__((noinline,optimize("O3"))) void DrawLinearSegment(uint8_t* const* pages,unsigned offset,unsigned runs,uint32_t* pixels,int shift){
 ++pvzh1_linear_actor_diagnostic[0];pvzh1_linear_actor_diagnostic[1]+=runs;
 const uint8_t* p=nullptr;unsigned available=0;
 auto refresh=[&](){p=pages[offset>>12]+(offset&4095);available=4096-(offset&4095);};
 auto word=[&](){if(!available)refresh();const uint32_t value=*reinterpret_cast<const uint32_t*>(p);p+=4;offset+=4;available-=4;return value;};
 for(unsigned i=0;i<runs;++i){
  const int at=std::bit_cast<int32_t>(word());const unsigned flags=word(),count=flags&32767;auto* dest=pixels+at+shift;unsigned left=count;
  while(left){if(!available)refresh();const unsigned n=std::min(left,available/4);const auto* literals=reinterpret_cast<const uint32_t*>(p);
   if(flags&32768){H1CopyPixels(dest,literals,n);dest+=n;}
   else for(unsigned j=0;j<n;++j){const uint32_t literal=literals[j],background=*dest,a=literal>>24;*dest++=0xff000000u|((literal&0xffffffu)+((((background&0xff00ffu)*a)>>8)&0xff00ffu)+((((background&0xff00u)*a)>>8)&0xff00u));}
   p+=n*4;offset+=n*4;available-=n*4;left-=n;
  }
 }
}
bool ReadComponent(FILE* f,unsigned offset,unsigned encoded,unsigned raw,bool palette,uint32_t){
 if(fseek(f,offset,SEEK_SET)||H1ResourceInflateReset(&inflateWork->inflater)!=Z_OK)return false;
 auto& z=inflateWork->inflater;z.next_in=nullptr;z.avail_in=0;z.next_out=nullptr;z.avail_out=0;
 uint8_t input[4096],extra;unsigned remaining=encoded;int result=Z_OK;
 while(result!=Z_STREAM_END){
  if(!z.avail_in&&remaining){unsigned n=std::min(4096u,remaining);if(fread(input,1,n,f)!=n)return false;remaining-=n;z.next_in=input;z.avail_in=n;}
  if(!z.avail_out){unsigned done=z.total_out;if(done>raw)return false;
   if(done==raw){z.next_out=&extra;z.avail_out=1;}
   else if(palette){z.next_out=reinterpret_cast<Bytef*>(cache.palettePages[done/4096]);z.avail_out=std::min(4096u,raw-done);}
   else{z.next_out=reinterpret_cast<Bytef*>(cache.entries)+done;z.avail_out=raw-done;}
  }
  const unsigned beforeIn=z.total_in,beforeOut=z.total_out;result=inflate(&z,Z_NO_FLUSH);
  if((result!=Z_OK&&result!=Z_STREAM_END)||z.total_out>raw||(beforeIn==z.total_in&&beforeOut==z.total_out))return false;
 }
 if(remaining||z.avail_in||z.total_in!=encoded||z.total_out!=raw)return false;
 return true;
}
__attribute__((optimize("Os"))) bool LoadShared(FILE* f,const uint32_t* h,unsigned total,unsigned slot){
 if(h[0]!=0x53503148u||h[1]!=1||slot!=h[2]*4+h[3]||h[2]>=NUM_REANIMS||h[3]>=4||!h[4]||h[4]>8192||!h[5]||h[5]>65536||!total)return false;
 auto range=[&](unsigned at,unsigned size){return at>=32&&at<=total&&size&&size<=total-at;};
 if(!range(h[6],h[7])||!range(h[10],h[11])||!range(h[13],h[14])||h[11]>h[4]*24+1024||h[14]>h[5]*4+1024)return false;
 cache.count=h[4];cache.colors=h[5];cache.perTrack=true;cache.dataOffset=64;
 if(!ReadCompressed(f,h[6]))return false;
 cache.entries=new(std::nothrow) Entry[cache.count];
 cache.palettePages=new(std::nothrow) uint32_t*[(cache.colors+1023)/1024]{};
 if(!cache.entries||!cache.palettePages)return false;
 for(unsigned i=0;i<(cache.colors+1023)/1024;++i){cache.palettePages[i]=new(std::nothrow) uint32_t[std::min(1024u,cache.colors-i*1024)];if(!cache.palettePages[i])return false;}
 static_assert(sizeof(Entry)==24);
 if(!ReadComponent(f,h[10],h[11],cache.count*24,false,h[12])||!ReadComponent(f,h[13],h[14],cache.colors*4,true,h[15]))return false;
 // Geometry pages stay encoded until an actual frame uses them. Entry order,
 // references, palettes and packet streams are verified by offline tooling.
 ++pvzh1_actor_page_diagnostic[7];return true;
}
__attribute__((optimize("Os"))) bool Load(const char* filename,uint32_t start=0,uint32_t length=0,uint32_t total=0,uint32_t slot=UINT32_MAX){
 auto path=Sexy::GetResourcePath(filename);FILE* f=fopen(path.c_str(),"rb");if(!f)return false;setvbuf(f,nullptr,_IOFBF,16384);
 uint8_t header[64]{};bool okay=(!start||fseek(f,start,SEEK_SET)==0)&&fread(header,1,64,f)==64;
 if(okay&&LE32(header)==0x53503148u){uint32_t h[16];for(unsigned i=0;i<16;++i)h[i]=LE32(header+i*4);
  okay=length==64;try{okay=okay&&LoadShared(f,h,total,slot);}catch(const std::runtime_error&){okay=false;}
  CloseLoadFile(f);if(!okay)Drop();return okay;
 }
 if(okay&&LE32(header)==0x5a503148u){okay=ReadCompressed(f,start);if(okay){const auto* page=Page(0);okay=page!=nullptr;if(okay)std::memcpy(header,page,64);}if(!okay){CloseLoadFile(f);Drop();return false;}}
 uint32_t h[16]{};for(unsigned i=0;i<16;++i)h[i]=LE32(header+i*4);
 if(!okay){CloseLoadFile(f);Drop();return false;}
 cache.perTrack=h[15]&2;cache.directLiterals=h[15]&4;cache.colorVariants=h[15]&8;cache.alignedRuns=h[15]&16;cache.linearRuns=h[15]&32;cache.segmentOffsets=h[15]&64;cache.extendedRuns=h[15]&128;cache.flashColumns=h[15]&256;cache.flashIndexed=h[15]&512;cache.denseTiles=h[15]&4096;cache.size=h[9];cache.count=h[4];cache.colors=h[6];cache.flashOffset=h[11];cache.flashColors=h[12];cache.flashShades=h[13];cache.dataOffset=h[8];cache.npages=(cache.size+4095)/4096;
 if(!cache.compressed)cache.pages=new(std::nothrow) uint8_t*[cache.npages]{};
 cache.entries=new(std::nothrow) Entry[cache.count];if(cache.colors)cache.palette=new(std::nothrow) uint32_t[cache.colors];
 if(!cache.pages||!cache.entries||(cache.colors&&!cache.palette))okay=false;
 for(unsigned i=0;okay&&!cache.compressed&&i<cache.npages;++i){unsigned n=std::min(4096u,cache.size-i*4096);
  cache.pages[i]=new(std::nothrow) uint8_t[n];if(!cache.pages[i]){okay=false;break;}
  unsigned start=i==0?64:0;if(i==0)std::memcpy(cache.pages[i],header,64);
  if(fread(cache.pages[i]+start,1,n-start,f)!=n-start){okay=false;break;}}
 CloseLoadFile(f);
 if(okay){Reader r(64);uint64_t key=0;uint32_t frame=0;
  for(unsigned i=0;i<cache.count;++i){auto& e=cache.entries[i];e.key=r.U32();e.key|=uint64_t(r.U32())<<32;e.frame=r.U32();e.offset=r.U32();e.bytes=r.U32();e.type=r.U32();
  }
  Reader colors(h[7]);for(unsigned i=0;i<cache.colors;++i)cache.palette[i]=colors.U32();
 }
 if(!okay){Drop();return false;}
 if(cache.flashColors){cache.flashNormal=new(std::nothrow) uint32_t[cache.flashColors];if(!cache.flashNormal){Drop();return false;}
  if(activeCache==&zombieBlendCache){
   // Offline-generated IDs refer to the existing body/hat palettes. No color
   // comparison, generation, or duplicate 26-shade table allocation at runtime.
   cache.flashReferences=new(std::nothrow) uint16_t[cache.flashColors];
   FILE* refs=cache.flashReferences?fopen(Sexy::GetResourcePath("zombie-blend-colors.h1cr").c_str(),"rb"):nullptr;
   const bool ready=refs&&zombieCache.flashNormal&&zombieCache.flashTablePages&&hatsCache&&hatsCache->flashNormal&&hatsCache->flashTablePages&&fread(cache.flashReferences,2,cache.flashColors,refs)==cache.flashColors;
   if(refs)fclose(refs);if(!ready){Drop();return false;}
   for(unsigned i=0;i<cache.flashColors;++i){unsigned ref=cache.flashReferences[i];cache.flashNormal[i]=(ref&0x8000u)?hatsCache->flashNormal[ref&0x7fffu]:zombieCache.flashNormal[ref];}
  }else for(unsigned i=0;i<cache.flashColors;++i){unsigned at=cache.flashOffset+(cache.flashColumns?i:i*cache.flashShades)*(cache.flashIndexed?2:4);const auto* p=Page(at>>12);if(!p){Drop();return false;}cache.flashNormal[i]=cache.flashIndexed?cache.palette[LE16(p+(at&4095))]:LE32(p+(at&4095));}
 }
 // The tiny pre-baked hit-color table used to evict animation pixels from
 // the same sparse LRU on every hit. Read it once into shared bounded blocks.
 if((activeCache==&zombieCache||activeCache==hatsCache||activeCache==&glowCache||activeCache==&blinkCache)&&cache.flashColors&&cache.flashShades){
  const unsigned words=cache.flashColors*cache.flashShades;
  const unsigned pageWords=cache.flashIndexed?2048:1024;
  cache.flashTableBytes=words*(cache.flashIndexed?2:4);cache.flashTablePageCount=(words+pageWords-1)/pageWords;
  cache.flashTablePages=new(std::nothrow) uint32_t*[cache.flashTablePageCount]{};
  if(!cache.flashTablePages){Drop();return false;}
  for(unsigned i=0;i<cache.flashTablePageCount;++i){
   unsigned n=std::min(pageWords,words-i*pageWords);cache.flashTablePages[i]=new(std::nothrow) uint32_t[cache.flashIndexed?(n+1)/2:n];
   if(!cache.flashTablePages[i]){Drop();return false;}
  }
  Reader colors(cache.flashOffset);for(unsigned i=0;i<words;++i){
   if(cache.flashIndexed)reinterpret_cast<uint16_t*>(cache.flashTablePages[i>>11])[i&2047]=colors.U16();
   else cache.flashTablePages[i>>10][i&1023]=colors.U32();}
 }
 if(cache.compressed)++pvzh1_actor_page_diagnostic[7];
 pvzh1_actor_pose_diagnostic[0]=cache.size+cache.count*sizeof(Entry)+cache.colors*4+cache.npages*sizeof(void*);
 pvzh1_actor_pose_diagnostic[1]=cache.count;return true;
}
const Entry* Find(uint64_t key,uint32_t frame){unsigned low=0,high=cache.count;while(low<high){unsigned mid=(low+high)/2;const auto& e=cache.entries[mid];if(e.key<key||(e.key==key&&e.frame<frame))low=mid+1;else high=mid;}
 return low<cache.count&&cache.entries[low].key==key&&cache.entries[low].frame==frame?cache.entries+low:nullptr;}
bool PrepareVariant(unsigned filter){
 if(!cache.colorVariants)return true;
 if(!filter)return cache.flashNormal!=nullptr;
 auto*& colors=cache.variantColors[filter-1];if(colors)return true;
 auto* values=new(std::nothrow) uint32_t[cache.flashColors];if(!values)return false;
 for(unsigned i=0;i<cache.flashColors;++i){const unsigned at=cache.flashOffset+(i*4+filter)*4;const auto* p=Page(at>>12);if(!p){delete[] values;return false;}values[i]=LE32(p+(at&4095));}
 colors=values;return true;
}
}
double H1ActorAnimIncrement(Reanimation* actor){
 const uint32_t rate=std::bit_cast<uint32_t>(actor->mAnimRate);
 auto*& bucket=signatures[(uintptr_t(actor)>>4)&127];auto* found=bucket;
 while(found&&(found->actor!=actor||found->perTrack))found=found->next;
 if(!found){found=new(std::nothrow) SignatureMemo{actor,bucket,nullptr,0,0,false};
  if(!found)return SECONDS_PER_UPDATE*actor->mAnimRate/actor->mFrameCount;
  bucket=found;signatureBytes+=sizeof(SignatureMemo);}
 if(found->stepRate!=rate||found->stepFrames!=actor->mFrameCount){
  found->stepRate=rate;found->stepFrames=actor->mFrameCount;
  found->step=SECONDS_PER_UPDATE*actor->mAnimRate/actor->mFrameCount;
 }return found->step;
}
bool H1ActorBaseInverse(Reanimation* actor,int track,Sexy::SexyTransform2D& inverse,bool store){
#ifdef PVZ_HOST_RUNNER
 if(!attachmentMemoEnabled)return false;
#endif
 if(!actor||!H1ActorPose::PlantType(actor->mReanimationType))return false;
 const int frame=actor->mFrameBasePose==-1?actor->mFrameStart:actor->mFrameBasePose;
 auto*& bucket=baseInverses[((uintptr_t(actor->mDefinition)>>4)^unsigned(track)^unsigned(frame))&31];auto* found=bucket;
 while(found&&(found->definition!=actor->mDefinition||found->track!=track||found->frame!=frame))found=found->next;
 if(!store){if(found){inverse=found->inverse;++pvzh1_base_inverse_diagnostic[0];return true;}++pvzh1_base_inverse_diagnostic[1];return false;}
 if(!found){found=new(std::nothrow) BasePoseMemo;if(!found)return false;found->definition=actor->mDefinition;found->track=track;found->frame=frame;found->next=bucket;bucket=found;signatureBytes+=sizeof(BasePoseMemo);++pvzh1_base_inverse_diagnostic[2];pvzh1_base_inverse_diagnostic[3]+=sizeof(BasePoseMemo);}
 found->inverse=inverse;return true;
}
bool H1ActorFrameTime(Reanimation* actor,ReanimatorFrameTime& value,bool store){
 if(!actor||actor->mFrameCount<=0||actor->mFrameStart<0||
    (std::bit_cast<uint32_t>(actor->mAnimTime)&0x7f800000u)==0x7f800000u)return false;
 if(!frameTimes)frameTimes=new FrameMemo[128][2];
 auto& set=frameTimes[FrameBucket(actor)];FrameMemo* found=nullptr;
 for(auto& p:set)if(p.actor==actor){found=&p;break;}
 const uint32_t time=std::bit_cast<uint32_t>(actor->mAnimTime);
 if(!store){
  if(found&&found->time==time&&found->start==actor->mFrameStart&&found->count==actor->mFrameCount&&found->loop==int(actor->mLoopType)){
   value=found->value;++pvzh1_frame_time_diagnostic[0];return true;
  }++pvzh1_frame_time_diagnostic[1];return false;
 }
 if(!found)found=!set[0].actor?&set[0]:!set[1].actor?&set[1]:&set[pvzh1_frame_time_diagnostic[2]&1];
 *found={actor,time,actor->mFrameStart,actor->mFrameCount,int(actor->mLoopType),value};++pvzh1_frame_time_diagnostic[2];return true;
}
void H1ActorTrackStateChanged(Reanimation* actor){if(activeTracks)for(auto& p:activeTracks[FrameBucket(actor)])if(p.actor==actor)p.generation=0;}
void H1ActorAttachmentsChanged(const void* attachmentSlot){
 // Creating a coin, projectile or blood effect cannot activate every plant's
 // tracks. Only the referenced track slot can change its attachment state.
 const auto address=uintptr_t(attachmentSlot);
 if(!activeTracks)return;
 for(unsigned bucket=0;bucket<128;++bucket)for(auto& p:activeTracks[bucket])if(p.actor&&p.tracks){
  const auto first=uintptr_t(p.tracks)+offsetof(ReanimatorTrackInstance,mAttachmentID);
  if(address>=first&&address-first<p.count*sizeof(ReanimatorTrackInstance)&&
     (address-first)%sizeof(ReanimatorTrackInstance)==0){p.generation=0;if(!++colorEpoch)colorEpoch=1;++pvzh1_color_memo_diagnostic[2];}
 }
}
int H1ActorNextActiveTrack(Reanimation* actor,int after){
 const unsigned count=actor->mDefinition->mTracks.count,begin=unsigned(after+1);
 if(begin>=count)return -1;
 if(!ActiveMemoType(actor->mReanimationType)||count>256
#ifdef PVZ_HOST_RUNNER
 ||!activeTracksEnabled
#endif
 )return int(begin);
 if(!activeTracks)activeTracks=new ActiveMemo[128][2];
 ++pvzh1_active_track_diagnostic[0];auto& set=activeTracks[FrameBucket(actor)];ActiveMemo* found=nullptr;
 for(auto& p:set)if(p.actor==actor){found=&p;break;}
 if(!found)found=!set[0].actor?&set[0]:!set[1].actor?&set[1]:&set[(activeReplacement++)&1];
 if(found->actor!=actor||found->definition!=actor->mDefinition||found->tracks!=actor->mTrackInstances||found->count!=count||found->generation!=attachmentGeneration){
  *found={actor,actor->mDefinition,actor->mTrackInstances,count,attachmentGeneration,{}};
  for(unsigned i=0;i<count;++i){if(ActiveTrack(actor,i))found->mask[i>>5]|=1u<<(i&31);if(found->firstAttachment==UINT32_MAX&&actor->mTrackInstances[i].mAttachmentID!=ATTACHMENTID_NULL)found->firstAttachment=i;}++pvzh1_active_track_diagnostic[1];
 }
 for(unsigned word=begin>>5;word<(count+31)/32;++word){uint32_t bits=found->mask[word];if(word==begin>>5)bits&=0xffffffffu<<(begin&31);
  while(bits){const unsigned bit=unsigned(__builtin_ctz(bits)),index=word*32+bit;bits&=bits-1;
   if(!ActiveTrack(actor,index)){found->mask[word]&=~(1u<<bit);continue;}
   ++pvzh1_active_track_diagnostic[2];pvzh1_active_track_diagnostic[3]+=index-begin;return int(index);
  }
 }pvzh1_active_track_diagnostic[3]+=count-begin;return -1;
}
bool H1ActorHasAttachments(Reanimation* actor){
 if(!actor||!actor->mDefinition||!actor->mDefinition->mTracks.count)return false;
 if(!ActiveMemoType(actor->mReanimationType)||actor->mDefinition->mTracks.count>256)return true;
#ifdef PVZ_HOST_RUNNER
 if(!activeTracksEnabled)return true;
#endif
 auto lookup=[&]()->ActiveMemo*{if(activeTracks)for(auto& p:activeTracks[FrameBucket(actor)])if(p.actor==actor&&p.definition==actor->mDefinition&&p.tracks==actor->mTrackInstances&&p.count==unsigned(actor->mDefinition->mTracks.count)&&p.generation==attachmentGeneration)return &p;return nullptr;};
 auto* found=lookup();
 // Nulling an attachment ID can also happen when its holder was already
 // deleted. Revalidate a formerly nonempty memo at its first recorded slot.
 if(found&&found->firstAttachment<found->count&&actor->mTrackInstances[found->firstAttachment].mAttachmentID==ATTACHMENTID_NULL){found->generation=0;found=nullptr;}
 if(!found){H1ActorNextActiveTrack(actor,-1);found=lookup();}
 return !found||found->firstAttachment<found->count;
}
bool H1ActorColorPropagationUnchanged(Reanimation* actor){
 // Only ordinary plants have a closed attachment tree without attacher scripts.
 // A newly attached blink invalidates ancestors through colorEpoch, even when
 // their own color stayed constant. Track-state mutations rebuild ActiveMemo.
 if(!actor||(actor->mReanimationType!=REANIM_PEASHOOTER&&actor->mReanimationType!=REANIM_SUNFLOWER))return false;
#ifdef PVZ_HOST_RUNNER
 if(!colorMemoEnabled)return false;
#endif
 if(!activeTracks)return false;
 ActiveMemo* found=nullptr;for(auto& p:activeTracks[FrameBucket(actor)])if(p.actor==actor&&p.generation==attachmentGeneration&&p.tracks==actor->mTrackInstances&&p.definition==actor->mDefinition){found=&p;break;}
 if(!found)return false;
 uint32_t words[14];unsigned n=0;
 for(const auto* color:{&actor->mColorOverride,&actor->mExtraAdditiveColor,&actor->mExtraOverlayColor})for(int value:{color->mRed,color->mGreen,color->mBlue,color->mAlpha})words[n++]=uint32_t(value);
 words[n++]=actor->mEnableExtraAdditiveDraw;words[n++]=actor->mEnableExtraOverlayDraw;
 if(found->colorEpoch==colorEpoch&&!std::memcmp(found->colors,words,sizeof(words))){++pvzh1_color_memo_diagnostic[0];return true;}
 std::memcpy(found->colors,words,sizeof(words));found->colorEpoch=colorEpoch;++pvzh1_color_memo_diagnostic[1];return false;
}
#ifdef PVZ_HOST_RUNNER
void H1ActorColorMemoTestEnable(bool enabled){colorMemoEnabled=enabled;if(!++colorEpoch)colorEpoch=1;}
#endif
bool H1ActorPosePrepare(bool pea,bool zombie,bool sod,bool normal,bool sun){
 if(baseCache.enabled&&(pea||normal)&&!H1DeferredOverlayPrepare())return false;
 baseCache.decodeLimit=64;
 // This measured hot stream keeps its compressed bytes in RAM. The shared
 // decoder above removes duplicate workspaces without adding per-frame I/O.
 baseCache.streamPreferred=false;
 // Several independently phased zombies share this bank. The native Boss
 // scene exceeded 64 decoded pages and reread the same compressed pages.
 zombieCache.decodeLimit=DecodedSlots;
 auto prepare=[](Cache& selected,const char* name){
  if(!selected.enabled)return true;
  CacheScope scope(&selected);
  if(!cache.tried){cache.tried=cache.busy=true;if(!Load(name)){Drop();++pvzh1_actor_pose_diagnostic[7];}else if(&selected==&normalCache)++pvzh1_normal_actor_diagnostic[3];cache.busy=false;}
  if((&selected==&zombieCache||&selected==&baseCache)&&cache.entries){
   // Allocate this bounded working set during Loading. First appearance and
   // later independent zombie phases must not compete for heap pages mid-draw.
   cache.busy=true;const bool ready=ReserveDecoded(cache);
   cache.busy=false;if(!ready){Drop();++pvzh1_actor_pose_diagnostic[7];}
  }else DropDecoded(cache);
  return cache.entries!=nullptr;
 };
 bool okay=(!pea||prepare(baseCache,"pea-transitions.h1ap"));
 okay=(!zombie||prepare(zombieCache,"zombie-hit.h1ap"))&&okay;
 if(zombie&&zombieBlendCache.enabled&&!H1CombatSpritesPrepare())okay=false;
 zombieBlendCache.decodeLimit=32;zombieBlendCache.streamPreferred=true;
 if(!zombie&&zombieBlendCache.entries){CacheScope scope(&zombieBlendCache);Drop();cache.tried=false;}
 okay=(!zombie||prepare(zombieBlendCache,"zombie-blends.h1ap"))&&okay;
 okay=(!sod||prepare(sodCache,"sod-roll.h1ap"))&&okay;
 if(!sun&&sunCache.entries){CacheScope scope(&sunCache);Drop();cache.tried=false;}
 okay=(!sun||prepare(sunCache,"sun-poses.h1ap"))&&okay;
 if(!(pea||normal)&&blinkCache.entries){CacheScope scope(&blinkCache);Drop();cache.tried=false;}
 blinkCache.decodeLimit=16;
 okay=(!(pea||normal)||prepare(blinkCache,"plant-blinks.h1ap"))&&okay;
 if(!normal&&normalCache.entries){CacheScope scope(&normalCache);Drop();cache.tried=false;pvzh1_normal_actor_diagnostic[0]=0;}
 const bool normalReady=!normal||prepare(normalCache,"actor-normal.h1ap");okay=normalReady&&okay;
 if(normalReady&&normalCache.entries){pvzh1_normal_actor_diagnostic[0]=normalCache.size;pvzh1_linear_actor_diagnostic[3]=normalCache.linearRuns?52:20;}
 if(!okay)++pvzh1_actor_lifetime_diagnostic[7];CountResident();return okay;
}
static bool DirectPlantShadow(Sexy::Graphics*,Sexy::Image*,float,float,int,float,float);
bool H1PlantShadowDraw(Sexy::Graphics* g,Sexy::Image* image,float x,float y,int cel,float sx,float sy){return DirectPlantShadow(g,image,x,y,cel,sx,sy);}
__attribute__((optimize("Os"))) bool H1PlantPosePrepare(ReanimationType type,bool allFilters){
 if(H1ActorPose::ExtraType(type))return H1ExtraPosePrepare(type);
 if(!baseCache.enabled||!H1ActorPose::PerTrackType(type)||int(type)<0||int(type)>=NUM_REANIMS)return true;
 auto*& banks=plantCaches[int(type)];if(!banks)banks=new(std::nothrow) PlantBanks;
 if(!banks){++pvzh1_plant_pose_diagnostic[1];return false;}
 bool okay=true;
 for(unsigned filter=0;filter<(allFilters?4u:1u);++filter){
 Cache* shared=nullptr;for(auto* p:banks->items)if(p&&p->entries&&p->colorVariants){shared=p;break;}
 if(filter&&shared){CacheScope scope(shared);cache.busy=true;if(!PrepareVariant(filter)){++pvzh1_plant_pose_diagnostic[1];okay=false;}cache.busy=false;DropDecoded(cache);continue;}
 auto*& slot=banks->items[filter];if(!slot){slot=new(std::nothrow) Cache;if(slot){slot->decodeLimit=32;slot->streamPreferred=true;}}
 if(!slot){++pvzh1_plant_pose_diagnostic[1];return false;}
 if(slot->tried){okay=bool(slot->entries)&&okay;continue;}
 {CacheScope scope(slot);cache.tried=cache.busy=true;
  const char* filename="plant-poses.h1pp";
  uint32_t offset=0,bytes=0,containerBytes=0;const auto path=Sexy::GetResourcePath(filename);FILE* f=fopen(path.c_str(),"rb");
  if(f){uint8_t header[32],table[NUM_REANIMS*4*12];
   if(fread(header,1,32,f)==32&&LE32(header)==0x4c503148u&&(LE32(header+4)==1||LE32(header+4)==2)){
    const unsigned count=LE32(header+8),size=LE32(header+12),total=LE32(header+16);
    const unsigned index=unsigned(type)*4+filter;
    if(count>index&&count<=NUM_REANIMS*4&&size==count*12&&fread(table,1,size,f)==size&&fseek(f,0,SEEK_END)==0&&uint32_t(ftell(f))==total){
     containerBytes=total;const auto* item=table+index*12;offset=LE32(item);bytes=LE32(item+4);
     if(offset<32+size||bytes<64||offset>total||bytes>total-offset)bytes=0;
    }
   }fclose(f);
  }
  if((bytes&&Load(filename,offset,bytes,containerBytes,unsigned(type)*4+filter))&&cache.perTrack&&PrepareVariant(filter)){++pvzh1_plant_pose_diagnostic[0];pvzh1_plant_pose_diagnostic[2]+=cache.count;}
  else{Drop();++pvzh1_plant_pose_diagnostic[1];okay=false;}cache.busy=false;DropDecoded(cache);}
 }CountResident();return okay;
}
bool PreparePoleIdle(){
 CacheScope scope(&poleIdleCache);if(cache.tried)return cache.entries!=nullptr;
 cache.tried=cache.busy=true;cache.decodeLimit=16;
 // The preview was sampled at its own LCD anchor to preserve triangle-edge
 // rounding. Both this bank and the full action bank contain finished pixels.
 FILE*f=fopen(Sexy::GetResourcePath("zombie-poses.h1pp").c_str(),"rb");uint8_t row[12]{};
 bool okay=f&&fseek(f,32+unsigned(REANIM_POLEVAULTER)*4*12,SEEK_SET)==0&&fread(row,1,12,f)==12;
 if(f)fclose(f);
 okay=okay&&Load("zombie-poses.h1pp",LE32(row),LE32(row+4));
 cache.busy=false;DropDecoded(cache);return okay;
}
bool H1ExtraPosePrepare(ReanimationType type){
 if(!baseCache.enabled||!H1ActorPose::ExtraType(type))return true;
 if(H1UsePolePreviewBank(type)&&!PreparePoleIdle())return false;
 bool useBank=H1UseExtraActorBank(type);
#ifdef PVZ_HOST_RUNNER
 useBank|=extraTestAll;
#endif
 if(!useBank)return true;
 auto*& banks=plantCaches[int(type)];if(!banks)banks=new(std::nothrow) PlantBanks;
 if(!banks)return false;auto*& selected=banks->items[0];if(!selected)selected=new(std::nothrow) Cache;
 if(!selected)return false;CacheScope scope(selected);
 if(cache.tried)return cache.entries!=nullptr;
 cache.decodeLimit=DecodedSlots;cache.tried=cache.busy=true;
 char name[64];std::snprintf(name,sizeof(name),"extra-actors/%03d.h1ap",int(type));
 bool okay=Load(name);if(!okay){Drop();++pvzh1_extra_actor_diagnostic[2];}else ++pvzh1_extra_actor_diagnostic[0];
 cache.busy=false;DropDecoded(cache);CountResident();return okay;
}
bool H1SunflowerGlowPrepare(){
 if(!glowCache.enabled)return true;CacheScope scope(&glowCache);
 // All 89 baked intensities belong to this level's shared color table. Keep
 // it separate from geometry so differently phased flowers never reread it
 // from the card or evict each other's replay pages. Drop() frees both.
 cache.decodeLimit=48;cache.streamPreferred=true;
 if(!cache.tried){cache.tried=cache.busy=true;
  if(!Load("sunflower-glow.h1ap")||!ReserveDecoded(cache)){Drop();++pvzh1_actor_lifetime_diagnostic[7];}
  cache.busy=false;}
 CountResident();return cache.entries!=nullptr;
}
void H1PlantPoseDiscardUnneeded(const bool* types,const bool* filters){
 for(unsigned type=0;type<NUM_REANIMS;++type)if(auto*& banks=plantCaches[type]){
  bool kept=false;for(unsigned filter=0;filter<4;++filter)if(auto*& p=banks->items[filter]){
   if(!types[type]||(filter&&!filters[type])){{CacheScope scope(p);Drop();}delete p;p=nullptr;}else{if(!filters[type])for(auto*& colors:p->variantColors){delete[] colors;colors=nullptr;}kept=true;}
  }if(!kept){delete banks;banks=nullptr;}
 }CountResident();
}
void H1ActorPoseNoteBlend(Reanimation* actor,int time){
 if(!actor)return;
 Blend* selected=nullptr;for(auto& b:blends)if(b.actor==actor){selected=&b;break;}else if(!selected||b.use<selected->use)selected=&b;
 bool supported=time==20&&((actor->mReanimationType==REANIM_PEASHOOTER&&(actor->mFrameStart==29||actor->mFrameStart==54))||
  (actor->mReanimationType==REANIM_ZOMBIE&&((actor->mFrameStart==44&&actor->mFrameCount==47)||(actor->mFrameStart==138&&actor->mFrameCount==40))));
 if(supported&&actor->mReanimationType==REANIM_ZOMBIE)for(int i=0;i<actor->mDefinition->mTracks.count;++i)if(actor->mTrackInstances[i].mBlendCounter){supported=false;break;}
 // An unsupported/nested new blend must invalidate an older sampled source.
 if(!supported){if(selected->actor==actor)*selected={};return;}
 *selected={actor,FrameHalf(actor),unsigned(time),++blendClock,unsigned(actor->mFrameStart)};
}
__attribute__((optimize("O3"))) static void DrawExtendedRuns(Reader& stream,unsigned runs,uint32_t* pixels,int dx,int dy,int cx,int cy,int cr,int cb){
 const bool direct=cache.directLiterals;const unsigned sampleBytes=direct?4:2;
 const uint32_t* const palette=cache.palette;
 auto read=[&](){return direct?stream.U32():palette[stream.U16()];};
 for(unsigned run=0;run<runs;++run){int x,y;unsigned flags;stream.Run(false,x,y,flags);x+=dx;y+=dy;unsigned n=flags&4095;const bool constant=flags&4096;
  if(y<cy||y>=cb||x>=cr||x+int(n)<=cx){stream.Skip((constant?1:n)*sampleBytes);continue;}
  unsigned first=std::max(0,cx-x),last=std::min(int(n),cr-x);auto*dest=pixels+y*480+x+first;uint32_t value=0;
  if(constant)value=read();else stream.Skip(first*sampleBytes);
  // One page/format/opacity decision per contiguous run, not per pixel.
  // No allocation or reader call occurs inside emit: a borrowed page cannot
  // be evicted while its contiguous samples are being consumed.
  auto visit=[&](auto emit) __attribute__((always_inline)) {
   unsigned left=last-first;
   if(constant){while(left--){emit(value);}return;}
   while(left){const uint8_t* source;unsigned count=stream.TakeSamples(left,sampleBytes,source);
    if(!count){emit(read());--left;continue;}
    if(direct)for(unsigned i=0;i<count;++i,source+=4)emit(LE32(source));
    else for(unsigned i=0;i<count;++i,source+=2)emit(palette[LE16(source)]);
    left-=count;
   }
  };
  if(flags&32768)visit([&](uint32_t v) __attribute__((always_inline)) {*dest++=v;});
  else if(flags&16384)visit([&](uint32_t v) __attribute__((always_inline)) {
   uint32_t p=*dest;unsigned rb=(p&0xff00ffu)+(v&0xff00ffu),g=(p&0xff00u)+(v&0xff00u),carry=rb&0x1000100u;
   rb|=carry-(carry>>8);if(g&0x10000u)g=0xff00u;*dest++=(p&0xff000000u)|(rb&0xff00ffu)|(g&0xff00u);
  });
  else if(flags&8192)visit([&](uint32_t v) __attribute__((always_inline)) {
   uint32_t p=*dest;unsigned a=v>>24,inv=256-a;*dest++=0xff000000u|((((p&0xff00ffu)*inv+(v&0xff00ffu)*a)>>8)&0xff00ffu)|((((p&0xff00u)*inv+(v&0xff00u)*a)>>8)&0xff00u);
  });
  else visit([&](uint32_t v) __attribute__((always_inline)) {
   uint32_t p=*dest;unsigned inv=v>>24;*dest++=0xff000000u|((v&0xffffffu)+((((p&0xff00ffu)*inv)>>8)&0xff00ffu)+((((p&0xff00u)*inv)>>8)&0xff00u));
  });
  if(!constant)stream.Skip((n-last)*sampleBytes);
 }
}

static bool DrawFromCache(Reanimation* actor,Sexy::Graphics* g,int group,uint64_t explicitKey=0,uint32_t explicitFrame=UINT32_MAX){
 H1_BANK_PHASE(guardPhase,0);
 if(!cache.enabled||cache.busy||!cache.entries||!actor||!(cache.perTrack?H1ActorPose::PerTrackType(actor->mReanimationType):H1ActorPose::Supported(actor->mReanimationType)))return false;
 if(actor->mDead)return true;
 if(!actor->mDefinition||!actor->mTrackInstances)return false;
 bool present=false;for(int i=0;i<actor->mDefinition->mTracks.count;++i)if(actor->mTrackInstances[i].mRenderGroup==group){present=true;break;}
 if(!present)return true; // Original DrawRenderGroup also draws no attachments.
 if(!g||g->mScaleX!=1||g->mScaleY!=1||g->mDrawMode!=Sexy::Graphics::DRAWMODE_NORMAL||g->mColorizeImages){++pvzh1_actor_pose_diagnostic[4];return false;}
 auto* screen=Sexy::H1GetPhysicalScreen(dynamic_cast<Sexy::GLImage*>(g->mDestImage));
 if(!screen||!screen->Active()||!screen->mGLInterface->mTransformStack.empty())return false;
 auto* target=screen->Target();if(target!=h1DirtyCanvas.owner||!target->mBits||target->mWidth!=480||target->mHeight!=272)return false;
 unsigned blendRemaining=0;for(int i=0;i<actor->mDefinition->mTracks.count;++i)blendRemaining=std::max<unsigned>(blendRemaining,actor->mTrackInstances[i].mBlendCounter);
 if(activeCache==&zombieBlendCache&&!blendRemaining)return false;
 unsigned blendFrame=0;bool blendReady=false;
 if(blendRemaining&&blendRemaining<=20&&actor->mReanimationType==REANIM_PEASHOOTER){
  for(auto& b:blends)if(b.actor==actor&&b.time==20){unsigned step=20-blendRemaining;
   if(actor->mFrameStart==54&&actor->mAnimRate==35&&b.from>=58&&b.from<=106){blendFrame=0x80000000u|(b.from<<8)|step;blendReady=true;}
   else if(actor->mFrameStart==29&&(b.from==156||b.from==132)){blendFrame=(b.from==156?0x90000000u:0xa0000000u)|(FrameHalf(actor)<<8)|step;blendReady=true;}break;
  }
 }
 if(activeCache==&zombieBlendCache&&blendRemaining&&blendRemaining<=20&&actor->mReanimationType==REANIM_ZOMBIE){
  for(const auto& b:blends)if(b.actor==actor&&b.time==20){const unsigned step=20-blendRemaining;unsigned direction=0,bucket=0;
   bool known=b.clip==44&&actor->mFrameStart==138&&actor->mFrameCount==40&&actor->mAnimRate==36&&b.from>=88&&b.from<=180;
   // Only the measured walk-to-eat hotspot is selected for offline storage.
   // The reverse transition was not a measured hotspot and is not expanded.
   // Mid-transition speed changes or nonstandard target clocks retain their
   // own rendering. A stale source must never masquerade as a baked packet.
   if(known&&std::abs(actor->mAnimTime-float(step)*actor->mAnimRate/(100.f*actor->mFrameCount))<.00001f){blendFrame=H1ActorPose::ZombieBlendFrame(b.from,H1ActorPose::ZombieDisplayBlendStep(step),direction,bucket);blendReady=true;}break;
  }
  if(!blendReady)return false;
 }
 unsigned flashShade=0;bool flashReady=false;
 if(actor->mReanimationType==REANIM_ZOMBIE&&actor->mEnableExtraAdditiveDraw){const auto& c=actor->mExtraAdditiveColor;
  if(c.mAlpha==255&&c.mRed>=10&&c.mRed<=250&&c.mRed==c.mGreen&&c.mRed==c.mBlue&&c.mRed%10==0){flashShade=c.mRed/10;flashReady=true;}}
 if(cache.flashShades==89&&(actor->mReanimationType==REANIM_SUNFLOWER||actor->mReanimationType==REANIM_PEASHOOTER)&&actor->mEnableExtraAdditiveDraw){const auto& c=actor->mExtraAdditiveColor;
  if(c.mRed==c.mGreen&&c.mRed==c.mBlue&&c.mRed>=0&&c.mRed<=255){
   if(c.mAlpha==255&&(c.mRed%3==0||c.mRed==128)){flashShade=c.mRed==128?86:c.mRed/3;flashReady=true;}
   else if(c.mRed==255&&(c.mAlpha==196||c.mAlpha==92)){flashShade=c.mAlpha==196?87:88;flashReady=true;}
  }
 }
 H1_BANK_PHASE_END(guardPhase);
 uint64_t key=explicitKey;bool signature;
 {H1_BANK_PHASE(signaturePhase,1);signature=explicitKey||MemoSignature(actor,group,key,blendReady,flashReady,cache.perTrack);}
 if(!signature){++pvzh1_actor_pose_diagnostic[3];if(cache.perTrack)++pvzh1_plant_pose_diagnostic[6];return false;}
 H1_BANK_PHASE(packetPhase,2);
 ReanimatorFrameTime time;actor->GetFrameTime(&time);unsigned frame=time.mAnimFrameBeforeInt*2;
 if(time.mAnimFrameAfterInt!=time.mAnimFrameBeforeInt){if(time.mFraction>=.75f)frame=time.mAnimFrameAfterInt*2;else if(time.mFraction>=.25f)++frame;}
 if(actor->mReanimationType==REANIM_SUN){frame=time.mAnimFrameBeforeInt*4;if(time.mAnimFrameAfterInt!=time.mAnimFrameBeforeInt){unsigned part=unsigned(time.mFraction*4+.5f);frame=part==4?time.mAnimFrameAfterInt*4:frame+part;}}
 if(blendReady)frame=blendFrame;
 if(explicitFrame!=UINT32_MAX)frame=explicitFrame;
 const auto* entry=Find(key,frame);if(!entry){++pvzh1_actor_pose_diagnostic[2];if(cache.perTrack)++pvzh1_plant_pose_diagnostic[7];return false;}
 Reader r(entry->offset);const int baseX=int16_t(r.U16()),baseY=int16_t(r.U16());const unsigned segments=r.U16(),mode=r.U16();const bool flash=mode&1,perTrack=mode&2,direct=mode&4;const unsigned sampleBytes=direct?4:2;
 if(flashReady&&!flash){++pvzh1_actor_pose_diagnostic[3];return false;}
 const uint32_t* variantPalette=nullptr;
 if(cache.colorVariants){const unsigned filter=unsigned(int(actor->mFilterEffect)+1);variantPalette=filter?cache.variantColors[filter-1]:cache.flashNormal;if(!variantPalette)return false;}
 const int left=int16_t(r.U16()),top=int16_t(r.U16()),right=int16_t(r.U16()),bottom=int16_t(r.U16());
 // Actor translation is snapped only at the LCD raster boundary. Simulation,
 // animation clocks, authored curves and attachment updates remain original.
 const float px=(actor->mOverlayMatrix.m02+g->mTransX)*(480.f/1060.f),py=(actor->mOverlayMatrix.m12+g->mTransY)*(272.f/600.f);
 const int dx=int(px+(px<0?-.5f:.5f))-baseX,dy=int(py+(py<0?-.5f:.5f))-baseY;
 const auto clip=screen->MapRect(g->mClipRect);const int cx=std::max(0,clip.mX),cy=std::max(0,clip.mY),cr=std::min(480,clip.mX+clip.mWidth),cb=std::min(272,clip.mY+clip.mHeight);
 // Custom clips keep the original triangle-edge rounding. At LCD boundaries
 // crop the already sampled bitmap, including zombies entering the screen.
 if(!cache.extendedRuns&&(cx||cy||cr!=480||cb!=272)&&(left+baseX+dx<cx||top+baseY+dy<cy||right+baseX+dx>cr||bottom+baseY+dy>cb)){++pvzh1_actor_pose_diagnostic[6];return false;}
 // Mark this actor's coverage once, rather than once per authored track.
 // Attachments still draw outside the protected pixel scopes below, so their
 // own coverage and any raw access remain visible to the dirty canvas.
 h1DirtyCanvas.Mark(target,left+baseX+dx,top+baseY+dy,right-left,bottom-top);
 H1_BANK_PHASE_END(packetPhase);
 cache.busy=true;
 #ifdef PVZ_NATIVE_PROFILE
 PVZProfileScope replayOperations(PVZ_PROFILE_ACTOR_LOOP);
#endif
 for(unsigned s=0;s<segments;++s){int track,segmentX,segmentY;unsigned draw,runs,payload;r.Segment(cache.segmentOffsets,track,draw,runs,payload,segmentX,segmentY);const bool drawn=draw&1,compact=draw&32768;
  if(track==-2){
   // Offline empty ranges carry attachment order, not pixel payloads. Visit
   // only the track instances; never read/decode a shared empty geometry page.
   const unsigned first=payload&65535,last=first+(payload>>16);
   if(!flash||perTrack)for(unsigned i=first;i<last;++i){auto& t=actor->mTrackInstances[i];
    if((!perTrack||t.mRenderGroup==group)&&t.mAttachmentID!=ATTACHMENTID_NULL){H1_BANK_PHASE(attachmentPhase,5);cache.busy=false;AttachmentDraw(t.mAttachmentID,g,!drawn);cache.busy=true;}}
   continue;
  }
#ifdef PVZ_HOST_RUNNER
  if(pvzh1_host_replay_segment)pvzh1_host_replay_segment(int(actor->mReanimationType),mode,runs,payload,track>=0&&actor->mTrackInstances[track].mAttachmentID!=ATTACHMENTID_NULL,track<0||actor->mTrackInstances[track].mRenderGroup==group);
#endif
  Reader stream(r.offset);
  if(payload&0x80000000u){stream=Reader(cache.dataOffset+(payload&0x7fffffffu)+4);}else r.Skip(payload);const auto runStart=stream.offset;
  if((flash||perTrack)&&track>=0){if(track>=actor->mDefinition->mTracks.count){cache.busy=false;return false;}const auto& instance=actor->mTrackInstances[track];if(instance.mRenderGroup!=group)continue;
   if(flash&&!perTrack){const char* name=actor->mDefinition->mTracks.tracks[track].mName;
    if((mode&16)&&H1ActorPose::ZombieHatTrack(name)){const bool bucket=!std::strcmp(name,"anim_bucket");unsigned variant=0;
     if(instance.mImageOverride==(bucket?Sexy::IMAGE_REANIM_ZOMBIE_BUCKET2:Sexy::IMAGE_REANIM_ZOMBIE_CONE2))variant=1;
     else if(instance.mImageOverride==(bucket?Sexy::IMAGE_REANIM_ZOMBIE_BUCKET3:Sexy::IMAGE_REANIM_ZOMBIE_CONE3))variant=2;
     if(variant!=((draw>>8)&3))continue;
    }else if(bool(instance.mImageOverride)!=drawn)continue;
   }
  }
  if(runs){H1_BANK_PHASE(pixelPhase,4);H1CanvasProtectedPixels drawing(target);auto* pixels=target->mBits.get();
   if(cache.denseTiles){DrawDenseTiles(stream,runs,pixels,baseX+dx+segmentX,baseY+dy+segmentY,cx,cy,cr,cb);}
   else if(cache.extendedRuns){DrawExtendedRuns(stream,runs,pixels,baseX+dx+segmentX,baseY+dy+segmentY,cx,cy,cr,cb);}
   else if(cache.linearRuns&&left+baseX+dx>=cx&&top+baseY+dy>=cy&&right+baseX+dx<=cr&&bottom+baseY+dy<=cb){
    DrawLinearSegment(cache.pages,stream.offset,runs,pixels,baseX+dx+segmentX+(baseY+dy+segmentY)*480);
   }else {if(cache.linearRuns)++pvzh1_linear_actor_diagnostic[2];for(unsigned i=0;i<runs;++i){int x,y;unsigned flags;stream.Run(compact,x,y,flags,cache.alignedRuns,cache.linearRuns,left);x+=baseX+dx+segmentX;y+=baseY+dy+segmentY;const unsigned n=flags&32767;
    if(y<cy||y>=cb||x>=cr||x+int(n)<=cx){stream.Skip(n*sampleBytes);continue;}
    unsigned first=std::max(0,cx-x),last=std::min(int(n),cr-x);stream.Skip(first*sampleBytes);auto* dest=pixels+y*480+x+first;
    const uint32_t* colors=cache.colorVariants?variantPalette:flash?cache.flashNormal:cache.palette;
    if(cache.palettePages){unsigned left=last-first;const bool opaque=flags&32768;
     auto emit=[&](unsigned id) __attribute__((always_inline)) {const uint32_t literal=cache.palettePages[id>>10][id&1023];
      if(opaque)*dest++=literal;else{const uint32_t p=*dest,a=literal>>24;*dest++=0xff000000u|((literal&0xffffffu)+((((p&0xff00ffu)*a)>>8)&0xff00ffu)+((((p&0xff00u)*a)>>8)&0xff00u));}};
     while(left){const uint8_t* bytes;unsigned count=stream.TakeSamples(left,2,bytes);if(!count){emit(stream.U16());--left;continue;}for(unsigned j=0;j<count;++j,bytes+=2)emit(LE16(bytes));left-=count;}
    }
    else if(direct&&cache.alignedRuns){unsigned left=last-first;
     while(left){const uint8_t* bytes;const unsigned count=stream.TakeSamples(left,4,bytes);const auto* literals=reinterpret_cast<const uint32_t*>(bytes);
      if(flags&32768){H1CopyPixels(dest,literals,count);dest+=count;}
      else for(unsigned j=0;j<count;++j){const uint32_t literal=literals[j],p=*dest,a=literal>>24;*dest++=0xff000000u|((literal&0xffffffu)+((((p&0xff00ffu)*a)>>8)&0xff00ffu)+((((p&0xff00u)*a)>>8)&0xff00u));}
      left-=count;
     }
    }
    else if(colors||direct){unsigned left=last-first;const bool opaque=flags&32768;
     auto emit=[&](uint32_t v) __attribute__((always_inline)) {const uint32_t literal=direct?v:colors[v];if(opaque)*dest++=literal;else{const uint32_t p=*dest,a=literal>>24;*dest++=0xff000000u|((literal&0xffffffu)+((((p&0xff00ffu)*a)>>8)&0xff00ffu)+((((p&0xff00u)*a)>>8)&0xff00u));}};
     while(left){const uint8_t* bytes;const unsigned count=stream.TakeSamples(left,sampleBytes,bytes);if(!count){emit(direct?stream.U32():stream.U16());--left;continue;}for(unsigned j=0;j<count;++j,bytes+=sampleBytes)emit(direct?LE32(bytes):LE16(bytes));left-=count;}
    }
    else if(flags&32768){for(unsigned j=first;j<last;++j){unsigned id=stream.U16();*dest++=FlashColor(id,0);}}
    else for(unsigned j=first;j<last;++j){unsigned id=stream.U16();const uint32_t literal=flash?FlashColor(id,0):cache.palette[id],p=*dest,inverse=literal>>24;
     const uint32_t rb=(((p&0xff00ffu)*inverse)>>8)&0xff00ffu,green=(((p&0xff00u)*inverse)>>8)&0xff00u;
     *dest++=0xff000000u|((literal&0xffffffu)+(rb|green));}
    stream.Skip((n-last)*sampleBytes);
   }}
   if(flash&&flashShade){Reader add(runStart);for(unsigned i=0;i<runs;++i){int x,y;unsigned flags;add.Run(compact,x,y,flags);x+=baseX+dx+segmentX;y+=baseY+dy+segmentY;unsigned n=flags&32767;
     if(y<cy||y>=cb||x>=cr||x+int(n)<=cx){add.Skip(n*2);continue;}unsigned first=std::max(0,cx-x),last=std::min(int(n),cr-x);add.Skip(first*2);auto* dest=pixels+y*480+x+first;
     auto emit=[&](unsigned id) __attribute__((always_inline)) {uint32_t v=FlashColor(id,flashShade),p=*dest;unsigned red=((p>>16)&255)+((v>>16)&255),green=((p>>8)&255)+((v>>8)&255),blue=(p&255)+(v&255);*dest++=(p&0xff000000u)|(std::min(255u,red)<<16)|(std::min(255u,green)<<8)|std::min(255u,blue);};
     unsigned left=last-first;while(left){const uint8_t* bytes;unsigned count=add.TakeSamples(left,2,bytes);if(!count){emit(add.U16());--left;continue;}for(unsigned j=0;j<count;++j,bytes+=2)emit(LE16(bytes));left-=count;}add.Skip((n-last)*2);
   }}
  }
  if(track>=0&&(!flash||perTrack)&&actor->mTrackInstances[track].mAttachmentID!=ATTACHMENTID_NULL){H1_BANK_PHASE(attachmentPhase,5);cache.busy=false;AttachmentDraw(actor->mTrackInstances[track].mAttachmentID,g,!drawn);cache.busy=true;}
 }
 {H1_BANK_PHASE(commitPhase,6);target->BitsChanged();Sexy::H1ScreenNoteDraw(screen);}
 cache.busy=false;++pvzh1_actor_pose_diagnostic[5];
 if(activeCache==&blinkCache)++pvzh1_dense_replay_diagnostic[0];
 else if(activeCache==&sunCache)++pvzh1_dense_replay_diagnostic[1];
 else if(activeCache==&zombieBlendCache)++pvzh1_dense_replay_diagnostic[2];
 if(int(actor->mReanimationType)>=0&&actor->mReanimationType<NUM_REANIMS)++pvzh1_baked_actor_diagnostic[actor->mReanimationType];
 if(cache.extendedRuns)++pvzh1_extra_actor_diagnostic[1];
 if(perTrack)++pvzh1_plant_pose_diagnostic[5];
 ++pvzh1_actor_pose_diagnostic[actor->mReanimationType==REANIM_SUN?12:8+unsigned(actor->mReanimationType==REANIM_SUNFLOWER?1:actor->mReanimationType==REANIM_ZOMBIE?2:actor->mReanimationType==REANIM_SODROLL?3:0)];return true;
}
__attribute__((optimize("Os"))) static bool NoteFallback(Reanimation* actor){
 if(baseCache.enabled&&actor&&!actor->mDead&&int(actor->mReanimationType)>=0&&actor->mReanimationType<NUM_REANIMS)++pvzh1_live_actor_diagnostic[actor->mReanimationType];
 if(!actor||actor->mDead||!gLawnApp||!gLawnApp->mBoard||gLawnApp->mGameScene!=SCENE_PLAYING)return false;
 const auto type=actor->mReanimationType;auto& d=pvzh1_actor_fallback_diagnostic;++d[0];++d[type==REANIM_PEASHOOTER?1:type==REANIM_SUNFLOWER?2:type==REANIM_WALLNUT?3:type==REANIM_SUN?4:5];
 unsigned flags=unsigned(actor->mEnableExtraAdditiveDraw)|(unsigned(actor->mEnableExtraOverlayDraw)<<1)|(unsigned(int(actor->mFilterEffect)+1)<<2),blend=0;
 for(int i=0;i<actor->mDefinition->mTracks.count;++i)blend=std::max<unsigned>(blend,actor->mTrackInstances[i].mBlendCounter);flags|=blend<<16;
 if(blend)++d[7];if(actor->mColorOverride!=Sexy::Color::White)++d[8];if(flags&1)++d[9];if(flags&2)++d[10];if(actor->mFilterEffect!=FILTER_EFFECT_NONE)++d[11];
 if(H1ActorPose::LinearBits(actor->mOverlayMatrix.m00)!=0x3f800000u||H1ActorPose::LinearBits(actor->mOverlayMatrix.m11)!=0x3f800000u||H1ActorPose::LinearBits(actor->mOverlayMatrix.m01)||H1ActorPose::LinearBits(actor->mOverlayMatrix.m10))++d[12];
 return false;
}
static bool CombatDraw(uint64_t key,unsigned frame,Sexy::Graphics* g,int x,int y,const Sexy::Rect* physicalClip=nullptr){
 if(!combatEnabled||!combatCache||!combatCache->entries||!g)return false;
 auto*screen=Sexy::H1GetPhysicalScreen(dynamic_cast<Sexy::GLImage*>(g->mDestImage));
 if(!screen||!screen->Active()||!screen->mGLInterface->mTransformStack.empty())return false;
 auto*target=screen->Target();if(target!=h1DirtyCanvas.owner||!target->mBits)return false;
 CacheScope scope(combatCache);auto*e=Find(key,frame);if(!e)throw std::runtime_error("Missing offline combat sprite");
 Reader r(e->offset);r.Skip(4);unsigned segments=r.U16();r.U16();int left=int16_t(r.U16()),top=int16_t(r.U16()),right=int16_t(r.U16()),bottom=int16_t(r.U16());
 const auto clip=physicalClip?*physicalClip:screen->MapRect(g->mClipRect);int cx=std::max(0,clip.mX),cy=std::max(0,clip.mY),cr=std::min(480,clip.mX+clip.mWidth),cb=std::min(272,clip.mY+clip.mHeight);
 cache.busy=true;{
 H1CanvasKnownDraw drawing(target,x+left,y+top,right-left,bottom-top);
 for(unsigned s=0;s<segments;++s){r.U16();r.U16();unsigned runs=r.U32(),payload=r.U32();int sx=int16_t(r.U16()),sy=int16_t(r.U16());Reader stream(r.offset);
  if(payload&0x80000000u){stream=Reader(cache.dataOffset+(payload&0x7fffffffu));stream.U32();}else r.Skip(payload);
  DrawExtendedRuns(stream,runs,target->mBits.get(),x+sx,y+sy,cx,cy,cr,cb);
 }}cache.busy=false;target->BitsChanged();screen->NoteDraw();return true;
}
bool H1CombatSpritesPrepare(){
 if(!combatEnabled)return true;
 if(!hitLive){hitCapacity=64;hitLive=new(std::nothrow) HitLive[hitCapacity]{};if(!hitLive){hitCapacity=0;return false;}}
 auto prepare=[](Cache*& c,const char*name,bool stream){if(!c){c=new(std::nothrow) Cache;if(!c)return false;c->streamPreferred=stream;c->decodeLimit=32;}
  CacheScope scope(c);if(!cache.tried){cache.tried=cache.busy=true;if(!Load(name)){Drop();cache.busy=false;return false;}cache.busy=false;}
  if(cache.entries&&stream){cache.busy=true;const bool ready=ReserveDecoded(cache);cache.busy=false;if(!ready)return false;}
  return cache.entries!=nullptr;};
 const bool okay=prepare(combatCache,"combat-sprites.h1ap",false)&&prepare(hatsCache,"zombie-hats.h1ap",true);
 pvzh1_combat_sprite_diagnostic[0]=(combatCache?Resident(*combatCache):0)+(hatsCache?Resident(*hatsCache):0)+hitCapacity*sizeof(HitLive);CountResident();return okay;
}
bool H1CombatFlatDraw(const Sexy::H1ImageReplayContext& c){
 using namespace Sexy;using namespace H1Combat;
 if(!combatEnabled||!combatCache||c.mode!=Graphics::DRAWMODE_NORMAL||c.color!=Color::White)return false;
 Image* images[]={IMAGE_PROJECTILEPEA,IMAGE_PEA_SHADOWS,IMAGE_PLANTSHADOW,IMAGE_PLANTSHADOW2};unsigned kind=0;while(kind<4&&images[kind]!=c.source)++kind;if(kind==4)return false;
 const auto&m=c.matrix;if(m.m01!=0||m.m10!=0||m.m20!=0||m.m21!=0||m.m22!=1)return false;
 unsigned scale=0;while(scale<std::size(Scales)&&(std::fabs(m.m00-480.f/1060.f*Scales[scale].x)>.000001f||std::fabs(m.m11-272.f/600.f*Scales[scale].y)>.000001f))++scale;if(scale==std::size(Scales))return false;
 int w=c.source->GetCelWidth(),h=c.source->GetCelHeight();if(c.rect.mWidth!=w||c.rect.mHeight!=h||w<=0||h<=0||c.rect.mX<0||c.rect.mY<0||c.rect.mX%w||c.rect.mY%h)return false;
 unsigned cel=(c.rect.mY/h)*c.source->mNumCols+c.rect.mX/w;
 float px=m.m00*(c.center?-w*.5f:0.f)+m.m02+c.x,py=m.m11*(c.center?-h*.5f:0.f)+m.m12+c.y;if(!std::isfinite(px)||!std::isfinite(py))return false;
 auto*device=c.destination->mApp?c.destination->mApp->mGLInterface.get():nullptr;auto*screen=device?H1GetPhysicalScreen(device->mScreenImage.get()):nullptr;if(!screen||screen->Target()!=c.destination)return false;
 Graphics g(screen);
 // LCD translation snaps only the raster, exactly as the actor replay does.
 // Simulation coordinates and the original physical clip remain unchanged.
 if(!CombatDraw(FlatKey|kind,(cel<<8)|scale,&g,int(std::lround(px)),int(std::lround(py)),&c.clip))return false;
 c.destination->BitsChanged(); // Original image wrapper commits two triangles.
 c.source->mDrawn=true;H1ImageCacheTouch(dynamic_cast<MemoryImage*>(c.source));++pvzh1_combat_sprite_diagnostic[1];return true;
}
#ifdef PVZ_HOST_RUNNER
static bool directPlantShadowEnabled=true;
void H1DirectPlantShadowTestEnable(bool enabled){directPlantShadowEnabled=enabled;}
#endif
static bool DirectPlantShadow(Sexy::Graphics* g,Sexy::Image* image,float x,float y,int cel,float scaleX,float scaleY){
 using namespace Sexy;using namespace H1Combat;
 if(!image||(image!=IMAGE_PLANTSHADOW&&image!=IMAGE_PLANTSHADOW2))return false;
#ifdef PVZ_HOST_RUNNER
 if(!directPlantShadowEnabled)return false;
#endif
 if(!combatEnabled||!combatCache||!g||g->mScaleX!=1||g->mScaleY!=1||g->mColorizeImages||g->mDrawMode!=Graphics::DRAWMODE_NORMAL)return false;
 unsigned scale=0;while(scale<std::size(Scales)&&(scaleX!=Scales[scale].x||scaleY!=Scales[scale].y))++scale;
 if(scale==std::size(Scales)||cel<0||cel>=image->mNumCols)return false;
 auto* screen=H1GetPhysicalScreen(dynamic_cast<GLImage*>(g->mDestImage));
 if(!screen||!screen->Active()||!screen->mGLInterface->mTransformStack.empty())return false;
 // Same LCD coordinates as the public image wrapper, including its distinct
 // unit-scale corner and scaled-centre conventions. Only matrix setup and
 // the subsequent recognition of this already-known shadow are bypassed.
 const float sx=480.f/1060.f,sy=272.f/600.f;
 float px=(x+g->mTransX)*sx,py=(y+g->mTransY)*sy;
 if(scaleX!=1||scaleY!=1){
  const float hw=image->GetCelWidth()*.5f,hh=image->GetCelHeight()*.5f;
  const float offset=gSexyAppBase->Is3DAccelerated()?-.5f:0.f;
  px=(sx*scaleX)*-hw+((hw+x+g->mTransX)+offset)*sx;
  py=(sy*scaleY)*-hh+((hh+y+g->mTransY)+offset)*sy;
 }
 if(!std::isfinite(px)||!std::isfinite(py))return false;
 const Rect clip=screen->MapRect(g->mClipRect);
 if(!CombatDraw(FlatKey|(image==IMAGE_PLANTSHADOW?2u:3u),(unsigned(cel)<<8)|scale,g,int(std::lround(px)),int(std::lround(py)),&clip))return false;
 auto* target=screen->Target();target->BitsChanged();screen->NoteDraw();
 image->mDrawn=true;H1ImageCacheTouch(dynamic_cast<MemoryImage*>(image));
 ++pvzh1_combat_sprite_diagnostic[1];++pvzh1_shadow_pose_diagnostic[2];return true;
}
static HitLive* HitSlot(PvzpParticleSystem* s,bool create){
 if(!hitLive)return nullptr;HitLive* empty=nullptr;for(unsigned i=0;i<hitCapacity;++i){if(hitLive[i].system==s)return hitLive+i;if(!hitLive[i].system&&!empty)empty=hitLive+i;}
 if(!create)return nullptr;
 if(!empty){unsigned n=hitCapacity*2;auto*next=new(std::nothrow) HitLive[n]{};if(!next)throw std::bad_alloc();std::copy_n(hitLive,hitCapacity,next);delete[]hitLive;empty=next+hitCapacity;hitLive=next;hitCapacity=n;}
 *empty={s,0,s->mEmitterList.mHead?unsigned(s->mEmitterList.mHead->mValue)%H1Combat::Variants:0};
 for(auto*n=s->mEmitterList.mHead;n;n=n->mNext){auto*e=s->mParticleHolder->mEmitters.DataArrayGet(unsigned(n->mValue));for(auto*p=e->mParticleList.mHead;p;p=p->mNext){auto*v=s->mParticleHolder->mParticles.DataArrayTryToGet(unsigned(p->mValue));if(v)empty->age=std::max(empty->age,unsigned(std::max(0,v->mParticleAge+1)));}}
 return empty;
}
void H1CombatHitBegin(PvzpParticleSystem* s){if(combatEnabled&&hitLive&&s->mEffectType==PARTICLE_PEA_SPLAT)if(auto*l=HitSlot(s,true))l->age=0;}
void H1CombatHitUpdate(PvzpParticleSystem* s){if(combatEnabled&&hitLive&&s->mEffectType==PARTICLE_PEA_SPLAT&&!s->mDontUpdate)if(auto*l=HitSlot(s,true))++l->age;}
void H1CombatHitForget(PvzpParticleSystem* s){if(auto*l=HitSlot(s,false))*l={};}
bool H1CombatHitDraw(PvzpParticleSystem* s,Sexy::Graphics* g){
 if(!combatEnabled||!combatCache||s->mEffectType!=PARTICLE_PEA_SPLAT)return false;if(s->mDead)return true;
 auto*n=s->mEmitterList.mHead;if(!n)return true;auto*e=s->mParticleHolder->mEmitters.DataArrayGet(unsigned(n->mValue));bool add=e->mExtraAdditiveDrawOverride;
 for(auto*p=n;p;p=p->mNext){auto*v=s->mParticleHolder->mEmitters.DataArrayGet(unsigned(p->mValue));if(v->mImageOverride||v->mFrameOverride!=-1||v->mColorOverride!=Sexy::Color::White||v->mExtraAdditiveDrawOverride!=add)return false;}
 auto*l=HitSlot(s,true);if(!l)return false;
 int x=int(std::lround((e->mSystemCenter.x+g->mTransX)*480.f/1060)),y=int(std::lround((e->mSystemCenter.y+g->mTransY)*272.f/600));
 bool drawn=CombatDraw(H1Combat::HitKey|l->variant,std::min(l->age,H1Combat::HitLastAge)*2+add,g,x,y);if(drawn)++pvzh1_combat_sprite_diagnostic[2];return drawn;
}
void H1CombatFinishLoading(){
 bool alive[NUM_REANIMS]{};if(gLawnApp&&gLawnApp->mEffectSystem)for(auto*r:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)if(!r->mDead&&unsigned(r->mReanimationType)<NUM_REANIMS)alive[r->mReanimationType]=true;
 unsigned freed=0;for(unsigned t=0;t<NUM_REANIMS;++t)if(!alive[t]&&plantCaches[t])for(auto*c:plantCaches[t]->items)if(c&&!c->busy)freed+=DropDecoded(*c);
 pvzh1_combat_sprite_diagnostic[4]+=freed;CountResident();
}
#ifdef PVZ_HOST_RUNNER
void H1CombatSpritesTestEnable(bool enabled){combatEnabled=enabled;}
bool H1CombatSpritesTestDraw(uint64_t key,unsigned frame,Sexy::Graphics*g,int x,int y){return CombatDraw(key,frame,g,x,y);}
void H1CombatSpritesTestVariant(PvzpParticleSystem*s,unsigned variant){auto*l=HitSlot(s,true);if(!l)throw std::runtime_error("Combat test tracker unavailable");l->variant=variant;l->age=0;}
#endif
static void DrawZombieHats(Reanimation* actor,Sexy::Graphics* g,int group){
 if(!actor||actor->mDead||actor->mReanimationType!=REANIM_ZOMBIE)return;
 ReanimatorFrameTime time;actor->GetFrameTime(&time);
 // Use the same half-frame sample as the retained body, so a hat cannot
 // advance independently of the head underneath it.
 if(time.mAnimFrameAfterInt!=time.mAnimFrameBeforeInt){
  if(time.mFraction>=.75f){time.mAnimFrameBeforeInt=time.mAnimFrameAfterInt;time.mFraction=0;}
  else time.mFraction=time.mFraction>=.25f?.5f:0.f;
 }
 PvzpTriangleGroup triangles;
 for(int i=0;i<actor->mDefinition->mTracks.count;++i){
  auto& track=actor->mTrackInstances[i];
  if(track.mRenderGroup!=group||!H1ActorPose::ZombieHatTrack(actor->mDefinition->mTracks.tracks[i].mName))continue;
  bool drawn=false;bool offline=false;
  if(combatEnabled&&hatsCache&&hatsCache->entries){
   const bool bucket=!std::strcmp(actor->mDefinition->mTracks.tracks[i].mName,"anim_bucket");unsigned stage=0;
   if(track.mImageOverride==(bucket?Sexy::IMAGE_REANIM_ZOMBIE_BUCKET2:Sexy::IMAGE_REANIM_ZOMBIE_CONE2))stage=1;
   else if(track.mImageOverride==(bucket?Sexy::IMAGE_REANIM_ZOMBIE_BUCKET3:Sexy::IMAGE_REANIM_ZOMBIE_CONE3))stage=2;
   const bool canonical=!track.mImageOverride||stage||track.mImageOverride==(bucket?Sexy::IMAGE_REANIM_ZOMBIE_BUCKET1:Sexy::IMAGE_REANIM_ZOMBIE_CONE1);
   if(canonical){CacheScope scope(hatsCache);offline=DrawFromCache(actor,g,group,H1Combat::HatKey|(unsigned(bucket)*3+stage),time.mAnimFrameBeforeInt*2+(time.mFraction>=.5f));}
   if(offline){drawn=true;++pvzh1_combat_sprite_diagnostic[3];}
  }
  if(!offline)drawn=actor->DrawTrack(g,i,group,&triangles,&time);
  if(track.mAttachmentID!=ATTACHMENTID_NULL){triangles.DrawGroup(g);AttachmentDraw(track.mAttachmentID,g,!drawn);}
 }
 triangles.DrawGroup(g);
}
bool H1ActorPoseDraw(Reanimation* actor,Sexy::Graphics* g,int group){
 H1ResolveActorOverlay(actor);
 // Quantize only the raster sample, never a simulation matrix or an update.
 // Destruction restores the exact original bits before returning to callers.
 struct RasterScale {
  Reanimation* actor;float x,y;bool blink=false;
  explicit RasterScale(Reanimation* r):actor(r),x(r?r->mOverlayMatrix.m00:0),y(r?r->mOverlayMatrix.m11:0){
   if(!r||H1ActorPose::LinearBits(r->mOverlayMatrix.m01)||H1ActorPose::LinearBits(r->mOverlayMatrix.m10))return;
   if(blinkCache.enabled&&blinkCache.entries&&(r->mReanimationType==REANIM_PEASHOOTER||r->mReanimationType==REANIM_SUNFLOWER)&&r->mFrameStart==1&&r->mFrameCount==3&&std::abs(x-1.f)<.00001f&&y>=.5f&&y<=1.5f){
    r->mOverlayMatrix.m00=1;r->mOverlayMatrix.m11=float(unsigned(y*64+.5f))/64;blink=true;
   }else if(sunCache.enabled&&sunCache.entries&&r->mReanimationType==REANIM_SUN&&((x>=.25f&&x<=1.000001f)||std::abs(x-2)<.000001f)&&std::abs(x-y)<.000001f){
    r->mOverlayMatrix.m00=r->mOverlayMatrix.m11=float(std::max(13u,unsigned(x*50+.5f)))/50;
   }
  }
  ~RasterScale(){if(actor){actor->mOverlayMatrix.m00=x;actor->mOverlayMatrix.m11=y;}}
 } rasterScale(actor);
#ifdef PVZ_NATIVE_PROFILE
 PVZProfileScope dispatchProfile(PVZ_PROFILE_ACTOR_DISPATCH);
#endif
 const auto type=actor?actor->mReanimationType:REANIM_NONE;
 BankDrawScope bankDrawScope{int(type)};
 bool useExtra=H1UseExtraActorBank(type);
#ifdef PVZ_HOST_RUNNER
 useExtra|=extraTestAll;
#endif
 if(H1ActorPose::ExtraType(type)&&!useExtra&&!H1UsePolePreviewBank(type))return false;
 if(type==REANIM_POLEVAULTER){CacheScope scope(&poleIdleCache);if(DrawFromCache(actor,g,group))return true;}
 if(type==REANIM_ZOMBIE&&zombieBlendCache.enabled&&zombieBlendCache.entries){CacheScope scope(&zombieBlendCache);if(DrawFromCache(actor,g,group))return true;}
 if(rasterScale.blink){CacheScope scope(&blinkCache);if(DrawFromCache(actor,g,group))return true;}
 if(type==REANIM_SUNFLOWER&&actor->mEnableExtraAdditiveDraw){CacheScope scope(&glowCache);if(DrawFromCache(actor,g,group))return true;}
 if(type==REANIM_PEASHOOTER||type==REANIM_SUNFLOWER){CacheScope scope(&normalCache);if(DrawFromCache(actor,g,group)){++pvzh1_normal_actor_diagnostic[1];return true;}++pvzh1_normal_actor_diagnostic[2];}
 // Keep the existing transition/flash-specialized packets where available.
 {CacheScope scope(type==REANIM_SUN?&sunCache:type==REANIM_ZOMBIE?&zombieCache:type==REANIM_SODROLL?&sodCache:&baseCache);if(DrawFromCache(actor,g,group)){DrawZombieHats(actor,g,group);return true;}}
 const int filter=actor?int(actor->mFilterEffect)+1:-1;
 if(int(type)<0||int(type)>=NUM_REANIMS||filter<0||filter>=4||!plantCaches[int(type)])return NoteFallback(actor);
 auto* selected=plantCaches[int(type)]->items[filter];if(!selected)for(auto* p:plantCaches[int(type)]->items)if(p&&p->colorVariants){selected=p;break;}if(!selected||!selected->entries)return NoteFallback(actor);
 CacheScope scope(selected);++pvzh1_plant_pose_diagnostic[4];return DrawFromCache(actor,g,group)||NoteFallback(actor);
}
bool H1SodOfflineBegin(Board* board){
 CacheScope scope(&sodCache);
 if(!cache.enabled||!cache.entries||!Find(0x534f44524f4c4c31ull,0)||!board||board->mApp->mGameMode!=GAMEMODE_ADVENTURE||!board->mApp->IsFirstTimeAdventureMode()||board->mX!=130||board->mY||(board->mLevel!=1&&board->mLevel!=2&&board->mLevel!=4))return false;
 for(unsigned frame=1;frame<=90;++frame)if(!Find(0x534f44524f4c4c31ull,frame))return false;
 sodBoard=board;sodStart=board->mCutScene->mCutsceneTime;pvzh1_sod_offline_diagnostic[0]=1;++pvzh1_sod_offline_diagnostic[3];return true;
}
bool H1SodOfflineDraw(Board* board,Sexy::Graphics* g){
 CacheScope scope(&sodCache);
 if(!cache.enabled||cache.busy||!cache.entries||board!=sodBoard||!board||!board->mCutScene||!g)return false;
 int age=board->mCutScene->mCutsceneTime-sodStart;if(age<0)return false;if(age>2250){H1ActorPoseFinishSod();return false;}
 auto* screen=Sexy::H1GetPhysicalScreen(dynamic_cast<Sexy::GLImage*>(g->mDestImage));if(!screen||!screen->Active()||g->mTransX!=130||g->mTransY||g->mScaleX!=1||g->mScaleY!=1)return false;
 auto* target=screen->Target();if(target!=h1DirtyCanvas.owner||!target->mBits)return false;
 unsigned frame=std::min(90,(age+12)/25);const auto* entry=Find(0x534f44524f4c4c31ull,frame);if(!entry){++pvzh1_sod_offline_diagnostic[2];return false;}
 auto clip=screen->MapRect(g->mClipRect);int cx=std::max(0,clip.mX),cy=std::max(0,clip.mY),cr=std::min(480,clip.mX+clip.mWidth),cb=std::min(272,clip.mY+clip.mHeight);
 const int count=board->mLevel==1?1:2;
 for(int instance=0;instance<count;++instance){int lx=board->mLevel==4?-3:0,ly=board->mLevel==1?0:board->mLevel==2?(instance?111:-102):(instance?203:-198);
  const int dx=(lx*480+(lx<0?-530:530))/1060,dy=(ly*272+(ly<0?-300:300))/600;
  Reader r(entry->offset);r.U16();r.U16();unsigned segments=r.U16();r.U16();int left=int16_t(r.U16()),top=int16_t(r.U16()),right=int16_t(r.U16()),bottom=int16_t(r.U16());cache.busy=true;
  H1CanvasKnownDraw drawing(target,left+dx,top+dy,right-left,bottom-top);auto* pixels=target->mBits.get();
  for(unsigned s=0;s<segments;++s){r.U16();const bool compact=r.U16()&32768;unsigned runs=r.U32();r.U32();for(unsigned i=0;i<runs;++i){int x,y;unsigned flags;r.Run(compact,x,y,flags);x+=dx;y+=dy;unsigned n=flags&32767;
    if(y<cy||y>=cb||x>=cr||x+int(n)<=cx){r.Skip(n*2);continue;}unsigned first=std::max(0,cx-x),last=std::min(int(n),cr-x);r.Skip(first*2);auto* dest=pixels+y*480+x+first;
    for(unsigned j=first;j<last;++j){uint32_t literal=cache.palette[r.U16()];if(flags&32768)*dest++=literal;else{uint32_t p=*dest,a=literal>>24;*dest++=0xff000000u|((literal&0xffffffu)+((((p&0xff00ffu)*a)>>8)&0xff00ffu)+((((p&0xff00u)*a)>>8)&0xff00u));}}r.Skip((n-last)*2);
   }}cache.busy=false;
 }
 target->BitsChanged();Sexy::H1ScreenNoteDraw(screen);++pvzh1_sod_offline_diagnostic[1];pvzh1_sod_offline_diagnostic[4]=age;pvzh1_sod_offline_diagnostic[5]=frame;return true;
}
extern "C" size_t pvzh1_actor_pose_release_pressure(size_t){
 // Encoded frames and palette stay resident. Releasing only idle decoded
 // pages needs no allocation or file IO, including while a save is growing.
 size_t bytes=DropDecoded(baseCache);if(!bytes)bytes=DropDecoded(sunCache);
 if(!bytes)bytes=DropDecoded(poleIdleCache);
 if(!bytes)bytes=DropDecoded(zombieCache);if(!bytes)bytes=DropDecoded(sodCache);
 for(auto* banks:plantCaches)if(!bytes&&banks)for(auto* p:banks->items)if(!bytes&&p)bytes=DropDecoded(*p);
 if(bytes){++pvzh1_plant_pose_diagnostic[10];pvzh1_plant_pose_diagnostic[11]+=bytes;CountResident();}return bytes;
}
void H1ActorPoseFinishSod(){CacheScope scope(&sodCache);if(cache.busy)return;pvzh1_actor_lifetime_diagnostic[4]+=Resident(cache);Drop();sodBoard=nullptr;pvzh1_sod_offline_diagnostic[0]=0;CountResident();}
void H1ActorPoseForget(Reanimation* actor){
 H1DeferredOverlayForget(actor);
 if(frameTimes)for(auto& p:frameTimes[FrameBucket(actor)])if(p.actor==actor){p.actor=nullptr;++pvzh1_frame_time_diagnostic[3];}
 if(activeTracks)for(auto& p:activeTracks[FrameBucket(actor)])if(p.actor==actor)p.actor=nullptr;
 for(bool perTrack:{false,true}){auto** link=&signatures[((uintptr_t(actor)>>4)^unsigned(perTrack))&127];while(*link){auto* p=*link;if(p->actor!=actor){link=&p->next;continue;}*link=p->next;signatureBytes-=sizeof(SignatureMemo)+p->count*4;delete[] p->words;delete p;++pvzh1_actor_lifetime_diagnostic[5];}}
 for(auto& b:blends)if(b.actor==actor)b={};
 const int type=actor?int(actor->mReanimationType):-1;
 if(type>=0&&type<NUM_REANIMS&&plantCaches[type]&&gLawnApp&&gLawnApp->mEffectSystem&&gLawnApp->mEffectSystem->mReanimationHolder){
  bool alive=false;for(auto*other:gLawnApp->mEffectSystem->mReanimationHolder->mReanimations)if(other!=actor&&!other->mDead&&int(other->mReanimationType)==type){alive=true;break;}
  if(!alive){size_t freed=0;for(auto*p:plantCaches[type]->items)if(p&&!p->busy){CacheScope scope(p);
    if(type==REANIM_READYSETPLANT||type==REANIM_FINAL_WAVE||type==REANIM_ZOMBIES_WON||type==REANIM_BOSS||type==REANIM_CRAZY_DAVE||type==REANIM_ZOMBIE_HAND){freed+=Resident(cache);Drop();cache.tried=false;}
    else freed+=DropDecoded(cache);
   }if(freed){pvzh1_extra_actor_diagnostic[3]+=freed;CountResident();}}
 }
}
void H1ActorPoseForgetDefinition(ReanimatorDefinition* definition){
 H1DeferredOverlayForgetDefinition(definition);
 for(auto*& head:baseInverses){auto** link=&head;while(*link){auto* p=*link;if(p->definition!=definition){link=&p->next;continue;}*link=p->next;signatureBytes-=sizeof(BasePoseMemo);--pvzh1_base_inverse_diagnostic[2];pvzh1_base_inverse_diagnostic[3]-=sizeof(BasePoseMemo);delete p;++pvzh1_actor_lifetime_diagnostic[6];}}
}
extern "C" void pvzh1_actor_pose_shutdown(){H1DeferredOverlayShutdown();delete[]activeTracks;activeTracks=nullptr;delete[]frameTimes;frameTimes=nullptr;for(auto** p:{&combatCache,&hatsCache})if(*p){{CacheScope scope(*p);Drop();}delete *p;*p=nullptr;}delete[]hitLive;hitLive=nullptr;hitCapacity=0;pvzh1_combat_sprite_diagnostic[0]=0;DropSignatures();for(auto*& banks:plantCaches)if(banks){for(auto* p:banks->items)if(p){{CacheScope scope(p);Drop();}delete p;}delete banks;banks=nullptr;}if(shadowCache){{CacheScope scope(shadowCache);Drop();}delete shadowCache;shadowCache=nullptr;pvzh1_shadow_pose_diagnostic[1]=0;}for(auto* p:{&baseCache,&sunCache,&normalCache,&zombieCache,&sodCache,&glowCache,&poleIdleCache,&blinkCache,&zombieBlendCache}){CacheScope scope(p);Drop();cache.tried=false;}pvzh1_normal_actor_diagnostic[0]=0;CountResident();sodBoard=nullptr;for(auto& b:blends)b={};}
#ifdef PVZ_HOST_RUNNER
void H1ExtraPoseTestIdleEnable(bool enabled){poleIdleCache.enabled=enabled;}
void H1ExtraPoseTestAllEnable(bool enabled){extraTestAll=enabled;}
void H1ActorActiveTracksTestEnable(bool enabled){activeTracksEnabled=enabled;if(activeTracks)for(unsigned bucket=0;bucket<128;++bucket)for(auto& p:activeTracks[bucket])p.generation=0;}
bool H1ActorPosePhaseTestPrepare(unsigned index){
 if(index>2)return false;Cache* caches[]={&baseCache,&zombieCache,&sodCache};const char* names[]={"pea-transitions.h1ap","zombie-hit.h1ap","sod-roll.h1ap"};CacheScope scope(caches[index]);
 if(!cache.tried){cache.tried=cache.busy=true;if(!Load(names[index])){Drop();++pvzh1_actor_pose_diagnostic[7];}cache.busy=false;}CountResident();return cache.entries!=nullptr;
}
void H1ActorMatrixTestEnable(bool enabled){attachmentMemoEnabled=enabled;}
void H1ActorPoseTestEnable(bool v){baseCache.enabled=sunCache.enabled=normalCache.enabled=zombieCache.enabled=sodCache.enabled=glowCache.enabled=poleIdleCache.enabled=blinkCache.enabled=zombieBlendCache.enabled=v;if(shadowCache)shadowCache->enabled=v;for(auto* banks:plantCaches)if(banks)for(auto* p:banks->items)if(p)p->enabled=v;}
void H1ActorPoseLegacyTestEnable(bool v){baseCache.enabled=sunCache.enabled=normalCache.enabled=zombieCache.enabled=sodCache.enabled=glowCache.enabled=poleIdleCache.enabled=blinkCache.enabled=zombieBlendCache.enabled=v;}
void H1ActorPosePageTestSlots(unsigned n){pageSlotLimit=std::max(1u,std::min(DecodedSlots,n));}
#endif
