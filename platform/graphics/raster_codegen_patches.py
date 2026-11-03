"""H1-only 32bpp registration pruning and bounded hot-kernel optimization.

H1's MemoryImage destinations are uint32 ARGB/RGB; LCD565 conversion happens
after rendering. Keep all public declarations and the128-entry table ABI.
The linker can collect16bpp implementations once their registry roots vanish.
"""
from pathlib import Path
import re


def wrap_registrations(path, pattern, count, marker):
    text = path.read_text(encoding='utf8')
    rows = list(re.finditer(pattern, text, re.M))
    if len(rows) != count:
        raise RuntimeError(f'Pinned raster registration count changed: {path}: {len(rows)} != {count}')
    if marker in text:
        # Idempotence still checks that the original functions were retained
        # under one H1 guard, rather than deleting the non-H1 implementation.
        start = text.index('#ifndef H1_PVZ_PORTABLE // ' + marker)
        end = text.index('#endif // ' + marker, start)
        if not all(start < row.start() < end for row in rows):
            raise RuntimeError('Raster registry guard no longer encloses expected rows: ' + marker)
        return
    first,last=rows[0].start(),rows[-1].end()
    text=(text[:first]+'#ifndef H1_PVZ_PORTABLE // '+marker+'\n'+text[first:last]+
          '#endif // '+marker+'\n'+text[last:])
    path.write_text(text,encoding='utf8',newline='\n')


def apply(source_root):
    source_root=Path(source_root)
    wrap_registrations(source_root/'SexyAppFramework/graphics/SWTri.cpp',
        r'^\s*gDrawTriFunc\[\d+\] = DrawTriangle_0(?:565|555)_[^\n]+\n',64,
        'PVZH1_UNREACHABLE_STANDARD_16BPP_REGISTRY')
    wrap_registrations(source_root/'PvzpLib/PvzpCommon.cpp',
        r'^\s*SWTri_AddDrawTriFunc\([^\n]*0x0(?:565|555)[^\n]*\n',32,
        'PVZH1_UNREACHABLE_PVZP_16BPP_REGISTRY')
    path=source_root/'SexyAppFramework/graphics/SWTri/SWTri_DrawTriangle.inc'
    text=path.read_text(encoding='utf8')
    marker='PVZH1_HOT_0888_TEXTURED_OPTIONS'
    if marker not in text:
        if text.count('void\tSexy::funcname(')!=1 or text.count('#undef NAME5')!=1:
            raise RuntimeError('Pinned hot raster function generator changed')
        begin=('// '+marker+': only16normal RGB32 textured variants; no fast math.\n'
               '#if defined(H1_PVZ_PORTABLE) && TRI0 == 1 && TRI1 == 1\n'
               '#pragma GCC push_options\n'
               '#pragma GCC optimize("O2", "omit-frame-pointer")\n'
               '#endif\n')
        end=('\n#if defined(H1_PVZ_PORTABLE) && TRI0 == 1 && TRI1 == 1\n'
             '#pragma GCC pop_options\n#endif // '+marker+'\n')
        path.write_text(begin+text+end,encoding='utf8',newline='\n')
    elif text.count('#pragma GCC optimize("O2", "omit-frame-pointer")')!=1 or text.count('#pragma GCC pop_options')!=1:
        raise RuntimeError('Hot raster optimization guard changed')
