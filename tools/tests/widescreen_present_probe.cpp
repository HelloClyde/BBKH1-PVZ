#define PVZH1_DIRTY_PRESENT_TEST
#include "graphics/DirtyPresentation.h"
#include "graphics/DirtyCanvas.h"
#include <vector>
#include <cassert>
#include <cstdio>
int main(){
    constexpr unsigned W=Sexy::H1ScreenWidth,H=Sexy::H1ScreenHeight;
    H1DirtyPresentation dirty;H1DirtyCanvas canvas;H1PresentationMap map,reference;
    std::vector<uint32_t> pixels(W*H),expected(480*272+8,0xc1a0f00du),first=expected,second=expected;
    for(unsigned i=0;i<pixels.size();++i)pixels[i]=0xff000000u|((i*1664525u+1013904223u)&0xffffffu);
    canvas.Bind(&pixels,pixels.data());const unsigned key=17;
    const int positions[][2]={{0,0},{(int)Sexy::H1LogicalWidth-1,599},{0,599},{(int)Sexy::H1LogicalWidth-1,0},{530,300},{-1,272},{1060,600},{800,0}};
    unsigned checks=0;
    for(unsigned frame=0;frame<320;++frame){
        canvas.Begin(&pixels);assert(canvas.Root(&pixels,&key,sizeof(key)));
        for(unsigned rectangle=0;rectangle<3;++rectangle){
            const unsigned x=rectangle==0?(frame%2?W-1:0):(frame*47+rectangle*173)%W;
            const unsigned y=rectangle==0?(frame%3?H-1:0):(frame*71+rectangle*89)%H;
            const unsigned width=std::min<unsigned>(W-x,1+(frame*17+rectangle)%25),height=std::min<unsigned>(H-y,1+(frame+rectangle)%12);
            canvas.Mark(&pixels,x,y,width,height);
            for(unsigned row=y;row<y+height;++row)for(unsigned col=x;col<x+width;++col)pixels[row*W+col]^=0x00b785a3u;
        }
        auto& output=frame%37<19?first:second;
        if(frame%53==0){dirty.Reset();output.assign(output.size(),0xc1a0f00du);}
        const auto* point=positions[(frame/4)%8];
        assert(H1PresentMapped(expected.data()+4,pixels.data(),W,H,point[0],point[1],reference));
        assert(dirty.Present(output.data()+4,pixels.data(),W,H,point[0],point[1],map,canvas.Presentation(pixels.data())));
        assert(output==expected);++checks;
    }
    assert(dirty.partialFrames>200&&dirty.skippedBlocks>dirty.changedBlocks);
    // Repeated full damage bypasses the comparison cache, then a partial
    // update must rebuild it before using it. Alpha-only differences remain
    // invisible on the LCD; guard pixels around both surfaces must survive.
    uint32_t fullDamage[H1DirtyCanvas::Rows];for(auto& row:fullDamage)row=H1DirtyCanvas::All;
    for(unsigned frame=0;frame<40;++frame){
        for(unsigned i=0;i<pixels.size();++i)pixels[i]^=frame%2?0xff000000u:0x00439b17u;
        assert(H1PresentMapped(expected.data()+4,pixels.data(),W,H,-1,-1,reference));
        assert(dirty.Present(first.data()+4,pixels.data(),W,H,-1,-1,map,fullDamage));
        assert(first==expected);++checks;
    }
    assert(dirty.Present(first.data()+4,pixels.data(),W,H,-1,-1,map));assert(first==expected);++checks;
    for(int fail:{0,17,33}){
        dirty.Reset();dirty.failAfter=fail;
        assert(!dirty.Present(first.data()+4,pixels.data(),W,H,0,0,map));
        assert(dirty.ResidentBytes()==0);const auto failures=dirty.allocationFailures;
        dirty.failAfter=-1;assert(!dirty.Present(first.data()+4,pixels.data(),W,H,0,0,map));assert(dirty.allocationFailures==failures);
        dirty.Reset();assert(dirty.Present(first.data()+4,pixels.data(),W,H,0,0,map));++checks;
    }
    dirty.Reset();assert(!dirty.ResidentBytes());
    std::printf("{\"passed\":true,\"width\":%u,\"height\":%u,\"exact_LCD_frames\":%u,\"damage_tiles\":true,\"cursor_all_edges\":true,\"allocation_failure_latched\":true,\"released_bytes\":0}\n",W,H,checks);
}
