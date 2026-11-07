"""Apply checked lazy-image integration to a generated PvZ-Portable source tree.

The pinned upstream checkout is never edited. Call apply(source_root) from the
main source preparation script after copying the upstream sources.
"""
from pathlib import Path
import importlib.util

MARKER='PVZH1_LAZY_IMAGE_CACHE'
RASTER_MARKER='PVZH1_COMPLETE_SOFTWARE_DRAWING'

def _replace(text,old,new,label):
    count=text.count(old)
    if count!=1:
        raise RuntimeError(f'{label}: expected one pinned-source match, found {count}')
    return text.replace(old,new,1)

def _body(text,signature,body):
    if text.count(signature)!=1:raise RuntimeError(f'Expected pinned signature: {signature}')
    start=text.index(signature);left=text.index('{',start);depth=1;right=left+1
    while depth:
        if text[right]=='{':depth+=1
        elif text[right]=='}':depth-=1
        right+=1
    return text[:left]+'{\n'+body+'\n}'+text[right:]

def patch_memory_header(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    if RASTER_MARKER in text:return
    needle='\tvirtual void\t\t\tSetImageMode(bool hasTrans, bool hasAlpha);'
    extra='''    // PVZH1_COMPLETE_SOFTWARE_DRAWING: Image base implementations are empty.
    void BltMirror(Image* image, int x, int y, const Rect& src, const Color& color, int mode, bool linear=true) override;
    void StretchBltMirror(Image* image, const Rect& dest, const Rect& src, const Rect& clip, const Color& color, int mode, bool fast) override;

'''
    text=_replace(text,needle,extra+needle,'MemoryImage mirror declarations')
    path.write_text(text,encoding='utf-8',newline='\n')

def patch_memory_raster(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    if RASTER_MARKER in text:return
    text='#include "graphics/SoftwareRaster.h" // '+RASTER_MARKER+'\n'+text
    text=_replace(text,'void MemoryImage::FillRect(const Rect& theRect, const Color& theColor, [[maybe_unused]] int theDrawMode)\n{',
        'void MemoryImage::FillRect(const Rect& theRect, const Color& theColor, [[maybe_unused]] int theDrawMode)\n{\n'
        '\tif (theDrawMode == Graphics::DRAWMODE_ADDITIVE) { H1FillRect(this, theRect, theColor, theDrawMode); return; }','MemoryImage additive fill')
    text=_body(text,'void MemoryImage::StretchBlt(Image* theImage,', '''    if (theSrcRect.mWidth <= 0 || theSrcRect.mHeight <= 0) return;
    SexyTransform2D transform;
    transform.Scale((float)theDestRect.mWidth/theSrcRect.mWidth, (float)theDestRect.mHeight/theSrcRect.mHeight);
    transform.Translate((float)theDestRect.mX, (float)theDestRect.mY);
    H1DrawTransformed(this, theImage, theSrcRect, theClipRect, theColor, theDrawMode, transform, !fastStretch);''')
    text=_body(text,'void MemoryImage::BltMatrix(Image* theImage,',
        '    H1DrawTransformed(this, theImage, theSrcRect, theClipRect, theColor, theDrawMode, theMatrix, blend, x, y, true);')
    text=_body(text,'void MemoryImage::BltTrianglesTex(Image *theTexture,',
        '    H1DrawTriangles(this, theTexture, theVertices, theNumTriangles, theClipRect, theColor, theDrawMode, tx, ty, blend);')
    text=_body(text,'void MemoryImage::FillScanLinesWithCoverage(Span* theSpans,',
        '    H1Coverage(this, theSpans, theSpanCount, theColor, theDrawMode, theCoverage, theCoverX, theCoverY, theCoverWidth, theCoverHeight);')
    text+='''
void MemoryImage::BltMirror(Image* image, int x, int y, const Rect& src, const Color& color, int mode, bool linear)
{
    SexyTransform2D transform;
    transform.Scale(-1, 1); transform.Translate((float)(x+src.mWidth), (float)y);
    H1DrawTransformed(this, image, src, Rect(0,0,mWidth,mHeight), color, mode, transform, linear);
}
void MemoryImage::StretchBltMirror(Image* image, const Rect& dest, const Rect& src, const Rect& clip, const Color& color, int mode, bool fast)
{
    if (src.mWidth <= 0 || src.mHeight <= 0) return;
    SexyTransform2D transform;
    transform.Scale(-(float)dest.mWidth/src.mWidth, (float)dest.mHeight/src.mHeight);
    transform.Translate((float)(dest.mX+dest.mWidth), (float)dest.mY);
    H1DrawTransformed(this, image, src, clip, color, mode, transform, !fast);
}
'''
    path.write_text(text,encoding='utf-8',newline='\n')

def patch_memory(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    if MARKER in text:return
    text=_replace(text,'#include "MemoryImage.h"',
        '#include "MemoryImage.h"\n#include "graphics/MemoryImageCache.h"\n#include "graphics/TextReplay.h" // '+MARKER,'MemoryImage include')
    text=_replace(text,'MemoryImage::~MemoryImage()\n{',
        'MemoryImage::~MemoryImage()\n{\n\tH1ImageReplayForget(this);\n\tH1ImageCacheForget(this);','MemoryImage destructor')
    text=_replace(text,'void MemoryImage::BitsChanged()\n{',
        'void MemoryImage::BitsChanged()\n{\n\tH1ImageCacheForget(this);','MemoryImage mutation')
    text=_replace(text,'void MemoryImage::PurgeBits()\n{',
        'void MemoryImage::PurgeBits()\n{\n\tif (H1ImageCacheContains(this)) { mPurgeBits = true; return; }','MemoryImage purge')
    text=_replace(text,'uint32_t* MemoryImage::GetBits()\n{',
        'uint32_t* MemoryImage::GetBits()\n{\n\tH1ImageCacheTouch(this);\n\tH1ImageCacheLoad(this);','MemoryImage pixels')
    text=_replace(text,'bool MemoryImage::Palletize()\n{',
        'bool MemoryImage::Palletize()\n{\n'
        '\t// A palette is an optional lossless optimization; keep unopened file sources lazy.\n'
        '\tif (H1ImageCacheContains(this) && !mBits && !mColorTable) return false;',
        'defer optional source palette allocation')
    path.write_text(text,encoding='utf-8',newline='\n')

def _pin_entry(text,signature,code):
    if text.count(signature)!=1:raise RuntimeError('Expected pinned pressure signature: '+signature)
    left=text.index('{',text.index(signature))
    return text[:left+1]+'\n'+code+text[left+1:]

def patch_memory_pressure(path):
    path=Path(path);text=path.read_text(encoding='utf8')
    marker='PVZH1_PRESSURE_PIXEL_PINS'
    if marker in text:return
    text='// '+marker+'\n'+text
    for signature in ('void* MemoryImage::GetNativeAlphaData(',
                      'uchar* MemoryImage::GetRLAlphaData(',
                      'uchar* MemoryImage::GetRLAdditiveData(',
                      'bool MemoryImage::Palletize('):
        member={'void* MemoryImage::GetNativeAlphaData(':'mNativeAlphaData',
                'uchar* MemoryImage::GetRLAlphaData(':'mRLAlphaData',
                'uchar* MemoryImage::GetRLAdditiveData(':'mRLAdditiveData',
                'bool MemoryImage::Palletize(':'mColorTable'}[signature]
        text=_pin_entry(text,signature,'    H1ImageCacheTouch(this);\n    H1ImageCacheAccountingChange h1Accounting(this,!'+member+');')
    for signature in ('void MemoryImage::NormalBlt(', 'void MemoryImage::AdditiveBlt(',
                      'void MemoryImage::Blt(', 'void MemoryImage::BltF(',
                      'void MemoryImage::BltRotated(', 'void MemoryImage::SlowStretchBlt(',
                      'void MemoryImage::FastStretchBlt('):
        text=_pin_entry(text,signature,'    H1ImageCacheTouch(this);\n'
            '    H1ImageCacheTouch(dynamic_cast<MemoryImage*>(theImage));')
    path.write_text(text,encoding='utf8',newline='\n')

def patch_alpha_palette(path):
    path=Path(path);text=path.read_text(encoding='utf8')
    marker='PVZH1_EXACT_A8_PALETTE'
    if marker in text:return
    for signature in ('void MemoryImage::NormalBlt(', 'void MemoryImage::AdditiveBlt('):
        begin=text.index(signature);end=text.index('\nvoid MemoryImage::',begin+1)
        body=text[begin:end]
        needle='\tif (aSrcMemoryImage != nullptr)\n\t{\n\t\tif (aSrcMemoryImage->mColorTable == nullptr)'
        replacement='\tif (aSrcMemoryImage != nullptr)\n\t{\n' \
            '\t\t// '+marker+': prepare before borrowing either source representation.\n' \
            '\t\tif (aSrcMemoryImage != this) H1ImageCachePrepareAlphaPalette(aSrcMemoryImage);\n' \
            '\t\tif (aSrcMemoryImage->mColorTable == nullptr)'
        body=_replace(body,needle,replacement,signature+' exact A8 source preparation')
        text=text[:begin]+body+text[end:]
    path.write_text(text,encoding='utf8',newline='\n')

def patch_font_glyph_pages(path):
    path=Path(path);text=path.read_text(encoding='utf8')
    marker='PVZH1_PAGED_RGBA_FONT_GLYPH'
    if marker in text:return
    for name,mode in (('NormalBlt','NORMAL'),('AdditiveBlt','ADDITIVE')):
        signature='void MemoryImage::'+name+'('
        begin=text.index(signature);end=text.index('\nvoid MemoryImage::',begin+1)
        body=text[begin:end]
        needle='\t\tif (aSrcMemoryImage != this) H1ImageCachePrepareAlphaPalette(aSrcMemoryImage);'
        replacement='\t\tif (aSrcMemoryImage != this) { // '+marker+'\n' \
            '\t\t\tH1ImageCachePrepareAlphaPalette(aSrcMemoryImage);\n' \
            '\t\t\tif (H1ImageCacheBltFontGlyph(aSrcMemoryImage,this,theX,theY,theSrcRect,theColor,Graphics::DRAWMODE_'+mode+')) return;\n\t\t}'
        body=_replace(body,needle,replacement,name+' bounded paged-font glyph')
        text=text[:begin]+body+text[end:]
    path.write_text(text,encoding='utf8',newline='\n')

def patch_app_pressure(path):
    path=Path(path);text=path.read_text(encoding='utf8')
    marker='PVZH1_APP_PRESSURE_PIXEL_PINS'
    if marker in text:return
    text='// '+marker+'\n'+text
    for signature in ('void SexyAppBase::ColorizeImage(', 'GLImage* SexyAppBase::CreateColorizedImage(',
                      'GLImage* SexyAppBase::CopyImage(Image* theImage, const Rect& theRect)'):
        text=_pin_entry(text,signature,'    H1ImageCacheTouch(dynamic_cast<MemoryImage*>(theImage));')
    text=_pin_entry(text,'Sexy::GLImage* SexyAppBase::CreateCrossfadeImage(',
        '    H1ImageCacheTouch(dynamic_cast<MemoryImage*>(theImage1));\n'
        '    H1ImageCacheTouch(dynamic_cast<MemoryImage*>(theImage2));')
    path.write_text(text,encoding='utf8',newline='\n')

def patch_app(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    if MARKER in text:return
    text='#include "graphics/MemoryImageCache.h"\n#include "graphics/TextReplay.h" // '+MARKER+'\n'+text
    start=text.index('Sexy::GLImage* SexyAppBase::GetImage(const std::string& theFileName, bool commitBits)')
    end=text.index('Sexy::GLImage* SexyAppBase::CreateCrossfadeImage',start)
    text=text[:start]+'''Sexy::GLImage* SexyAppBase::GetImage(const std::string& theFileName, bool commitBits)
{
    (void)commitBits;
    auto image = std::make_unique<GLImage>(mGLInterface.get());
    if (!H1ImageCacheDescribe(image.get(), theFileName, ImageLib::gAutoLoadAlpha, ImageLib::gAlphaComposeColor))
        return nullptr;
    return image.release();
}

'''+text[end:]
    text=_replace(text,'if (theImage == nullptr || theImage->mBits == nullptr)\n\t\treturn nullptr;',
        'if (theImage == nullptr || theImage->GetBits() == nullptr)\n\t\treturn nullptr;','lazy custom cursor pixels')
    path.write_text(text,encoding='utf-8',newline='\n')

def patch_sanding(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    if MARKER in text:return
    text='#include "graphics/MemoryImageCache.h"\n#include "graphics/TextReplay.h" // '+MARKER+'\n'+text
    start=text.index('void FixPixelsOnAlphaEdgeForBlending(Image* theImage)')
    end=text.index('\nvoid SexyMatrix3Transpose',start)
    section=text[start:end]
    section=_replace(section,'aImage->mFilePath.clear();',
        'if (!H1ImageCacheAfterSanding(aImage, FixPixelsOnAlphaEdgeForBlending, AverageNearByPixels))\n\t\taImage->mFilePath.clear();','sanding reload descriptor')
    text=text[:start]+section+text[end:]
    path.write_text(text,encoding='utf-8',newline='\n')

def patch_storage_sanding(path):
    path=Path(path);text=path.read_text(encoding='utf8')
    marker='PVZH1_STORAGE_SANDING'
    if marker in text:return
    text=_replace(text,'void FixPixelsOnAlphaEdgeForBlending(Image* theImage)\n{',
        'void FixPixelsOnAlphaEdgeForBlending(Image* theImage)\n{\n'
        '    // '+marker+': keep reduced source pixels physical, including their alpha edge recipe.\n'
        '    if (H1ImageCacheSandingStorage((MemoryImage*)theImage, FixPixelsOnAlphaEdgeForBlending, AverageNearByPixels)) return;',
        'physical source sanding')
    path.write_text(text,encoding='utf8',newline='\n')

def patch_resource_alpha(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    marker='PVZH1_LAZY_ALPHA_GRIDS'
    if marker in text:return
    text='#include "graphics/MemoryImageCache.h"\n#include "graphics/TextReplay.h" // '+marker+'\n'+text
    text=_body(text,'bool ResourceManager::LoadAlphaGridImage(ImageRes *theRes, GLImage *theImage)',
        '''    if (!H1ImageCacheSetAlpha(theImage, theRes->mAlphaGridImage, theRes->mRows, theRes->mCols))
        return Fail(std::format("Failed to describe alpha grid: {}", theRes->mAlphaGridImage));
    return true;''')
    text=_body(text,'bool ResourceManager::LoadAlphaImage(ImageRes *theRes, GLImage *theImage)',
        '''    if (!H1ImageCacheSetAlpha(theImage, theRes->mAlphaImage, 1, 1))
        return Fail(std::format("Failed to describe alpha image: {}", theRes->mAlphaImage));
    return true;''')
    path.write_text(text,encoding='utf-8',newline='\n')

def patch_filters(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    marker='PVZH1_FILTER_IMAGE_PIXEL_CONTRACT'
    if marker in text:return
    needle='uint32_t* ptr = theImage->mBits.get();'
    if text.count(needle)!=2:raise RuntimeError('Expected the two original filter pixel accessors')
    text='// '+marker+'\n'+text.replace(needle,'uint32_t* ptr = theImage->GetBits();')
    old='''			ptr++;
		}
	}
}

void FilterEffectDoWashedOut'''
    new='''			ptr++;
		}
	}
	theImage->BitsChanged();
}

void FilterEffectDoWashedOut'''
    text=_replace(text,old,new,'filter luminosity mutation')
    text=_replace(text,'\t\t\t*ptr++ |= 0x00FFFFFF;\n}',
        '\t\t\t*ptr++ |= 0x00FFFFFF;\n\ttheImage->BitsChanged();\n}','filter white mutation')
    path.write_text(text,encoding='utf-8',newline='\n')

def patch_graphics_scratch(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    marker='PVZH1_LAZY_COVERAGE_SCRATCH'
    if marker in text:return
    text=_replace(text,'static uint8_t aCoverageBuffer[256*256];',
        'static std::unique_ptr<uint8_t[]> aCoverageBuffer; // '+marker+'\n'
        '\tif (!aCoverageBuffer) aCoverageBuffer = std::make_unique<uint8_t[]>(256*256);','AA coverage scratch storage')
    text=_replace(text,'uint8_t* coverPtr = aCoverageBuffer;',
        'uint8_t* coverPtr = aCoverageBuffer.get();','AA scratch pointer')
    path.write_text(text,encoding='utf-8',newline='\n')

def patch_font_command_pool(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    marker='PVZH1_HEAP_FONT_COMMAND_POOL'
    if marker in text:return
    text=_replace(text,'static RenderCommand gRenderCommandPool[POOL_SIZE];',
        'static std::unique_ptr<RenderCommand[]> gRenderCommandPool; // '+marker,'font command pool backing')
    text=_replace(text,'std::scoped_lock anAutoCrit(gRenderCritSec);',
        'std::scoped_lock anAutoCrit(gRenderCritSec);\n'
        '\tif (!gRenderCommandPool) gRenderCommandPool = std::make_unique<RenderCommand[]>(POOL_SIZE);','font command pool allocation')
    path.write_text(text,encoding='utf-8',newline='\n')

def patch_triangle_zero_alpha(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    marker='PVZH1_TRIANGLE_ZERO_MODULATED_ALPHA'
    if marker in text:return
    text=_replace(text,'#include "SWTri_TexelARGB.inc"',
        '#include "SWTri_TexelARGB.inc"\n\t\t\tif (alpha > 0) { // '+marker,'post-modulation transparent triangle pixel')
    text=_replace(text,'\t\t}\n\t}\n\t#elif defined(MOD_ARGB)',
        '\t\t\t}\n\t\t}\n\t}\n\t#elif defined(MOD_ARGB)','transparent pixel end')
    path.write_text(text,encoding='utf-8',newline='\n')

def patch_common_font_command_pool(path):
    path=Path(path);text=path.read_text(encoding='utf-8')
    marker='PVZH1_HEAP_MATRIX_FONT_COMMAND_POOL'
    if marker in text:return
    text='#include <memory>\n'+_replace(text,'static RenderCommand gRenderCommandPool[POOL_SIZE];',
        'static std::unique_ptr<RenderCommand[]> gRenderCommandPool; // '+marker,'matrix font command backing')
    text=_replace(text,'\tif (!aFont->mFontData->mInitialized)\n\t\treturn;\n\n\taFont->Prepare();',
        '\tif (!aFont->mFontData->mInitialized)\n\t\treturn;\n'
        '\tif (!gRenderCommandPool) gRenderCommandPool = std::make_unique<RenderCommand[]>(POOL_SIZE);\n\n\taFont->Prepare();',
        'matrix font command allocation')
    path.write_text(text,encoding='utf-8',newline='\n')

def apply(source_root):
    root=Path(source_root)
    common=root/'PvzpLib/PvzpCommon.cpp'
    common_text=common.read_text(encoding='utf8')
    if 'PVZH1_MATRIX_TEXT_REPLAY' not in common_text:
        common_text='#include "graphics/TextReplay.h" // PVZH1_MATRIX_TEXT_REPLAY\n'+common_text
        start=common_text.index('void PvzpDrawStringMatrix(')
        left=common_text.index('{',start);right=left+1;depth=1
        while depth:
            if common_text[right]=='{':depth+=1
            elif common_text[right]=='}':depth-=1
            right+=1
        body=common_text[start:right]
        body=_replace(body,'\taFont->Prepare();','\taFont->Prepare();\n    if(Sexy::H1TextReplayTryDrawMatrix(aFont,g,theMatrix,aFinalString,theColor))return;\n    Sexy::H1TextReplayCapture matrixText(aFont,g,0,0,aFinalString,theColor,&theMatrix);','matrix text before glyph layout')
        body=body[:-1]+'    matrixText.Commit();\n}'
        common_text=common_text[:start]+body+common_text[right:]
        common.write_text(common_text,encoding='utf8',newline='\n')
    framework=next((p for p in (root/'src/SexyAppFramework',root/'SexyAppFramework',root)
                    if (p/'graphics/MemoryImage.cpp').is_file()),None)
    if framework is None:raise RuntimeError(f'No SexyAppFramework under {root}')
    patch_memory(framework/'graphics/MemoryImage.cpp')
    canvas_path=framework/'graphics/MemoryImage.cpp'
    canvas_text=canvas_path.read_text(encoding='utf8')
    if 'PVZH1_DIRTY_CANVAS_RAW_ACCESS' not in canvas_text:
        canvas_text='#include "graphics/DirtyCanvas.h" // PVZH1_DIRTY_CANVAS_RAW_ACCESS\n'+canvas_text
        canvas_text=_replace(canvas_text,'uint32_t* MemoryImage::GetBits()\n{',
            'uint32_t* MemoryImage::GetBits()\n{\n    h1DirtyCanvas.Raw(this);','retained canvas raw access')
        canvas_path.write_text(canvas_text,encoding='utf8',newline='\n')
    patch_memory_header(framework/'graphics/MemoryImage.h')
    patch_memory_raster(framework/'graphics/MemoryImage.cpp')
    patch_memory_pressure(framework/'graphics/MemoryImage.cpp')
    patch_alpha_palette(framework/'graphics/MemoryImage.cpp')
    patch_font_glyph_pages(framework/'graphics/MemoryImage.cpp')
    patch_graphics_scratch(framework/'graphics/Graphics.cpp')
    patch_font_command_pool(framework/'graphics/ImageFont.cpp')
    patch_common_font_command_pool(framework.parent/'PvzpLib/PvzpCommon.cpp')
    font_path=framework/'graphics/ImageFont.cpp'
    font_text=font_path.read_text(encoding='utf8')
    if 'PVZH1_EXACT_TEXT_REPLAY' not in font_text:
        font_text='#include "graphics/TextReplay.h" // PVZH1_EXACT_TEXT_REPLAY\n'+font_text
        font_text=_replace(font_text,'ImageFont::~ImageFont()\n{','ImageFont::~ImageFont()\n{\n    H1TextReplayForget(this);','text lifetime')
        font_text=_replace(font_text,'void ImageFont::GenerateActiveFontLayers()\n{','void ImageFont::GenerateActiveFontLayers()\n{\n    H1TextReplayForget(this);','text generation')
        font_text=_replace(font_text,'\tPrepare();\n\n\tbool colorizeImages = g->GetColorizeImages();',
            '\tPrepare();\n    const bool textReplayAllowed=theDrawnAreas==nullptr&&theWidth==nullptr;\n'
            '    if(textReplayAllowed&&H1TextReplayTryDraw(this,g,theX,theY,theString,theColor))return;\n\n\tbool colorizeImages = g->GetColorizeImages();','text replay before layout')
        font_text=_replace(font_text,'\tColor anOrigColor = g->GetColor();',
            '\tColor anOrigColor = g->GetColor();\n    H1TextReplayCapture textCapture(textReplayAllowed?this:nullptr,g,theX,theY,theString,theColor);','text operation capture')
        font_text=_replace(font_text,'\tg->SetColorizeImages(colorizeImages);\n}\n\nvoid ImageFont::DrawString(',
            '\tg->SetColorizeImages(colorizeImages);\n    textCapture.Commit();\n}\n\nvoid ImageFont::DrawString(','text capture commit')
        font_path.write_text(font_text,encoding='utf8',newline='\n')
    font_text=font_path.read_text(encoding='utf8')
    if 'PVZH1_FONT_METRIC_CACHE' not in font_text:
        signature='int ImageFont::CharWidthKern(char32_t theChar, char32_t thePrevChar)'
        start=font_text.index(signature);left=font_text.index('{',start);end=left+1;depth=1
        while depth:
            if font_text[end]=='{':depth+=1
            elif font_text[end]=='}':depth-=1
            end+=1
        body=font_text[start:end]
        body=_replace(body,'\tPrepare();','\tPrepare();\n    // PVZH1_FONT_METRIC_CACHE: invalidated with active font layers.\n'
            '    const char32_t metricChar=theChar,metricPrevious=thePrevChar;\n    int cachedWidth=0;\n'
            '    if(H1FontMetricLookup(this,metricChar,metricPrevious,cachedWidth))return cachedWidth;','font metric lookup')
        body=_replace(body,'\treturn aMaxXPos;','    H1FontMetricStore(this,metricChar,metricPrevious,aMaxXPos);\n\treturn aMaxXPos;','font metric store')
        font_path.write_text(font_text[:start]+body+font_text[end:],encoding='utf8',newline='\n')
    for name in ('SWTri_Pixel8888.inc','SWTri_Pixel888.inc'):
        patch_triangle_zero_alpha(framework/'graphics/SWTri'/name)
    patch_app(framework/'SexyAppBase.cpp')
    patch_app_pressure(framework/'SexyAppBase.cpp')
    patch_resource_alpha(framework/'misc/ResourceManager.cpp')
    pvzp=framework.parent/'PvzpLib/PvzpCommon.cpp'
    patch_sanding(pvzp)
    patch_storage_sanding(pvzp)
    patch_filters(framework.parent/'PvzpLib/FilterEffect.cpp')
    page_patch=Path(__file__).with_name('data_array_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_paged_data_array',page_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply_to_header(framework.parent/'PvzpLib/DataArray.h',32)
    font_patch=Path(__file__).with_name('font_parser_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_font_parser',font_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)
    board_path=framework.parent/'Lawn/Board.cpp'
    board_text=board_path.read_text(encoding='utf8')
    if 'PVZH1_RETAINED_STATIC_SOD' not in board_text:
        board_text='#include "graphics/DirtyCanvas.h" // PVZH1_RETAINED_STATIC_SOD\n'+board_text
        board_text=_replace(board_text,'void Board::DrawBackdrop(Graphics* g)\n{',
            'void Board::DrawBackdrop(Graphics* g)\n{\n'
            '    const uint64_t canvasDomain[4]={(uint64_t)(uintptr_t)this,((uint64_t)(uint32_t)mLevel<<32)|(uint32_t)mBackground,\n'
            '        ((uint64_t)(uint32_t)mSodPosition<<32)|(uint32_t)mApp->mGameScene,(uint32_t)mApp->mGameMode};\n'
            '    h1DirtyCanvas.Domain(canvasDomain,sizeof(canvasDomain));','static backdrop domain')
        lines=board_text.splitlines(keepends=True);count=0
        for i,line in enumerate(lines):
            if 'g->DrawImage(Sexy::IMAGE_SOD' in line and 'aClipG.' not in line:
                lines[i]=line[:len(line)-len(line.lstrip())]+'{ H1CanvasStaticLayer staticSod; '+line.strip()+' }\n';count+=1
        if count!=4:raise RuntimeError(f'Expected four original sod blits, got {count}')
        board_path.write_text(''.join(lines),encoding='utf8',newline='\n')
    widget_patch=Path(__file__).with_name('widget_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_widget_pixel_epoch',widget_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)
    definition_patch=Path(__file__).with_name('definition_compaction_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_definition_compaction',definition_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)
    allocator_patch=Path(__file__).with_name('allocator_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_allocator_cache',allocator_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)
    pool_patch=Path(__file__).with_name('pool_epoch_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_pool_pixel_epochs',pool_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)
    selector_patch=Path(__file__).with_name('selector_definition_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_selector_definition',selector_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)
    texture_patch=Path(__file__).with_name('texture_page_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_texture_page_view',texture_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)
    reduced_patch=Path(__file__).with_name('reduced_texture_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_reduced_texture_cells',reduced_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)
    alpha_patch=Path(__file__).with_name('alpha_storage_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_alpha_storage',alpha_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)

    additive_patch=Path(__file__).with_name('additive_patches.py')
    spec=importlib.util.spec_from_file_location('pvzh1_direct_additive',additive_patch)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    module.apply(framework.parent)
