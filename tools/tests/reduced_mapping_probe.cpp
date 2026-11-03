#include "ReducedTextureMapping.h"
#include <cassert>
#include <cstdio>

int main()
{
    unsigned long long cases=0;
    for(unsigned width=5;width<5000;width+=7){
        const unsigned sw=(width*9+10)/20;
        for(unsigned x=0;x+3<width;x+=13){
            const unsigned x1=x+3;
            float u[3]={(float)x/width,(float)x1/width,(float)x/width};
            float v[3]={0,0,1};Sexy::H1ReducedTextureBounds b{};
            const bool nonempty=Sexy::H1MapReducedTriangle(u,v,width,37,sw,17,b);
            assert(b.left==(int)((uint64_t)x*sw/width));
            assert(b.right==(int)((uint64_t)x1*sw/width));
            assert(nonempty==(b.right>b.left));
            ++cases;
        }
    }
    float u[3]={0.12123f,0.52123f,0.2f},v[3]={0,0,1};
    const float original=u[0];Sexy::H1ReducedTextureBounds b{};
    assert(Sexy::H1MapReducedTriangle(u,v,700,60,315,27,b));
    assert(u[0]==original&&b.left==0&&b.right==315);++cases;
    /* Neighboring bins touch at the same floor boundary and never borrow an
     * adjacent frame when their logical width is not divisible by the ratio. */
    for(unsigned cell=0;cell<9;++cell){
        float cu[3]={(float)cell/9,(float)(cell+1)/9,(float)cell/9};
        float cv[3]={0,0,1};
        assert(Sexy::H1MapReducedTriangle(cu,cv,63,20,28,9,b));
        assert(b.left==(int)(cell*28/9)&&b.right==(int)((cell+1)*28/9));
        ++cases;
    }
    std::printf("floor mapping cases: %llu PASS\n",cases);
}
