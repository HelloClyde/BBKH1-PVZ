"""Keep original unlimited pools, with modest H1 refill batches and empty trim."""
from pathlib import Path

def apply(source_root):
    root=Path(source_root)
    path=root/'PvzpLib/PvzpCommon.cpp'
    text=path.read_text(encoding='utf8');marker='PVZH1_BOUNDED_POOL_REFILL'
    if marker not in text:
        old='\tpAllocator->Initialize(16, theSize);'
        new='''\tint aGrowCount = 16;
#ifdef H1_PVZ_PORTABLE
    // PVZH1_BOUNDED_POOL_REFILL: batch size, never a logical item limit.
    // Set only on first Initialize; existing blocks keep their original span.
    if (theSize > 0) {
        aGrowCount = 4096 / theSize;
        if (aGrowCount < 1) aGrowCount = 1;
        if (aGrowCount > 16) aGrowCount = 16;
    }
#endif
\tpAllocator->Initialize(aGrowCount, theSize);'''
        if text.count(old)!=1:raise RuntimeError('Pinned global pool initialization changed')
        path.write_text(text.replace(old,new,1),encoding='utf8',newline='\n')
    path=root/'SexyAppFramework/widget/WidgetManager.cpp'
    text=path.read_text(encoding='utf8');marker='PVZH1_ROOT_EMPTY_POOL_TRIM'
    if marker in text:return
    old='extern "C" void pvzh1_reanim_atlas_end_frame(void*);'
    if text.count(old)!=1:raise RuntimeError('Pinned root atlas callback declaration changed')
    text=text.replace(old,old+'\nextern "C" void pvzh1_global_allocator_trim_empty(void*);',1)
    old='''    if (h1WidgetDrawDepth == 1 && h1RootScreenDraw && GLImage::Check3D(mImage))
        pvzh1_reanim_atlas_end_frame(nullptr);'''
    new='''    if (h1WidgetDrawDepth == 1 && h1RootScreenDraw && GLImage::Check3D(mImage)) {
        pvzh1_reanim_atlas_end_frame(nullptr);
        // PVZH1_ROOT_EMPTY_POOL_TRIM: no temporary draw borrowers exist yet.
        pvzh1_global_allocator_trim_empty(nullptr);
    }'''
    if text.count(old)!=1:raise RuntimeError('Pinned root screen atlas boundary changed')
    path.write_text(text.replace(old,new,1),encoding='utf8',newline='\n')
