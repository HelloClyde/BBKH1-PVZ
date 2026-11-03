"""Mark completed, top-level screen widgets as safe pixel-use boundaries.

DrawWidgetsTo can be called while an outer drawing function owns raw pixels.
It participates in the nesting guard but never advances the cache epoch.
"""
from pathlib import Path

MARKER='PVZH1_TOP_LEVEL_WIDGET_PIXEL_EPOCH'


def patch_widget(path):
    text=path.read_text(encoding='utf8')
    if MARKER in text:return
    anchor='using namespace Sexy;'
    if text.count(anchor)!=1:raise RuntimeError('Pinned WidgetManager namespace changed')
    support='''
#ifdef H1_PVZ_PORTABLE
// PVZH1_TOP_LEVEL_WIDGET_PIXEL_EPOCH: no source pointer survives DrawAll.
// Offscreen/nested drawing never authorizes a release of outer-call pixels.
extern "C" void pvzh1_cache_complete_draw_batch();
namespace {
unsigned h1WidgetDrawDepth=0;
bool h1RootScreenDraw=false;
struct H1WidgetDrawScope {
    H1WidgetDrawScope(bool screen){if(!h1WidgetDrawDepth)h1RootScreenDraw=screen;++h1WidgetDrawDepth;}
    ~H1WidgetDrawScope(){if(!--h1WidgetDrawDepth)h1RootScreenDraw=false;}
};
}
extern "C" int pvzh1_widget_draw_active(){return h1WidgetDrawDepth != 0;}
extern "C" int pvzh1_widget_screen_batch_active(){return h1WidgetDrawDepth==1&&h1RootScreenDraw;}
extern "C" void pvzh1_cache_complete_screen_batch(){
    if(h1WidgetDrawDepth==1&&h1RootScreenDraw)pvzh1_cache_complete_draw_batch();
}
extern "C" void pvzh1_cache_complete_atlas_batch(){
    if(!h1WidgetDrawDepth||(h1WidgetDrawDepth==1&&h1RootScreenDraw))pvzh1_cache_complete_draw_batch();
}
#endif
'''
    text=text.replace(anchor,anchor+'\n'+support,1)
    for signature,screen in (('void WidgetManager::DrawWidgetsTo(Graphics* g)','false'),
                             ('bool WidgetManager::DrawScreen()','true')):
        old=signature+'\n{'
        if text.count(old)!=1:raise RuntimeError('Pinned WidgetManager drawing signature changed')
        text=text.replace(old,old+'\n#ifdef H1_PVZ_PORTABLE\n\tH1WidgetDrawScope h1DrawScope('+screen+');\n#endif',1)
    old='\t\t\t\taWidget->DrawAll(&aModalFlags, &aClipG);\n\n\t\t\t\taDirtyCount++;'
    if text.count(old)!=1:raise RuntimeError('Pinned DrawScreen widget boundary changed')
    text=text.replace(old,'''\t\t\t\taWidget->DrawAll(&aModalFlags, &aClipG);
#ifdef H1_PVZ_PORTABLE
                if (h1WidgetDrawDepth == 1)
                    pvzh1_cache_complete_draw_batch();
#endif

\t\t\t\taDirtyCount++;''',1)
    path.write_text(text,encoding='utf8',newline='\n')


def patch_triangles(path):
    text=path.read_text(encoding='utf8')
    marker='PVZH1_COMPLETE_TRIANGLE_GROUP_EPOCH'
    if marker in text:return
    anchor='EffectSystem* gEffectSystem = nullptr;'
    if text.count(anchor)!=1:raise RuntimeError('Pinned effect system globals changed')
    text=text.replace(anchor,'#ifdef H1_PVZ_PORTABLE\nextern "C" void pvzh1_cache_complete_screen_batch();\n#endif\n\n'+anchor,1)
    anchor='\t\tmTriangleCount = 0;\n\t\tmHasMixedDrawModes = false;\n\t\tgPvzpTriangleDrawAdditive = false;'
    if text.count(anchor)!=1:raise RuntimeError('Pinned triangle group completion changed')
    text=text.replace(anchor,anchor+'''
#ifdef H1_PVZ_PORTABLE
        // PVZH1_COMPLETE_TRIANGLE_GROUP_EPOCH: backend is synchronous and all
        // group state is reset. The group/callers retain Image*/UV metadata.
        if (GLImage::Check3D(g->mDestImage))
            pvzh1_cache_complete_screen_batch();
#endif''',1)
    path.write_text(text,encoding='utf8',newline='\n')


def patch_atlas(path):
    text=path.read_text(encoding='utf8')
    marker='PVZH1_COMPLETE_NEW_ATLAS_EPOCH'
    if marker in text:return
    anchor='constexpr const int NO_BASE_POSE = -2;'
    if text.count(anchor)!=1:raise RuntimeError('Pinned reanimator global anchor changed')
    text=text.replace(anchor,'#ifdef H1_PVZ_PORTABLE\nextern "C" void pvzh1_cache_complete_atlas_batch();\n#endif\n\n'+anchor,1)
    anchor='\t\tPvzpLogLn("LOADING:Long atlas \'{}\' {} ms on {}", aParam.mReanimFileName, aDuration, LawnGetCurrentLevelName());\n}'
    if text.count(anchor)!=1:raise RuntimeError('Pinned new-atlas completion changed')
    text=text.replace(anchor,anchor[:-1]+'''#ifdef H1_PVZ_PORTABLE
    // PVZH1_COMPLETE_NEW_ATLAS_EPOCH: builder and LOW_MEMORY source reset are
    // complete; only definition/Image* metadata reaches Preload/Initialize.
    pvzh1_cache_complete_atlas_batch();
#endif
}''',1)
    path.write_text(text,encoding='utf8',newline='\n')


def patch_reanim_matrix(path):
    text=path.read_text(encoding='utf8')
    marker='PVZH1_COMPLETE_REANIM_MATRIX_EPOCH'
    if marker in text:return
    anchor='constexpr const int NO_BASE_POSE = -2;'
    if text.count(anchor)!=1:raise RuntimeError('Pinned reanimator matrix header anchor changed')
    text=text.replace(anchor,'''#ifdef H1_PVZ_PORTABLE
#include "graphics/GLImage.h"
extern "C" void pvzh1_cache_complete_screen_batch();
#ifdef PVZ_HOST_RUNNER
extern "C" void pvzh1_host_reanim_batch_completed(void*);
#endif
#endif

'''+anchor,1)
    anchor='\telse\n\t\tPvzpBltMatrix(g, theImage, theTransform, theClipRect, theColor, theDrawMode, theSrcRect);\n}'
    if text.count(anchor)!=1:raise RuntimeError('Pinned ReanimBltMatrix completion changed')
    text=text.replace(anchor,anchor[:-1]+'''#ifdef H1_PVZ_PORTABLE
    // PVZH1_COMPLETE_REANIM_MATRIX_EPOCH: both synchronous matrix/blit paths
    // have returned. DrawTrack retains Image*/matrix metadata, not pixels.
    if (GLImage::Check3D(g->mDestImage)) {
        pvzh1_cache_complete_screen_batch();
#ifdef PVZ_HOST_RUNNER
        pvzh1_host_reanim_batch_completed(theImage);
#endif
    }
#endif
}''',1)
    path.write_text(text,encoding='utf8',newline='\n')


def patch_atlas_entry(path):
    text=path.read_text(encoding='utf8')
    marker='PVZH1_PRE_DRAW_UNUSED_ATLAS_GC'
    if marker in text:return
    anchor='extern "C" void pvzh1_cache_complete_draw_batch();'
    if text.count(anchor)!=1:raise RuntimeError('Pinned widget cache declaration changed')
    text=text.replace(anchor,anchor+'\nextern "C" void pvzh1_reanim_atlas_end_frame(void*);',1)
    anchor='\tH1WidgetDrawScope h1DrawScope(true);\n#endif'
    if text.count(anchor)!=1:raise RuntimeError('Pinned screen drawing scope changed')
    text=text.replace(anchor,'''\tH1WidgetDrawScope h1DrawScope(true);
    // PVZH1_PRE_DRAW_UNUSED_ATLAS_GC: root screen drawing has not begun.
    // Temporary Draw/MakeCached animations have not been constructed yet;
    // the existing collector protects every holder instance, including dead.
    if (h1WidgetDrawDepth == 1 && h1RootScreenDraw && GLImage::Check3D(mImage))
        pvzh1_reanim_atlas_end_frame(nullptr);
#endif''',1)
    path.write_text(text,encoding='utf8',newline='\n')


def apply(source_root):
    root=Path(source_root)
    patch_widget(root/'SexyAppFramework/widget/WidgetManager.cpp')
    patch_triangles(root/'PvzpLib/EffectSystem.cpp')
    patch_atlas(root/'PvzpLib/Reanimator.cpp')
    patch_reanim_matrix(root/'PvzpLib/Reanimator.cpp')
    patch_atlas_entry(root/'SexyAppFramework/widget/WidgetManager.cpp')
