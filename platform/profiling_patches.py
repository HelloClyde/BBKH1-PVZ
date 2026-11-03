"""Optional boundary-only counters for an isolated native diagnostic profile.

The normal build preprocesses the additions away. Pinned game statements and
return paths stay byte-for-byte unchanged; scopes also cover early returns.
"""
from pathlib import Path

GUARD = '#if defined(H1_PVZ_PORTABLE) && defined(PVZ_NATIVE_PROFILE)'
INCLUDE = GUARD + '\n#include "profiler.h"\n#endif\n'


def apply(source_root):
    root = Path(source_root)
    for relative, signature, category, label in (
        ('SexyAppFramework/SexyAppBase.cpp', 'bool SexyAppBase::DoUpdateFrames()',
         'PVZ_PROFILE_UPDATE', 'Update'),
        ('SexyAppFramework/SexyAppBase.cpp', 'void SexyAppBase::DoUpdateFramesF(float theFrac)',
         'PVZ_PROFILE_UPDATE', 'UpdateFraction'),
        ('SexyAppFramework/SexyAppBase.cpp', 'bool SexyAppBase::DrawDirtyStuff()',
         'PVZ_PROFILE_FRAME', 'Frame'),
        ('SexyAppFramework/widget/WidgetManager.cpp', 'bool WidgetManager::DrawScreen()',
         'PVZ_PROFILE_DRAW', 'Draw'),
        ('SexyAppFramework/graphics/SWTri.cpp',
         'void SWHelper::SWDrawShape(XYZStruct *theVerts, int theNumVerts, MemoryImage *theImage, const Color &theColor, [[maybe_unused]] int theDrawMode, const Rect &theClipRect, void *theSurface, int thePitch, int thePixelFormat, bool blend, bool vertexColor)',
         'PVZ_PROFILE_SW_DRAW_SHAPE', 'SWDrawShape'),
    ):
        path = root / relative
        text = path.read_text(encoding='utf8')
        marker = 'PVZH1_PROFILE_' + label.upper() + '_BOUNDARY'
        if marker in text:
            continue
        anchor = 'using namespace Sexy;'
        if text.count(anchor) != 1:
            raise RuntimeError('Pinned profiling namespace changed: ' + relative)
        entry = signature + '\n{'
        if text.count(entry) != 1:
            raise RuntimeError('Pinned profiling boundary changed: ' + signature)
        if INCLUDE not in text:
            text = text.replace(anchor, INCLUDE + '\n' + anchor, 1)
        text = text.replace(entry, entry + '\n' + GUARD + '\n'
                            '    // ' + marker + ': inclusive whole-call CPU count; no timer waits.\n'
                            '    PVZProfileScope pvzProfile' + label + 'Scope(' + category + ');\n'
                            '#endif', 1)
        path.write_text(text, encoding='utf8', newline='\n')
