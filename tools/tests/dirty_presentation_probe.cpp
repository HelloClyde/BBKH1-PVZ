#define PVZH1_DIRTY_PRESENT_TEST
#include "graphics/DirtyPresentation.h"
#include <vector>
#include <cassert>
#include <cstdio>
int main(){
    H1DirtyPresentation dirty;H1PresentationMap map,referenceMap;
    std::vector<uint32_t> source(360*270),a(480*272+8,0xdeadbeef),b=a,c=a;
    for(unsigned i=0;i<source.size();++i)source[i]=0xff000000u|((i*1664525u+1013904223u)&0xffffffu);
    const int points[][2]={{400,300},{0,0},{799,599},{0,599},{799,0},{-1,300},{800,600},{-800,-600},{123,456},{800,0}};
    unsigned checks=0;
    for(unsigned frame=0;frame<240;++frame){
        if(frame%7==0)for(unsigned i=0;i<17;++i){unsigned x=(frame*37+i*179)%360,y=(frame*53+i*135)%270;source[y*360+x]^=0x00376ac5u;}
        if(frame%13==0)for(unsigned x:{0u,1u,11u,12u,179u,180u,181u,359u})for(unsigned y:{0u,1u,134u,135u,136u,269u})source[y*360+x]^=0x00f33c69u;
        if(frame%19==0)source[(frame*137)%source.size()]^=0xff000000u; // Alpha never reaches the RGB32 LCD.
        const auto saved=source;
        int px=points[(frame/3)%10][0],py=points[(frame/3)%10][1];
        auto& output=frame%31<16?b:c; // Surface changes must repaint complete margins and pixels.
        if(frame%41==0){dirty.Reset();output.assign(output.size(),0xdeadbeef);}
        assert(H1PresentMapped(a.data()+4,source.data(),360,270,px,py,referenceMap));
        assert(dirty.Present(output.data()+4,source.data(),360,270,px,py,map));
        assert(a==output&&source==saved);++checks;
    }
    assert(dirty.partialFrames>100&&dirty.skippedBlocks>dirty.changedBlocks);
    // Geometry fallback invalidates cached content. Returning to 360x270
    // cannot leave a cursor or pixels from the intervening full-canvas frame.
    std::vector<uint32_t> full(800*600,0xff123456u);
    assert(!dirty.Present(b.data()+4,full.data(),800,600,400,300,map));
    assert(H1PresentMapped(b.data()+4,full.data(),800,600,400,300,map));
    assert(H1PresentMapped(a.data()+4,source.data(),360,270,0,0,referenceMap));
    assert(dirty.Present(b.data()+4,source.data(),360,270,0,0,map));assert(a==b);++checks;
    for(int failure:{0,13,26}){
        dirty.Reset();dirty.failAfter=failure;
        assert(!dirty.Present(b.data()+4,source.data(),360,270,0,0,map));
        assert(dirty.ResidentBytes()==0);const auto failures=dirty.allocationFailures;
        dirty.failAfter=-1; // A failed process/window does not retry on every frame.
        for(unsigned i=0;i<3;++i)assert(!dirty.Present(b.data()+4,source.data(),360,270,0,0,map));
        assert(dirty.allocationFailures==failures);
        dirty.Reset();assert(dirty.Present(b.data()+4,source.data(),360,270,0,0,map));
        assert(a==b);++checks;
    }
    const auto partial=dirty.partialFrames,skipped=dirty.skippedBlocks,changed=dirty.changedBlocks;
    dirty.Reset();assert(dirty.ResidentBytes()==0);
    std::printf("{\"all_passed\":true,\"exact_framebuffer_cases\":%u,\"partial_frames\":%u,\"skipped_blocks\":%u,\"changed_blocks\":%u,\"cursor_edges_and_motion\":true,\"surface_and_geometry_invalidation\":true,\"allocation_failure_latched\":true,\"released_bytes\":0}\n",checks,partial,skipped,changed);
}
