"""Logical-cell sampling and fixed-startup atlas policy for offline H1 packs."""
from pathlib import Path


def replace_one(text, old, new, label):
    if text.count(old) != 1:
        raise RuntimeError('Pinned reduced-texture context changed: ' + label)
    return text.replace(old, new, 1)


def apply(source_root):
    source_root = Path(source_root)
    graphics = source_root / 'SexyAppFramework/graphics'
    path = graphics / 'SWTri.h'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_REDUCED_TEXTURE_CELL_CLAMP' not in text:
        text = replace_one(text,
            '        const unsigned int * const *pages = nullptr;',
            '''        const unsigned int * const *pages = nullptr;
        // PVZH1_REDUCED_TEXTURE_CELL_CLAMP: only physical nearest views use it.
        bool reducedClamp = false;
        int clampLeft=0,clampTop=0,clampRight=0,clampBottom=0;
        unsigned int ReadClamped(int x,int y) const {
            if(x<clampLeft)x=clampLeft;
            if(x>=clampRight)x=clampRight-1;
            if(y<clampTop)y=clampTop;
            if(y>=clampBottom)y=clampBottom-1;
            return pTexture[(unsigned int)y*(unsigned int)pitch+(unsigned int)x];
        }''', 'physical cell clamp')
        path.write_text(text, encoding='utf8', newline='\n')

    path = graphics / 'SWTri/SWTri_GetTexel.inc'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_REDUCED_NEAREST_FETCH' not in text:
        old = '''\t\tunsigned int\tt_pos = ((v)>>16)*tex_pitch + ((u)>>16);
\t\ttex = t_pos<tex_endpos?textureInfo->Read(t_pos):0;'''
        new = '''#ifdef H1_PVZ_PORTABLE
        // PVZH1_REDUCED_NEAREST_FETCH: clamp the selected half-open cell.
        if(textureInfo->reducedClamp)tex=textureInfo->ReadClamped(u>>16,v>>16);
        else
#endif
        {
\t\tunsigned int\tt_pos = ((v)>>16)*tex_pitch + ((u)>>16);
\t\ttex = t_pos<tex_endpos?textureInfo->Read(t_pos):0;
        }'''
        text = replace_one(text, old, new, 'physical nearest cell fetch')
        path.write_text(text, encoding='utf8', newline='\n')

    path = graphics / 'SWTri.cpp'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_REDUCED_CELL_UV_BEFORE_CLIP' not in text:
        text = replace_one(text, '#include "MemoryImageCache.h"',
            '#include "MemoryImageCache.h"\n#include "ReducedTextureMapping.h"', 'mapping declaration')
        old = '''                // PVZH1_PHYSICAL_TEXTURE_UV: geometry remains logical.
                unsigned int textureWidth=theImage->mWidth,textureHeight=theImage->mHeight;
                bool storageTexture=false;
#ifdef H1_PVZ_PORTABLE
                storageTexture=Sexy::H1ImageCachePrepareStorageTexture(theImage,&textureInfo.pTexture,&textureWidth,&textureHeight);
                if(storageTexture)blend=false; // Offline filtering already targets the LCD texel density.
#endif
'''
        text = replace_one(text, old, '', 'move physical preparation before vertex conversion')
        old = '\t\t\tSWTextureInfo\ttextureInfo{};'
        new = '''\t\t\tSWTextureInfo\ttextureInfo{};
            // PVZH1_PHYSICAL_TEXTURE_UV: geometry remains logical.
            unsigned int textureWidth=theImage?theImage->mWidth:0;
            unsigned int textureHeight=theImage?theImage->mHeight:0;
            bool storageTexture=false;
#ifdef H1_PVZ_PORTABLE
            // PVZH1_REDUCED_CELL_UV_BEFORE_CLIP: copy only the current three
            // strip vertices. No assumption is made about total strip length.
            XYZStruct mappedTriangle[3];
            if(textured)storageTexture=Sexy::H1ImageCachePrepareStorageTexture(theImage,&textureInfo.pTexture,&textureWidth,&textureHeight);
            if(storageTexture){
                blend=false;
                float u[3],v[3];
                for(int j=0;j<3;++j){mappedTriangle[j]=*aTriRef[j];u[j]=mappedTriangle[j].mU;v[j]=mappedTriangle[j].mV;}
                Sexy::H1ReducedTextureBounds bounds{};
                if(!Sexy::H1MapReducedTriangle(u,v,theImage->mWidth,theImage->mHeight,textureWidth,textureHeight,bounds)){
                    ++aTVertPtr;++aTriCounter;aOddTriangle=!aOddTriangle;continue;
                }
                for(int j=0;j<3;++j){mappedTriangle[j].mU=u[j];mappedTriangle[j].mV=v[j];aTriRef[j]=&mappedTriangle[j];}
                vCount=clipShape(clipped,aTriRef,clipX0,clipX1,clipY0,clipY1);
                textureInfo.reducedClamp=true;
                textureInfo.clampLeft=bounds.left;textureInfo.clampRight=bounds.right;
                textureInfo.clampTop=bounds.top;textureInfo.clampBottom=bounds.bottom;
            }
#endif'''
        text = replace_one(text, old, new, 'preclip physical vertex mapping')
        path.write_text(text, encoding='utf8', newline='\n')

    path = graphics / 'SWTri.cpp'
    text = path.read_text(encoding='utf8')
    if '#include "SoftwareRaster.h"' not in text:
        text = replace_one(text, '#include "ReducedTextureMapping.h"',
            '#include "ReducedTextureMapping.h"\n#include "SoftwareRaster.h"', 'span declaration')
        path.write_text(text, encoding='utf8', newline='\n')
    if '#define PVZH1_SWTRI_NORMAL_SHADER 1' not in text:
        text = replace_one(text, '#include "SoftwareRaster.h"',
            '#include "SoftwareRaster.h"\n// Only this TU uses the original normal 0888 shader. EffectSystem shares\n// SWTri_Loop.inc with custom additive shaders and must never use this fast path.\n#define PVZH1_SWTRI_NORMAL_SHADER 1', 'normal shader translation-unit scope')
        path.write_text(text, encoding='utf8', newline='\n')
    path = graphics / 'SWTri/SWTri_Loop.inc'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_REDUCED_HORIZONTAL_SPAN' not in text:
        old = '\twhile(width-- > 0)'
        new = '''#if defined(H1_PVZ_PORTABLE) && defined(PVZH1_SWTRI_NORMAL_SHADER) && TRI0==1 && defined(TEXTURED) && !defined(LINEAR_BLEND) && !defined(MOD_ARGB) && !defined(GLOBAL_ARGB)
    // PVZH1_REDUCED_HORIZONTAL_SPAN: original clipping and fixed-point setup
    // have already run. All other formats, tints and geometry retain the oracle.
    if (Sexy::H1FastReducedSpan(pix,width,textureInfo,u,v,du,dv,
#if defined(TEX_ALPHA)
        true
#else
        false
#endif
    )) width=0;
#endif
\twhile(width-- > 0)'''
        text = replace_one(text, old, new, 'axis aligned reduced spans')
        path.write_text(text, encoding='utf8', newline='\n')
    elif 'defined(PVZH1_SWTRI_NORMAL_SHADER)' not in text:
        old = '#if defined(H1_PVZ_PORTABLE) && TRI0==1 && defined(TEXTURED) && !defined(LINEAR_BLEND) && !defined(MOD_ARGB) && !defined(GLOBAL_ARGB)'
        new = '#if defined(H1_PVZ_PORTABLE) && defined(PVZH1_SWTRI_NORMAL_SHADER) && TRI0==1 && defined(TEXTURED) && !defined(LINEAR_BLEND) && !defined(MOD_ARGB) && !defined(GLOBAL_ARGB)'
        text = replace_one(text, old, new, 'normal shader guard upgrade')
        path.write_text(text, encoding='utf8', newline='\n')

    path = source_root / 'PvzpLib/Reanimator.cpp'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_REDUCED_PACK_NO_NEW_ATLAS' not in text:
        text = '#include "graphics/MemoryImageCache.h"\n' + text
        old = 'void ReanimationCreateAtlas(ReanimatorDefinition* theDefinition, ReanimationType theReanimationType)\n{'
        new = old + '''
#ifdef H1_PVZ_PORTABLE
    // PVZH1_REDUCED_PACK_NO_NEW_ATLAS: fixed before startup. The original
    // no-atlas image/cel branch retains logical frame geometry and pivots.
    if(theDefinition->mReanimAtlas==nullptr&&Sexy::H1ImageCacheReducedPack())return;
#endif'''
        text = replace_one(text, old, new, 'fixed startup atlas policy')
        path.write_text(text, encoding='utf8', newline='\n')

    _apply_screen_sampler(graphics)
    _apply_render_plan(graphics)
    _apply_text_replay(graphics)


def _apply_screen_sampler(graphics):
    """Inline only normal 32-bit screen fetches; custom Effect shaders stay old."""
    path = graphics / 'SWTri.h'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_INLINE_SCREEN_FETCH' not in text:
        text = replace_one(text, '#include "MemoryImage.h"',
            '#include "MemoryImage.h"\n#ifdef PVZH1_SAMPLER_TEST\nextern "C" bool pvzh1_sampler_test_enabled;\n#endif', 'sampler test declaration')
        old = '        unsigned int ReadClamped(int x,int y) const {'
        new = '''        // PVZH1_INLINE_SCREEN_FETCH: called only by normal 0888 shaders.
        // Do not force-expand these readers in the 16-bit/offscreen/custom
        // shader families; this bounds linked code growth.
        __attribute__((always_inline)) inline unsigned int ReadScreen(unsigned int index) const {
#ifdef PVZH1_SAMPLER_TEST
            if(!pvzh1_sampler_test_enabled)return Read(index);
#endif
            if(pages)return pages[index>>10u][index&1023u];
            return pTexture[index];
        }
        __attribute__((always_inline)) inline unsigned int ReadScreenClamped(int x,int y) const {
#ifdef PVZH1_SAMPLER_TEST
            if(!pvzh1_sampler_test_enabled)return ReadClamped(x,y);
#endif
            if(x<clampLeft)x=clampLeft;
            if(x>=clampRight)x=clampRight-1;
            if(y<clampTop)y=clampTop;
            if(y>=clampBottom)y=clampBottom-1;
            return pTexture[(unsigned int)y*(unsigned int)pitch+(unsigned int)x];
        }
        unsigned int ReadClamped(int x,int y) const {'''
        text = replace_one(text, old, new, 'inline screen fetches')
        path.write_text(text, encoding='utf8', newline='\n')


    path = graphics / 'SWTri.cpp'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_INLINE_SCREEN_FIXED_FLOOR' not in text:
        old = '// Begin clipping stuff'
        new = '''// PVZH1_INLINE_SCREEN_FIXED_FLOOR: retain the original nonpositive
// boundary convention exactly, including x==0 selecting the previous texel.
static __attribute__((always_inline)) inline int H1ScreenFixedFloor(int x)
{
#ifdef PVZH1_SAMPLER_TEST
    if(!pvzh1_sampler_test_enabled)return FixedFloor(x);
#endif
    if(x>0)return x&0xFFFF0000;
    return (x&0xFFFF0000)-0x10000;
}

// Begin clipping stuff'''
        text = replace_one(text, old, new, 'inline screen floor')
    if 'PVZH1_OPAQUE_STORAGE_SHADER' not in text:
        old = '                blend=false;\n                float u[3],v[3];'
        new = '''                blend=false;
                // PVZH1_OPAQUE_STORAGE_SHADER: LoadStorage has classified the
                // actual bytes for unforced images. All-alpha-255 nearest
                // storage need not retain talpha solely from the old linear
                // request. Forced modes and every transparent image stay old.
                if(
#ifdef PVZH1_SAMPLER_TEST
                   pvzh1_sampler_test_enabled&&
#endif
                   thePixelFormat==0x888&&!theImage->mForcedMode&&
                   !theImage->mHasAlpha&&!theImage->mHasTrans)talpha=false;
                float u[3],v[3];'''
        text = replace_one(text, old, new, 'opaque storage shader selection')
    path.write_text(text, encoding='utf8', newline='\n')

    path = graphics / 'SWTri/SWTri_GetTexel.inc'
    text = path.read_text(encoding='utf8')
    if 'PVZH1_SCREEN_FETCH_DISPATCH' not in text:
        if text.count('textureInfo->Read(')!=5 or text.count('textureInfo->ReadClamped(')!=1 or text.count('FixedFloor(')!=2:
            raise RuntimeError('Pinned screen texel fetch/floor count changed')
        text = text.replace('textureInfo->ReadClamped(', 'PVZH1_SCREEN_CLAMP(')
        text = text.replace('textureInfo->Read(', 'PVZH1_SCREEN_READ(')
        text = text.replace('FixedFloor(', 'PVZH1_SCREEN_FLOOR(')
        prefix = '''// PVZH1_SCREEN_FETCH_DISPATCH: the shared include also belongs to
// EffectSystem custom shaders; only the normal SWTri TU opts into this path.
#if defined(H1_PVZ_PORTABLE) && defined(PVZH1_SWTRI_NORMAL_SHADER) && TRI0==1
#define PVZH1_SCREEN_READ(index) textureInfo->ReadScreen(index)
#define PVZH1_SCREEN_CLAMP(x,y) textureInfo->ReadScreenClamped(x,y)
#define PVZH1_SCREEN_FLOOR(value) H1ScreenFixedFloor(value)
#else
#define PVZH1_SCREEN_READ(index) textureInfo->Read(index)
#define PVZH1_SCREEN_CLAMP(x,y) textureInfo->ReadClamped(x,y)
#define PVZH1_SCREEN_FLOOR(value) FixedFloor(value)
#endif
'''
        text = prefix + text + '\n#undef PVZH1_SCREEN_READ\n#undef PVZH1_SCREEN_CLAMP\n#undef PVZH1_SCREEN_FLOOR\n'
        path.write_text(text, encoding='utf8', newline='\n')


def _apply_render_plan(graphics):
    path=graphics/'SWTri.cpp'
    text=path.read_text(encoding='utf8')
    if 'PVZH1_UNCLIPPED_TRIANGLE' not in text:
        old='   vertexReservoirUsed = 0;\n\n   SWHelper::XYZStruct *  buf[64];'
        new='''   vertexReservoirUsed = 0;
#ifdef H1_PVZ_PORTABLE
   // PVZH1_UNCLIPPED_TRIANGLE: the four original clipping passes copy the
   // same vertex pointers, in the same order, when all vertices are inside.
   bool inside=true;
#ifdef PVZ_HOST_RUNNER
   inside=pvzh1_geometry_fast_enabled;
#endif
   for(unsigned i=0;inside&&i<3;++i){
       const auto* p=src[i];
       inside=p->mX>=left&&p->mX<=right&&p->mY>=top&&p->mY<=bottom;
   }
   if(inside){dst[0]=src[0];dst[1]=src[1];dst[2]=src[2];dst[3]=nullptr;return 3;}
#endif

   SWHelper::XYZStruct *  buf[64];'''
        text=replace_one(text,old,new,'unclipped triangle pointer passthrough')
    if '#include "RenderPlanCache.h"' not in text:
        text=replace_one(text,'#include "SoftwareRaster.h"','#include "SoftwareRaster.h"\n#include "RenderPlanCache.h"','render plan declaration')
    path.write_text(text,encoding='utf8',newline='\n')
    path=graphics/'SWTri/SWTri_Loop.inc'
    text=path.read_text(encoding='utf8')
    if 'PVZH1_RENDER_PLAN_RECORD' not in text:
        marker='#if defined(H1_PVZ_PORTABLE) && defined(PVZH1_SWTRI_NORMAL_SHADER) && TRI0==1 && defined(TEXTURED) && !defined(LINEAR_BLEND) && !defined(MOD_ARGB) && !defined(GLOBAL_ARGB)'
        hook='''#if defined(H1_PVZ_PORTABLE) && defined(PVZH1_SWTRI_NORMAL_SHADER) && TRI0==1 && defined(TEXTURED) && !defined(LINEAR_BLEND) && !defined(MOD_ARGB) && !defined(GLOBAL_ARGB)
    // PVZH1_RENDER_PLAN_RECORD: zero allocation; custom Effect shaders excluded.
    if(pvzh1_render_plan_capture_active)Sexy::H1RenderPlanRecordSpan(pix,width,
        textureInfo->pTexture,textureInfo->pitch,textureInfo->clampLeft,textureInfo->clampTop,
        textureInfo->clampRight,textureInfo->clampBottom,u,v,du,dv,
#if defined(TEX_ALPHA)
        true
#else
        false
#endif
    );
#endif
'''
        text=replace_one(text,marker,hook+marker,'record original scanline before fast span')
        path.write_text(text,encoding='utf8',newline='\n')


def _apply_text_replay(graphics):
    path=graphics/'SWTri.h';text=path.read_text(encoding='utf8')
    if 'PVZH1_TEXT_CAPTURE_FLAG' not in text:
        text=replace_one(text,'        bool reducedClamp = false;',
            '        bool captureText = false; // PVZH1_TEXT_CAPTURE_FLAG\n        bool reducedClamp = false;','text capture texture flag')
        path.write_text(text,encoding='utf8',newline='\n')
    path=graphics/'SWTri.cpp';text=path.read_text(encoding='utf8')
    if 'PVZH1_TEXT_CAPTURE_TEXTURE' not in text:
        text=replace_one(text,'#include "SoftwareRaster.h"','#include "SoftwareRaster.h"\n#include "TextReplay.h"','text capture declaration')
        text=replace_one(text,'SWTextureInfo\ttextureInfo{};',
            'SWTextureInfo\ttextureInfo{};\n#ifdef H1_PVZ_PORTABLE\n            textureInfo.captureText=pvzh1_text_capture_active!=0; // PVZH1_TEXT_CAPTURE_TEXTURE\n#endif','text capture at triangle boundary')
        path.write_text(text,encoding='utf8',newline='\n')
    path=graphics/'SWTri/SWTri_DrawTriangle.inc';text=path.read_text(encoding='utf8')
    if 'PVZH1_TEXT_CAPTURE_LOCAL' not in text:
        text=replace_one(text,'\t// Sort vertices by Y component',
            '#if defined(H1_PVZ_PORTABLE) && defined(PVZH1_SWTRI_NORMAL_SHADER) && TRI0==1 && defined(TEXTURED)\n'
            '    const bool h1CaptureText=textureInfo->captureText; // PVZH1_TEXT_CAPTURE_LOCAL\n#endif\n\n\t// Sort vertices by Y component','text capture local flag')
        path.write_text(text,encoding='utf8',newline='\n')
    path=graphics/'SWTri/SWTri_Pixel888.inc';text=path.read_text(encoding='utf8')
    if 'PVZH1_TEXT_LITERAL_CAPTURE' not in text:
        guard='#if defined(H1_PVZ_PORTABLE) && defined(PVZH1_SWTRI_NORMAL_SHADER) && TRI0==1\n'
        partial='*pix = 0xFF000000 | ((trb|tg) + (prb|pg));'
        text=replace_one(text,partial,guard+'    if(h1CaptureText)pvzh1_text_capture_pixel(pix,(alpha<<24)|(trb|tg)); // PVZH1_TEXT_LITERAL_CAPTURE\n#endif\n\t\t\t\t\t'+partial,'partial glyph capture')
        opaque='*pix = 0xFF000000 | tex;'
        if text.count(opaque)!=2:raise RuntimeError('Expected two original opaque textured stores')
        text=text.replace(opaque,guard+'    if(h1CaptureText)pvzh1_text_capture_pixel(pix,0xff000000u|tex);\n#endif\n\t\t\t\t\t'+opaque)
        path.write_text(text,encoding='utf8',newline='\n')

