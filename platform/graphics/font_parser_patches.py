"""Retire loaded font parser trees without changing GetDefine or font metrics.

The pinned engine never parses another command after successful FontData::Load.
ImageFont::GetDefine remains supported through exactly the original textual
DataElementToString output, retained as a SingleDataElement under the same key.
All compiled glyph/layer/kerning/image data owns its storage independently.
"""
from pathlib import Path

MARKER='PVZH1_COMPACT_FONT_PARSER'

def apply(source_root):
    path=Path(source_root)/'SexyAppFramework/graphics/ImageFont.cpp'
    text=path.read_text(encoding='utf8')
    if MARKER in text:return
    signature='bool FontData::Load(SexyAppBase* theSexyApp, const std::string& theFontDescFileName)'
    if text.count(signature)!=1:raise RuntimeError('Pinned FontData::Load signature changed')
    helper=r'''// PVZH1_COMPACT_FONT_PARSER: the descriptor is compiled; retain its public
// textual define API while discarding recursively allocated parser-only nodes.
void H1CompactFontParserData(Sexy::FontData& data)
{
    for (auto& entry : data.mDefineMap)
    {
        if (!entry.second || !entry.second->mIsList)
            continue; // Existing scalar output already owns precisely this string.
        auto scalar = std::make_unique<Sexy::SingleDataElement>();
        scalar->mString = data.DataElementToString(entry.second.get());
        // Conversion completes before the tree is destroyed. No font-layer
        // fields borrow this tree; their strings/rects/maps are owned values.
        entry.second = std::move(scalar);
    }
    // clear() retains the largest parsed command's capacity. Parsing is over.
    std::string().swap(data.mCurrentLine);
}

'''
    text=text.replace(signature,helper+signature,1)
    old='mInitialized = LoadDescriptor(theFontDescFileName); ;'
    if text.count(old)!=1:raise RuntimeError('Pinned FontData successful-load context changed')
    text=text.replace(old,'''mInitialized = LoadDescriptor(theFontDescFileName);
#ifdef H1_PVZ_PORTABLE
    if (mInitialized)
        H1CompactFontParserData(*this);
#endif''',1)
    path.write_text(text,encoding='utf8',newline='\n')
