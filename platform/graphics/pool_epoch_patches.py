"""Mark only original completed background/pool batches as pixel-safe."""
from pathlib import Path

def apply(source_root):
    root=Path(source_root)
    board=root/'Lawn/Board.cpp';text=board.read_text(encoding='utf8')
    if 'PVZH1_COMPLETED_BACKDROP_BEFORE_POOL' not in text:
        anchor='void Board::DrawBackdrop(Graphics* g)'
        if text.count(anchor)!=1:raise RuntimeError('Pinned backdrop function changed')
        text=text.replace(anchor,'''#ifdef H1_PVZ_PORTABLE
#include "graphics/GLImage.h"
extern "C" void pvzh1_cache_complete_screen_batch();
#endif

'''+anchor,1)
        anchor='''\tif (StageHasPool())
\t{
\t\tmApp->mPoolEffect->PoolEffectDraw(g, StageIsNight());'''
        if text.count(anchor)!=1:raise RuntimeError('Pinned backdrop pool transition changed')
        text=text.replace(anchor,'''\tif (StageHasPool())
\t{
#ifdef H1_PVZ_PORTABLE
        // PVZH1_COMPLETED_BACKDROP_BEFORE_POOL: synchronous blits returned;
        // DrawBackdrop retains only Image*/Rect metadata, never source bits.
        if (GLImage::Check3D(g->mDestImage)) pvzh1_cache_complete_screen_batch();
#endif
\t\tmApp->mPoolEffect->PoolEffectDraw(g, StageIsNight());''',1)
        board.write_text(text,encoding='utf8',newline='\n')
    pool=root/'Lawn/System/PoolEffect.cpp';text=pool.read_text(encoding='utf8')
    if 'PVZH1_COMPLETED_POOL_TEXTURE_BATCHES' in text:return
    anchor='constexpr const int CAUSTIC_IMAGE_WIDTH = 128;'
    if text.count(anchor)!=1:raise RuntimeError('Pinned pool constants changed')
    text=text.replace(anchor,'''#ifdef H1_PVZ_PORTABLE
// PVZH1_COMPLETED_POOL_TEXTURE_BATCHES: each original synchronous batch ended.
extern "C" void pvzh1_cache_complete_screen_batch();
static void H1PoolTextureBatchComplete(Sexy::Graphics *g) {
    if (Sexy::GLImage::Check3D(g->mDestImage)) pvzh1_cache_complete_screen_batch();
}
#endif

'''+anchor,1)
    for image,layer in [('IMAGE_POOL_BASE_NIGHT','0'),('IMAGE_POOL_SHADING_NIGHT','1'),('IMAGE_POOL_BASE','0'),('IMAGE_POOL_SHADING','1'),('mCausticImage.get()','2')]:
        anchor=f'g->DrawTrianglesTex({image}, aVertArray[{layer}], 150);'
        if text.count(anchor)!=1:raise RuntimeError('Pinned original pool texture draw changed: '+image)
        text=text.replace(anchor,anchor+'\n#ifdef H1_PVZ_PORTABLE\n        H1PoolTextureBatchComplete(g);\n#endif',1)
    pool.write_text(text,encoding='utf8',newline='\n')
