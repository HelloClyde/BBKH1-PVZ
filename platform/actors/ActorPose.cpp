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
#include "Common.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <zlib.h>
extern "C" {volatile uint32_t pvzh1_actor_pose_diagnostic[16];}
extern "C" {volatile uint32_t pvzh1_sod_offline_diagnostic[8];}
namespace {
using namespace H1ActorPose;
struct Entry{uint64_t key;uint32_t frame,offset,bytes,type;};
struct Cache{uint8_t** pages=nullptr;uint32_t* palette=nullptr;uint32_t* flashNormal=nullptr;Entry* entries=nullptr;
 uint32_t size=0,npages=0,count=0,colors=0,flashOffset=0,flashColors=0,dataOffset=0;bool tried=false,busy=false,enabled=true;} cache,sunCache;
struct SunScope {bool active;explicit SunScope(bool sun):active(sun){if(active)std::swap(cache,sunCache);}~SunScope(){if(active)std::swap(cache,sunCache);}};
uint32_t Resident(const Cache& c){return c.entries?c.size+c.count*sizeof(Entry)+c.colors*4+c.npages*sizeof(void*)+(c.flashNormal?c.flashColors*4:0):0;}
void CountResident(){pvzh1_actor_pose_diagnostic[0]=Resident(cache)+Resident(sunCache);pvzh1_actor_pose_diagnostic[1]=(cache.entries?cache.count:0)+(sunCache.entries?sunCache.count:0);}
struct Blend {Reanimation* actor=nullptr;unsigned from=0,time=0,use=0;} blends[64];unsigned blendClock=0;
Board* sodBoard=nullptr;int sodStart=0;
unsigned FrameHalf(Reanimation* r){ReanimatorFrameTime t;r->GetFrameTime(&t);unsigned n=t.mAnimFrameBeforeInt*2;if(t.mAnimFrameAfterInt!=t.mAnimFrameBeforeInt){if(t.mFraction>=.75f)n=t.mAnimFrameAfterInt*2;else if(t.mFraction>=.25f)++n;}return n;}
uint16_t LE16(const uint8_t* p){return uint16_t(p[0])|(uint16_t(p[1])<<8);}
uint32_t LE32(const uint8_t* p){return uint32_t(LE16(p))|(uint32_t(LE16(p+2))<<16);}
void Drop(){if(cache.pages){for(unsigned i=0;i<cache.npages;++i)delete[] cache.pages[i];delete[] cache.pages;}
 delete[] cache.flashNormal;cache.flashNormal=nullptr;delete[] cache.palette;delete[] cache.entries;cache.pages=nullptr;cache.palette=nullptr;cache.entries=nullptr;
 cache.size=cache.npages=cache.count=cache.colors=cache.flashOffset=cache.flashColors=cache.dataOffset=0;pvzh1_actor_pose_diagnostic[0]=0;}
__attribute__((always_inline)) inline uint32_t FlashColor(unsigned index,unsigned shade){
 // The validated palette offset and every entry are four-byte aligned; a word
 // never crosses a 4 KiB page. H1 and the supported host runner are little endian.
 if(!shade&&cache.flashNormal)return cache.flashNormal[index];
 unsigned at=cache.flashOffset+(index*26+shade)*4;
 return *reinterpret_cast<const uint32_t*>(cache.pages[at>>12]+(at&4095));
}
struct Reader{
 uint32_t offset;const uint8_t* p=nullptr;unsigned available=0;
 explicit Reader(uint32_t o):offset(o){}
 uint8_t U8(){if(!available){p=cache.pages[offset/4096]+offset%4096;available=std::min(4096-offset%4096,cache.size-offset);}uint8_t v=*p++;++offset;--available;return v;}
 __attribute__((always_inline)) inline uint16_t U16(){if(!available){p=cache.pages[offset/4096]+offset%4096;available=std::min(4096-offset%4096,cache.size-offset);}
  if(available==1){uint16_t low=U8();return low|(uint16_t(U8())<<8);}
  uint16_t v=LE16(p);p+=2;offset+=2;available-=2;return v;}
 uint32_t U32(){uint32_t low=U16();return low|(uint32_t(U16())<<16);}
 void Run(bool compact,int& x,int& y,unsigned& flags){if(compact){unsigned xy=U16(),f=U8();x=int8_t(xy);y=int8_t(xy>>8);flags=(f&127)|((f&128)<<8);}else{x=int16_t(U16());y=int16_t(U16());flags=U16();}}
 void Skip(unsigned n){if(!n)return;offset+=n;if(n<=available){p+=n;available-=n;}else{p=nullptr;available=0;}}
};
bool ValidPacket(const Entry& e){
 if(e.offset<64||e.bytes<16||e.offset>cache.size||e.bytes>cache.size-e.offset)return false;
 Reader r(e.offset);r.U16();r.U16();unsigned segments=r.U16(),flash=r.U16();if(!segments||segments>257||flash>1||(flash&&!cache.flashColors))return false;
 for(int i=0;i<4;++i)r.U16();const auto end=e.offset+e.bytes;
 for(unsigned s=0;s<segments;++s){if(r.offset+12>end)return false;const int track=int16_t(r.U16());unsigned draw=r.U16();bool compact=draw&32768;if(track< -1||track>255||(draw&32766))return false;
  unsigned runs=r.U32(),bytes=r.U32();Reader stream(r.offset);
  if(bytes&0x80000000u){unsigned relative=bytes&0x7fffffffu;if(!flash||relative>cache.size-cache.dataOffset-4)return false;stream=Reader(cache.dataOffset+relative);bytes=stream.U32();if(bytes>cache.size-stream.offset)return false;}
  else{if(bytes>end-r.offset)return false;r.Skip(bytes);}
  const auto stop=stream.offset+bytes;
  for(unsigned i=0;i<runs;++i){if(stream.offset+(compact?3:6)>stop)return false;int x,y;unsigned flags;stream.Run(compact,x,y,flags);unsigned n=flags&32767;if(!n||n>480||2*n>stop-stream.offset)return false;
   for(unsigned j=0;j<n;++j)if(stream.U16()>=(flash?cache.flashColors:cache.colors))return false;}
  if(stream.offset!=stop)return false;
 }return r.offset==end;
}
bool Load(const char* filename="actor-poses.h1ap"){
 auto path=Sexy::GetResourcePath(filename);FILE* f=fopen(path.c_str(),"rb");if(!f)return false;
 uint8_t header[64]{};bool okay=fread(header,1,64,f)==64;
 uint32_t h[16]{};for(unsigned i=0;i<16;++i)h[i]=LE32(header+i*4);
 if(!okay||h[0]!=Magic||h[1]!=Version||h[2]!=480||h[3]!=272||!h[4]||h[4]>8192||h[5]!=64||!h[6]||h[6]>65536||h[7]!=64+h[4]*24||h[12]>65536||
    (h[12]?(h[13]!=26||h[11]!=h[7]+h[6]*4||h[8]!=h[11]+h[12]*104):(h[11]||h[13]||h[8]!=h[7]+h[6]*4))||(h[14]&&h[14]!=0x314d4352)||h[9]<h[8]||h[9]>12*1024*1024){fclose(f);return false;}
 cache.size=h[9];cache.count=h[4];cache.colors=h[6];cache.flashOffset=h[11];cache.flashColors=h[12];cache.dataOffset=h[8];cache.npages=(cache.size+4095)/4096;
 cache.pages=new(std::nothrow) uint8_t*[cache.npages]{};
 cache.entries=new(std::nothrow) Entry[cache.count];cache.palette=new(std::nothrow) uint32_t[cache.colors];
 if(!cache.pages||!cache.entries||!cache.palette)okay=false;
 uint32_t crc=crc32(0,nullptr,0);
 for(unsigned i=0;okay&&i<cache.npages;++i){unsigned n=std::min(4096u,cache.size-i*4096);cache.pages[i]=new(std::nothrow) uint8_t[n];if(!cache.pages[i]){okay=false;break;}
  unsigned start=i==0?64:0;if(i==0)std::memcpy(cache.pages[i],header,64);
  if(fread(cache.pages[i]+start,1,n-start,f)!=n-start){okay=false;break;}crc=crc32(crc,cache.pages[i]+start,n-start);}
 if(okay&&(crc!=h[10]||fgetc(f)!=EOF))okay=false;fclose(f);
 if(okay){Reader r(64);uint64_t key=0;uint32_t frame=0;
  for(unsigned i=0;i<cache.count;++i){auto& e=cache.entries[i];e.key=r.U32();e.key|=uint64_t(r.U32())<<32;e.frame=r.U32();e.offset=r.U32();e.bytes=r.U32();e.type=r.U32();
   if(e.offset<h[8]||(i&&(e.key<key||(e.key==key&&e.frame<=frame)))){okay=false;break;}key=e.key;frame=e.frame;}
  Reader colors(h[7]);for(unsigned i=0;i<cache.colors;++i)cache.palette[i]=colors.U32();
  for(unsigned i=0;okay&&i<cache.count;++i)okay=ValidPacket(cache.entries[i]);
 }
 if(!okay){Drop();return false;}
 if(cache.flashColors){cache.flashNormal=new(std::nothrow) uint32_t[cache.flashColors];if(cache.flashNormal)for(unsigned i=0;i<cache.flashColors;++i){unsigned at=cache.flashOffset+i*104;cache.flashNormal[i]=LE32(cache.pages[at>>12]+(at&4095));}}
 pvzh1_actor_pose_diagnostic[0]=cache.size+cache.count*sizeof(Entry)+cache.colors*4+cache.npages*sizeof(void*);
 pvzh1_actor_pose_diagnostic[1]=cache.count;return true;
}
const Entry* Find(uint64_t key,uint32_t frame){unsigned low=0,high=cache.count;while(low<high){unsigned mid=(low+high)/2;const auto& e=cache.entries[mid];if(e.key<key||(e.key==key&&e.frame<frame))low=mid+1;else high=mid;}
 return low<cache.count&&cache.entries[low].key==key&&cache.entries[low].frame==frame?cache.entries+low:nullptr;}
}
void H1ActorPosePrepare(){
 if(!cache.tried&&cache.enabled){cache.tried=cache.busy=true;if(!Load())++pvzh1_actor_pose_diagnostic[7];cache.busy=false;}
 if(cache.entries&&!sunCache.tried&&sunCache.enabled){SunScope scope(true);cache.tried=cache.busy=true;if(!Load("sun-poses.h1ap"))++pvzh1_actor_pose_diagnostic[7];cache.busy=false;}
 CountResident();
}
void H1ActorPoseNoteBlend(Reanimation* actor,int time){
 if(!actor||actor->mReanimationType!=REANIM_PEASHOOTER||time!=20||(actor->mFrameStart!=29&&actor->mFrameStart!=54))return;
 Blend* selected=nullptr;for(auto& b:blends)if(b.actor==actor){selected=&b;break;}else if(!selected||b.use<selected->use)selected=&b;
 *selected={actor,FrameHalf(actor),unsigned(time),++blendClock};
}
bool H1ActorPoseDraw(Reanimation* actor,Sexy::Graphics* g,int group){
 SunScope scope(actor&&actor->mReanimationType==REANIM_SUN);
 if(!cache.enabled||cache.busy||!cache.entries||!actor||!H1ActorPose::Supported(actor->mReanimationType))return false;
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
 uint64_t key;if(!H1ActorPose::Signature(actor,group,key,blendReady,flashReady)){++pvzh1_actor_pose_diagnostic[3];return false;}
 ReanimatorFrameTime time;actor->GetFrameTime(&time);unsigned frame=time.mAnimFrameBeforeInt*2;
 if(time.mAnimFrameAfterInt!=time.mAnimFrameBeforeInt){if(time.mFraction>=.75f)frame=time.mAnimFrameAfterInt*2;else if(time.mFraction>=.25f)++frame;}
 if(actor->mReanimationType==REANIM_SUN){frame=time.mAnimFrameBeforeInt*4;if(time.mAnimFrameAfterInt!=time.mAnimFrameBeforeInt){unsigned part=unsigned(time.mFraction*4+.5f);frame=part==4?time.mAnimFrameAfterInt*4:frame+part;}}
 if(blendReady)frame=blendFrame;
 const auto* entry=Find(key,frame);if(!entry){++pvzh1_actor_pose_diagnostic[2];return false;}
 Reader r(entry->offset);const int baseX=int16_t(r.U16()),baseY=int16_t(r.U16());const unsigned segments=r.U16(),flash=r.U16();
 if(flashReady&&!flash){++pvzh1_actor_pose_diagnostic[3];return false;}
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
  if(flash&&track>=0){if(track>=actor->mDefinition->mTracks.count){cache.busy=false;return false;}const auto& instance=actor->mTrackInstances[track];if(instance.mRenderGroup!=group||bool(instance.mImageOverride)!=drawn)continue;}
  {H1CanvasProtectedPixels drawing(target);auto* pixels=target->mBits.get();
   for(unsigned i=0;i<runs;++i){int x,y;unsigned flags;stream.Run(compact,x,y,flags);x+=baseX+dx;y+=baseY+dy;const unsigned n=flags&32767;
    if(y<cy||y>=cb||x>=cr||x+int(n)<=cx){stream.Skip(n*2);continue;}
    unsigned first=std::max(0,cx-x),last=std::min(int(n),cr-x);stream.Skip(first*2);auto* dest=pixels+y*480+x+first;
    if(flags&32768){for(unsigned j=first;j<last;++j){unsigned id=stream.U16();*dest++=flash?FlashColor(id,0):cache.palette[id];}}
    else for(unsigned j=first;j<last;++j){unsigned id=stream.U16();const uint32_t literal=flash?FlashColor(id,0):cache.palette[id],p=*dest,inverse=literal>>24;
     const uint32_t rb=(((p&0xff00ffu)*inverse)>>8)&0xff00ffu,green=(((p&0xff00u)*inverse)>>8)&0xff00u;
     *dest++=0xff000000u|((literal&0xffffffu)+(rb|green));}
    stream.Skip((n-last)*2);
   }
   if(flash&&flashShade){Reader add(runStart);for(unsigned i=0;i<runs;++i){int x,y;unsigned flags;add.Run(compact,x,y,flags);x+=baseX+dx;y+=baseY+dy;unsigned n=flags&32767;
     if(y<cy||y>=cb||x>=cr||x+int(n)<=cx){add.Skip(n*2);continue;}unsigned first=std::max(0,cx-x),last=std::min(int(n),cr-x);add.Skip(first*2);auto* dest=pixels+y*480+x+first;
     for(unsigned j=first;j<last;++j){uint32_t v=FlashColor(add.U16(),flashShade),p=*dest;unsigned red=((p>>16)&255)+((v>>16)&255),green=((p>>8)&255)+((v>>8)&255),blue=(p&255)+(v&255);*dest++=(p&0xff000000u)|(std::min(255u,red)<<16)|(std::min(255u,green)<<8)|std::min(255u,blue);}add.Skip((n-last)*2);
   }}
  }
  if(track>=0&&!flash){cache.busy=false;AttachmentDraw(actor->mTrackInstances[track].mAttachmentID,g,!drawn);cache.busy=true;}
 }
 target->BitsChanged();Sexy::H1ScreenNoteDraw(screen);cache.busy=false;++pvzh1_actor_pose_diagnostic[5];
 ++pvzh1_actor_pose_diagnostic[actor->mReanimationType==REANIM_SUN?12:8+unsigned(actor->mReanimationType==REANIM_SUNFLOWER?1:actor->mReanimationType==REANIM_ZOMBIE?2:actor->mReanimationType==REANIM_SODROLL?3:0)];return true;
}
bool H1SodOfflineBegin(Board* board){
 if(!cache.enabled||!cache.entries||!Find(0x534f44524f4c4c31ull,0)||!board||board->mApp->mGameMode!=GAMEMODE_ADVENTURE||!board->mApp->IsFirstTimeAdventureMode()||board->mX!=130||board->mY||(board->mLevel!=1&&board->mLevel!=2&&board->mLevel!=4))return false;
 for(unsigned frame=1;frame<=90;++frame)if(!Find(0x534f44524f4c4c31ull,frame))return false;
 sodBoard=board;sodStart=board->mCutScene->mCutsceneTime;pvzh1_sod_offline_diagnostic[0]=1;++pvzh1_sod_offline_diagnostic[3];return true;
}
bool H1SodOfflineDraw(Board* board,Sexy::Graphics* g){
 if(!cache.enabled||cache.busy||!cache.entries||board!=sodBoard||!board||!board->mCutScene||!g)return false;
 int age=board->mCutScene->mCutsceneTime-sodStart;if(age<0)return false;if(age>2250){sodBoard=nullptr;pvzh1_sod_offline_diagnostic[0]=0;return false;}
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
extern "C" void pvzh1_actor_pose_shutdown(){Drop();cache.tried=false;{SunScope scope(true);Drop();cache.tried=false;}CountResident();sodBoard=nullptr;for(auto& b:blends)b={};}
#ifdef PVZ_HOST_RUNNER
void H1ActorPoseTestEnable(bool v){cache.enabled=sunCache.enabled=v;}
#endif
