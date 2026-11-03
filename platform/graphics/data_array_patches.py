"""Retain original DataArray IDs/capacity with stable, value-initialized pages.

The H1 preparation pipeline uses 32 slots per page. The standalone differential
test also checks 64-slot pages against the untouched pinned implementation.
"""
from pathlib import Path

def replace(text,old,new):
    if text.count(old)!=1:raise RuntimeError('Pinned DataArray prototype context changed: '+old[:80])
    return text.replace(old,new,1)

def apply_to_header(path,page_size=64):
    if page_size not in (32,64):raise ValueError('Reviewed page sizes are 32 and 64')
    path=Path(path);text=path.read_text(encoding='utf8')
    if 'PVZH1_PAGED_DATA_ARRAY' in text:return
    text=replace(text,'#include <new>','#include <new>\n#include <vector>\n#include <cstdint>\n#include <algorithm>')
    storage=r'''    // PVZH1_PAGED_DATA_ARRAY: stable pages, original logical slots.
    class PagedItems {
        static constexpr unsigned PAGE_SIZE = $PAGE_SIZE$;
        mutable std::vector<std::unique_ptr<DataArrayItem[]>> mPages;
        unsigned mCapacity=0;
        bool mInitialized=false;
    public:
        bool operator==(std::nullptr_t) const { return !mInitialized; }
        void Initialize(unsigned capacity) {
            std::vector<std::unique_ptr<DataArrayItem[]>> pages((capacity+PAGE_SIZE-1)/PAGE_SIZE);
            mPages=std::move(pages);mCapacity=capacity;mInitialized=true;
        }
        void Ensure(unsigned index) const {
            PVZP_ASSERT(index<mCapacity);
            auto& page=mPages[index/PAGE_SIZE];
            if(!page) {
                const unsigned base=(index/PAGE_SIZE)*PAGE_SIZE;
                const unsigned count=std::min(PAGE_SIZE,mCapacity-base);
                auto allocated=std::make_unique<DataArrayItem[]>(count);
                page=std::move(allocated);
            }
        }
        DataArrayItem& operator[](unsigned index) const {
            Ensure(index);return mPages[index/PAGE_SIZE][index%PAGE_SIZE];
        }
        unsigned IndexOf(const T *item) const {
            const uintptr_t address=reinterpret_cast<uintptr_t>(static_cast<const DataArrayItem*>(item));
            for(unsigned p=0;p<mPages.size();++p) {
                if(!mPages[p])continue;
                const unsigned count=std::min(PAGE_SIZE,mCapacity-p*PAGE_SIZE);
                const uintptr_t begin=reinterpret_cast<uintptr_t>(mPages[p].get());
                if(address>=begin && address-begin<(size_t)count*sizeof(DataArrayItem) &&
                   (address-begin)%sizeof(DataArrayItem)==0)
                    return p*PAGE_SIZE+(unsigned)((address-begin)/sizeof(DataArrayItem));
            }
            PVZP_ASSERT(false,"DataArray pointer does not belong to a page");
            return mCapacity;
        }
        unsigned AllocatedPages() const {
            unsigned result=0;for(auto& page:mPages)if(page)++result;return result;
        }
        void reset() {
            std::vector<std::unique_ptr<DataArrayItem[]>>().swap(mPages);
            mCapacity=0;mInitialized=false;
        }
    } mItems;'''.replace('$PAGE_SIZE$',str(page_size))
    text=replace(text,'\tstd::unique_ptr<DataArrayItem[]>\tmItems;',storage)
    text=replace(text,'mItems = std::make_unique<DataArrayItem[]>(theMaxSize);','mItems.Initialize(theMaxSize);')
    text=replace(text,'return static_cast<unsigned int>(static_cast<const DataArrayItem*>(theItem) - mItems.get());',
                 'return mItems.IndexOf(theItem);')
    text=replace(text,'\t\tunsigned int aNext = mMaxUsedCount;',
        '\t\t// Allocate before changing ID/free-list/high-water state: bad_alloc leaves it intact.\n'
        '\t\tmItems.Ensure(mFreeListHead == mMaxUsedCount ? mMaxUsedCount : mFreeListHead);\n'
        '\t\tunsigned int aNext = mMaxUsedCount;')
    text=replace(text,'\tunsigned int& DataArrayGetIDAt(unsigned int theIndex)',
        '\tunsigned int DataArrayAllocatedPages() const { return mItems.AllocatedPages(); }\n\n'
        '\tunsigned int& DataArrayGetIDAt(unsigned int theIndex)')
    path.write_text(text,encoding='utf8',newline='\n')
