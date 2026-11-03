/* Replay pixels sampled by the original renderer at asset-build time.
 * No runtime matrix/UV calculation or sampling is used by this path.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "SelectorOffline.h"
#include "Lawn/Widget/GameSelector.h"
#include "Lawn/Widget/GameButton.h"
#include "Lawn/System/PlayerInfo.h"
#include "LawnApp.h"
#include "Resources.h"
#include "PvzpLib/Reanimator.h"
#include "PvzpLib/Attachment.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImage.h"
#include "graphics/GLImage.h"
#include "Common.h"
#include "../graphics/PhysicalScreen.h"
#include "../graphics/PixelCopy.h"
#include "../graphics/DirtyCanvas.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>
#include <limits>

extern "C" { volatile uint32_t pvzh1_selector_offline_diagnostic[16]; }
extern "C" { volatile uint32_t pvzh1_selector_cull_diagnostic[4]; }
#ifdef PVZH1_WIDESCREEN
extern "C" {volatile uint32_t pvzh1_selector_retained_diagnostic[4];}
#endif
namespace {
constexpr uint32_t Magic=0x424d3148u,Width=Sexy::H1ScreenWidth,Height=Sexy::H1ScreenHeight,Pixels=Width*Height;
constexpr uint32_t HeaderBytes=64,EntryBytes=40,MaximumEntries=1024;
/* A malformed file must not direct arbitrary amounts of RAM or file offsets.
 * This is a file-sanity bound, not a resident-buffer budget. Each entry owns
 * its own allocation; there is no large contiguous whole-bundle allocation. */
constexpr uint32_t MaximumFileBytes=64u*1024u*1024u;
struct Run {uint32_t destination;uint16_t count,kind;uint32_t word;};
static_assert(sizeof(Run)==12);
struct Entry {
    uint32_t kind=0,id=0,variant=0,color=0,runs=0,pixels=0,bytes=0,position=0,dimensions=0;
    unsigned char* data=nullptr;
};
struct WelcomePixel {uint32_t index,before,after;};
struct Welcome {
    uint32_t* before=nullptr;
    WelcomePixel* changes=nullptr;
    uint32_t count=0;
    char* name=nullptr;
    const void* profile=nullptr;
    const void* font=nullptr;
    bool capturing=false,ready=false;
};
struct VisibleEntry {const Entry* entry=nullptr;Run* runs=nullptr;uint32_t count=0;};
struct VisibleGroup {
    VisibleEntry entries[128]{};
    uint32_t count=0,bytes=0,skipped=0;
    bool ready=false,rejected=false,seeded=false;
    uint32_t seedRevision=0;
    uint64_t version=0;
};
struct State {
    GameSelector* owner=nullptr;
    Entry* entries=nullptr;
    uint32_t count=0,pose=0,tracks=0,resident=0;
    bool attempted=false,loaded=false;
    Welcome welcome;
    VisibleGroup background,foreground;
    unsigned char* foregroundOpaque=nullptr;
    uint32_t foregroundRevision=0;
    bool presentationScene=false;
#ifdef PVZH1_WIDESCREEN
    bool retainedScene=false;
    unsigned foregroundPrefix=0;
    uint32_t* menuPixels=nullptr;
    uint32_t restored[H1DirtyCanvas::Rows]{};
#endif
} state;
uint64_t visibilitySerial=0;
#ifdef PVZ_HOST_RUNNER
bool testEnabled=true;
bool testRejectVisibility=false;
#endif
template<class T>T* VisibilityAllocate(size_t count){
#ifdef PVZ_HOST_RUNNER
    if(testRejectVisibility)return nullptr;
#endif
    return new(std::nothrow) T[count];
}
void Add(unsigned i,uint32_t n=1){pvzh1_selector_offline_diagnostic[i]=pvzh1_selector_offline_diagnostic[i]+n;}
void Count(){
    pvzh1_selector_offline_diagnostic[0]=1;
    pvzh1_selector_offline_diagnostic[1]=state.loaded?state.count:0;
    pvzh1_selector_offline_diagnostic[2]=state.resident;
    if(state.resident>pvzh1_selector_offline_diagnostic[3])pvzh1_selector_offline_diagnostic[3]=state.resident;
}
uint32_t Word(const unsigned char* p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
uint16_t Half(const unsigned char* p){return (uint16_t)((unsigned)p[0]|(unsigned)p[1]<<8);}
void ClearWelcome(){
    auto& w=state.welcome;
    if(w.before){delete[] w.before;state.resident-=Pixels*4;}
    if(w.changes){delete[] w.changes;state.resident-=w.count*sizeof(WelcomePixel);}
    if(w.name){state.resident-=(uint32_t)std::strlen(w.name)+1;delete[] w.name;}
    w=Welcome{};Count();
}
void ClearVisible(VisibleGroup& group){
    for(uint32_t i=0;i<group.count;++i)delete[] group.entries[i].runs;
    state.resident-=group.bytes;group=VisibleGroup{};
}
void Clear(){
#ifdef PVZH1_WIDESCREEN
    if(h1DirtyCanvas.beforeContext==&state){h1DirtyCanvas.beforeDraw=nullptr;h1DirtyCanvas.beforeContext=nullptr;}
#endif
    ClearWelcome();
    ClearVisible(state.background);ClearVisible(state.foreground);
    if(state.foregroundOpaque){delete[] state.foregroundOpaque;state.resident-=Pixels;}
    for(uint32_t i=0;i<state.count;++i)delete[] state.entries[i].data;
    delete[] state.entries;
    state=State{};Count();
}
bool Read(FILE* file,void* buffer,size_t bytes){return !bytes||std::fread(buffer,1,bytes,file)==bytes;}
bool Range(uint32_t offset,uint32_t bytes,uint32_t end){return offset<=end&&bytes<=end-offset;}
bool Validate(Entry& e){
    if(e.kind<1||e.kind>2||e.runs>Pixels*4u||e.pixels>Pixels*4u)return false;
    if((uint64_t)e.runs*12u+(uint64_t)e.pixels*4u!=e.bytes)return false;
    const unsigned char* run=e.data;
    for(uint32_t i=0;i<e.runs;++i,run+=12){
        const uint32_t destination=Word(run),word=Word(run+8);
        const uint16_t count=Half(run+4),kind=Half(run+6);
        if(!count||kind>1||destination>=Pixels||count>Width-destination%Width||
           count>Pixels-destination||word>e.pixels||count>e.pixels-word)return false;
        if(kind==0){const unsigned char* p=e.data+(size_t)e.runs*12+(size_t)word*4;
            for(unsigned j=0;j<count;++j,p+=4)if(p[3]!=255)return false;
        }
    }
    return true;
}
bool Load(const char* filename){
    FILE* file=std::fopen(filename,"rb");
    if(!file){Add(8);return false;}
    unsigned char header[HeaderBytes]{};bool okay=Read(file,header,sizeof(header));
    uint32_t h[16]{};for(unsigned i=0;i<16;++i)h[i]=Word(header+i*4);
    okay=okay&&h[0]==Magic&&h[1]==1&&h[2]==HeaderBytes&&h[3]>=HeaderBytes&&h[3]<=MaximumFileBytes&&
        h[4]==Width&&h[5]==Height&&h[6]==Sexy::H1LogicalWidth&&h[7]==Sexy::H1LogicalHeight&&h[8]&&h[8]<=MaximumEntries&&
        h[9]==HeaderBytes&&h[10]==EntryBytes&&h[12]&&h[12]<=128&&
        Range(h[9],h[8]*EntryBytes,h[3]);
    if(okay){okay=std::fseek(file,0,SEEK_END)==0;long actual=okay?std::ftell(file):-1;
        okay=actual>=0&&(uint64_t)actual==h[3]&&std::fseek(file,HeaderBytes,SEEK_SET)==0;}
    Entry* entries=okay?new(std::nothrow) Entry[h[8]]:nullptr;
    if(okay&&!entries){Add(10);okay=false;}
    unsigned char* directory=okay?new(std::nothrow) unsigned char[h[8]*EntryBytes]:nullptr;
    if(okay&&!directory){Add(10);okay=false;}
    if(okay)okay=Read(file,directory,(size_t)h[8]*EntryBytes);
    uint32_t resident=entries?h[8]*(uint32_t)sizeof(Entry):0;
    uint32_t position=HeaderBytes+h[8]*EntryBytes;
    for(uint32_t i=0;okay&&i<h[8];++i){
        const unsigned char* row=directory+(size_t)i*EntryBytes;
        auto& e=entries[i];e.kind=Word(row);e.id=Word(row+4);e.variant=Word(row+8);e.color=Word(row+12);
        e.runs=Word(row+16);e.pixels=Word(row+20);const uint32_t offset=Word(row+24);
        e.bytes=Word(row+28);e.position=Word(row+32);e.dimensions=Word(row+36);
        okay=offset>=HeaderBytes+h[8]*EntryBytes&&Range(offset,e.bytes,h[3])&&
            e.bytes<=(uint64_t)Pixels*4u*16u;
        if(!okay)break;
        for(uint32_t j=0;j<i;++j)if(entries[j].kind==e.kind&&entries[j].id==e.id&&entries[j].variant==e.variant&&entries[j].color==e.color){okay=false;break;}
        if(!okay)break;
        if(e.bytes){e.data=new(std::nothrow) unsigned char[e.bytes];if(!e.data){Add(10);okay=false;break;}resident+=e.bytes;}
        // The packer emits entries in offset order. Read that common case
        // sequentially: no directory/payload seeks or per-frame file I/O.
        okay=(offset==position||std::fseek(file,offset,SEEK_SET)==0)&&Read(file,e.data,e.bytes)&&Validate(e);
        position=offset+e.bytes;
    }
    delete[] directory;
    std::fclose(file);
    if(!okay){if(entries){for(uint32_t i=0;i<h[8];++i)delete[] entries[i].data;delete[] entries;}Add(9);return false;}
    state.entries=entries;state.count=h[8];state.pose=h[11];state.tracks=h[12];state.resident+=resident;state.loaded=true;
    Add(11);Count();return true;
}
#ifndef PVZH1_SELECTOR_OFFLINE_STANDALONE
Sexy::H1PhysicalScreen* Screen(Sexy::Graphics* g){
    if(!g||!g->mDestImage)return nullptr;
    auto* image=dynamic_cast<Sexy::GLImage*>(g->mDestImage);
    auto* screen=image?Sexy::H1GetPhysicalScreen(image):nullptr;
    if(!screen||!screen->Active()||screen->mWidth!=(int)Sexy::H1LogicalWidth||screen->mHeight!=(int)Sexy::H1LogicalHeight||
       screen->Target()->mWidth!=(int)Width||screen->Target()->mHeight!=(int)Height||
       g->mDrawMode!=Sexy::Graphics::DRAWMODE_NORMAL||g->mScaleX!=1.0f||g->mScaleY!=1.0f||
       (g->mColorizeImages&&g->mColor!=Sexy::Color::White)||
       g->mClipRect!=Sexy::Rect(0,0,Sexy::H1LogicalWidth,Sexy::H1LogicalHeight))return nullptr;
    return screen;
}
bool Idle(GameSelector* selector){
    return selector&&selector->mApp&&selector->mApp->mGameSelector.get()==selector&&
        selector->mSelectorState==SELECTOR_IDLE&&!selector->mStartingGame&&!selector->mLoading&&
        !selector->mSlideCounter&&!selector->mX&&!selector->mY;
}
bool Ready(GameSelector* selector){
#ifdef PVZ_HOST_RUNNER
    if(!testEnabled)return false;
#endif
    if(!Idle(selector))return false;
    if(state.owner!=selector){Clear();state.owner=selector;}
    if(!state.attempted){state.attempted=true;
        #ifdef PVZH1_WIDESCREEN
        std::string path=Sexy::GetResourcePath("h1menu/selector-wide.h1mb");
#else
        std::string path=Sexy::GetResourcePath("h1menu/selector.h1mb");
#endif
#ifdef PVZ_HOST_RUNNER
        if(const char* test=std::getenv("PVZ_HOST_SELECTOR_BUNDLE"))path=test;
#endif
        Load(path.c_str());}
    return state.loaded;
}
#endif
Entry* Find(uint32_t kind,uint32_t id,uint32_t variant,uint32_t color){
    for(uint32_t i=0;i<state.count;++i){auto& e=state.entries[i];
        if(e.kind==kind&&e.id==id&&e.variant==variant&&e.color==color)return &e;}
    return nullptr;
}
void MarkPixels(uint32_t* pixels,uint32_t destination,unsigned count){
    if(pixels==h1DirtyCanvas.pixels)h1DirtyCanvas.Mark(h1DirtyCanvas.owner,destination%Width,destination/Width,count,1);
}
void Replay(const Entry& e,uint32_t* pixels,bool track=true){
    if(!e.runs)return; // An explicit transparent/marker entry owns no payload.
    const unsigned char* run=e.data;
    const unsigned char* words=e.data+(size_t)e.runs*12;
    for(uint32_t i=0;i<e.runs;++i,run+=12){
        const uint32_t destination=Word(run),word=Word(run+8);
        const unsigned count=Half(run+4),kind=Half(run+6);uint32_t* out=pixels+destination;
        if(track)MarkPixels(pixels,destination,count);
        const unsigned char* input=words+(size_t)word*4;
        if(!kind){std::memcpy(out,input,(size_t)count*4);continue;}
        for(unsigned j=0;j<count;++j,input+=4){const uint32_t literal=Word(input),inverse=literal>>24,p=out[j];
            const uint32_t rb=(((p&0xff00ffu)*inverse)>>8)&0xff00ffu;
            const uint32_t green=(((p&0xff00u)*inverse)>>8)&0xff00u;
            out[j]=0xff000000u|((literal&0xffffffu)+(rb|green));}
    }
}
#ifndef PVZH1_SELECTOR_OFFLINE_STANDALONE
/* A later opaque fragment replaces everything previously drawn at its pixel.
 * Keep partial fragments in their original order and keep every attachment at
 * its original track boundary. Plans only store slices of the offline runs;
 * no image resampling, matrix calculation or premultiplication is introduced.
 * The backdrop may also omit pixels the validated foreground will overwrite. */
bool SameVisible(const VisibleGroup& plan,Entry* const* selected,unsigned count,bool seeded){
    if((!plan.ready&&!plan.rejected)||plan.count!=count||plan.seeded!=seeded||
       (seeded&&plan.seedRevision!=state.foregroundRevision))return false;
    for(unsigned i=0;i<count;++i)if(plan.entries[i].entry!=selected[i])return false;
    return true;
}
void RememberVisibleKey(VisibleGroup& plan,Entry* const* selected,unsigned count,bool seeded){
    plan.count=count;plan.seeded=seeded;plan.seedRevision=state.foregroundRevision;
    for(unsigned i=0;i<count;++i)plan.entries[i].entry=selected[i];
}
bool BuildVisible(VisibleGroup& plan,Entry* const* selected,unsigned count,bool seeded,bool publishOpaque){
    if(SameVisible(plan,selected,count,seeded))return plan.ready;
    ClearVisible(plan);
    RememberVisibleKey(plan,selected,count,seeded);
    // A run describes an entire visible span, not one pixel. Start small and
    // grow only when an actual span is emitted; reserving by pixel count both
    // wasted RAM and made a failed large allocation repeat every frame.
    uint32_t capacity=128;
    auto* covered=VisibilityAllocate<unsigned char>(Pixels);
    auto* scratch=VisibilityAllocate<Run>(capacity);
    if(!covered||!scratch){delete[] covered;delete[] scratch;plan.rejected=true;Add(10);return false;}
    if(seeded)std::memcpy(covered,state.foregroundOpaque,Pixels);
    else std::memset(covered,0,Pixels);
    plan.count=count;plan.seeded=seeded;plan.seedRevision=state.foregroundRevision;
    bool okay=true;
    for(unsigned remaining=count;remaining&&okay;){
        const unsigned index=--remaining;const Entry& e=*selected[index];
        auto& output=plan.entries[index];output.entry=&e;
        uint32_t used=0,visiblePixels=0;
        for(uint32_t nr=e.runs;nr&&okay;){
            const unsigned char* r=e.data+(size_t)(--nr)*12;
            const uint32_t destination=Word(r),word=Word(r+8);
            const uint16_t count=Half(r+4),kind=Half(r+6);
            for(uint32_t n=count;n&&okay;){
                const uint32_t j=--n,dst=destination+j,src=word+j;
                if(!covered[dst]){
                    ++visiblePixels;
                    if(used&&scratch[used-1].kind==kind&&scratch[used-1].destination==dst+1&&
                       scratch[used-1].word==src+1&&scratch[used-1].count<65535){
                        --scratch[used-1].destination;--scratch[used-1].word;++scratch[used-1].count;
                    }else{
                        if(used==capacity){
                            const uint32_t next=capacity<=Pixels*2?capacity*2:0;
                            Run* grown=next?VisibilityAllocate<Run>(next):nullptr;
                            if(!grown){okay=false;break;}
                            std::memcpy(grown,scratch,(size_t)used*sizeof(Run));
                            delete[] scratch;scratch=grown;capacity=next;
                        }
                        scratch[used++]={dst,1,kind,src};
                    }
                }
                if(!kind)covered[dst]=1;
            }
        }
        if(!okay)break;
        output.runs=used?VisibilityAllocate<Run>(used):nullptr;
        if(used&&!output.runs){okay=false;break;}
        output.count=used;plan.bytes+=used*sizeof(Run);state.resident+=used*sizeof(Run);
        for(uint32_t i=0;i<used;++i)output.runs[i]=scratch[used-i-1];
        plan.skipped+=e.pixels-visiblePixels;
    }
    delete[] scratch;
    if(okay&&publishOpaque){
        if(!state.foregroundOpaque)state.resident+=Pixels;
        delete[] state.foregroundOpaque;state.foregroundOpaque=covered;covered=nullptr;
        ++state.foregroundRevision;
    }
    delete[] covered;
    if(!okay){ClearVisible(plan);RememberVisibleKey(plan,selected,count,seeded);plan.rejected=true;Add(10);Count();return false;}
    plan.ready=true;
    if(visibilitySerial!=UINT64_MAX)plan.version=++visibilitySerial;
    Add(14);Count();return true;
}
void ReplayVisible(const VisibleEntry& visible,uint32_t* pixels,bool track=true,const uint32_t* tiles=nullptr){
    const Entry& entry=*visible.entry;
    const uint32_t* words=(const uint32_t*)(entry.data+(size_t)entry.runs*12);
    for(uint32_t i=0;i<visible.count;++i){const Run& r=visible.runs[i];
        if(track)MarkPixels(pixels,r.destination,r.count);
        auto draw=[&](unsigned offset,unsigned count){
            uint32_t* out=pixels+r.destination+offset;const uint32_t* input=words+r.word+offset;
            if(!r.kind){H1CopyPixels(out,input,count);return;}
            for(unsigned j=0;j<count;++j){const uint32_t literal=input[j],inverse=literal>>24,p=out[j];
                const uint32_t rb=(((p&0xff00ffu)*inverse)>>8)&0xff00ffu;
                const uint32_t green=(((p&0xff00u)*inverse)>>8)&0xff00u;
                out[j]=0xff000000u|((literal&0xffffffu)+(rb|green));}
        };
        if(!tiles){draw(0,r.count);continue;}
        const unsigned x=r.destination%Width,y=r.destination/Width;
        const uint32_t mask=tiles[y/H1DirtyCanvas::TileHeight];
        if(!mask)continue;if(mask==H1DirtyCanvas::All){draw(0,r.count);continue;}
        for(unsigned offset=0;offset<r.count;){
            const unsigned at=x+offset,count=std::min<unsigned>(r.count-offset,H1DirtyCanvas::TileWidth-at%H1DirtyCanvas::TileWidth);
            if(mask&(uint32_t(1)<<(at/H1DirtyCanvas::TileWidth)))draw(offset,count);
            offset+=count;
        }
    }
}
#ifdef PVZH1_WIDESCREEN
void RestoreMenuMask(const uint32_t* desired){
    if(!state.retainedScene||state.menuPixels!=h1DirtyCanvas.pixels)return;
    uint32_t needed[H1DirtyCanvas::Rows]{};bool changed=false;
    for(unsigned row=0;row<H1DirtyCanvas::Rows;++row){
        const uint32_t mask=desired[row]&~state.restored[row];needed[row]=mask;state.restored[row]|=mask;
        if(!mask)continue;changed=true;
        for(unsigned remaining=mask;remaining;remaining&=remaining-1){const unsigned tile=__builtin_ctz(remaining);
            pvzh1_selector_retained_diagnostic[1]=pvzh1_selector_retained_diagnostic[1]+1;
            for(unsigned y=row*H1DirtyCanvas::TileHeight;y<(row+1)*H1DirtyCanvas::TileHeight;++y){
                uint32_t* out=state.menuPixels+y*Width+tile*H1DirtyCanvas::TileWidth;
                for(unsigned x=0;x<H1DirtyCanvas::TileWidth;x+=4)out[x]=out[x+1]=out[x+2]=out[x+3]=0xff000000u;
            }
        }
    }
    if(changed){
        for(unsigned i=0;i<state.background.count;++i)ReplayVisible(state.background.entries[i],state.menuPixels,false,needed);
        // A foreground attachment may enter a previously untouched tile. It
        // needs the static prefix at this exact track boundary, then later
        // tracks will paint their suffix over it in the original order.
        for(unsigned i=0;i<state.foregroundPrefix;++i)ReplayVisible(state.foreground.entries[i],state.menuPixels,false,needed);
        if(state.foregroundPrefix)pvzh1_selector_retained_diagnostic[2]=pvzh1_selector_retained_diagnostic[2]+1;
    }
}
void RestoreMenuBeforeDraw(void* context,unsigned first,unsigned last,uint32_t mask){
    if(context!=&state||!state.retainedScene)return;
    bool needed=false;for(unsigned row=first;row<=last;++row)needed|=(mask&~state.restored[row])!=0;
    if(!needed)return;
    uint32_t desired[H1DirtyCanvas::Rows]{};for(unsigned row=first;row<=last;++row)desired[row]=mask;
    RestoreMenuMask(desired);
}
void RejectRetainedMenu(){
    h1DirtyCanvas.beforeDraw=nullptr;h1DirtyCanvas.beforeContext=nullptr;
    if(!state.retainedScene)return;
    uint32_t all[H1DirtyCanvas::Rows];for(auto& mask:all)mask=H1DirtyCanvas::All;
    RestoreMenuMask(all);state.retainedScene=false;state.presentationScene=false;
    h1DirtyCanvas.keyValid=false;h1DirtyCanvas.MarkAll();
}
#endif

#endif
#ifndef PVZH1_SELECTOR_OFFLINE_STANDALONE
bool TrackVariant(unsigned index,const ReanimatorTrackInstance& t,uint32_t& variant){
    variant=0;
    if(t.mBlendCounter||t.mShakeX!=0||t.mShakeY!=0)return false;
    if(!t.mImageOverride)return true;
    (void)index;
    if(t.mImageOverride==Sexy::IMAGE_REANIM_SELECTORSCREEN_WOODSIGN2_PRESS||t.mImageOverride==Sexy::IMAGE_REANIM_SELECTORSCREEN_WOODSIGN3_PRESS)variant=1;
    else if(t.mImageOverride==Sexy::IMAGE_REANIM_SELECTORSCREEN_ALMANAC_SHADOW)variant=2;
    else if(t.mImageOverride==Sexy::IMAGE_REANIM_SELECTORSCREEN_KEY_SHADOW)variant=3;
    else return false;
    return true;
}
bool SelectorPose(const Reanimation* animation){
    if(!animation||animation->mDead||animation->mReanimationType!=REANIM_SELECTOR_SCREEN||
       !animation->mDefinition||animation->mDefinition->mTracks.count!=(int)state.tracks||
       animation->mFrameStart+animation->mFrameCount-1!=(int)state.pose||animation->mAnimTime!=1.0f||
       animation->mLoopCount<=0||animation->mColorOverride!=Sexy::Color::White||
       animation->mEnableExtraAdditiveDraw||animation->mEnableExtraOverlayDraw||animation->mFilterEffect!=FILTER_EFFECT_NONE)return false;
    const auto& m=animation->mOverlayMatrix;
    return m.m00==1&&m.m01==0&&m.m02==0.5f&&m.m10==0&&m.m11==1&&m.m12==0.5f&&m.m20==0&&m.m21==0&&m.m22==1;
}
bool SelectGroup(Reanimation* animation,int group,Entry** selected,unsigned* indices,unsigned& count){
    count=0;
    for(unsigned i=0;i<state.tracks;++i){const auto& track=animation->mTrackInstances[i];if(track.mRenderGroup!=group)continue;
        uint32_t variant=0;if(!TrackVariant(i,track,variant))return false;
        auto* e=Find(1,i,variant,track.mTrackColor.ToInt());if(!e){Add(12);return false;}
        if(track.mAttachmentID!=ATTACHMENTID_NULL&&!e->runs)return false;
        indices[count]=i;selected[count++]=e;
    }
    return true;
}
#endif
}

#ifndef PVZH1_SELECTOR_OFFLINE_STANDALONE
bool H1SelectorSkipClippedAchievements(Sexy::Graphics* g){
#ifdef PVZ_HOST_RUNNER
    if(!testEnabled)return false;
#endif
    // This particular page draws only inside its inherited clip and never
    // resets it. Its 70 tiles and all achievement text otherwise do layout and
    // source preparation every menu frame, even while parked below the LCD.
    if(!g||(g->mClipRect.mWidth>0&&g->mClipRect.mHeight>0))return false;
    auto* image=dynamic_cast<Sexy::GLImage*>(g->mDestImage);
    auto* screen=image?Sexy::H1GetPhysicalScreen(image):nullptr;
    if(!screen||!screen->Active())return false;
    ++pvzh1_selector_cull_diagnostic[0];return true;
}
bool H1SelectorOfflineDrawGroup(GameSelector* selector,Sexy::Graphics* g,Reanimation* animation,int group){
    // A rejected background must not inherit the prior frame's declaration
    // that its foreground is static, even if that foreground still matches.
    if(group==1){state.presentationScene=false;
#ifdef PVZH1_WIDESCREEN
        state.retainedScene=false;state.foregroundPrefix=0;h1DirtyCanvas.beforeDraw=nullptr;h1DirtyCanvas.beforeContext=nullptr;
#endif
    }
#ifdef PVZH1_WIDESCREEN
    if(group==RENDER_GROUP_NORMAL){state.foregroundPrefix=0;h1DirtyCanvas.beforeDraw=nullptr;h1DirtyCanvas.beforeContext=nullptr;}
    auto reject=[&](){if(group==RENDER_GROUP_NORMAL)RejectRetainedMenu();Add(7);return false;};
#else
    auto reject=[&](){Add(7);return false;};
#endif
    auto* screen=Screen(g);
    if(!screen||g->mTransX!=0||g->mTransY!=0||!Ready(selector)||!SelectorPose(animation))return reject();
    Entry* selected[128]{};unsigned indices[128]{};unsigned count=0;
    if(!SelectGroup(animation,group,selected,indices,count))return reject();
    VisibleGroup* visible=nullptr;
    if(group==1){
        Entry* normal[128]{};unsigned normalIndices[128]{},normalCount=0;
        const bool foregroundReady=SelectGroup(animation,RENDER_GROUP_NORMAL,normal,normalIndices,normalCount)&&
            BuildVisible(state.foreground,normal,normalCount,false,true);
        if(BuildVisible(state.background,selected,count,foregroundReady,false))visible=&state.background;
        if(visible&&foregroundReady&&state.background.version&&state.foreground.version){
            const uint64_t sceneKey[4]={0x53454c4543544f52ull,(uint64_t)(uintptr_t)selector,state.background.version,state.foreground.version};
#ifdef PVZH1_WIDESCREEN
            bool attachments=false;
            for(unsigned i=0;i<count;++i)attachments|=animation->mTrackInstances[indices[i]].mAttachmentID!=ATTACHMENTID_NULL;
            state.presentationScene=h1DirtyCanvas.Root(screen->Target(),sceneKey,sizeof(sceneKey),attachments);
            if(state.presentationScene&&!attachments){
                pvzh1_selector_retained_diagnostic[0]=pvzh1_selector_retained_diagnostic[0]+1;
                state.retainedScene=true;state.menuPixels=screen->Target()->mBits.get();std::memset(state.restored,0,sizeof(state.restored));
                if(h1DirtyCanvas.limited)RestoreMenuMask(h1DirtyCanvas.previous);
                else {uint32_t all[H1DirtyCanvas::Rows];for(auto& mask:all)mask=H1DirtyCanvas::All;RestoreMenuMask(all);++h1DirtyCanvas.fullClears;}
                h1DirtyCanvas.beforeContext=&state;h1DirtyCanvas.beforeDraw=RestoreMenuBeforeDraw;
            }
#else
            state.presentationScene=h1DirtyCanvas.Root(screen->Target(),sceneKey,sizeof(sceneKey),true);
#endif
        }
    }else if(group==RENDER_GROUP_NORMAL){
        if(BuildVisible(state.foreground,selected,count,false,true))visible=&state.foreground;
    }
#ifdef PVZH1_WIDESCREEN
    if(group==RENDER_GROUP_NORMAL&&state.retainedScene&&!visible)RejectRetainedMenu();
#endif
    H1CanvasProtectedPixels canvasPixels(screen->Target());
    uint32_t* target=screen->Target()->GetBits();
    for(unsigned i=0;i<count;++i){
#ifdef PVZH1_WIDESCREEN
        if(state.retainedScene&&group==1)continue; // restored once before cloud fragments
        const uint32_t* tiles=state.retainedScene?state.restored:nullptr;
#else
        const uint32_t* tiles=nullptr;
#endif
        if(visible)ReplayVisible(visible->entries[i],target,!state.presentationScene,tiles);else Replay(*selected[i],target,!state.presentationScene);
        auto& attachment=animation->mTrackInstances[indices[i]].mAttachmentID;
        if(attachment!=ATTACHMENTID_NULL){
#ifdef PVZH1_WIDESCREEN
            if(state.retainedScene&&group==RENDER_GROUP_NORMAL){
                state.foregroundPrefix=i+1;h1DirtyCanvas.beforeContext=&state;h1DirtyCanvas.beforeDraw=RestoreMenuBeforeDraw;
            }
#endif
            AttachmentDraw(attachment,g,false);
#ifdef PVZH1_WIDESCREEN
            if(group==RENDER_GROUP_NORMAL){h1DirtyCanvas.beforeContext=nullptr;h1DirtyCanvas.beforeDraw=nullptr;}
#endif
        }}
    if(count){screen->Target()->BitsChanged();screen->NoteDraw();}
    if(visible)Add(15,visible->skipped);
    Add(4);return true;
}

bool H1SelectorOfflineDrawButton(NewLawnButton* button,Sexy::Graphics* g){
    if(!button||!button->mParent)return false;
    auto* selector=dynamic_cast<GameSelector*>(button->mParent);if(!selector)return false;
    if(button->mBtnNoDraw)return true;
    auto* screen=Screen(g);
    if(!screen||!Ready(selector)||!SelectorPose(selector->mApp->ReanimationGet(selector->mSelectorReanimID))||
       !button->mLabel.empty()||button->mComponentImage||g->mTransX!=(float)button->mX||g->mTransY!=(float)button->mY)return false;
    uint32_t variant=0;
    const bool down=(button->mIsDown&&button->mIsOver&&!button->mDisabled)^button->mInverted;
    if(down)variant=2;
    else if(button->mDisabled&&button->HaveButtonImage(button->mDisabledImage,button->mDisabledRect))variant=3;
    else if(button->mOverAlpha>0&&button->HaveButtonImage(button->mOverImage,button->mOverRect)){
        if(button->mOverAlpha!=1.0)return false;variant=1;
    }else if((button->mIsOver||button->mIsDown)&&button->HaveButtonImage(button->mOverImage,button->mOverRect))variant=1;
    if(button->mId==100&&selector->mShowStartButton)variant|=256;
    auto* e=Find(2,(uint32_t)button->mId,variant,button->mColors.mBkg.ToInt());if(!e){Add(12);return false;}
    const int x=(int16_t)(e->position&65535),y=(int16_t)(e->position>>16);
    if(button->mX!=x||button->mY!=y||button->mWidth!=(int)(e->dimensions&65535)||button->mHeight!=(int)(e->dimensions>>16))return false;
    H1CanvasProtectedPixels canvasPixels(screen->Target());
    Replay(*e,screen->Target()->GetBits());if(e->runs){screen->Target()->BitsChanged();screen->NoteDraw();}
    // Match the original button's final Graphics state, even for empty labels.
    g->SetColorizeImages(false);
    if(down||button->mIsOver){g->SetFont(button->mHiliteFont?button->mHiliteFont:button->mFont.get());g->SetColor(button->mColors.mLabelHilite);}
    else {g->SetFont(button->mFont.get());g->SetColor(button->mColors.mLabel);}
    Add(5);return true;
}

bool H1SelectorOfflineWelcomeBegin(GameSelector* selector,Sexy::Graphics* g){
    auto* screen=Screen(g);
    if(!screen||g->mTransX!=0||g->mTransY!=0||!Ready(selector)||!selector->mApp->mPlayerInfo||
       !SelectorPose(selector->mApp->ReanimationGet(selector->mSelectorReanimID)))return false;
    auto& w=state.welcome;const auto* profile=selector->mApp->mPlayerInfo;const auto& name=profile->mName;
    H1CanvasProtectedPixels canvasPixels(screen->Target());
    uint32_t* target=screen->Target()->GetBits();
    if(w.ready&&w.profile==profile&&w.font==Sexy::FONT_BRIANNETOD16&&w.name&&name==w.name){
        bool same=true;for(uint32_t i=0;i<w.count;++i)if(target[w.changes[i].index]!=w.changes[i].before){same=false;break;}
        if(same){for(uint32_t i=0;i<w.count;++i){MarkPixels(target,w.changes[i].index,1);target[w.changes[i].index]=w.changes[i].after;}
            if(w.count){screen->Target()->BitsChanged();screen->NoteDraw();}Add(6);return true;}
    }
    ClearWelcome();
    w.before=new(std::nothrow) uint32_t[Pixels];
    if(!w.before){Add(10);return false;}state.resident+=Pixels*4;
    w.name=new(std::nothrow) char[name.size()+1];
    if(!w.name){ClearWelcome();Add(10);return false;}
    std::memcpy(w.name,name.c_str(),name.size()+1);state.resident+=(uint32_t)name.size()+1;
    w.profile=profile;w.font=Sexy::FONT_BRIANNETOD16;w.capturing=true;
    std::memcpy(w.before,target,Pixels*4);Count();return false;
}
void H1SelectorOfflineWelcomeEnd(GameSelector* selector,Sexy::Graphics* g){
    if(state.owner!=selector||!state.welcome.capturing)return;
    auto* screen=Screen(g);if(!screen){ClearWelcome();return;}
    H1CanvasProtectedPixels canvasPixels(screen->Target());
    auto& w=state.welcome;const uint32_t* target=screen->Target()->GetBits();uint32_t count=0;
    for(uint32_t i=0;i<Pixels;++i)if(w.before[i]!=target[i])++count;
    WelcomePixel* changes=count?new(std::nothrow) WelcomePixel[count]:nullptr;
    if(count&&!changes){ClearWelcome();Add(10);return;}
    uint32_t n=0;for(uint32_t i=0;i<Pixels;++i)if(w.before[i]!=target[i])changes[n++]={i,w.before[i],target[i]};
    delete[] w.before;state.resident-=Pixels*4;w.before=nullptr;w.changes=changes;w.count=count;
    state.resident+=count*sizeof(WelcomePixel);w.capturing=false;w.ready=true;Add(13);Count();
}
#endif
void H1SelectorOfflineRelease(GameSelector* selector){if(state.owner==selector)Clear();}
#ifdef PVZ_HOST_RUNNER
void H1SelectorOfflineTestEnable(bool enabled){testEnabled=enabled;}
void H1SelectorOfflineTestRejectVisibility(bool enabled){testRejectVisibility=enabled;}
#endif
