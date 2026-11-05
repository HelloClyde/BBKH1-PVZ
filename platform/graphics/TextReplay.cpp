/* Captured opaque/partial operations preserve original ordering and /256
 * rounding on every background. No source pixel pointers survive a draw.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "TextReplay.h"
#include "graphics/ImageFont.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "PhysicalScreen.h"
#include "MemoryImageCache.h"
#include "DirtyCanvas.h"
#include "PixelCopy.h"
#include <algorithm>
#include <bit>
#include <cstring>
#include <new>

extern "C" {uint32_t pvzh1_text_capture_active;volatile uint32_t pvzh1_text_replay_diagnostic[8];}
#ifdef PVZ_HOST_RUNNER
// Asset compiler observer: the existing shader supplies background-independent
// opaque/partial literals. No observer or callback dispatch enters the BDA.
extern "C" {void (*pvzh1_host_offline_pixel_capture)(uint32_t*,uint32_t)=nullptr;}
extern "C" {void (*pvzh1_host_offline_additive_capture)(uint32_t*,uint32_t)=nullptr;}
#endif
extern "C" {volatile uint32_t pvzh1_font_metric_diagnostic[4];}
extern "C" volatile uint32_t pvzh1_physical_screen_diagnostic[8];
namespace Sexy {namespace {
constexpr unsigned Slots=72,MaximumOps=8192,PageBytes=3072,Words=PageBytes/4;
constexpr std::size_t Budget=4u*1024u*1024u;
struct Raw {uint32_t destination,literal;};
struct Run {uint32_t destination,word;uint16_t count,opaque;};
constexpr unsigned Runs=PageBytes/sizeof(Run),RunPages=(MaximumOps+Runs-1)/Runs,WordPages=(MaximumOps+Words-1)/Words;
struct Key {uint64_t words[64];unsigned used;};
struct Entry {
    Key key{};ImageFont* font=nullptr;MemoryImage* image=nullptr;char* text=nullptr;uint32_t hash=0,length=0,count=0,operations=0;
    Run* runs[RunPages]{};uint32_t* words[WordPages]{};
    unsigned runPages=0,wordPages=0;std::size_t bytes=0;uint64_t use=0;
    int left=0,top=0,width=0,height=0;
    uint32_t targetBumps=0,logicalBumps=0,drawCalls=0;
};
struct State {
    Entry entries[Slots]{};Raw scratch[MaximumOps]{};
    uintptr_t surface=0;unsigned used=0;bool failed=false,busy=false,disabled=false;
    ImageFont* rejected[16]{};unsigned rejectedNext=0;
    const void* capture=nullptr;std::size_t bytes=0;uint64_t clock=0;
    uint32_t imageRejected[16]{};unsigned imageRejectedNext=0;
    uint32_t matrixSeen[32]{};unsigned matrixSeenCount=0,matrixSeenNext=0;
} state;
bool cacheEnabled=true;
struct Metric {ImageFont* font=nullptr;char32_t character=0,previous=0;int pointSize=0,width=0;};
Metric metrics[512];
unsigned MetricSlot(ImageFont* font,char32_t character,char32_t previous){
    uint32_t value=(uint32_t)(uintptr_t)font^((uint32_t)character*0x9e3779b9u)^((uint32_t)previous*0x85ebca6bu);
    return (value^(value>>16))&511;
}
#ifdef PVZ_HOST_RUNNER
int failAfter=-1;
#endif
void Add(unsigned i,uint32_t n=1){pvzh1_text_replay_diagnostic[i]=pvzh1_text_replay_diagnostic[i]+n;}
void Stats(){
    unsigned count=0;for(auto& e:state.entries)if(e.text)++count;
    pvzh1_text_replay_diagnostic[4]=(uint32_t)state.bytes;
    if(state.bytes>pvzh1_text_replay_diagnostic[5])pvzh1_text_replay_diagnostic[5]=(uint32_t)state.bytes;
    pvzh1_text_replay_diagnostic[6]=count;
}
void Drop(Entry& e){
    for(unsigned i=0;i<e.runPages;++i)delete[] e.runs[i];
    for(unsigned i=0;i<e.wordPages;++i)delete[] e.words[i];
    delete[] e.text;state.bytes-=e.bytes;e=Entry{};
}
Entry* Oldest(){Entry* old=nullptr;for(auto& e:state.entries)if(e.text&&(!old||e.use<old->use))old=&e;return old;}
template<class T>T* Allocate(unsigned count){
#ifdef PVZ_HOST_RUNNER
    if(failAfter==0)return nullptr;if(failAfter>0)--failAfter;
#endif
    return new(std::nothrow) T[count];
}
MemoryImage* Eligible(ImageFont* font,Graphics* g,std::string_view text,bool matrix=false){
    if(!cacheEnabled||!font||!g||text.empty()||text.size()>256||font->mScale!=1.0||
       (!matrix&&(g->mScaleX!=1.f||g->mScaleY!=1.f))||g->mDrawMode!=Graphics::DRAWMODE_NORMAL||
       (matrix&&(text.size()>32||font->mPointSize>32)))return nullptr;
    auto* screen=H1GetPhysicalScreen(dynamic_cast<GLImage*>(g->mDestImage));
    if(!screen||!screen->Active()||!screen->mGLInterface||!screen->mGLInterface->mTransformStack.empty())return nullptr;
    auto* target=screen->Target();
    if(target!=h1DirtyCanvas.owner||target->mWidth!=(int)H1ScreenWidth||target->mHeight!=(int)H1ScreenHeight||!target->mBits||
       !target->mForcedMode||target->mHasAlpha||target->mHasTrans||font->mActiveLayerList.empty()||font->mActiveLayerList.size()>4)return nullptr;
    for(const auto& layer:font->mActiveLayerList){
        if(!layer.mBaseFontLayer||(layer.mBaseFontLayer->mDrawMode!=-1&&layer.mBaseFontLayer->mDrawMode!=0))return nullptr;
        auto* image=dynamic_cast<MemoryImage*>(layer.mScaledImage);
        if(!image||!H1ImageCacheSourceCookie(image))return nullptr;
    }
    if(matrix&&font->mActiveLayerList.size()>3)return nullptr;
    return target;
}
uint64_t Pair(uint32_t a,uint32_t b){return ((uint64_t)a<<32)|b;}
bool MakeKey(ImageFont* font,Graphics* g,int x,int y,std::string_view text,const Color& color,Key& key,MemoryImage*& target,const SexyMatrix3* matrix=nullptr){
    target=Eligible(font,g,text,matrix!=nullptr);if(!target)return false;
    auto* w=key.words;unsigned n=0;
    w[n++]=(uintptr_t)font;w[n++]=(uintptr_t)target;w[n++]=Pair(x,y);
    w[n++]=Pair(color.mRed,color.mGreen);w[n++]=Pair(color.mBlue,color.mAlpha);
    w[n++]=Pair(std::bit_cast<uint32_t>(g->mTransX),std::bit_cast<uint32_t>(g->mTransY));
    w[n++]=Pair(g->mClipRect.mX,g->mClipRect.mY);w[n++]=Pair(g->mClipRect.mWidth,g->mClipRect.mHeight);
    w[n++]=Pair(g->mDrawMode,(unsigned)g->mLinearBlend|((unsigned)g->mFastStretch<<1)|((unsigned)g->mColorizeImages<<2)|((unsigned)g->mIs3D<<3)|((unsigned)g->mWriteColoredString<<4));
    w[n++]=Pair(font->mPointSize,font->mForceScaledImagesWhite);w[n++]=(uintptr_t)font->mFontData;
    w[n++]=font->mActiveLayerList.size();
    for(const auto& layer:font->mActiveLayerList){
        auto* image=static_cast<MemoryImage*>(layer.mScaledImage);const auto* base=layer.mBaseFontLayer;
        w[n++]=(uintptr_t)image;w[n++]=H1ImageCacheSourceCookie(image);w[n++]=image->mBitsChangedCount;
        w[n++]=(uintptr_t)base;w[n++]=Pair(image->mWidth,image->mHeight);
        w[n++]=Pair(base->mColorMult.mRed,base->mColorMult.mGreen);w[n++]=Pair(base->mColorMult.mBlue,base->mColorMult.mAlpha);
        w[n++]=Pair(base->mColorAdd.mRed,base->mColorAdd.mGreen);w[n++]=Pair(base->mColorAdd.mBlue,base->mColorAdd.mAlpha);
        w[n++]=Pair(base->mDrawMode,base->mPointSize);
        w[n++]=Pair(base->mOffset.mX,base->mOffset.mY);w[n++]=Pair(base->mAscent,base->mSpacing);
        w[n++]=Pair(base->mBaseOrder,base->mUseAlphaCorrection);
    }
    if(matrix){
        w[n++]=0x4d4154524958ull;
        for(unsigned i=0;i<9;i+=2){uint32_t a=std::bit_cast<uint32_t>(matrix->m[i/3][i%3]),b=i+1<9?std::bit_cast<uint32_t>(matrix->m[(i+1)/3][(i+1)%3]):0;
            if((a&0x7f800000u)==0x7f800000u||(b&0x7f800000u)==0x7f800000u)return false;w[n++]=Pair(a,b);}
        w[n++]=Pair(std::bit_cast<uint32_t>(g->mScaleX),std::bit_cast<uint32_t>(g->mScaleY));
    }
    key.used=n;
    return true;
}
uint32_t Hash(const Key& key){uint32_t h=2166136261u;for(unsigned i=0;i<key.used;++i){uint64_t word=key.words[i];h=(h^(uint32_t)word)*16777619u;h=(h^(uint32_t)(word>>32))*16777619u;}return h;}
bool ImageKey(const H1ImageReplayContext& c,Key& key,MemoryImage*& source){
 if(!cacheEnabled||c.mode!=Graphics::DRAWMODE_NORMAL||c.destination!=h1DirtyCanvas.owner||!c.destination||
    !c.destination->mBits||c.destination->mWidth!=480||c.destination->mHeight!=272||!c.destination->mForcedMode||
    c.destination->mHasAlpha||c.destination->mHasTrans)return false;
 source=dynamic_cast<MemoryImage*>(c.source);
 // Generated small pictures already own their final pixels. File-backed
 // atlases keep their existing sparse/reduced storage and replay plans.
 if(!source||source==c.destination||!source->mBits||!source->mFilePath.empty()||source->mWidth<=0||source->mHeight<=0||
    uint64_t(source->mWidth)*source->mHeight>65536||(source->mRenderFlags&RenderImageFlag_Repeat))return false;
 auto*w=key.words;unsigned n=0;w[n++]=0x494d414745ull;w[n++]=(uintptr_t)source;w[n++]=(uintptr_t)source->mBits.get();w[n++]=(uintptr_t)c.destination;
 w[n++]=Pair(source->mWidth,source->mHeight);w[n++]=Pair(source->mBitsChangedCount,unsigned(source->mHasAlpha)|(unsigned(source->mHasTrans)<<1)|(unsigned(source->mForcedMode)<<2));
 w[n++]=Pair(c.rect.mX,c.rect.mY);w[n++]=Pair(c.rect.mWidth,c.rect.mHeight);w[n++]=Pair(c.clip.mX,c.clip.mY);w[n++]=Pair(c.clip.mWidth,c.clip.mHeight);
 w[n++]=Pair(c.color.mRed,c.color.mGreen);w[n++]=Pair(c.color.mBlue,c.color.mAlpha);w[n++]=Pair(c.linear,c.center);
 for(unsigned i=0;i<9;++i){uint32_t value=std::bit_cast<uint32_t>(c.matrix.m[i/3][i%3]);if((value&0x7f800000u)==0x7f800000u)return false;w[n++]=value;}
 uint32_t x=std::bit_cast<uint32_t>(c.x),y=std::bit_cast<uint32_t>(c.y);if((x&0x7f800000u)==0x7f800000u||(y&0x7f800000u)==0x7f800000u)return false;w[n++]=Pair(x,y);key.used=n;for(unsigned i=n;i<64;++i)w[i]=0;return true;
}
bool RepeatedLiteral(uint32_t hash){
 bool seen=false;for(unsigned i=0;i<state.matrixSeenCount;++i)seen|=state.matrixSeen[i]==hash;
 state.matrixSeen[state.matrixSeenNext++%32]=hash;state.matrixSeenCount=std::min(32u,state.matrixSeenCount+1);return seen;
}
Run& At(Entry& e,unsigned i){return e.runs[i/Runs][i%Runs];}
void Replay(Entry& e,MemoryImage* target){
    H1CanvasKnownDraw drawing(target,e.left,e.top,e.width,e.height);
    uint32_t* destination=target->GetBits();
    for(unsigned i=0;i<e.count;++i){const auto& r=At(e,i);unsigned word=r.word,count=r.count;auto* out=destination+r.destination;
        while(count){unsigned span=std::min<unsigned>(count,Words-word%Words);const auto* in=e.words[word/Words]+word%Words;
            if(r.opaque)H1CopyPixels(out,in,span);
            else for(unsigned j=0;j<span;++j){const uint32_t literal=in[j],inverse=literal>>24,p=out[j];
                const uint32_t rb=(((p&0xff00ffu)*inverse)>>8)&0xff00ffu,green=(((p&0xff00u)*inverse)>>8)&0xff00u;
                out[j]=0xff000000u|((literal&0xffffffu)+(rb|green));
            }
            count-=span;word+=span;out+=span;
        }
    }
    if(e.targetBumps){target->BitsChanged();target->mBitsChangedCount+=e.targetBumps-1;}
}
} // anonymous
static bool TryDrawText(ImageFont* font,Graphics* g,int x,int y,std::string_view text,const Color& color,const SexyMatrix3* matrix){
    if(state.busy||state.capture||pvzh1_text_capture_active)return false;Key key{};MemoryImage* target=nullptr;
    if(!MakeKey(font,g,x,y,text,color,key,target,matrix))return false;
    const uint32_t hash=Hash(key);
    for(auto& e:state.entries)if(e.text&&e.hash==hash&&e.length==text.size()&&key.used==e.key.used&&!std::memcmp(key.words,e.key.words,sizeof(key.words))&&!std::memcmp(e.text,text.data(),text.size())){
        state.busy=true;e.use=++state.clock;Replay(e,target);
        auto* screen=H1GetPhysicalScreen(dynamic_cast<GLImage*>(g->mDestImage));
        screen->mBitsChangedCount+=e.logicalBumps;
        pvzh1_physical_screen_diagnostic[7]=pvzh1_physical_screen_diagnostic[7]+e.drawCalls;
        for(auto& layer:font->mActiveLayerList){layer.mScaledImage->mDrawn=true;H1ImageCacheTouch(static_cast<MemoryImage*>(layer.mScaledImage));}
        state.busy=false;Add(0);return true;
    }
    Add(1);return false;
}
bool H1TextReplayTryDraw(ImageFont* font,Graphics* g,int x,int y,std::string_view text,const Color& color){return TryDrawText(font,g,x,y,text,color,nullptr);}
bool H1TextReplayTryDrawMatrix(ImageFont* font,Graphics* g,const SexyMatrix3& matrix,std::string_view text,const Color& color){return TryDrawText(font,g,0,0,text,color,&matrix);}
bool H1ImageReplayTryDraw(const H1ImageReplayContext& c){
 if(state.busy||state.capture||pvzh1_text_capture_active)return false;Key key;MemoryImage* source=nullptr;if(!ImageKey(c,key,source))return false;uint32_t hash=Hash(key);
 for(auto&e:state.entries)if(e.text&&e.image==source&&e.hash==hash&&e.key.used==key.used&&!std::memcmp(e.key.words,key.words,sizeof(key.words))){
  state.busy=true;e.use=++state.clock;Replay(e,c.destination);source->mDrawn=true;state.busy=false;Add(0);return true;
 }
 Add(1);return false;
}
H1TextReplayCapture::H1TextReplayCapture(const H1ImageReplayContext& c):imageContext(&c){
 if(state.disabled||state.busy||state.capture||pvzh1_text_capture_active)return;Key key;MemoryImage* source=nullptr;if(!ImageKey(c,key,source))return;
 uint32_t hash=Hash(key);for(auto rejected:state.imageRejected)if(hash==rejected)return;if(!RepeatedLiteral(hash))return;
 target=c.destination;state.surface=(uintptr_t)target->mBits.get();state.used=0;state.failed=false;state.capture=this;
 targetBefore=target->mBitsChangedCount;active=true;pvzh1_text_capture_active=1;
}
void H1ImageReplayForget(MemoryImage* image){
 for(auto&e:state.entries)if(e.image==image){if(state.busy)e.image=nullptr;else Drop(e);}Stats();
}
H1TextReplayCapture::H1TextReplayCapture(ImageFont* f,Graphics* g,int px,int py,std::string_view s,const Color& c,const SexyMatrix3* transform)
    :font(f),graphics(g),x(px),y(py),text(s),color(c),matrix(transform){
    if(state.disabled||state.busy||state.capture||pvzh1_text_capture_active)return;
    for(auto* rejected:state.rejected)if(rejected==f)return;
    Key key{};if(!MakeKey(f,g,px,py,s,c,key,target,matrix))return;
    if(matrix){uint32_t hash=Hash(key);bool seen=false;for(unsigned i=0;i<state.matrixSeenCount;++i)seen|=state.matrixSeen[i]==hash;
        if(!seen){state.matrixSeen[state.matrixSeenNext++%32]=hash;state.matrixSeenCount=std::min(32u,state.matrixSeenCount+1);return;}}
    state.surface=(uintptr_t)target->mBits.get();state.used=0;state.failed=false;state.capture=this;
    targetBefore=target->mBitsChangedCount;
    logicalBefore=static_cast<H1PhysicalScreen*>(graphics->mDestImage)->mBitsChangedCount;
    callsBefore=pvzh1_physical_screen_diagnostic[7];
    active=true;pvzh1_text_capture_active=1;
}
H1TextReplayCapture::~H1TextReplayCapture(){if(active&&state.capture==this){pvzh1_text_capture_active=0;state.capture=nullptr;}}
void H1TextReplayCapture::Commit(){
    if(!active||state.capture!=this)return;
    pvzh1_text_capture_active=0;state.capture=nullptr;active=false;
    if(state.failed){
     if(imageContext){Key rejected{};MemoryImage* source=nullptr;if(ImageKey(*imageContext,rejected,source))state.imageRejected[state.imageRejectedNext++%16]=Hash(rejected);}
     else if(!matrix)state.rejected[state.rejectedNext++%16]=font;Add(3);return;
    }
    Key key{};MemoryImage* current=nullptr;
    MemoryImage* source=nullptr;
    if(imageContext){if(!ImageKey(*imageContext,key,source))return;current=imageContext->destination;}
    else if(!MakeKey(font,graphics,x,y,text,color,key,current,matrix))return;
    if(current!=target)return;
    unsigned runCount=0;
    for(unsigned i=0;i<state.used;++i)if(!i||state.scratch[i].destination!=state.scratch[i-1].destination+1||
        (state.scratch[i].literal>>24==255)!=(state.scratch[i-1].literal>>24==255))++runCount;
    Entry candidate{};candidate.key=key;candidate.font=font;candidate.image=source;candidate.hash=Hash(key);candidate.length=(uint32_t)text.size();candidate.operations=state.used;
    candidate.targetBumps=target->mBitsChangedCount-targetBefore;
    candidate.logicalBumps=imageContext?0:static_cast<H1PhysicalScreen*>(graphics->mDestImage)->mBitsChangedCount-logicalBefore;
    candidate.drawCalls=imageContext?0:pvzh1_physical_screen_diagnostic[7]-callsBefore;
    const unsigned nr=(runCount+Runs-1)/Runs,nw=(state.used+Words-1)/Words;
    const std::size_t required=(std::size_t)(nr+nw)*PageBytes+text.size()+1;
    while(state.bytes+required>Budget){auto* old=Oldest();if(!old)return;Drop(*old);}
    bool okay=true;candidate.text=Allocate<char>((unsigned)text.size()+1);if(!candidate.text)okay=false;
    for(unsigned i=0;okay&&i<nr;++i){auto* page=Allocate<Run>(Runs);if(!page)okay=false;else candidate.runs[candidate.runPages++]=page;}
    for(unsigned i=0;okay&&i<nw;++i){auto* page=Allocate<uint32_t>(Words);if(!page)okay=false;else candidate.words[candidate.wordPages++]=page;}
    if(!okay){state.disabled=true;Add(7);Drop(candidate);Stats();return;}
    if(!text.empty())std::memcpy(candidate.text,text.data(),text.size());candidate.text[text.size()]=0;
    int left=H1ScreenWidth,top=H1ScreenHeight,right=-1,bottom=-1;
    for(unsigned i=0;i<state.used;++i){const auto& op=state.scratch[i];candidate.words[i/Words][i%Words]=op.literal;
        const unsigned px=op.destination%H1ScreenWidth,py=op.destination/H1ScreenWidth;left=std::min<int>(left,px);top=std::min<int>(top,py);right=std::max<int>(right,px);bottom=std::max<int>(bottom,py);
        if(i&&op.destination==state.scratch[i-1].destination+1&&(op.literal>>24==255)==(state.scratch[i-1].literal>>24==255))++At(candidate,candidate.count-1).count;
        else At(candidate,candidate.count++)={op.destination,i,1,(uint16_t)(op.literal>>24==255)};
    }
    if(state.used){candidate.left=left;candidate.top=top;candidate.width=right-left+1;candidate.height=bottom-top+1;}
    Entry* slot=nullptr;for(auto& e:state.entries)if(!e.text){slot=&e;break;}
    if(!slot){slot=Oldest();Drop(*slot);}
    candidate.bytes=required;candidate.use=++state.clock;state.bytes+=required;*slot=candidate;Stats();Add(2);
}
void H1TextReplayForget(ImageFont* font){if(state.busy)return;for(auto& e:state.entries)if(e.text&&e.font==font)Drop(e);for(auto& rejected:state.rejected)if(rejected==font)rejected=nullptr;for(auto& metric:metrics)if(metric.font==font)metric=Metric{};Stats();}
bool H1FontMetricLookup(ImageFont* font,char32_t character,char32_t previous,int& width){
    if(!cacheEnabled||font->mScale!=1.0||font->mActiveLayerList.empty())return false;
    const auto& m=metrics[MetricSlot(font,character,previous)];
    if(m.font==font&&m.character==character&&m.previous==previous&&m.pointSize==font->mPointSize){width=m.width;pvzh1_font_metric_diagnostic[0]=pvzh1_font_metric_diagnostic[0]+1;return true;}
    pvzh1_font_metric_diagnostic[1]=pvzh1_font_metric_diagnostic[1]+1;return false;
}
void H1FontMetricStore(ImageFont* font,char32_t character,char32_t previous,int width){
    if(!cacheEnabled||font->mScale!=1.0||font->mActiveLayerList.empty())return;
    metrics[MetricSlot(font,character,previous)]={font,character,previous,font->mPointSize,width};
    pvzh1_font_metric_diagnostic[2]=pvzh1_font_metric_diagnostic[2]+1;
}
#ifdef PVZ_HOST_RUNNER
void H1TextReplayTestEnable(bool value){cacheEnabled=value;}
void H1TextReplayTestFailure(int value){failAfter=value;state.disabled=false;}
#endif
} // Sexy
extern "C" void pvzh1_text_capture_pixel(uint32_t* destination,uint32_t literal){
#ifdef PVZ_HOST_RUNNER
    if(pvzh1_host_offline_pixel_capture){pvzh1_host_offline_pixel_capture(destination,literal);return;}
#endif
    using namespace Sexy;const uintptr_t address=(uintptr_t)destination;
    if(address<state.surface||(address-state.surface)%4||(address-state.surface)/4>=H1ScreenWidth*H1ScreenHeight||state.used==MaximumOps){state.failed=true;return;}
    state.scratch[state.used++]={(uint32_t)((address-state.surface)/4),literal};
}
extern "C" std::size_t pvzh1_text_replay_release_pressure(std::size_t){
    using namespace Sexy;if(state.busy)return 0;auto* old=Oldest();if(!old)return 0;
    const std::size_t bytes=old->bytes;Drop(*old);Stats();return bytes;
}
