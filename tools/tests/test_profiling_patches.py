"""Check optional timing hooks without compiling/changing game logic or devices."""
from pathlib import Path
import hashlib
import importlib.util
import json
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/profiling-patch-test'
OUT.mkdir(parents=True, exist_ok=True)
spec = importlib.util.spec_from_file_location('profile_patch', ROOT / 'platform/profiling_patches.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
files = (
    ('SexyAppFramework/SexyAppBase.cpp', 'bool SexyAppBase::DoUpdateFrames()', 'UPDATE', 'Update'),
    ('SexyAppFramework/SexyAppBase.cpp', 'void SexyAppBase::DoUpdateFramesF(float theFrac)', 'UPDATE', 'UpdateFraction'),
    ('SexyAppFramework/SexyAppBase.cpp', 'bool SexyAppBase::DrawDirtyStuff()', 'FRAME', 'Frame'),
    ('SexyAppFramework/widget/WidgetManager.cpp', 'bool WidgetManager::DrawScreen()', 'DRAW', 'Draw'),
    ('SexyAppFramework/graphics/SWTri.cpp',
     'void SWHelper::SWDrawShape(XYZStruct *theVerts, int theNumVerts, MemoryImage *theImage, const Color &theColor, [[maybe_unused]] int theDrawMode, const Rect &theClipRect, void *theSurface, int thePitch, int thePixelFormat, bool blend, bool vertexColor)',
     'SW_DRAW_SHAPE', 'SWDrawShape'),
)


def strip(text, relative):
    include = module.INCLUDE + '\n'
    assert text.count(include) == 1
    text = text.replace(include, '', 1)
    for file, signature, category, label in files:
        if file != relative:
            continue
        injection = ('\n' + module.GUARD + '\n'
                     '    // PVZH1_PROFILE_' + label.upper() + '_BOUNDARY: inclusive whole-call CPU count; no timer waits.\n'
                     '    PVZProfileScope pvzProfile' + label + 'Scope(PVZ_PROFILE_' + category + ');\n#endif')
        assert text.count(injection) == 1
        assert signature + '\n{' + injection in text
        text = text.replace(injection, '', 1)
    return text


checks = []
for source, label in ((ROOT / 'upstream/src', 'pinned'),
                      (ROOT / 'build/native-hardware-opt-pool/source', 'frozen-8fbd')):
    destination = OUT / label
    originals = {}
    for relative, _, _, _ in files:
        path = destination / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        originals[relative] = (source / relative).read_text(encoding='utf8')
        path.write_text(originals[relative], encoding='utf8', newline='\n')
    module.apply(destination)
    first = {relative: (destination / relative).read_bytes() for relative, _, _, _ in files}
    module.apply(destination)
    for relative, signature, category, scope in files:
        actual = (destination / relative).read_text(encoding='utf8')
        assert first[relative] == (destination / relative).read_bytes(), 'Not idempotent'
        assert strip(actual, relative) == originals[relative], 'Business source changed'
    checks.append(label + ': checked anchors + idempotence + complete reverse equivalence')

# Check that the complete preparation pipeline composes with existing root draw
# scopes/cache hooks while preserving the frozen business implementation.
sys.path.insert(0, str(ROOT / 'tools'))
from prepare import prepare
prepared, _ = prepare(OUT / 'prepared/source')
for relative, signature, category, scope in files:
    actual = (prepared / relative).read_text(encoding='utf8')
    baseline = (ROOT / 'build/native-hardware-opt-pool/source' / relative).read_text(encoding='utf8')
    if relative.endswith('/SWTri.cpp'):
        # Renderer optimization patches can independently change SWTri.cpp.
        # Pinned/frozen tests above isolate our insertion; complete prepare
        # must keep its unique boundary and remain idempotent as a whole.
        before = (prepared / relative).read_bytes()
        module.apply(prepared)
        assert (prepared / relative).read_bytes() == before
        assert 'PVZH1_PROFILE_SWDRAWSHAPE_BOUNDARY' in actual
    else:
        assert strip(actual, relative) == baseline
checks.append('Complete prepare: unchanged business files reverse to 8fbd; independently optimized SWTri retains the checked idempotent boundary')
report = dict(passed=True, checks=checks, macro='PVZ_NATIVE_PROFILE',
              scopes=dict(update='DoUpdateFrames + DoUpdateFramesF; count means update passes, including early return',
                          draw='WidgetManager::DrawScreen; excludes present/redraw',
                          frame='SexyAppBase::DrawDirtyStuff; inclusive render + present + nested audio/cache',
                          swdraw='SWDrawShape whole call including clipping/setup/raster; subset of DRAW'),
              source_sha256=hashlib.sha256((ROOT / 'platform/profiling_patches.py').read_bytes()).hexdigest(),
              integration_sha256=hashlib.sha256((ROOT / 'platform/source_patches.py').read_bytes()).hexdigest(),
              compiled=False, device_used=False, game_state_modified=False)
(OUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report))
