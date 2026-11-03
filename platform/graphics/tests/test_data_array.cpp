/* Generic storage differential test. No Lawn business objects are replaced. */
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <vector>
#include <new>
#define DataArray OriginalDataArray
#include "original.h"
#undef DataArray
#undef __DATAARRAY_H__
#define DataArray PagedDataArray
#include "paged.h"
#undef DataArray

static bool failNextArray=false;
void *operator new[](size_t bytes){
    if(failNextArray){failNextArray=false;throw std::bad_alloc();}
    if(void *p=malloc(bytes?bytes:1))return p;throw std::bad_alloc();
}
void operator delete[](void *p) noexcept {free(p);}
void operator delete[](void *p,size_t) noexcept {free(p);}

struct Probe {
    uint32_t value=17;
    uint32_t extra[7]{};
};
template<class A,class B> static void Same(A& a,B& b){
    assert(a.mFreeListHead==b.mFreeListHead&&a.mMaxUsedCount==b.mMaxUsedCount&&a.mSize==b.mSize&&
           a.mNextKey==b.mNextKey&&a.mMaxSize==b.mMaxSize);
    std::vector<unsigned> x,y;
    for(auto *p:a)x.push_back(a.DataArrayGetID(p));
    for(auto *p:b)y.push_back(b.DataArrayGetID(p));
    assert(x==y);
    for(unsigned i=0;i<a.mMaxUsedCount;++i){
        assert(a.DataArrayGetIDAt(i)==b.DataArrayGetIDAt(i));
        assert(a.DataArrayGetItemAt(i).value==b.DataArrayGetItemAt(i).value);
    }
}
int main(){
    OriginalDataArray<Probe> original;PagedDataArray<Probe> paged;
    original.DataArrayInitialize(1024,"original");paged.DataArrayInitialize(1024,"paged");
    assert(paged.DataArrayAllocatedPages()==0);Same(original,paged);
    failNextArray=true;
    try {paged.DataArrayAlloc();assert(false);}catch(const std::bad_alloc&){}
    Same(original,paged);assert(paged.DataArrayAllocatedPages()==0);
    std::vector<unsigned> ids;std::vector<Probe*> pointers;
    for(unsigned i=0;i<1024;++i){
        auto *a=original.DataArrayAlloc();auto *b=paged.DataArrayAlloc();a->value=b->value=i*17;
        auto id=original.DataArrayGetID(a);assert(id==paged.DataArrayGetID(b));ids.push_back(id);pointers.push_back(b);
        assert(paged.DataArrayGetIndex(b)==i);
        for(unsigned j=0;j<=i;j+=31)assert(paged.DataArrayGet(ids[j])==pointers[j]);
    }
    Same(original,paged);assert(paged.mSize==1024&&paged.mMaxSize==1024);
    const unsigned fullPages=paged.DataArrayAllocatedPages();
    uint32_t rng=1234567;
    for(unsigned step=0;step<100000;++step){
        rng=rng*1664525u+1013904223u;
        if(!ids.empty()&&(ids.size()==1024||(rng&3))){
            unsigned pos=(rng>>5)%ids.size(),id=ids[pos];
            original.DataArrayFree(original.DataArrayGet(id));paged.DataArrayFree(paged.DataArrayGet(id));
            assert(!original.DataArrayTryToGet(id)&&!paged.DataArrayTryToGet(id));
            ids.erase(ids.begin()+pos);pointers.erase(pointers.begin()+pos);
        }else{
            auto *a=original.DataArrayAlloc();auto *b=paged.DataArrayAlloc();a->value=b->value=rng;
            unsigned id=original.DataArrayGetID(a);assert(id==paged.DataArrayGetID(b));ids.push_back(id);pointers.push_back(b);
        }
        if(step%251==0){Same(original,paged);for(unsigned j=0;j<ids.size();++j)assert(paged.DataArrayGet(ids[j])==pointers[j]);}
    }
    Same(original,paged);assert(paged.DataArrayAllocatedPages()==fullPages);
    /* Same fields and index-based accesses as original portable save loops,
     * including inactive slots below the original high-water mark. */
    PagedDataArray<Probe> restored;restored.DataArrayInitialize(1024,"restored");
    restored.mFreeListHead=original.mFreeListHead;restored.mMaxUsedCount=original.mMaxUsedCount;
    restored.mSize=original.mSize;restored.mNextKey=original.mNextKey;
    for(unsigned i=0;i<original.mMaxUsedCount;++i){
        restored.DataArrayGetIDAt(i)=original.DataArrayGetIDAt(i);
        restored.DataArrayResetItemAt(i)=original.DataArrayGetItemAt(i);
    }
    Same(original,restored);
    original.DataArrayFreeAll();paged.DataArrayFreeAll();restored.DataArrayFreeAll();Same(original,paged);
    bool wrapped=false;
    for(unsigned i=0;i<70000;++i){
        const unsigned before=original.mNextKey;
        auto *a=original.DataArrayAlloc();auto *b=paged.DataArrayAlloc();
        assert(original.DataArrayGetID(a)==paged.DataArrayGetID(b));
        if(before==65535&&original.mNextKey==1)wrapped=true;
        original.DataArrayFree(a);paged.DataArrayFree(b);
    }
    assert(wrapped);Same(original,paged);
    assert(paged.DataArrayAllocatedPages()==fullPages); /* never release touched pages mid-life */
    paged.DataArrayDispose();assert(paged.DataArrayAllocatedPages()==0);
    puts("paged differential passed: capacity 1024, 100000 alloc/free operations, IDs/slots/order/wrap, stable pointers, save access, bad_alloc atomicity");
}
