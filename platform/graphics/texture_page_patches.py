"""Keep original SWTri math while reading immutable ARGB texture pages."""
from pathlib import Path


def replace_one(text, old, new, label):
    if text.count(old) != 1:
        raise RuntimeError('Pinned SWTri context changed: ' + label)
    return text.replace(old, new, 1)


def apply(source_root):
    graphics = Path(source_root) / 'SexyAppFramework/graphics'
    path = graphics / 'SWTri.h'
    text = path.read_text(encoding='utf8')
    marker = 'PVZH1_PAGED_SW_TEXTURE_VIEW'
    if marker not in text:
        old = '''\tstruct\tSWTextureInfo
\t{
\t\tconst unsigned int *\tpTexture;
\t\tint pitch;
\t\tunsigned int endpos;
\t};'''
        new = '''\tstruct\tSWTextureInfo
\t{
\t\tconst unsigned int *\tpTexture;
\t\tint pitch;
\t\tunsigned int endpos;
#ifdef H1_PVZ_PORTABLE
        // PVZH1_PAGED_SW_TEXTURE_VIEW: the original flat fields stay first.
        const unsigned int * const *pages = nullptr;
#endif
        unsigned int Read(unsigned int index) const {
#ifdef H1_PVZ_PORTABLE
            if (pages) return pages[index >> 10u][index & 1023u];
#endif
            return pTexture[index];
        }
\t};'''
        text = replace_one(text, old, new, 'texture view fields')
        path.write_text(text, encoding='utf8', newline='\n')

    path = graphics / 'SWTri/SWTri_GetTexel.inc'
    text = path.read_text(encoding='utf8')
    marker = 'PVZH1_PAGED_SW_TEXEL_FETCH'
    if marker not in text:
        if text.count('pTexture[') != 5:
            raise RuntimeError('Pinned SWTri must have exactly five texel fetches')
        replacements = [
            ('pTexture[t_pos]', 'textureInfo->Read(t_pos)'),
            ('pTexture[t_pos+1]', 'textureInfo->Read(t_pos+1)'),
            ('pTexture[t_pos+tex_pitch]', 'textureInfo->Read(t_pos+tex_pitch)'),
            ('pTexture[t_pos+tex_pitch+1]', 'textureInfo->Read(t_pos+tex_pitch+1)'),
        ]
        # The base fetch occurs in nearest and bilinear paths; the other three
        # are distinct neighbors. Retain every original index, bounds guard,
        # filtering factor, channel operation and blending formula verbatim.
        if text.count(replacements[0][0]) != 2:
            raise RuntimeError('Pinned SWTri base texel fetches changed')
        for old, new in replacements:
            text = text.replace(old, new)
        text = text.replace('\tunsigned int tex;',
                            '\t// PVZH1_PAGED_SW_TEXEL_FETCH: storage selection only.\n\tunsigned int tex;', 1)
        if marker not in text or 'pTexture[' in text:
            raise RuntimeError('Paged SWTri fetch replacement is incomplete')
        path.write_text(text, encoding='utf8', newline='\n')

    path = graphics / 'SWTri.cpp'
    text = path.read_text(encoding='utf8')
    marker = 'PVZH1_PAGED_SW_TEXTURE_PREPARE'
    if marker not in text:
        text = replace_one(text, '#include "SWTri.h"',
                           '#include "SWTri.h"\n#ifdef H1_PVZ_PORTABLE\n#include "MemoryImageCache.h"\n#endif',
                           'cache declaration include')
        # Even a paged shader copies the original flat pointer to an unused
        # local in generated kernels. Zero-initialize it instead of exposing an
        # indeterminate pointer; pitch/endpos are filled below as before.
        text = replace_one(text, '\t\t\tSWTextureInfo\ttextureInfo;',
                           '\t\t\tSWTextureInfo\ttextureInfo{};', 'initialized local view')
        old = '\t\t\t\ttextureInfo.pTexture = reinterpret_cast<unsigned int *>(theImage->GetBits());'
        new = '''#ifdef H1_PVZ_PORTABLE
                // PVZH1_PAGED_SW_TEXTURE_PREPARE: only after original clipping.
                // Preparation pins the source; no view survives this draw.
                if (!Sexy::H1ImageCachePrepareTexturePages(theImage, &textureInfo.pages))
#endif
\t\t\t\ttextureInfo.pTexture = reinterpret_cast<unsigned int *>(theImage->GetBits());'''
        text = replace_one(text, old, new, 'clipped texture access')
        path.write_text(text, encoding='utf8', newline='\n')
    text=path.read_text(encoding='utf8')
    if 'PVZH1_PHYSICAL_TEXTURE_UV' not in text:
        text=replace_one(text,'\t\t\tif (textured)\n\t\t\t{\n\t\t\t\tfor (unsigned int i = 0; i < vCount; ++i)',
            '''\t\t\tif (textured)
\t\t\t{
                // PVZH1_PHYSICAL_TEXTURE_UV: geometry remains logical.
                unsigned int textureWidth=theImage->mWidth,textureHeight=theImage->mHeight;
                bool storageTexture=false;
#ifdef H1_PVZ_PORTABLE
                storageTexture=Sexy::H1ImageCachePrepareStorageTexture(theImage,&textureInfo.pTexture,&textureWidth,&textureHeight);
                if(storageTexture)blend=false; // Offline filtering already targets the LCD texel density.
#endif
\t\t\t\tfor (unsigned int i = 0; i < vCount; ++i)''','physical texture preparation')
        text=replace_one(text,'clipped[i]->mU * (float) theImage->mWidth * 65536.0f','clipped[i]->mU * (float) textureWidth * 65536.0f','physical U')
        text=replace_one(text,'clipped[i]->mV * (float) theImage->mHeight * 65536.0f','clipped[i]->mV * (float) textureHeight * 65536.0f','physical V')
        text=replace_one(text,'if (!Sexy::H1ImageCachePrepareTexturePages(theImage, &textureInfo.pages))','if (!storageTexture && !Sexy::H1ImageCachePrepareTexturePages(theImage, &textureInfo.pages))','physical before paged')
        text=replace_one(text,'textureInfo.pitch = theImage->mWidth;','textureInfo.pitch = textureWidth;','physical pitch')
        text=replace_one(text,'textureInfo.endpos = theImage->mWidth*theImage->mHeight;','textureInfo.endpos = textureWidth*textureHeight;','physical end')
        path.write_text(text,encoding='utf8',newline='\n')
