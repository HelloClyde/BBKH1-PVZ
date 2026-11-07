/* Ordered original-shader literals, baked at the LCD resolution. There is no
 * texture sampling or geometry setup in replay. LGPL-3.0-or-later. */
#include "ActorPose.h"
#include "ActorPoseFormat.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CutScene.h"
#include "PvzpLib/Attachment.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/PixelCopy.h"
#include "graphics/ReanimTrack.h"
#include "Common.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdexcept>
#include <zlib.h>
extern "C" {volatile uint32_t pvzh1_actor_pose_diagnostic[16];}
extern "C" {volatile uint32_t pvzh1_sod_offline_diagnostic[8];}
extern "C" {volatile uint32_t pvzh1_actor_page_diagnostic[8];}
extern "C" {volatile uint32_t pvzh1_plant_pose_diagnostic[12];}
extern "C" {volatile uint32_t pvzh1_base_inverse_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_actor_fallback_diagnostic[64];}
extern "C" {volatile uint32_t pvzh1_shadow_pose_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_normal_actor_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_actor_lifetime_diagnostic[8];}
extern "C" {volatile uint32_t pvzh1_frame_time_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_linear_actor_diagnostic[4];}
extern "C" {volatile uint32_t pvzh1_active_track_diagnostic[4];}
namespace {
using namespace H1ActorPose;
struct Entry{uint64_t key;uint32_t frame,offset,bytes,type;};
struct EncodedPage{uint32_t offset,bytes,crc;};
constexpr unsigned DecodedSlots=128,InflateArena=49152;
#ifdef PVZ_HOST_RUNNER
unsigned pageSlotLimit=DecodedSlots;
#else
constexpr unsigned pageSlotLimit=DecodedSlots;
#endif
struct Cache{uint8_t** pages=nullptr;uint32_t* palette=nullptr;uint32_t** palettePages=nullptr;uint32_t* flashNormal=nullptr;uint32_t* variantColors[3]{};Entry* entries=nullptr;
 uint32_t size=0,npages=0,count=0,colors=0,flashOffset=0,flashColors=0,flashShades=0,dataOffset=0;
 EncodedPage* encoded=nullptr;uint8_t** blob=nullptr;uint16_t* slotForPage=nullptr;uint8_t* arena=nullptr;
 uint8_t* decoded[DecodedSlots]{};uint32_t decodedPage[DecodedSlots]{},stamp[DecodedSlots]{},clock=0,encodedBytes=0,blobCount=0,arenaUsed=0,rawCRC=0;
 z_stream inflater{};unsigned decodeLimit=DecodedSlots;bool compressed=false,inflateReady=false,tried=false,busy=false,enabled=true,perTrack=false,directLiterals=false,colorVariants=false,alignedRuns=false,linearRuns=false;} baseCache,sunCache,normalCache,zombieCache,sodCache,glowCache;
struct PlantBanks{Cache* items[4]{};};
Cache* activeCache=&baseCache;PlantBanks* plantCaches[NUM_REANIMS]{};Cache* shadowCache=nullptr;
#define cache (*activeCache)
struct CacheScope{Cache* previous;explicit CacheScope(Cache* value):previous(activeCache){activeCache=value;}~CacheScope(){activeCache=previous;}};
struct SignatureMemo{Reanimation* actor;SignatureMemo* next;uint32_t* words=nullptr;uint64_t key=0;unsigned count=0;bool perTrack;uint32_t stepRate=0;int stepFrames=0;double step=0;};
SignatureMemo* signatures[128]{};unsigned signatureBytes=0;
struct BasePoseMemo{BasePoseMemo* next;ReanimatorDefinition* definition;int track,frame;Sexy::SexyTransform2D inverse;};
BasePoseMemo* baseInverses[32]{};
struct FrameMemo {Reanimation* actor=nullptr;uint32_t time=0;int start=0,count=0,loop=0;ReanimatorFrameTime value{};} frameTimes[128][2];
unsigned FrameBucket(Reanimation* actor){const uintptr_t p=uintptr_t(actor);return ((p>>4)^(p>>13))&127;}
struct ActiveMemo {Reanimation* actor=nullptr;ReanimatorDefinition* definition=nullptr;ReanimatorTrackInstance* tracks=nullptr;unsigned count=0,generation=0;uint32_t mask[8]{};} activeTracks[128][2];
unsigned attachmentGeneration=1,activeReplacement=0;
#ifdef PVZ_HOST_RUNNER
bool activeTracksEnabled=true;
#endif
bool ActiveTrack(Reanimation* actor,unsigned index){const auto& t=actor->mTrackInstances[index];return t.mBlendCounter>0||
 (std::bit_cast<uint32_t>(t.mShakeOverride)&0x7fffffffu)||t.mAttachmentID!=ATTACHMENTID_NULL||H1IsAttacherTrack(actor->mDefinition->mTracks.tracks[index].mName);}
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
size_t DropDecoded(Cache& c){
 if(c.busy||!c.compressed)return 0;size_t bytes=0;
 for(unsigned i=0;i<DecodedSlots;++i)if(c.decoded[i]){
  if(c.stamp[i]){c.pages[c.decodedPage[i]]=nullptr;c.slotForPage[c.decodedPage[i]]=UINT16_MAX;}
  delete[] c.decoded[i];c.decoded[i]=nullptr;c.stamp[i]=0;bytes+=4096;
 }return bytes;
}
uint32_t Resident(const Cache& c){if(!c.entries)return 0;uint32_t bytes=c.size;
 if(c.compressed){bytes=c.encodedBytes+c.blobCount*sizeof(void*)+c.npages*(sizeof(EncodedPage)+sizeof(uint16_t))+InflateArena;for(auto* p:c.decoded)if(p)bytes+=4096;}
 for(auto* p:c.variantColors)if(p)bytes+=c.flashColors*4;
 return bytes+c.count*sizeof(Entry)+c.colors*4+c.npages*sizeof(void*)+(c.flashNormal?c.flashColors*4:0)+(c.palettePages?((c.colors+1023)/1024)*sizeof(void*):0);}
void CountResident(){unsigned bytes=Resident(baseCache)+Resident(sunCache)+Resident(normalCache)+Resident(zombieCache)+Resident(sodCache)+Resident(glowCache)+signatureBytes,entries=baseCache.count+sunCache.count+normalCache.count+zombieCache.count+sodCache.count+glowCache.count;
 pvzh1_actor_lifetime_diagnostic[0]=Resident(baseCache);pvzh1_actor_lifetime_diagnostic[1]=Resident(zombieCache);pvzh1_actor_lifetime_diagnostic[2]=Resident(sodCache);pvzh1_actor_lifetime_diagnostic[3]=Resident(normalCache);
 if(shadowCache){bytes+=Resident(*shadowCache);entries+=shadowCache->count;}
 for(auto* banks:plantCaches)if(banks)for(auto* p:banks->items)if(p){bytes+=Resident(*p);entries+=p->count;}
 pvzh1_actor_pose_diagnostic[0]=bytes;pvzh1_actor_pose_diagnostic[1]=entries;}
struct Blend {Reanimation* actor=nullptr;unsigned from=0,time=0,use=0;} blends[64];unsigned blendClock=0;
Board* sodBoard=nullptr;int sodStart=0;
unsigned FrameHalf(Reanimation* r){ReanimatorFrameTime t;r->GetFrameTime(&t);unsigned n=t.mAnimFrameBeforeInt*2;if(t.mAnimFrameAfterInt!=t.mAnimFrameBeforeInt){if(t.mFraction>=.75f)n=t.mAnimFrameAfterInt*2;else if(t.mFraction>=.25f)++n;}return n;}
uint16_t LE16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t LE32(const uint8_t* p){return uint32_t(LE16(p))|(uint32_t(LE16(p+2))<<16);}
void Drop(){if(cache.inflateReady)inflateEnd(&cache.inflater);cache.inflateReady=false;
 if(cache.compressed){for(auto*& p:cache.decoded){delete[] p;p=nullptr;}}
 if(cache.pages){if(!cache.compressed)for(unsigned i=0;i<cache.npages;++i)delete[] cache.pages[i];delete[] cache.pages;}
 if(cache.blob){for(unsigned i=0;i<cache.blobCount;++i)delete[] cache.blob[i];delete[] cache.blob;}
 delete[] cache.encoded;delete[] cache.slotForPage;delete[] cache.arena;
 cache.encoded=nullptr;cache.blob=nullptr;cache.slotForPage=nullptr;cache.arena=nullptr;cache.inflater={};cache.compressed=false;
 cache.encodedBytes=cache.blobCount=cache.arenaUsed=cache.clock=cache.rawCRC=0;cache.perTrack=cache.directLiterals=cache.colorVariants=cache.alignedRuns=cache.linearRuns=false;for(unsigned i=0;i<DecodedSlots;++i){cache.decodedPage[i]=0;cache.stamp[i]=0;}
 for(auto*& p:cache.variantColors){delete[] p;p=nullptr;}
 if(cache.palettePages){for(unsigned i=0;i<(cache.colors+1023)/1024;++i)delete[] cache.palettePages[i];delete[] cache.palettePages;cache.palettePages=nullptr;}
 delete[] cache.flashNormal;cache.flashNormal=nullptr;delete[] cache.palette;delete[] cache.entries;cache.pages=nullptr;cache.palette=nullptr;cache.entries=nullptr;
 cache.size=cache.npages=cache.count=cache.colors=cache.flashOffset=cache.flashColors=cache.flashShades=cache.dataOffset=0;pvzh1_actor_pose_diagnostic[0]=0;}
voidpf PageAlloc(voidpf opaque,uInt n,uInt size){auto& c=*(Cache*)opaque;const uint64_t bytes=uint64_t(n)*size;unsigned at=(c.arenaUsed+7)&~7u;
 if(bytes>InflateArena-at)return nullptr;c.arenaUsed=at+(unsigned)bytes;return c.arena+at;}
void PageFree(voidpf,voidpf){}
void BlobRead(unsigned offset,unsigned bytes,uint8_t* out){while(bytes){unsigned n=std::min(bytes,4096-(offset&4095));std::memcpy(out,cache.blob[offset>>12]+(offset&4095),n);out+=n;offset+=n;bytes-=n;}}
const uint8_t* Page(unsigned page){
 if(!cache.compressed)return cache.pages[page];
 if(cache.pages[page]){cache.stamp[cache.slotForPage[page]]=++cache.clock;++pvzh1_actor_page_diagnostic[3];return cache.pages[page];}
 unsigned slot=0;for(unsigned i=1;i<std::min(pageSlotLimit,cache.decodeLimit);++i)if(cache.stamp[i]<cache.stamp[slot])slot=i;
 if(!cache.decoded[slot]){cache.decoded[slot]=new(std::nothrow) uint8_t[4096];if(!cache.decoded[slot])return nullptr;}
 if(cache.stamp[slot]){cache.pages[cache.decodedPage[slot]]=nullptr;cache.slotForPage[cache.decodedPage[slot]]=UINT16_MAX;}
 uint8_t input[4128];const auto& e=cache.encoded[page];BlobRead(e.offset,e.bytes,input);
 const unsigned bytes=std::min(4096u,cache.size-page*4096);auto& z=cache.inflater;
 bool ok=inflateReset(&z)==Z_OK;z.next_in=input;z.avail_in=e.bytes;z.next_out=cache.decoded[slot];z.avail_out=bytes;
 ok=ok&&inflate(&z,Z_FINISH)==Z_STREAM_END&&z.total_in==e.bytes&&z.total_out==bytes&&crc32(0,cache.decoded[slot],bytes)==e.crc;
 if(!ok){++pvzh1_actor_page_diagnostic[4];return nullptr;}
 cache.decodedPage[slot]=page;cache.slotForPage[page]=slot;cache.pages[page]=cache.decoded[slot];cache.stamp[slot]=++cache.clock;
 ++pvzh1_actor_page_diagnostic[2];unsigned resident=0;for(auto* p:cache.decoded)if(p)resident+=4096;
 pvzh1_actor_page_diagnostic[1]=std::max((uint32_t)pvzh1_actor_page_diagnostic[1],resident);return cache.pages[page];
}
bool ReadCompressed(FILE* f,uint32_t start){
 uint8_t header[32];if(fseek(f,start,SEEK_SET)||fread(header,1,32,f)!=32)return false;
 uint32_t h[8];for(unsigned i=0;i<8;++i)h[i]=LE32(header+i*4);
 if(h[0]!=0x5a503148u||h[1]!=1||h[2]<64||h[2]>12*1024*1024||h[3]!=(h[2]+4095)/4096||h[4]!=4096||h[5]>h[2]+h[3]*32||h[6]!=h[3]*12)return false;
 cache.compressed=true;cache.size=h[2];cache.npages=h[3];cache.encodedBytes=h[5];cache.rawCRC=h[7];cache.blobCount=(h[5]+4095)/4096;
 cache.pages=new(std::nothrow) uint8_t*[cache.npages]{};cache.encoded=new(std::nothrow) EncodedPage[cache.npages];
 cache.slotForPage=new(std::nothrow) uint16_t[cache.npages];cache.blob=new(std::nothrow) uint8_t*[cache.blobCount]{};cache.arena=new(std::nothrow) uint8_t[InflateArena];
 if(!cache.pages||!cache.encoded||!cache.slotForPage||!cache.blob||!cache.arena)return false;
 unsigned offset=0;for(unsigned i=0;i<cache.npages;++i){uint8_t row[12];if(fread(row,1,12,f)!=12)return false;auto& e=cache.encoded[i];e={LE32(row),LE32(row+4),LE32(row+8)};
  if(e.offset!=offset||!e.bytes||e.bytes>4128||e.bytes>cache.encodedBytes-offset)return false;offset+=e.bytes;cache.slotForPage[i]=UINT16_MAX;}
 if(offset!=cache.encodedBytes)return false;
 for(unsigned i=0;i<cache.blobCount;++i){unsigned n=std::min(4096u,cache.encodedBytes-i*4096);cache.blob[i]=new(std::nothrow) uint8_t[n];if(!cache.blob[i]||fread(cache.blob[i],1,n,f)!=n)return false;}
 cache.inflater.zalloc=PageAlloc;cache.inflater.zfree=PageFree;cache.inflater.opaque=&cache;cache.arenaUsed=0;
 if(inflateInit(&cache.inflater)!=Z_OK)return false;cache.inflateReady=true;
 pvzh1_actor_page_diagnostic[0]=cache.encodedBytes;pvzh1_actor_page_diagnostic[5]=cache.size;pvzh1_actor_page_diagnostic[6]=DecodedSlots*4096;
 return true;
}
__attribute__((always_inline)) inline uint32_t FlashColor(unsigned index,unsigned shade){
 // The validated palette offset and every entry are four-byte aligned; a word
 // never crosses a 4 KiB page. H1 and the supported host runner are little endian.
 if(!shade&&cache.flashNormal)return cache.flashNormal[index];
 unsigned at=cache.flashOffset+(index*cache.flashShades+shade)*4;
 const auto* p=Page(at>>12);if(!p)throw std::runtime_error("Invalid offline actor page");return *reinterpret_cast<const uint32_t*>(p+(at&4095));
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
 uint32_t U32(){uint32_t low=U16();return low|(uint32_t(U16())<<16);}
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
bool ValidPacket(const Entry& e){
 if(e.offset<64||e.bytes<16||e.offset>cache.size||e.bytes>cache.size-e.offset)return false;
 Reader r(e.offset);r.U16();r.U16();unsigned segments=r.U16(),mode=r.U16();const bool flash=mode&1;
 const bool direct=mode&4;const unsigned width=direct?4:2;
 if(!segments||segments>257||(mode&~63u)||bool(mode&2)!=cache.perTrack||direct!=cache.directLiterals||bool(mode&8)!=cache.colorVariants||bool(mode&16)!=cache.alignedRuns||bool(mode&32)!=cache.linearRuns||(flash&&(!cache.flashColors||direct||cache.colorVariants))||(cache.alignedRuns&&((e.offset&3)||(mode!=20&&mode!=52))))return false;
 const int left=int16_t(r.U16()),top=int16_t(r.U16()),right=int16_t(r.U16()),bottom=int16_t(r.U16());const auto end=e.offset+e.bytes;
 if(cache.linearRuns&&(left>right||top>bottom||right-left>480||left< -480||right>481||top< -272||bottom>273))return false;
 for(unsigned s=0;s<segments;++s){if(r.offset+12>end)return false;const int track=int16_t(r.U16());unsigned draw=r.U16();bool compact=draw&32768;if(track< -1||track>255||(draw&32766))return false;
  if(compact&&cache.alignedRuns)return false;unsigned runs=r.U32(),bytes=r.U32();Reader stream(r.offset);
  if(bytes&0x80000000u){unsigned relative=bytes&0x7fffffffu;if(!flash||relative>cache.size-cache.dataOffset-4)return false;stream=Reader(cache.dataOffset+relative);bytes=stream.U32();if(bytes>cache.size-stream.offset)return false;}
  else{if(bytes>end-r.offset)return false;r.Skip(bytes);}
  const auto stop=stream.offset+bytes;
  for(unsigned i=0;i<runs;++i){if(stream.offset+(compact?3:cache.alignedRuns?8:6)>stop)return false;int x,y;unsigned flags;stream.Run(compact,x,y,flags,cache.alignedRuns,cache.linearRuns,left);unsigned n=flags&32767;if(!n||n>480||width*n>stop-stream.offset)return false;
   if(cache.linearRuns&&(x<left||x>right||n>unsigned(right-x)||y<top||y>=bottom))return false;
   for(unsigned j=0;j<n;++j){if(direct){const auto v=stream.U32();if(bool(flags&32768)!=(v>>24==255))return false;}else if(stream.U16()>=((flash||cache.colorVariants)?cache.flashColors:cache.colors))return false;}}
  if(stream.offset!=stop)return false;
 }return r.offset==end;
}
bool ReadComponent(FILE* f,unsigned offset,unsigned encoded,unsigned raw,bool palette,uint32_t checksum){
 if(fseek(f,offset,SEEK_SET)||inflateReset(&cache.inflater)!=Z_OK)return false;
 auto& z=cache.inflater;z.next_in=nullptr;z.avail_in=0;z.next_out=nullptr;z.avail_out=0;
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
 uint32_t crc=crc32(0,nullptr,0);
 if(palette)for(unsigned at=0;at<raw;at+=4096)crc=crc32(crc,reinterpret_cast<const Bytef*>(cache.palettePages[at/4096]),std::min(4096u,raw-at));
 else crc=crc32(crc,reinterpret_cast<const Bytef*>(cache.entries),raw);
 return crc==checksum;
}
__attribute__((optimize("Os"))) bool LoadShared(FILE* f,const uint32_t* h,unsigned total,unsigned slot){
 if(h[0]!=0x53503148u||h[1]!=1||slot!=h[2]*4+h[3]||h[2]>=NUM_REANIMS||h[3]>=4||!h[4]||h[4]>8192||!h[5]||h[5]>65536||!total)return false;
 auto range=[&](unsigned at,unsigned size){return at>=32&&at<=total&&size&&size<=total-at;};
 if(!range(h[6],h[7])||!range(h[10],h[11])||!range(h[13],h[14])||h[11]>h[4]*24+1024||h[14]>h[5]*4+1024)return false;
 cache.count=h[4];cache.colors=h[5];cache.perTrack=true;cache.dataOffset=64;
 if(!ReadCompressed(f,h[6])||cache.size!=h[8]||cache.rawCRC!=h[9]||uint32_t(ftell(f))!=h[6]+h[7])return false;
 cache.entries=new(std::nothrow) Entry[cache.count];
 cache.palettePages=new(std::nothrow) uint32_t*[(cache.colors+1023)/1024]{};
 if(!cache.entries||!cache.palettePages)return false;
 for(unsigned i=0;i<(cache.colors+1023)/1024;++i){cache.palettePages[i]=new(std::nothrow) uint32_t[std::min(1024u,cache.colors-i*1024)];if(!cache.palettePages[i])return false;}
 static_assert(sizeof(Entry)==24);
 if(!ReadComponent(f,h[10],h[11],cache.count*24,false,h[12])||!ReadComponent(f,h[13],h[14],cache.colors*4,true,h[15]))return false;
 uint32_t crc=crc32(0,nullptr,0);
 for(unsigned i=0;i<cache.npages;++i){const auto* page=Page(i);if(!page)return false;crc=crc32(crc,page,std::min(4096u,cache.size-i*4096));}
 if(crc!=h[9])return false;
 uint64_t key=0;uint32_t frame=0;
 for(unsigned i=0;i<cache.count;++i){const auto& e=cache.entries[i];
  if(e.type!=h[2]||e.offset<64||(i&&(e.key<key||(e.key==key&&e.frame<=frame)))||!ValidPacket(e))return false;key=e.key;frame=e.frame;
 }
 ++pvzh1_actor_page_diagnostic[7];return true;
}
__attribute__((optimize("Os"))) bool Load(const char* filename,uint32_t start=0,uint32_t length=0,uint32_t total=0,uint32_t slot=UINT32_MAX){
 auto path=Sexy::GetResourcePath(filename);FILE* f=fopen(path.c_str(),"rb");if(!f)return false;
 uint8_t header[64]{};bool okay=(!start||fseek(f,start,SEEK_SET)==0)&&fread(header,1,64,f)==64;
 if(okay&&LE32(header)==0x53503148u){uint32_t h[16];for(unsigned i=0;i<16;++i)h[i]=LE32(header+i*4);
  okay=length==64;try{okay=okay&&LoadShared(f,h,total,slot);}catch(const std::runtime_error&){okay=false;}
  fclose(f);if(!okay)Drop();return okay;
 }
 if(okay&&LE32(header)==0x5a503148u){okay=ReadCompressed(f,start);if(okay){const auto* page=Page(0);okay=page!=nullptr;if(okay)std::memcpy(header,page,64);}if(!okay){fclose(f);Drop();return false;}}
 uint32_t h[16]{};for(unsigned i=0;i<16;++i)h[i]=LE32(header+i*4);
 if(!okay||(cache.compressed&&h[9]!=cache.size)||h[0]!=Magic||h[1]!=Version||h[2]!=480||h[3]!=272||!h[4]||h[4]>8192||h[5]!=64||(!(h[15]&4)&&!h[6])||h[6]>65536||((h[15]&4)&&h[6])||h[7]!=64+h[4]*24||h[12]>65536||
    (h[12]?(h[13]<2||h[13]>256||h[11]!=h[7]+h[6]*4||h[8]!=h[11]+h[12]*h[13]*4):(h[11]||h[13]||h[8]!=h[7]+h[6]*4))||(h[14]&&h[14]!=0x314d4352)||h[9]<h[8]||h[9]>12*1024*1024){fclose(f);if(cache.compressed)Drop();return false;}
 if((h[15]!=0&&h[15]!=2&&h[15]!=6&&h[15]!=10&&h[15]!=20&&h[15]!=52)||(h[15]==10&&(!h[12]||h[13]!=4))||((h[15]==20||h[15]==52)&&(h[12]||(h[8]&3)))||(h[15]==52&&cache.compressed)){fclose(f);Drop();return false;}
 cache.perTrack=h[15]&2;cache.directLiterals=h[15]&4;cache.colorVariants=h[15]&8;cache.alignedRuns=h[15]&16;cache.linearRuns=h[15]&32;cache.size=h[9];cache.count=h[4];cache.colors=h[6];cache.flashOffset=h[11];cache.flashColors=h[12];cache.flashShades=h[13];cache.dataOffset=h[8];cache.npages=(cache.size+4095)/4096;
 if(!cache.compressed)cache.pages=new(std::nothrow) uint8_t*[cache.npages]{};
 cache.entries=new(std::nothrow) Entry[cache.count];if(cache.colors)cache.palette=new(std::nothrow) uint32_t[cache.colors];
 if(!cache.pages||!cache.entries||(cache.colors&&!cache.palette))okay=false;
 uint32_t crc=crc32(0,nullptr,0),whole=crc;
 for(unsigned i=0;okay&&i<cache.npages;++i){unsigned n=std::min(4096u,cache.size-i*4096);
  if(cache.compressed){const auto* p=Page(i);if(!p){okay=false;break;}whole=crc32(whole,p,n);unsigned start=i==0?64:0;crc=crc32(crc,p+start,n-start);continue;}
  cache.pages[i]=new(std::nothrow) uint8_t[n];if(!cache.pages[i]){okay=false;break;}
  unsigned start=i==0?64:0;if(i==0)std::memcpy(cache.pages[i],header,64);
  if(fread(cache.pages[i]+start,1,n-start,f)!=n-start){okay=false;break;}crc=crc32(crc,cache.pages[i]+start,n-start);}
 if(okay&&(crc!=h[10]||(cache.compressed&&whole!=cache.rawCRC)||(length?uint32_t(ftell(f))!=start+length:fgetc(f)!=EOF)))okay=false;fclose(f);
 if(okay){Reader r(64);uint64_t key=0;uint32_t frame=0;
  for(unsigned i=0;i<cache.count;++i){auto& e=cache.entries[i];e.key=r.U32();e.key|=uint64_t(r.U32())<<32;e.frame=r.U32();e.offset=r.U32();e.bytes=r.U32();e.type=r.U32();
   if(e.offset<h[8]||(i&&(e.key<key||(e.key==key&&e.frame<=frame)))){okay=false;break;}key=e.key;frame=e.frame;}
  Reader colors(h[7]);for(unsigned i=0;i<cache.colors;++i)cache.palette[i]=colors.U32();
  for(unsigned i=0;okay&&i<cache.count;++i)okay=ValidPacket(cache.entries[i]);
 }
 if(!okay){Drop();return false;}
 if(cache.flashColors){cache.flashNormal=new(std::nothrow) uint32_t[cache.flashColors];if(cache.flashNormal)for(unsigned i=0;i<cache.flashColors;++i){unsigned at=cache.flashOffset+i*cache.flashShades*4;const auto* p=Page(at>>12);if(!p){Drop();return false;}cache.flashNormal[i]=LE32(p+(at&4095));}}
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
void H1ActorTrackStateChanged(Reanimation* actor){for(auto& p:activeTracks[FrameBucket(actor)])if(p.actor==actor)p.generation=0;}
void H1ActorAttachmentsChanged(){if(!++attachmentGeneration){for(auto& set:activeTracks)for(auto& p:set)p.generation=0;attachmentGeneration=1;}}
int H1ActorNextActiveTrack(Reanimation* actor,int after){
 const unsigned count=actor->mDefinition->mTracks.count,begin=unsigned(after+1);
 if(begin>=count)return -1;
 if(!H1ActorPose::PlantType(actor->mReanimationType)||count>256
#ifdef PVZ_HOST_RUNNER
 ||!activeTracksEnabled
#endif
 )return int(begin);
 ++pvzh1_active_track_diagnostic[0];auto& set=activeTracks[FrameBucket(actor)];ActiveMemo* found=nullptr;
 for(auto& p:set)if(p.actor==actor){found=&p;break;}
 if(!found)found=!set[0].actor?&set[0]:!set[1].actor?&set[1]:&set[(activeReplacement++)&1];
 if(found->actor!=actor||found->definition!=actor->mDefinition||found->tracks!=actor->mTrackInstances||found->count!=count||found->generation!=attachmentGeneration){
  *found={actor,actor->mDefinition,actor->mTrackInstances,count,attachmentGeneration,{}};
  for(unsigned i=0;i<count;++i)if(ActiveTrack(actor,i))found->mask[i>>5]|=1u<<(i&31);++pvzh1_active_track_diagnostic[1];
 }
 for(unsigned word=begin>>5;word<(count+31)/32;++word){uint32_t bits=found->mask[word];if(word==begin>>5)bits&=0xffffffffu<<(begin&31);
  while(bits){const unsigned bit=unsigned(__builtin_ctz(bits)),index=word*32+bit;bits&=bits-1;
   if(!ActiveTrack(actor,index)){found->mask[word]&=~(1u<<bit);continue;}
   ++pvzh1_active_track_diagnostic[2];pvzh1_active_track_diagnostic[3]+=index-begin;return int(index);
  }
 }pvzh1_active_track_diagnostic[3]+=count-begin;return -1;
}
bool H1ActorPosePrepare(bool pea,bool zombie,bool sod){
 baseCache.decodeLimit=zombieCache.decodeLimit=64;
 auto prepare=[](Cache& selected,const char* name){
  if(!selected.enabled)return true;
  CacheScope scope(&selected);
  if(!cache.tried){cache.tried=cache.busy=true;if(!Load(name)){Drop();++pvzh1_actor_pose_diagnostic[7];}else if(&selected==&normalCache)++pvzh1_normal_actor_diagnostic[3];cache.busy=false;}
  return cache.entries!=nullptr;
 };
 bool okay=(!pea||prepare(baseCache,"pea-transitions.h1ap"));
 okay=(!zombie||prepare(zombieCache,"zombie-hit.h1ap"))&&okay;
 okay=(!sod||prepare(sodCache,"sod-roll.h1ap"))&&okay;
 okay=prepare(sunCache,"sun-poses.h1ap")&&okay;
 const bool normalReady=prepare(normalCache,"actor-normal.h1ap");okay=normalReady&&okay;
 if(normalReady&&normalCache.entries){pvzh1_normal_actor_diagnostic[0]=normalCache.size;pvzh1_linear_actor_diagnostic[3]=normalCache.linearRuns?52:20;}
 if(!shadowCache){shadowCache=new(std::nothrow) Cache;if(shadowCache)shadowCache->enabled=baseCache.enabled;}
 if(shadowCache&&shadowCache->enabled&&!shadowCache->tried){CacheScope scope(shadowCache);cache.tried=cache.busy=true;if(Load("plant-shadows.h1ap")){++pvzh1_shadow_pose_diagnostic[0];pvzh1_shadow_pose_diagnostic[1]=cache.size;}cache.busy=false;}
 if(baseCache.enabled&&(!shadowCache||(shadowCache->enabled&&!shadowCache->entries)))okay=false;
 if(!okay)++pvzh1_actor_lifetime_diagnostic[7];CountResident();return okay;
}
bool H1PlantShadowDraw(Sexy::Graphics* g,Sexy::Image* image,float x,float y,int cel,float sx,float sy){
 if(image!=Sexy::IMAGE_PLANTSHADOW&&image!=Sexy::IMAGE_PLANTSHADOW2)return false;
 if(!shadowCache||!shadowCache->enabled||!shadowCache->entries||shadowCache->busy)return false;
 if(!g||x!=-3||y!=51||cel||sx!=1||sy!=1||g->mScaleX!=1||g->mScaleY!=1||g->mColorizeImages||!g->mLinearBlend||g->mDrawMode!=Sexy::Graphics::DRAWMODE_NORMAL)return false;
 auto* screen=Sexy::H1GetPhysicalScreen(dynamic_cast<Sexy::GLImage*>(g->mDestImage));
 if(!screen||!screen->Active()||!screen->mGLInterface->mTransformStack.empty())return false;
 auto* target=screen->Target();if(target!=h1DirtyCanvas.owner||!target->mBits)return false;
 const auto clip=screen->MapRect(g->mClipRect);if(clip!=Sexy::Rect(0,0,480,272))return false;
 CacheScope scope(shadowCache);
 const uint64_t key=(uint64_t(std::bit_cast<uint32_t>(g->mTransX))<<32)|std::bit_cast<uint32_t>(g->mTransY);
 const auto* entry=Find(key,image==Sexy::IMAGE_PLANTSHADOW2);if(!entry){++pvzh1_shadow_pose_diagnostic[3];return false;}
 Reader r(entry->offset);r.U16();r.U16();const unsigned segments=r.U16();r.U16();const int left=int16_t(r.U16()),top=int16_t(r.U16()),right=int16_t(r.U16()),bottom=int16_t(r.U16());
 cache.busy=true;H1CanvasKnownDraw drawing(target,left,top,right-left,bottom-top);auto* pixels=target->mBits.get();
 for(unsigned s=0;s<segments;++s){r.U16();const bool compact=r.U16()&32768;const unsigned runs=r.U32();r.U32();
  for(unsigned i=0;i<runs;++i){int px,py;unsigned flags;r.Run(compact,px,py,flags);unsigned left=flags&32767;if(py<0||py>=272||px<0||px+left>480){cache.busy=false;return false;}auto* dest=pixels+py*480+px;
   auto emit=[&](unsigned id){const uint32_t v=cache.palette[id],p=*dest,a=v>>24;*dest++=(flags&32768)?v:0xff000000u|((v&0xffffffu)+((((p&0xff00ffu)*a)>>8)&0xff00ffu)+((((p&0xff00u)*a)>>8)&0xff00u));};
   while(left){const uint8_t* bytes;unsigned n=r.TakeSamples(left,2,bytes);if(!n){emit(r.U16());--left;continue;}for(unsigned j=0;j<n;++j,bytes+=2)emit(LE16(bytes));left-=n;}
  }
 }
 target->BitsChanged();Sexy::H1ScreenNoteDraw(screen);cache.busy=false;++pvzh1_shadow_pose_diagnostic[2];return true;
}
__attribute__((optimize("Os"))) bool H1PlantPosePrepare(ReanimationType type,bool allFilters){
 if(!baseCache.enabled||!H1ActorPose::PlantType(type)||int(type)<0||int(type)>=NUM_REANIMS)return true;
 auto*& banks=plantCaches[int(type)];if(!banks)banks=new(std::nothrow) PlantBanks;
 if(!banks){++pvzh1_plant_pose_diagnostic[1];return false;}
 bool okay=true;
 for(unsigned filter=0;filter<(allFilters?4u:1u);++filter){
 Cache* shared=nullptr;for(auto* p:banks->items)if(p&&p->entries&&p->colorVariants){shared=p;break;}
 if(filter&&shared){CacheScope scope(shared);cache.busy=true;if(!PrepareVariant(filter)){++pvzh1_plant_pose_diagnostic[1];okay=false;}cache.busy=false;continue;}
 auto*& slot=banks->items[filter];if(!slot){slot=new(std::nothrow) Cache;if(slot)slot->decodeLimit=32;}
 if(!slot){++pvzh1_plant_pose_diagnostic[1];return false;}
 if(slot->tried){okay=bool(slot->entries)&&okay;continue;}
 {CacheScope scope(slot);cache.tried=cache.busy=true;
  uint32_t offset=0,bytes=0,containerBytes=0;const auto path=Sexy::GetResourcePath("plant-poses.h1pp");FILE* f=fopen(path.c_str(),"rb");
  if(f){uint8_t header[32],table[NUM_REANIMS*4*12];
   if(fread(header,1,32,f)==32&&LE32(header)==0x4c503148u&&(LE32(header+4)==1||LE32(header+4)==2)){
    const unsigned count=LE32(header+8),size=LE32(header+12),total=LE32(header+16);
    const unsigned index=unsigned(type)*4+filter;
    if(count>index&&count<=NUM_REANIMS*4&&size==count*12&&fread(table,1,size,f)==size&&crc32(0,table,size)==LE32(header+20)&&fseek(f,0,SEEK_END)==0&&uint32_t(ftell(f))==total){
     containerBytes=total;const auto* item=table+index*12;offset=LE32(item);bytes=LE32(item+4);
     if(offset<32+size||bytes<64||offset>total||bytes>total-offset)bytes=0;
    }
   }fclose(f);
  }
  if((bytes&&Load("plant-poses.h1pp",offset,bytes,containerBytes,unsigned(type)*4+filter))&&cache.perTrack&&PrepareVariant(filter)){++pvzh1_plant_pose_diagnostic[0];pvzh1_plant_pose_diagnostic[2]+=cache.count;}
  else{Drop();++pvzh1_plant_pose_diagnostic[1];okay=false;}cache.busy=false;}
 }CountResident();return okay;
}
bool H1SunflowerGlowPrepare(){
 if(!glowCache.enabled)return true;CacheScope scope(&glowCache);
 if(!cache.tried){cache.tried=cache.busy=true;if(!Load("sunflower-glow.h1ap")){Drop();++pvzh1_actor_lifetime_diagnostic[7];}cache.busy=false;}
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
 if(!actor||actor->mReanimationType!=REANIM_PEASHOOTER||time!=20||(actor->mFrameStart!=29&&actor->mFrameStart!=54))return;
 Blend* selected=nullptr;for(auto& b:blends)if(b.actor==actor){selected=&b;break;}else if(!selected||b.use<selected->use)selected=&b;
 *selected={actor,FrameHalf(actor),unsigned(time),++blendClock};
}
static bool DrawFromCache(Reanimation* actor,Sexy::Graphics* g,int group){
 if(!cache.enabled||cache.busy||!cache.entries||!actor||!(cache.perTrack?H1ActorPose::PlantType(actor->mReanimationType):H1ActorPose::Supported(actor->mReanimationType)))return false;
 if(actor->mDead)return true;
 if(!actor->mDefinition||!actor->mTrackInstances)return false;
 bool present=false;for(int i=0;i<actor->mDefinition->mTracks.count;++i)if(actor->mTrackInstances[i].mRenderGroup==group){present=true;break;}
 if(!present)return true; // Original DrawRenderGroup also draws no attachments.
 if(!g||g->mScaleX!=1||g->mScaleY!=1||g->mDrawMode!=Sexy::Graphics::DRAWMODE_NORMAL||g->mColorizeImages){++pvzh1_actor_pose_diagnostic[4];return false;}
 auto* screen=Sexy::H1GetPhysicalScreen(dynamic_cast<Sexy::GLImage*>(g->mDestImage));
 if(!screen||!screen->Active()||!screen->mGLInterface->mTransformStack.empty())return false;
 auto* target=screen->Target();if(target!=h1DirtyCanvas.owner||!target->mBits||target->mWidth!=480||target->mHeight!=272)return false;
 unsigned blendRemaining=0;for(int i=0;i<actor->mDefinition->mTracks.count;++i)blendRemaining=std::max<unsigned>(blendRemaining,actor->mTrackInstances[i].mBlendCounter);
 unsigned blendFrame=0;bool blendReady=false;
 if(blendRemaining&&blendRemaining<=20&&actor->mReanimationType==REANIM_PEASHOOTER){
  for(auto& b:blends)if(b.actor==actor&&b.time==20){unsigned step=20-blendRemaining;
   if(actor->mFrameStart==54&&actor->mAnimRate==35&&b.from>=58&&b.from<=106){blendFrame=0x80000000u|(b.from<<8)|step;blendReady=true;}
   else if(actor->mFrameStart==29&&(b.from==156||b.from==132)){blendFrame=(b.from==156?0x90000000u:0xa0000000u)|(FrameHalf(actor)<<8)|step;blendReady=true;}break;
  }
 }
 unsigned flashShade=0;bool flashReady=false;
 if(actor->mReanimationType==REANIM_ZOMBIE&&actor->mEnableExtraAdditiveDraw){const auto& c=actor->mExtraAdditiveColor;
  if(c.mAlpha==255&&c.mRed>=10&&c.mRed<=250&&c.mRed==c.mGreen&&c.mRed==c.mBlue&&c.mRed%10==0){flashShade=c.mRed/10;flashReady=true;}}
 if(cache.flashShades==89&&actor->mReanimationType==REANIM_SUNFLOWER&&actor->mEnableExtraAdditiveDraw){const auto& c=actor->mExtraAdditiveColor;
  if(c.mRed==c.mGreen&&c.mRed==c.mBlue&&c.mRed>=0&&c.mRed<=255){
   if(c.mAlpha==255&&(c.mRed%3==0||c.mRed==128)){flashShade=c.mRed==128?86:c.mRed/3;flashReady=true;}
   else if(c.mRed==255&&(c.mAlpha==196||c.mAlpha==92)){flashShade=c.mAlpha==196?87:88;flashReady=true;}
  }
 }
 uint64_t key;if(!MemoSignature(actor,group,key,blendReady,flashReady,cache.perTrack)){++pvzh1_actor_pose_diagnostic[3];if(cache.perTrack)++pvzh1_plant_pose_diagnostic[6];return false;}
 ReanimatorFrameTime time;actor->GetFrameTime(&time);unsigned frame=time.mAnimFrameBeforeInt*2;
 if(time.mAnimFrameAfterInt!=time.mAnimFrameBeforeInt){if(time.mFraction>=.75f)frame=time.mAnimFrameAfterInt*2;else if(time.mFraction>=.25f)++frame;}
 if(actor->mReanimationType==REANIM_SUN){frame=time.mAnimFrameBeforeInt*4;if(time.mAnimFrameAfterInt!=time.mAnimFrameBeforeInt){unsigned part=unsigned(time.mFraction*4+.5f);frame=part==4?time.mAnimFrameAfterInt*4:frame+part;}}
 if(blendReady)frame=blendFrame;
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
 if((cx||cy||cr!=480||cb!=272)&&(left+baseX+dx<cx||top+baseY+dy<cy||right+baseX+dx>cr||bottom+baseY+dy>cb)){++pvzh1_actor_pose_diagnostic[6];return false;}
 // Mark this actor's coverage once, rather than once per authored track.
 // Attachments still draw outside the protected pixel scopes below, so their
 // own coverage and any raw access remain visible to the dirty canvas.
 h1DirtyCanvas.Mark(target,left+baseX+dx,top+baseY+dy,right-left,bottom-top);
 cache.busy=true;
 for(unsigned s=0;s<segments;++s){const int track=int16_t(r.U16());unsigned draw=r.U16();const bool drawn=draw&1,compact=draw&32768;const unsigned runs=r.U32(),payload=r.U32();Reader stream(r.offset);
  if(payload&0x80000000u){stream=Reader(cache.dataOffset+(payload&0x7fffffffu));stream.U32();}else r.Skip(payload);const auto runStart=stream.offset;
  if((flash||perTrack)&&track>=0){if(track>=actor->mDefinition->mTracks.count){cache.busy=false;return false;}const auto& instance=actor->mTrackInstances[track];if(instance.mRenderGroup!=group||(flash&&!perTrack&&bool(instance.mImageOverride)!=drawn))continue;}
  {H1CanvasProtectedPixels drawing(target);auto* pixels=target->mBits.get();
   if(cache.linearRuns&&left+baseX+dx>=cx&&top+baseY+dy>=cy&&right+baseX+dx<=cr&&bottom+baseY+dy<=cb){
    DrawLinearSegment(cache.pages,stream.offset,runs,pixels,baseX+dx+(baseY+dy)*480);
   }else {if(cache.linearRuns)++pvzh1_linear_actor_diagnostic[2];for(unsigned i=0;i<runs;++i){int x,y;unsigned flags;stream.Run(compact,x,y,flags,cache.alignedRuns,cache.linearRuns,left);x+=baseX+dx;y+=baseY+dy;const unsigned n=flags&32767;
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
   if(flash&&flashShade){Reader add(runStart);for(unsigned i=0;i<runs;++i){int x,y;unsigned flags;add.Run(compact,x,y,flags);x+=baseX+dx;y+=baseY+dy;unsigned n=flags&32767;
     if(y<cy||y>=cb||x>=cr||x+int(n)<=cx){add.Skip(n*2);continue;}unsigned first=std::max(0,cx-x),last=std::min(int(n),cr-x);add.Skip(first*2);auto* dest=pixels+y*480+x+first;
     for(unsigned j=first;j<last;++j){uint32_t v=FlashColor(add.U16(),flashShade),p=*dest;unsigned red=((p>>16)&255)+((v>>16)&255),green=((p>>8)&255)+((v>>8)&255),blue=(p&255)+(v&255);*dest++=(p&0xff000000u)|(std::min(255u,red)<<16)|(std::min(255u,green)<<8)|std::min(255u,blue);}add.Skip((n-last)*2);
   }}
  }
  if(track>=0&&(!flash||perTrack)){cache.busy=false;AttachmentDraw(actor->mTrackInstances[track].mAttachmentID,g,!drawn);cache.busy=true;}
 }
 target->BitsChanged();Sexy::H1ScreenNoteDraw(screen);cache.busy=false;++pvzh1_actor_pose_diagnostic[5];
 if(perTrack)++pvzh1_plant_pose_diagnostic[5];
 ++pvzh1_actor_pose_diagnostic[actor->mReanimationType==REANIM_SUN?12:8+unsigned(actor->mReanimationType==REANIM_SUNFLOWER?1:actor->mReanimationType==REANIM_ZOMBIE?2:actor->mReanimationType==REANIM_SODROLL?3:0)];return true;
}
__attribute__((optimize("Os"))) static bool NoteFallback(Reanimation* actor){
 if(!actor||actor->mDead||!gLawnApp||!gLawnApp->mBoard||gLawnApp->mGameScene!=SCENE_PLAYING)return false;
 const auto type=actor->mReanimationType;auto& d=pvzh1_actor_fallback_diagnostic;++d[0];++d[type==REANIM_PEASHOOTER?1:type==REANIM_SUNFLOWER?2:type==REANIM_WALLNUT?3:type==REANIM_SUN?4:5];
 unsigned flags=unsigned(actor->mEnableExtraAdditiveDraw)|(unsigned(actor->mEnableExtraOverlayDraw)<<1)|(unsigned(int(actor->mFilterEffect)+1)<<2),blend=0;
 for(int i=0;i<actor->mDefinition->mTracks.count;++i)blend=std::max<unsigned>(blend,actor->mTrackInstances[i].mBlendCounter);flags|=blend<<16;
 if(blend)++d[7];if(actor->mColorOverride!=Sexy::Color::White)++d[8];if(flags&1)++d[9];if(flags&2)++d[10];if(actor->mFilterEffect!=FILTER_EFFECT_NONE)++d[11];
 if(H1ActorPose::LinearBits(actor->mOverlayMatrix.m00)!=0x3f800000u||H1ActorPose::LinearBits(actor->mOverlayMatrix.m11)!=0x3f800000u||H1ActorPose::LinearBits(actor->mOverlayMatrix.m01)||H1ActorPose::LinearBits(actor->mOverlayMatrix.m10))++d[12];
 return false;
}
bool H1ActorPoseDraw(Reanimation* actor,Sexy::Graphics* g,int group){
 const auto type=actor?actor->mReanimationType:REANIM_NONE;
 if(type==REANIM_SUNFLOWER&&actor->mEnableExtraAdditiveDraw){CacheScope scope(&glowCache);if(DrawFromCache(actor,g,group))return true;}
 if(type==REANIM_PEASHOOTER||type==REANIM_SUNFLOWER){CacheScope scope(&normalCache);if(DrawFromCache(actor,g,group)){++pvzh1_normal_actor_diagnostic[1];return true;}++pvzh1_normal_actor_diagnostic[2];}
 // Keep the existing transition/flash-specialized packets where available.
 {CacheScope scope(type==REANIM_SUN?&sunCache:type==REANIM_ZOMBIE?&zombieCache:type==REANIM_SODROLL?&sodCache:&baseCache);if(DrawFromCache(actor,g,group))return true;}
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
 if(!bytes)bytes=DropDecoded(zombieCache);if(!bytes)bytes=DropDecoded(sodCache);
 for(auto* banks:plantCaches)if(!bytes&&banks)for(auto* p:banks->items)if(!bytes&&p)bytes=DropDecoded(*p);
 if(bytes){++pvzh1_plant_pose_diagnostic[10];pvzh1_plant_pose_diagnostic[11]+=bytes;CountResident();}return bytes;
}
void H1ActorPoseFinishSod(){CacheScope scope(&sodCache);if(cache.busy)return;pvzh1_actor_lifetime_diagnostic[4]+=Resident(cache);Drop();sodBoard=nullptr;pvzh1_sod_offline_diagnostic[0]=0;CountResident();}
void H1ActorPoseForget(Reanimation* actor){
 for(auto& p:frameTimes[FrameBucket(actor)])if(p.actor==actor){p.actor=nullptr;++pvzh1_frame_time_diagnostic[3];}
 for(auto& p:activeTracks[FrameBucket(actor)])if(p.actor==actor)p.actor=nullptr;
 for(bool perTrack:{false,true}){auto** link=&signatures[((uintptr_t(actor)>>4)^unsigned(perTrack))&127];while(*link){auto* p=*link;if(p->actor!=actor){link=&p->next;continue;}*link=p->next;signatureBytes-=sizeof(SignatureMemo)+p->count*4;delete[] p->words;delete p;++pvzh1_actor_lifetime_diagnostic[5];}}
 for(auto& b:blends)if(b.actor==actor)b={};
}
void H1ActorPoseForgetDefinition(ReanimatorDefinition* definition){
 for(auto*& head:baseInverses){auto** link=&head;while(*link){auto* p=*link;if(p->definition!=definition){link=&p->next;continue;}*link=p->next;signatureBytes-=sizeof(BasePoseMemo);--pvzh1_base_inverse_diagnostic[2];pvzh1_base_inverse_diagnostic[3]-=sizeof(BasePoseMemo);delete p;++pvzh1_actor_lifetime_diagnostic[6];}}
}
extern "C" void pvzh1_actor_pose_shutdown(){DropSignatures();for(auto*& banks:plantCaches)if(banks){for(auto* p:banks->items)if(p){{CacheScope scope(p);Drop();}delete p;}delete banks;banks=nullptr;}if(shadowCache){{CacheScope scope(shadowCache);Drop();}delete shadowCache;shadowCache=nullptr;pvzh1_shadow_pose_diagnostic[1]=0;}for(auto* p:{&baseCache,&sunCache,&normalCache,&zombieCache,&sodCache,&glowCache}){CacheScope scope(p);Drop();cache.tried=false;}pvzh1_normal_actor_diagnostic[0]=0;CountResident();sodBoard=nullptr;for(auto& b:blends)b={};}
#ifdef PVZ_HOST_RUNNER
void H1ActorActiveTracksTestEnable(bool enabled){activeTracksEnabled=enabled;for(auto& set:activeTracks)for(auto& p:set)p.generation=0;}
bool H1ActorPosePhaseTestPrepare(unsigned index){
 if(index>2)return false;Cache* caches[]={&baseCache,&zombieCache,&sodCache};const char* names[]={"pea-transitions.h1ap","zombie-hit.h1ap","sod-roll.h1ap"};CacheScope scope(caches[index]);
 if(!cache.tried){cache.tried=cache.busy=true;if(!Load(names[index])){Drop();++pvzh1_actor_pose_diagnostic[7];}cache.busy=false;}CountResident();return cache.entries!=nullptr;
}
void H1ActorMatrixTestEnable(bool enabled){attachmentMemoEnabled=enabled;}
void H1ActorPoseTestEnable(bool v){baseCache.enabled=sunCache.enabled=normalCache.enabled=zombieCache.enabled=sodCache.enabled=glowCache.enabled=v;if(shadowCache)shadowCache->enabled=v;for(auto* banks:plantCaches)if(banks)for(auto* p:banks->items)if(p)p->enabled=v;}
void H1ActorPoseLegacyTestEnable(bool v){baseCache.enabled=sunCache.enabled=normalCache.enabled=zombieCache.enabled=sodCache.enabled=glowCache.enabled=v;}
void H1ActorPosePageTestSlots(unsigned n){pageSlotLimit=std::max(1u,std::min(DecodedSlots,n));}
#endif
