/* Optional replay cache. Every recorded sample comes from the original SWTri
 * scan conversion. It never flattens images or retains borrowed pixel storage.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "RenderPlanCache.h"
#include "ScreenLayout.h"
#include "PixelCopy.h"
#include "DirtyCanvas.h"
#include "TextReplay.h"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <algorithm>
#ifndef PVZH1_PLAN_STANDALONE
#include "SoftwareRaster.h"
#include "MemoryImageCache.h"
#include "PhysicalScreen.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "SexyAppBase.h"
#include <cmath>
#include <bit>
#endif

extern "C" {
volatile uint32_t pvzh1_render_plan_diagnostic[24];
uint32_t pvzh1_render_plan_capture_active;
}
namespace Sexy { namespace {
using namespace H1PlanTest;
constexpr unsigned Entries=64,PageBytes=3072,PageLimit=21,RowLimit=H1ScreenWidth;
constexpr std::size_t Budget=512u*1024u;
enum :uint8_t {Empty,Candidate,Count,Fill,Ready,Rejected};
enum :uint8_t {Opaque=0,Partial=1,Copy=2,KindMask=3,Row=4,Interior=8};
struct Op {uint32_t destination,u,v;int32_t du,dv;uint16_t count;uint8_t kind,pad;};
static_assert(sizeof(Op)==24);
constexpr unsigned PerPage=PageBytes/sizeof(Op);
struct Summary {uint32_t ops=0,rows=0,pixels=0,transparent=0,copy=0,opaque=0,partial=0;};
struct Bounds {unsigned left=0,top=0,right=0,bottom=0;};
struct Entry {
    Key key{};Hooks hooks{};uint64_t use=0;uint8_t phase=Empty;
    Op* pages[PageLimit]{};unsigned pageCount=0;Summary expected{};Bounds bounds{};bool completeOpaque=false;
    Bounds coverageBounds{};
};
struct Active {
    Entry* entry=nullptr;Hooks hooks{};View view{};uint32_t* destination=nullptr;
    uint32_t width=0,height=0,token=0;bool failed=false;Summary summary{};
};
struct State {Entry entries[Entries]{};Active active{};uint64_t clock=0;uint32_t serial=0;std::size_t capacity=0;bool busy=false,borrowing=false;};
State state;
constexpr std::size_t Metadata=sizeof(State)+sizeof(pvzh1_render_plan_diagnostic);
void Add(unsigned at,uint32_t amount=1){pvzh1_render_plan_diagnostic[at]=pvzh1_render_plan_diagnostic[at]+amount;}
void Stats(){
    unsigned live=0;for(const auto& e:state.entries)if(e.phase==Ready)++live;
    pvzh1_render_plan_diagnostic[0]=live;
    pvzh1_render_plan_diagnostic[1]=(uint32_t)state.capacity;
    if(state.capacity>pvzh1_render_plan_diagnostic[2])pvzh1_render_plan_diagnostic[2]=(uint32_t)state.capacity;
    pvzh1_render_plan_diagnostic[23]=(uint32_t)Metadata;
}
bool Same(const Key&a,const Key&b){return a.source==b.source&&a.destination==b.destination&&a.revision==b.revision&&!std::memcmp(a.words,b.words,sizeof(a.words));}
std::size_t Drop(Entry& e){
    const bool wasBusy=state.busy;state.busy=true;
    std::size_t bytes=(std::size_t)e.pageCount*PageBytes;
    for(unsigned i=0;i<e.pageCount;++i)e.hooks.release(e.hooks.context,e.pages[i]);
    state.capacity-=bytes;e=Entry{};Stats();state.busy=wasBusy;return bytes;
}
Entry* Oldest(Entry* exclude=nullptr,bool allocatedOnly=false){
    Entry* result=nullptr;
    for(auto& e:state.entries)if(&e!=exclude&&e.phase!=Empty&&(!allocatedOnly||e.pageCount)&&(!result||e.use<result->use))result=&e;
    return result;
}
Op& At(Entry& e,unsigned index){return e.pages[index/PerPage][index%PerPage];}
void Fail(){state.active.failed=true;}
void Emit(const Op& op){
    auto& a=state.active;
    if(a.failed)return;
    if(a.entry->phase==Fill){if(a.summary.ops>=a.entry->expected.ops){Fail();return;}At(*a.entry,a.summary.ops)=op;}
    if(a.summary.ops==PageLimit*PerPage){Fail();return;}
    ++a.summary.ops;
}
template<bool row,bool interior> __attribute__((always_inline)) inline uint32_t Read(const View& view,const Op& op,unsigned width,const Bounds& b,uint32_t u,uint32_t v){
    if constexpr(row)return view.Read(op.v+(u>>16));
    unsigned x=u>>16,y=v>>16;
    if constexpr(!interior){if(x<b.left)x=b.left;if(x>=b.right)x=b.right-1;if(y<b.top)y=b.top;if(y>=b.bottom)y=b.bottom-1;}
    return view.Read(y*width+x);
}
template<bool partial,bool row,bool interior> void Run(const Op& op,const View& view,const Bounds& bounds,uint32_t* destination){
    uint32_t u=op.u,v=op.v;auto* out=destination+op.destination;unsigned count=op.count;
    if constexpr(!partial){
        while(count>=4){
            *out++=Read<row,interior>(view,op,view.width,bounds,u,v)|0xff000000u;u+=(unsigned)op.du;v+=(unsigned)op.dv;
            *out++=Read<row,interior>(view,op,view.width,bounds,u,v)|0xff000000u;u+=(unsigned)op.du;v+=(unsigned)op.dv;
            *out++=Read<row,interior>(view,op,view.width,bounds,u,v)|0xff000000u;u+=(unsigned)op.du;v+=(unsigned)op.dv;
            *out++=Read<row,interior>(view,op,view.width,bounds,u,v)|0xff000000u;u+=(unsigned)op.du;v+=(unsigned)op.dv;count-=4;
        }
    }
    while(count--){
        uint32_t tex=Read<row,interior>(view,op,view.width,bounds,u,v);u+=(unsigned)op.du;v+=(unsigned)op.dv;
        if constexpr(partial){unsigned alpha=tex>>24,inverse=255-alpha;uint32_t p=*out,trb=(((tex&0xff00ffu)*alpha)>>8)&0xff00ffu,tg=(((tex&0xff00u)*alpha)>>8)&0xff00u,prb=(((p&0xff00ffu)*inverse)>>8)&0xff00ffu,pg=(((p&0xff00u)*inverse)>>8)&0xff00u;*out++=0xff000000u|((trb|tg)+(prb|pg));}
        else *out++=tex|0xff000000u;
    }
}
void ReplayOp(const Op& op,const View& view,const Bounds& bounds,uint32_t* destination){
    if((op.kind&KindMask)==Copy){
        if(!view.pages)H1CopyPixels(destination+op.destination,view.pixels+op.u,op.count);
        else {unsigned index=op.u,left=op.count;auto* out=destination+op.destination;while(left){unsigned n=std::min(left,1024u-(index&1023u));H1CopyPixels(out,view.pages[index>>10u]+(index&1023u),n);index+=n;out+=n;left-=n;}}
        return;}
    const bool partial=(op.kind&KindMask)==Partial;
    if(op.kind&Row){if(partial)Run<true,true,true>(op,view,bounds,destination);else Run<false,true,true>(op,view,bounds,destination);}
    else if(op.kind&Interior){if(partial)Run<true,false,true>(op,view,bounds,destination);else Run<false,false,true>(op,view,bounds,destination);}
    else {if(partial)Run<true,false,false>(op,view,bounds,destination);else Run<false,false,false>(op,view,bounds,destination);}
}
bool CompleteOpaque(Entry& e){
    if(e.expected.transparent||e.expected.partial||e.expected.pixels<H1ScreenWidth*H1ScreenHeight/2)return false;
    // Only a proven opaque rectangle becomes a retained background. Other
    // shapes keep exact replay without the background-foundation shortcut.
    // Bounded temporary storage: 16,320 bytes at 480x272, outside raster calls.
    uint32_t coverage[(H1ScreenWidth*H1ScreenHeight+31)/32]{};
    Bounds box{H1ScreenWidth,H1ScreenHeight,0,0};
    for(unsigned i=0;i<e.expected.ops;++i){const auto& op=At(e,i);
        if((op.kind&KindMask)==Partial)return false;
        const unsigned row=op.destination/H1ScreenWidth,x=op.destination%H1ScreenWidth;
        if(row>=H1ScreenHeight||x+op.count>H1ScreenWidth)return false;
        box.left=std::min(box.left,x);box.right=std::max(box.right,x+op.count);box.top=std::min(box.top,row);box.bottom=std::max(box.bottom,row+1);
        unsigned first=op.destination,remaining=op.count;
        while(remaining){unsigned n=std::min<unsigned>(remaining,32-first%32);
            const uint32_t mask=n==32?UINT32_MAX:((uint32_t(1)<<n)-1)<<(first%32);
            coverage[first/32]|=mask;first+=n;remaining-=n;
        }
    }
    unsigned area=0;
    for(unsigned row=box.top;row<box.bottom;++row){
        unsigned first=row*H1ScreenWidth+box.left,remaining=box.right-box.left;area+=remaining;
        while(remaining){unsigned n=std::min<unsigned>(remaining,32-first%32);
            const uint32_t mask=n==32?UINT32_MAX:((uint32_t(1)<<n)-1)<<(first%32);
            if((coverage[first/32]&mask)!=mask)return false;first+=n;remaining-=n;
        }
    }
    e.coverageBounds=box;return area>=H1ScreenWidth*H1ScreenHeight/2;
}

void Replay(Entry& e,const View& view,uint32_t* destination,bool foundation){
    state.busy=true;state.borrowing=true;
    const bool staticLayer=!foundation&&h1DirtyCanvas.rooted&&h1DirtyCanvas.staticDepth&&e.key.destination==h1DirtyCanvas.owner;
    if(foundation)for(unsigned y=0;y<H1ScreenHeight;++y){
        const uint32_t mask=h1DirtyCanvas.RestoreRow(y);
        if(!mask)continue;
        for(unsigned x=0;x<H1ScreenWidth;){
            const unsigned left=y>=e.coverageBounds.top&&y<e.coverageBounds.bottom?e.coverageBounds.left:0;
            const unsigned right=y>=e.coverageBounds.top&&y<e.coverageBounds.bottom?e.coverageBounds.right:0;
            if(x>=left&&x<right){x=right;continue;}
            const unsigned end=std::min<unsigned>(H1ScreenWidth,x+H1DirtyCanvas::TileWidth-x%H1DirtyCanvas::TileWidth);
            const unsigned stop=x<left?std::min<unsigned>(end,left):end;
            if(mask&(uint32_t(1)<<(x/H1DirtyCanvas::TileWidth)))for(unsigned p=x;p<stop;++p)destination[y*H1ScreenWidth+p]=0xff000000u;
            x=stop;
        }
    }
    for(unsigned i=0;i<e.expected.ops;++i){const auto& op=At(e,i);
        const unsigned row=op.destination/H1ScreenWidth,x=op.destination%H1ScreenWidth;
        if(!foundation&&!staticLayer){h1DirtyCanvas.Mark((void*)e.key.destination,x,row,op.count,1);ReplayOp(op,view,e.bounds,destination);continue;}
        const uint32_t mask=staticLayer?h1DirtyCanvas.DamageRow(row):h1DirtyCanvas.RestoreRow(row);
        if(mask==H1DirtyCanvas::All){h1DirtyCanvas.backgroundWritten+=op.count;ReplayOp(op,view,e.bounds,destination);continue;}
        if(!mask){h1DirtyCanvas.backgroundSkipped+=op.count;continue;}
        unsigned offset=0;
        while(offset<op.count){
            const unsigned at=x+offset,tile=at/H1DirtyCanvas::TileWidth;
            const uint32_t remaining=mask>>tile;
            if(!(remaining&1u)){
                const unsigned next=remaining?(tile+__builtin_ctz(remaining))*H1DirtyCanvas::TileWidth-x:op.count;
                const unsigned stop=std::min<unsigned>(next,op.count);
                h1DirtyCanvas.backgroundSkipped+=stop-offset;offset=stop;continue;
            }
            const unsigned end=(tile+__builtin_ctz(~remaining))*H1DirtyCanvas::TileWidth;
            const unsigned n=std::min<unsigned>(op.count-offset,end-at);
            {
                Op part=op;part.destination+=offset;part.count=(uint16_t)n;
                if((op.kind&KindMask)==Copy)part.u+=offset;
                else {part.u+=(uint32_t)op.du*offset;if(!(op.kind&Row))part.v+=(uint32_t)op.dv*offset;}
                ReplayOp(part,view,e.bounds,destination);
                h1DirtyCanvas.backgroundWritten+=n;
            }
            offset+=n;
        }
    }
    state.busy=false;state.borrowing=false;Add(3);Add(17,e.expected.pixels);Add(18,e.expected.transparent);Add(19,e.expected.copy);Add(20,e.expected.opaque);Add(21,e.expected.partial);
}
bool Reserve(Entry& e,unsigned operations){
    unsigned pages=(operations+PerPage-1)/PerPage;
    if(pages>PageLimit){Add(10);return false;}
    const std::size_t bytes=(std::size_t)pages*PageBytes;
    while(Metadata+state.capacity+bytes>Budget){auto* old=Oldest(&e,true);if(!old){Add(10);return false;}Drop(*old);}
    state.busy=true;
    for(unsigned i=0;i<pages;++i){
        auto* p=static_cast<Op*>(e.hooks.allocate(e.hooks.context,PageBytes));
        if(!p){state.busy=false;Add(11);return false;}
        e.pages[e.pageCount++]=p;state.capacity+=PageBytes;Stats();
    }
    state.busy=false;return true;
}
void Start(Entry& e,const Request& request,const Hooks& hooks,const View& view,H1RenderPlanToken& token){
    uint32_t serial=++state.serial;if(!serial)serial=++state.serial;
    state.active=Active{};auto& a=state.active;a.entry=&e;a.hooks=hooks;a.view=view;a.destination=request.destination;a.width=request.width;a.height=request.height;a.token=serial;
    state.busy=true;state.borrowing=true;pvzh1_render_plan_capture_active=1;token.value=serial;
}
} // anonymous

namespace H1PlanTest {
bool DrawOrBegin(const Request& request,const Hooks& hooks,H1RenderPlanToken& token){
    token.value=0;
    if(state.busy||state.active.entry){Add(12);return false;}
    if(!request.destination||!request.key.source||request.key.source==request.key.destination||request.width!=H1ScreenWidth||request.height!=H1ScreenHeight||!hooks.prepare||!hooks.revision||!hooks.allocate||!hooks.release)return false;
    View view{};
    // Prepare may allocate or pressure-trim plans. Do not take an Entry* first.
    if(!hooks.prepare(hooks.context,request.key.source,&view)||(!view.pixels&&!view.pages)||!view.revision||!view.width||!view.height||view.width>65535||view.height>65535||(uint64_t)view.width*view.height>UINT32_MAX){
        for(auto& e:state.entries)if(e.phase!=Empty&&e.key.source==request.key.source)Drop(e);
        Add(9);return false;
    }
    Key key=request.key;key.revision=view.revision;key.words[26]=view.width;key.words[27]=view.height;
    for(auto& e:state.entries)if(e.phase!=Empty&&e.key.source==key.source&&e.key.revision!=key.revision){Drop(e);Add(9);}
    Entry* entry=nullptr;for(auto& e:state.entries)if(e.phase!=Empty&&Same(e.key,key)){entry=&e;break;}
    if(entry){entry->use=++state.clock;
        if(entry->phase==Ready){
            const bool foundation=entry->completeOpaque&&h1DirtyCanvas.Root((void*)key.destination,&key,sizeof(key));
            Replay(*entry,view,request.destination,foundation);return true;
        }
        Add(4);
        if(entry->phase==Candidate){entry->phase=Count;Add(5);Start(*entry,request,hooks,view,token);}
        else if(entry->phase==Fill){Add(6);Start(*entry,request,hooks,view,token);}
        return false;
    }
    Add(4);Add(8);
    for(auto& e:state.entries)if(e.phase==Empty){entry=&e;break;}
    if(!entry){entry=Oldest();if(!entry)return false;Drop(*entry);}
    entry->key=key;entry->hooks=hooks;entry->use=++state.clock;entry->phase=Candidate;Stats();return false;
}
void Reset(){pvzh1_render_plan_shutdown();for(auto& x:pvzh1_render_plan_diagnostic)x=0;Stats();}
std::size_t Capacity(){return state.capacity;}
bool Borrowed(){return state.borrowing;}
} // H1PlanTest

void H1RenderPlanRecordSpan(uint32_t* destination,int count,const uint32_t* pixels,int pitch,int left,int top,int right,int bottom,uint32_t u,uint32_t v,int du,int dv,bool sourceAlpha){
    auto& a=state.active;if(!a.entry||a.failed||count<=0)return;
    const uintptr_t begin=(uintptr_t)a.destination,address=(uintptr_t)destination;
    if(pixels!=a.view.pixels||pitch!=(int)a.view.width||left<0||top<0||right<=left||bottom<=top||right>(int)a.view.width||bottom>(int)a.view.height||count>(int)RowLimit||address<begin||(address-begin)%4||((address-begin)/4)+(unsigned)count>(std::size_t)a.width*a.height||((address-begin)/4)%a.width+(unsigned)count>a.width){Fail();return;}
    const Bounds bounds{(unsigned)left,(unsigned)top,(unsigned)right,(unsigned)bottom};
    // Graphics clips large backgrounds into source rectangles before calling
    // the backend. Both original triangles must use the same physical cell.
    if(!a.summary.rows&&a.entry->phase==Count)a.entry->bounds=bounds;
    else if(std::memcmp(&bounds,&a.entry->bounds,sizeof(bounds))){Fail();return;}
    struct Sample {uint32_t index,y;uint16_t copy;uint8_t kind,alpha;bool interior,xInterior;};
    Sample samples[RowLimit];uint32_t su=u,sv=v;
    for(int i=0;i<count;++i,su+=(unsigned)du,sv+=(unsigned)dv){unsigned x=su>>16,y=sv>>16;bool xi=x>=bounds.left&&x<bounds.right,inside=xi&&y>=bounds.top&&y<bounds.bottom;if(x<bounds.left)x=bounds.left;if(x>=bounds.right)x=bounds.right-1;if(y<bounds.top)y=bounds.top;if(y>=bounds.bottom)y=bounds.bottom-1;unsigned index=y*a.view.width+x;unsigned actual=a.view.Read(index)>>24,alpha=sourceAlpha?actual:255;samples[i]={index,y,0,(uint8_t)(alpha<=8?3:alpha<240?Partial:Opaque),(uint8_t)actual,inside,xi};}
    for(int i=count-1;i>=0;--i)if(samples[i].kind==Opaque&&samples[i].alpha==255)samples[i].copy=(uint16_t)(1+((i+1<count&&samples[i+1].copy&&samples[i+1].index==samples[i].index+1)?samples[i+1].copy:0));
    ++a.summary.rows;a.summary.pixels+=(unsigned)count;
    for(int i=0;i<count;){
        if(samples[i].kind==3){++a.summary.transparent;++i;continue;}
        const int first=i;uint8_t kind=samples[i].copy>=8?(uint8_t)Copy:samples[i].kind;
        if(kind==Copy){i+=samples[i].copy;a.summary.copy+=(unsigned)(i-first);}
        else {do{++i;}while(i<count&&samples[i].kind==kind&&samples[i].copy<8);if(kind==Opaque)a.summary.opaque+=(unsigned)(i-first);else a.summary.partial+=(unsigned)(i-first);}
        Op op{(uint32_t)((address-begin)/4)+(unsigned)first,u+(uint32_t)du*(uint32_t)first,v+(uint32_t)dv*(uint32_t)first,du,dv,(uint16_t)(i-first),kind,0};
        if(kind==Copy){op.u=samples[first].index;op.v=0;op.du=op.dv=0;}
        else {bool row=true,interior=true,xInterior=true;for(int j=first;j<i;++j){row=row&&samples[j].y==samples[first].y;interior=interior&&samples[j].interior;xInterior=xInterior&&samples[j].xInterior;}if(row&&xInterior){op.kind|=Row;op.v=samples[first].y*a.view.width;op.dv=0;}else if(interior)op.kind|=Interior;}
        Emit(op);if(a.failed)return;
    }
}

void H1RenderPlanFinish(H1RenderPlanToken token){
    auto& a=state.active;if(!token.value||!a.entry||token.value!=a.token)return;
    Entry* e=a.entry;const Summary summary=a.summary;bool failed=a.failed;Hooks hooks=a.hooks;const void* source=e->key.source;const uint64_t revision=e->key.revision;
    // No storage pointer survives this boundary or any following allocation.
    pvzh1_render_plan_capture_active=0;state.active=Active{};state.busy=false;state.borrowing=false;
    if(failed||!summary.rows||hooks.revision(hooks.context,source)!=revision){Key key=e->key;Hooks saved=e->hooks;Drop(*e);e->key=key;e->hooks=saved;e->phase=Rejected;Add(22);return;}
    if(e->phase==Count){
        e->expected=summary;
        if(!Reserve(*e,summary.ops)||hooks.revision(hooks.context,source)!=revision){Key key=e->key;Hooks saved=e->hooks;Drop(*e);e->key=key;e->hooks=saved;e->phase=Rejected;Add(22);return;}
        e->phase=summary.ops?Fill:Ready;if(!summary.ops)Add(7);
    }else if(e->phase==Fill){
        if(std::memcmp(&summary,&e->expected,sizeof(summary))){Key key=e->key;Hooks saved=e->hooks;Drop(*e);e->key=key;e->hooks=saved;e->phase=Rejected;Add(22);return;}
        e->completeOpaque=CompleteOpaque(*e);e->phase=Ready;Add(7);
    }
    Stats();
}
void H1RenderPlanAbort(H1RenderPlanToken token){
    if(!token.value||!state.active.entry||state.active.token!=token.value)return;
    Entry* e=state.active.entry;pvzh1_render_plan_capture_active=0;state.active=Active{};state.busy=false;state.borrowing=false;Drop(*e);Add(22);
}

#ifndef PVZH1_PLAN_STANDALONE
#ifdef PVZ_HOST_RUNNER
static bool renderPlanEnabled=true;
void H1RenderPlanTestEnable(bool enabled){renderPlanEnabled=enabled;}
#endif
namespace {
bool Prepare(void*,const void* source,H1PlanTest::View* view){auto* image=(MemoryImage*)source;if(!H1ImageCachePrepareStorageRGBPages(image,&view->pages,&view->width,&view->height)&&!H1ImageCachePrepareStorageTexture(image,&view->pixels,&view->width,&view->height))return false;view->revision=H1ImageCacheStorageRevision(image);return view->revision!=0;}
uint64_t Revision(void*,const void* source){return H1ImageCacheStorageRevision((MemoryImage*)source);}
void* Allocate(void*,std::size_t bytes){return std::malloc(bytes);}
void Release(void*,void* pointer){std::free(pointer);}
}
namespace {
// Admit a geometry only after it repeats. Continuously moving sprites must not
// evict fixed UI/shadow plans before those plans reach their replay phase.
uint32_t seenGeometry[128]{};unsigned seenCount=0,seenNext=0;
bool RepeatedGeometry(const H1PlanTest::Key& key){
 uint64_t pointer=(uintptr_t)key.source;uint32_t hash=2166136261u^(uint32_t)pointer^(uint32_t)(pointer>>32);
 for(unsigned i=0;i<25;++i)hash=(hash^key.words[i])*16777619u;
 bool repeated=false;for(unsigned i=0;i<seenCount;++i)if(seenGeometry[i]==hash){repeated=true;break;}
 // Keep a recent query history, including hits, so stable plans stay admitted.
 seenGeometry[seenNext++%128]=hash;seenCount=std::min(128u,seenCount+1);return repeated;
}
}
bool H1RenderPlanDrawOrBegin(MemoryImage* destination,Image* source,const Rect& src,const Rect& clip,const Color& color,int mode,const SexyMatrix3& matrix,bool linear,float x,float y,bool center,H1RenderPlanToken& token){
    token.value=0;
    if(pvzh1_text_capture_active)return false;
    // Small stable shadows and UI pieces also repay repeated scan conversion.
    // Keep a cheap rejection only for very small source rectangles.
    const uint32_t scaleX=std::bit_cast<uint32_t>(matrix.m00),scaleY=std::bit_cast<uint32_t>(matrix.m11);
    if(scaleX<=0x3f800000u&&scaleY<=0x3f800000u&&src.mWidth>0&&src.mHeight>0&&
        uint64_t(src.mWidth)*uint64_t(src.mHeight)<256)return false;
#ifdef PVZ_HOST_RUNNER
    if(!renderPlanEnabled||std::getenv("PVZ_HOST_TRACE_PIXEL"))return false;
#endif
    auto* image=dynamic_cast<MemoryImage*>(source);
    if(!destination||!image||destination==image||!destination->mBits||destination->mWidth!=H1ScreenWidth||destination->mHeight!=H1ScreenHeight||!destination->mForcedMode||destination->mHasAlpha||destination->mHasTrans||image->mForcedMode||(image->mRenderFlags&RenderImageFlag_Repeat)||color!=Color::White||mode!=Graphics::DRAWMODE_NORMAL||src.mX<0||src.mY<0||src.mWidth<=0||src.mHeight<=0||(int64_t)src.mX+src.mWidth>image->mWidth||(int64_t)src.mY+src.mHeight>image->mHeight||clip.mWidth<=0||clip.mHeight<=0)return false;
    auto* device=destination->mApp?destination->mApp->mGLInterface.get():nullptr;auto* screen=device?H1GetPhysicalScreen(device->mScreenImage.get()):nullptr;
    if(!screen||!screen->Active()||screen->Target()!=destination)return false;
    const float fields[]={matrix.m00,matrix.m01,matrix.m02,matrix.m10,matrix.m11,matrix.m12,matrix.m20,matrix.m21,matrix.m22,x,y};for(float value:fields)if(!std::isfinite(value))return false;
    if((std::bit_cast<uint32_t>(matrix.m01)&0x7fffffffu)||(std::bit_cast<uint32_t>(matrix.m10)&0x7fffffffu)||
       (std::bit_cast<uint32_t>(matrix.m20)&0x7fffffffu)||(std::bit_cast<uint32_t>(matrix.m21)&0x7fffffffu)||
       std::bit_cast<uint32_t>(matrix.m22)!=0x3f800000u||!scaleX||scaleX>=0x7f800000u||!scaleY||scaleY>=0x7f800000u)return false;
    H1PlanTest::Request request{};request.key.source=image;request.key.destination=destination;request.destination=destination->mBits.get();request.width=H1ScreenWidth;request.height=H1ScreenHeight;
    const int integers[]={src.mX,src.mY,src.mWidth,src.mHeight,clip.mX,clip.mY,clip.mWidth,clip.mHeight,destination->mWidth,destination->mHeight,destination->mWidth,image->mWidth,image->mHeight,(center?1:0)|(linear?2:0)};
    for(unsigned i=0;i<14;++i)request.key.words[i]=(uint32_t)integers[i];for(unsigned i=0;i<11;++i)request.key.words[14+i]=std::bit_cast<uint32_t>(fields[i]);
    if(!RepeatedGeometry(request.key))return false;
    const float x0=center?-src.mWidth/2.f:0,y0=center?-src.mHeight/2.f:0;
    float left=matrix.m00*x0+matrix.m02+x-.5f,top=matrix.m11*y0+matrix.m12+y-.5f,right=matrix.m00*(x0+src.mWidth)+matrix.m02+x-.5f,bottom=matrix.m11*(y0+src.mHeight)+matrix.m12+y-.5f;
    if(!std::isfinite(left)||!std::isfinite(top)||!std::isfinite(right)||!std::isfinite(bottom))return false;
    if(left<clip.mX)left=(float)clip.mX;if(top<clip.mY)top=(float)clip.mY;if(right>clip.mX+clip.mWidth)right=(float)(clip.mX+clip.mWidth);if(bottom>clip.mY+clip.mHeight)bottom=(float)(clip.mY+clip.mHeight);
    if(left<0)left=0;if(top<0)top=0;if(right>H1ScreenWidth)right=H1ScreenWidth;if(bottom>H1ScreenHeight)bottom=H1ScreenHeight;
    if(right<=left||bottom<=top||(right-left)*(bottom-top)<64.f)return false;
    const H1PlanTest::Hooks hooks{nullptr,Prepare,Revision,Allocate,Release};return H1PlanTest::DrawOrBegin(request,hooks,token);
}
#endif
} // Sexy

extern "C" std::size_t pvzh1_render_plan_release_pressure(std::size_t){
    using namespace Sexy;Add(13);if(state.busy||state.active.entry){Add(12);return 0;}auto* e=Oldest(nullptr,true);if(!e)return 0;std::size_t bytes=Drop(*e);Add(14);Add(15,(uint32_t)bytes);return bytes;
}
extern "C" void pvzh1_render_plan_shutdown(){
    using namespace Sexy;if(state.busy||state.active.entry){Add(12);return;}for(auto& e:state.entries)if(e.phase!=Empty)Drop(e);Add(16);Stats();
}
