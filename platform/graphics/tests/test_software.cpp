#include "SexyAppBase.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/SoftwareDisplay.h"
#include "graphics/SoftwareRaster.h"
#include "graphics/MemoryImageCache.h"
#include "h1_resources.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <new>
#include <cstring>

extern "C" size_t pvzh1_cache_memory_pressure(size_t);
extern "C" void pvzh1_cache_pressure_stats(uint64_t*,uint64_t*,uint64_t*);
extern "C" void pvzh1_cache_complete_draw_batch();
extern "C" volatile uint32_t pvzh1_cache_diagnostic[6];
extern "C" uint64_t pvzh1_image_pixel_bytes_loaded();
static bool forbidAllocation=false,reenterOnDelete=false,insideDelete=false;
static size_t allocations=0,injectArrayBytes=0,injectedFreed=0;
static unsigned injectedCalls=0,nestedCalls=0;
static Sexy::MemoryImage *protectedImage=nullptr;
static bool protectPalette=false;
static void BeforeAllocation(size_t bytes,bool array){
    assert(!forbidAllocation);++allocations;
    if(array&&injectArrayBytes==bytes){
        injectArrayBytes=0;++injectedCalls;
        assert(protectedImage&&(protectedImage->mBits||protectPalette));
        if(protectPalette)assert(protectedImage->mColorIndices&&protectedImage->mColorTable);
        const size_t before=allocations;forbidAllocation=true;
        injectedFreed=pvzh1_cache_memory_pressure((size_t)-1);
        forbidAllocation=false;assert(allocations==before&&(protectedImage->mBits||protectPalette));
        if(protectPalette)assert(protectedImage->mColorIndices&&protectedImage->mColorTable);
    }
}
void *operator new(size_t n){BeforeAllocation(n,false);if(void*p=malloc(n?n:1))return p;throw std::bad_alloc();}
void *operator new[](size_t n){BeforeAllocation(n,true);if(void*p=malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void*p) noexcept {free(p);}
void operator delete(void*p,size_t) noexcept {free(p);}
void operator delete[](void*p) noexcept {
    if(p&&reenterOnDelete&&!insideDelete){insideDelete=true;++nestedCalls;assert(pvzh1_cache_memory_pressure(1)==0);insideDelete=false;}
    free(p);
}
void operator delete[](void*p,size_t) noexcept {operator delete[](p);}

namespace Sexy {
SexyAppBase *gSexyAppBase=nullptr;
void DispatchLogLn(SexyLogPriority,std::string_view){}
}
bool gInAssert=false;
using namespace Sexy;
static int presentCount=0,completeCount=0;
static int Present(const uint32_t *bits,int width,int height,void*)
{assert(bits&&width==64&&height==64);++presentCount;return 1;}
static void Complete(void*){++completeCount;}
static unsigned channel(uint32_t p,int shift){return (p>>shift)&255;}
static void CheckImage(MemoryImage *image)
{assert(image->GetBits()[image->mWidth*image->mHeight]==MEMORYCHECK_ID);}
static int CountPixels(MemoryImage *image)
{int n=0;for(int i=0;i<image->mWidth*image->mHeight;++i)if(image->GetBits()[i]&0xffffffu)++n;return n;}

static void WriteImage(const char *path,bool mask,unsigned size=32)
{
    FILE *f=fopen(path,"wb");assert(f);
    fwrite("H1I1",1,4,f);
    uint32_t fields[5]={size,size,mask?1u:4u,mask?1u:0u,size*size*(mask?1u:4u)};
    for(auto value:fields){unsigned char little[4]={(unsigned char)value,(unsigned char)(value>>8),(unsigned char)(value>>16),(unsigned char)(value>>24)};fwrite(little,1,4,f);}
    for(unsigned i=0;i<size*size;++i){
        unsigned char rgba[4]={40,80,120,(unsigned char)(i?255:0)};
        fwrite(mask?rgba+3:rgba,1,mask?1:4,f);
    }
    assert(fclose(f)==0);
}
static int sandCount=0;
static void SandFixture(Image *source)
{
    MemoryImage *image=static_cast<MemoryImage*>(source);
    ++sandCount;image->GetBits()[0]=0x00123456u;
    assert(H1ImageCacheAfterSanding(image,SandFixture));
}
static void TestCache(GLInterface *device)
{
    std::filesystem::create_directory("cache-fixtures");
    WriteImage("cache-fixtures/a.h1a",true);WriteImage("cache-fixtures/b.h1a",false);
    WriteImage("cache-fixtures/small.h1a",false,16);
    WriteImage("cache-fixtures/large.h1a",false,64);
    h1_resources_set_root("cache-fixtures");
    GLImage a(device),b(device),modified(device);
    assert(H1ImageCacheDescribe(&a,"a",true,0x345678));
    assert(H1ImageCacheDescribe(&b,"b",true,0xffffff));
    assert(a.mWidth==32&&a.mHeight==32&&!a.mBits&&!b.mBits);
    const auto initialLoads=H1ImageCacheGetStats().loads;
    assert(!b.Palletize()&&!b.mBits&&!b.mColorIndices&&!b.mColorTable);
    assert(H1ImageCacheGetStats().loads==initialLoads); /* metadata stays lazy */
    assert(a.GetBits()[1]==0xff345678u&&b.GetBits()[1]==0xff285078u);
    {
        const uint32_t expected0=b.GetBits()[0],expected1=b.GetBits()[1];
        assert(b.Palletize()&&!b.mBits&&b.mColorIndices&&b.mColorTable);
        const auto loads=H1ImageCacheGetStats().loads;
        assert(b.GetBits()[0]==expected0&&b.GetBits()[1]==expected1);
        assert(b.mBits&&!b.mColorIndices&&!b.mColorTable);
        assert(H1ImageCacheGetStats().loads==loads); /* lossless expansion, no I/O */
    }
    a.PurgeBits();assert(a.mBits); /* software purge never destroys source */
    H1ImageCacheSetBudget(5000);
    b.GetBits();
    assert(a.mBits&&b.mBits); /* naked pointers stay valid throughout the frame */
    H1ImageCacheEndFrame();
    assert(!a.mBits&&b.mBits); /* access-order LRU, no unnecessary frame grace */
    assert(H1ImageCacheGetStats().residentBytes<=5000);
    assert(a.GetBits()[1]==0xff345678u); /* original per-image alpha color survives */
    assert(H1ImageCacheGetStats().evictions>=1);
    SandFixture(&a);
    H1ImageCacheSetBudget(1);
    H1ImageCacheEndFrame();H1ImageCacheEndFrame();H1ImageCacheEndFrame();
    assert(!a.mBits);
    assert(a.GetBits()[0]==0x00123456u&&sandCount==2); /* deterministic rebuild */
    assert(H1ImageCacheDescribe(&modified,"b",true,0xffffff));
    modified.GetBits()[1]=0xffabcdefu;modified.BitsChanged();
    assert(modified.mFilePath.empty()&&!H1ImageCacheContains(&modified));
    H1ImageCacheEndFrame();H1ImageCacheEndFrame();H1ImageCacheEndFrame();
    assert(modified.mBits&&modified.GetBits()[1]==0xffabcdefu);
    assert(H1ImageCacheGetStats().loadFailures==0);
    {
        GLImage grid(device);
        assert(H1ImageCacheDescribe(&grid,"b",true,0xffffff));
        assert(H1ImageCacheSetAlpha(&grid,"small",2,2)&&!grid.mBits);
        assert(grid.GetBits()[1]==0x78285078u&&grid.GetBits()[17]==0x78285078u);
        H1ImageCacheEndFrame();H1ImageCacheEndFrame();H1ImageCacheEndFrame();
        assert(!grid.mBits&&grid.GetBits()[1]==0x78285078u);
    }
    {
        GLImage large(device),small(device);
        assert(H1ImageCacheDescribe(&large,"large",true,0xffffff));
        assert(H1ImageCacheDescribe(&small,"b",true,0xffffff));
        uint32_t *bits=large.GetBits();
        std::vector<uint32_t> expected(bits,bits+64*64);
        auto *screen=device->GetScreenImage();
        screen->FillRect(Rect(0,0,64,64),Color::Black,0);
        screen->Blt(&large,0,0,Rect(0,0,64,64),Color::White,0,false);
        std::vector<uint32_t> expectedFrame(screen->GetBits(),screen->GetBits()+64*64);
        small.GetBits(); /* smaller image is newer, as with foreground glyphs */
        const auto loads=H1ImageCacheGetStats().loads;
        H1ImageCacheSetBudget(24576);H1ImageCacheEndFrame();
        assert(large.mBits&&!small.mBits&&H1ImageCacheGetStats().residentBytes<=24576);
        assert(memcmp(large.GetBits(),expected.data(),64*64*4)==0);
        screen->FillRect(Rect(0,0,64,64),Color::Black,0);
        screen->Blt(&large,0,0,Rect(0,0,64,64),Color::White,0,false);
        assert(memcmp(screen->GetBits(),expectedFrame.data(),64*64*4)==0);
        assert(H1ImageCacheGetStats().loads==loads); /* background needs no reread */
        H1ImageCacheEndFrame(); /* finish the background's new touch */
        small.GetBits();H1ImageCacheSetBudget(5000);H1ImageCacheEndFrame();
        assert(!large.mBits&&small.mBits); /* unused old background gets no pin */
    }
    H1ImageCacheSetBudget(20u*1024u*1024u);
}

static size_t OnePressureWithoutAllocation(size_t requested){
    const size_t before=allocations;forbidAllocation=true;reenterOnDelete=true;
    const size_t freed=pvzh1_cache_memory_pressure(requested);
    reenterOnDelete=false;forbidAllocation=false;assert(allocations==before);return freed;
}
/* Model repeated failed allocation retries through exhaustion. The production
 * bridge stops earlier as soon as its real allocation succeeds. */
static size_t PressureWithoutAllocation(){
    size_t total=0;while(size_t n=OnePressureWithoutAllocation((size_t)-1))total+=n;return total;
}
static void TestPressureSelection(GLInterface *device){
    H1ImageCacheSetBudget(20u*1024u*1024u);
    {
        GLImage big(device),small(device),current(device);
        assert(H1ImageCacheDescribe(&big,"large",true,0xffffff));
        assert(H1ImageCacheDescribe(&small,"small",true,0xffffff));
        assert(H1ImageCacheDescribe(&current,"b",true,0xffffff));
        big.GetBits();small.GetBits();H1ImageCacheEndFrame();current.GetBits();
        auto stats=H1ImageCacheGetStats();
        assert(OnePressureWithoutAllocation(20)==1028);
        assert(big.mBits&&!small.mBits&&current.mBits);
        assert(H1ImageCacheGetStats().evictions==stats.evictions+1);
        const auto loads=H1ImageCacheGetStats().loads;
        assert(small.GetBits()[1]==0xff285078u&&H1ImageCacheGetStats().loads==loads+1);
        /* A prior-frame source outranks a smaller source already reused now. */
        pvzh1_cache_complete_draw_batch();current.GetBits();
        assert(OnePressureWithoutAllocation(20)==16388);
        assert(!big.mBits&&small.mBits&&current.mBits);
    }
    {
        GLImage small(device),middle(device),big(device);
        assert(H1ImageCacheDescribe(&small,"small",true,0xffffff));
        assert(H1ImageCacheDescribe(&middle,"b",true,0xffffff));
        assert(H1ImageCacheDescribe(&big,"large",true,0xffffff));
        small.GetBits();middle.GetBits();big.GetBits();H1ImageCacheEndFrame();
        assert(OnePressureWithoutAllocation(3000)==4100);
        assert(small.mBits&&!middle.mBits&&big.mBits);
        assert(OnePressureWithoutAllocation(20000)==1028); /* no fit -> LRU */
        assert(!small.mBits&&big.mBits);
    }
    {
        GLImage combined(device),contiguous(device);
        assert(H1ImageCacheDescribe(&combined,"b",true,0xffffff));
        assert(H1ImageCacheDescribe(&contiguous,"large",true,0xffffff));
        combined.GetBits();combined.GetNativeAlphaData(device);contiguous.GetBits();H1ImageCacheEndFrame();
        /* Combined bytes exceed 5K but neither of its individual buffers does. */
        assert(OnePressureWithoutAllocation(5000)==16388);
        assert(combined.mBits&&combined.mNativeAlphaData&&!contiguous.mBits);
    }
    puts("staged pressure selection passed: one source/call, cold-first, smallest real-buffer fit, LRU fallback, current pins and exact reload");
}
static void TestPressure(GLInterface *device){
    H1ImageCacheSetBudget(20u*1024u*1024u);
    for(int mode=0;mode<4;++mode){
        GLImage source(device),old1(device),old2(device),dynamic(device);
        assert(H1ImageCacheDescribe(&source,"b",true,0xffffff));
        assert(H1ImageCacheDescribe(&old1,"b",true,0xffffff));
        assert(H1ImageCacheDescribe(&old2,"b",true,0xffffff));
        source.GetBits();old1.GetBits();old2.GetBits();dynamic.Create(2,2);dynamic.GetBits()[0]=0xffabcde0;
        auto getter=[&]()->void*{
            if(mode==0)return source.GetBits();
            if(mode==1)return source.GetNativeAlphaData(device);
            if(mode==2)return source.GetRLAlphaData();
            return source.GetRLAdditiveData(device);
        };
        void *pointer=getter();unsigned char expected[8];memcpy(expected,pointer,8);
        H1ImageCacheEndFrame();assert(source.mBits&&old1.mBits&&old2.mBits);
        assert(getter()==pointer); /* cached getters must pin, too */
        assert(PressureWithoutAllocation()>=8200);
        assert(!old1.mBits&&!old2.mBits&&source.mBits&&memcmp(pointer,expected,8)==0);
        assert(dynamic.mBits&&dynamic.mBits[0]==0xffabcde0);
        assert(old1.GetBits()[1]==0xff285078u); /* exact old-frame reload */
    }
    assert(nestedCalls>0);
    {
        GLImage grid(device),old(device);
        assert(H1ImageCacheDescribe(&grid,"b",true,0xffffff));
        assert(H1ImageCacheSetAlpha(&grid,"small",2,2));
        assert(H1ImageCacheDescribe(&old,"b",true,0xffffff));old.GetBits();
        H1ImageCacheEndFrame();
        protectedImage=&grid;injectArrayBytes=16*4;protectPalette=false;
        const unsigned before=injectedCalls;
        assert(grid.GetBits()[1]==0x78285078u);
        assert(injectedCalls==before+1&&injectedFreed>0&&!old.mBits);
        /* Direct alpha changes also pin an already loaded, older image. */
        old.GetBits();H1ImageCacheEndFrame();injectArrayBytes=16*4;
        assert(H1ImageCacheSetAlpha(&grid,"small",2,2)&&grid.mBits&&grid.GetBits()[1]==0x78285078u);
        assert(!old.mBits);
    }
    {
        GLImage source(device),old(device),dest(device);
        assert(H1ImageCacheDescribe(&source,"b",true,0xffffff));source.GetBits();assert(source.Palletize());
        assert(H1ImageCacheDescribe(&old,"b",true,0xffffff));old.GetBits();H1ImageCacheEndFrame();
        dest.Create(8,8);protectedImage=&source;protectPalette=true;
        /* The old palette branch obtains pointers before dest.GetBits allocates. */
        injectArrayBytes=(8*8+1)*4;
        dest.Blt(&source,0,0,Rect(0,0,8,8),Color::White,0,false);
        assert(!injectArrayBytes&&source.mColorIndices&&source.mColorTable&&!old.mBits);
        assert(dest.GetBits()[1]==0xff274f77u); /* original /256 blend into alpha-zero destination */
        protectPalette=false;
    }
    {
        GLImage source(device),old(device);
        assert(H1ImageCacheDescribe(&source,"b",true,0xffffff));source.GetBits();assert(source.Palletize());
        assert(H1ImageCacheDescribe(&old,"b",true,0xffffff));old.GetBits();H1ImageCacheEndFrame();
        protectedImage=&source;protectPalette=true;injectArrayBytes=256*4;
        std::unique_ptr<GLImage> colored(device->mApp->CreateColorizedImage(&source,Color::White));
        assert(colored&&!injectArrayBytes&&!old.mBits&&source.mColorIndices&&source.mColorTable);
        assert(colored->GetBits()[1]==0xfe274f77u); /* original color helper arithmetic */
        protectPalette=false;
    }
    protectedImage=nullptr;injectArrayBytes=0;
    TestPressureSelection(device);
    {
        GLImage completed(device),active(device);
        assert(H1ImageCacheDescribe(&completed,"b",true,0xffffff));
        assert(H1ImageCacheDescribe(&active,"small",true,0xffffff));
        completed.GetBits();
        const auto frames=H1ImageCacheGetStats().frames;
        const auto count=allocations;forbidAllocation=true;
        pvzh1_cache_complete_draw_batch();
        forbidAllocation=false;assert(allocations==count&&completed.mBits);
        assert(H1ImageCacheGetStats().frames==frames);
        uint32_t *current=active.GetBits();const uint32_t expected=current[1];
        assert(PressureWithoutAllocation()>0&&!completed.mBits&&active.mBits);
        assert(current[1]==expected&&H1ImageCacheGetStats().frames==frames);
        assert(completed.GetBits()[1]==0xff285078u);
    }
    uint64_t calls=0,evicted=0,bytes=0;pvzh1_cache_pressure_stats(&calls,&evicted,&bytes);
    assert(calls>0&&evicted>0&&bytes>0);
    auto stats=H1ImageCacheGetStats();
    assert(pvzh1_cache_diagnostic[0]==stats.residentBytes&&pvzh1_cache_diagnostic[1]==stats.peakBytes);
    assert(pvzh1_cache_diagnostic[2]==stats.loads&&pvzh1_cache_diagnostic[3]==stats.evictions);
    assert(pvzh1_cache_diagnostic[4]==stats.frames&&pvzh1_cache_diagnostic[5]==(uint32_t)pvzh1_image_pixel_bytes_loaded());
    puts("allocation pressure passed: zero allocations, old-frame-only/full reclaim, current getters/palettes, alpha-row loading, original color helper, recursion guard, exact reload");
}

int main()
{
    forbidAllocation=true;const size_t before=allocations;
    pvzh1_cache_complete_draw_batch(); /* safe before cache initialization */
    assert(pvzh1_cache_memory_pressure(1024)==0); /* even before cache initialization */
    assert(allocations==before);forbidAllocation=false;
    SexyAppBase app;gSexyAppBase=&app;
    pvzh1_graphics_set_present(Present,nullptr);
    pvzh1_graphics_set_frame_complete(Complete,nullptr);
    app.mGLInterface=std::make_unique<GLInterface>(&app);
    GLInterface *device=app.mGLInterface.get();
    assert(device->Init(false));
    GLImage *screen=device->GetScreenImage();
    assert(screen->mWidth==64&&screen->mHeight==64&&GLImage::Check3D(screen));
    GLImage source(device);
    uint32_t pattern[16]={0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffffffu,
                         0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffffffu,
                         0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffffffu,
                         0xffff0000u,0xff00ff00u,0xff0000ffu,0xffffffffu};
    source.SetBits(pattern,4,4);
    assert(!GLImage::Check3D(&source));
    screen->FillRect(Rect(0,0,64,64),Color::Black,Graphics::DRAWMODE_NORMAL);
    screen->Blt(&source,2,3,Rect(0,0,4,4),Color::White,Graphics::DRAWMODE_NORMAL,false);
    assert(screen->GetBits()[3*64+2]==0xffff0000u&&screen->GetBits()[3*64+5]==0xffffffffu);
    screen->Blt(&source,-2,-2,Rect(0,0,4,4),Color::White,Graphics::DRAWMODE_NORMAL,false);
    assert(screen->GetBits()[0]==0xff0000ffu);
    screen->FillRect(Rect(10,10,2,2),Color(255,0,0),Graphics::DRAWMODE_NORMAL);
    screen->FillRect(Rect(10,10,2,2),Color(0,255,0,128),Graphics::DRAWMODE_NORMAL);
    uint32_t alpha=screen->GetBits()[10*64+10];
    assert(channel(alpha,16)>=125&&channel(alpha,16)<=129&&channel(alpha,8)>=126&&channel(alpha,8)<=129);
    screen->FillRect(Rect(10,10,2,2),Color(200,200,200),Graphics::DRAWMODE_ADDITIVE);
    assert((screen->GetBits()[10*64+10]&0xffffffu)==0xffffc8u);
    source.PurgeBits();assert(source.GetBits()[0]==pattern[0]);

    screen->Clear();
    screen->StretchBlt(&source,Rect(5,5,16,16),Rect(0,0,4,4),Rect(0,0,64,64),Color(100,200,255),0,true);
    assert(CountPixels(screen)>100); /* original FastStretch's tint branch draws nothing */
    CheckImage(screen);
    screen->Clear();
    screen->BltMirror(&source,5,5,Rect(0,0,4,4),Color::White,0,false);
    uint32_t mirrored=screen->GetBits()[6*64+5];
    assert(channel(mirrored,0)>200&&channel(mirrored,8)>200&&channel(mirrored,16)>200);
    screen->Clear();
    screen->BltRotated(&source,20,20,Rect(0,0,4,4),Rect(0,0,64,64),Color::White,0,0.5,2,2);
    assert(CountPixels(screen)>4);
    screen->Clear();
    SexyTransform2D matrix;matrix.Scale(3,2);matrix.RotateRad(0.25f);
    screen->BltMatrix(&source,30,30,matrix,Rect(0,0,64,64),Color::White,0,Rect(0,0,4,4),false);
    assert(CountPixels(screen)>20);CheckImage(screen);

    screen->Clear();
    GLImage white(device);white.Create(4,4);white.FillRect(Rect(0,0,4,4),Color::White,0);
    TriVertex triangle[1][3]={{{10,10,0,0},{30,10,1,0},{10,30,0,1}}};
    device->DrawTrianglesTex(triangle,1,Color(120,0,0),Graphics::DRAWMODE_ADDITIVE,&white,0,0,false);
    unsigned first=channel(screen->GetBits()[15*64+15],16);
    device->DrawTrianglesTex(triangle,1,Color(120,0,0),Graphics::DRAWMODE_ADDITIVE,&white,0,0,false);
    unsigned second=channel(screen->GetBits()[15*64+15],16);
    assert(first>90&&second>first+80); /* additive must accumulate, not source-over */
    for(int j=0;j<3;++j)triangle[0][j].color=0x00ffffffu;
    device->DrawTrianglesTex(triangle,1,Color::White,Graphics::DRAWMODE_ADDITIVE,&white,0,0,true);
    assert(channel(screen->GetBits()[15*64+15],16)==second); /* zero after color modulation: no 0/0 */
    triangle[0][1].color=0xffffffffu;
    device->DrawTrianglesTex(triangle,1,Color::White,Graphics::DRAWMODE_ADDITIVE,&white,0,0,true);
    for(int j=0;j<3;++j)triangle[0][j].color=0;
    CheckImage(screen);
    screen->Clear();
    TriVertex mixed[2][3]={{{10,10,0,0},{30,10,1,0},{10,30,0,1}},
                           {{10,10,0,0},{30,10,1,0},{10,30,0,1}}};
    const uint8_t modes[2]={Graphics::DRAWMODE_NORMAL,Graphics::DRAWMODE_ADDITIVE};
    device->DrawTrianglesTexMixed(mixed,2,Color(80,0,0),&white,modes,0,0,false);
    assert(channel(screen->GetBits()[15*64+15],16)>130);
    screen->Clear();
    TriVertex flat[1][3]={{{4,4},{25,4},{4,25}}};
    device->DrawTriangle(flat[0][0],flat[0][1],flat[0][2],Color(0,200,0),0);
    assert(channel(screen->GetBits()[8*64+8],8)>180);

    screen->Clear();
    Span spans[1]={{3,2,4}};
    const uint8_t coverage[4]={0,255,0,255};
    screen->FillScanLinesWithCoverage(spans,1,Color::White,0,coverage,2,3,4,1);
    assert(screen->GetBits()[3*64+2]==0&&screen->GetBits()[3*64+3]==0xffffffffu);
    assert(screen->GetBits()[3*64+4]==0&&screen->GetBits()[3*64+5]==0xffffffffu);
    assert(spans[0].mX==2&&spans[0].mY==3); /* original GLImage mutates caller spans */

    screen->Clear();source.mRenderFlags|=RenderImageFlag_Repeat;
    TriVertex repeating[2][3]={{{0,0,0,0},{16,0,2,0},{0,16,0,2}},
                               {{16,0,2,0},{16,16,2,2},{0,16,0,2}}};
    device->DrawTrianglesTex(repeating,2,Color::White,0,&source,0,0,false);
    assert(screen->GetBits()[3*64+1]==screen->GetBits()[3*64+9]);
    assert(screen->GetBits()[3*64+3]==screen->GetBits()[3*64+11]);
    assert(screen->GetBits()[3*64+1]!=screen->GetBits()[3*64+3]);
    CheckImage(screen);source.mRenderFlags=0;

    screen->Clear();
    SexyTransform2D translation;translation.Translate(20,10);
    device->PushTransform(translation);
    screen->Blt(&source,2,3,Rect(0,0,4,4),Color::White,0,false);
    assert(screen->GetBits()[14*64+23]!=0&&screen->GetBits()[4*64+3]==0);
    device->PopTransform();
    assert(device->Redraw()&&presentCount==1&&completeCount==1);
    assert((screen->GetBits()[14*64+23]&0xffffffu)==0); /* GL swap/clear lifecycle */
    CheckImage(screen);CheckImage(&source);
    {
        MemoryImage offscreen(&app);offscreen.Create(64,64);
        offscreen.BltMirror(&source,5,5,Rect(0,0,4,4),Color::White,0,false);
        uint32_t pixel=offscreen.GetBits()[6*64+5];
        assert(channel(pixel,0)>200&&channel(pixel,8)>200&&channel(pixel,16)>200);
        offscreen.Clear();
        offscreen.StretchBlt(&source,Rect(5,5,16,16),Rect(0,0,4,4),Rect(0,0,64,64),Color(100,200,255),0,true);
        assert(CountPixels(&offscreen)>100);
        offscreen.Clear();
        offscreen.BltTrianglesTex(&white,triangle,1,Rect(0,0,64,64),Color(120,0,0),1,0,0,false);
        offscreen.BltTrianglesTex(&white,triangle,1,Rect(0,0,64,64),Color(120,0,0),1,0,0,false);
        assert(channel(offscreen.GetBits()[15*64+15],16)>180);
        offscreen.FillRect(Rect(40,40,2,2),Color(100,100,100),1);
        assert((offscreen.GetBits()[40*64+40]&0xffffffu)==0x646464u);
        CheckImage(&offscreen);
        Graphics graphics(&offscreen);graphics.SetColor(Color(150,210,70));
        Point polygon[4]={{35,4},{58,5},{56,27},{32,26}};
        graphics.PolyFillAA(polygon,4);
        assert((offscreen.GetBits()[15*64+45]&0xffffffu)!=0);
    }
    TestCache(device);TestPressure(device);
    printf("software graphics passed: native MemoryImage/SWTri blit, alpha, tint, additive, mirror, stretch, rotate, matrix, triangles, repeat, coverage, transforms, present\n");
    puts("lazy image cache passed: metadata, original alpha color, frame pins, eviction/reload, sanding replay, mutation safety");
    return 0;
}
