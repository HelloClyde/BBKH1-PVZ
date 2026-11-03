// Offline CJK metrics shared by font faces; no font rasterizer on the device.
#include "misc/Buffer.h"
#include <limits>
#include <cstring>
#include <mutex>
namespace Sexy {
struct H1CJKFontTable {
    struct Glyph { uint32_t code,width,x,y,w,h; };
    std::vector<Glyph> glyphs;
};
}
namespace {
std::shared_ptr<Sexy::H1CJKFontTable> H1LoadCJKTable(Sexy::SexyAppBase* app,const std::string& path) {
    static std::map<std::string,std::weak_ptr<Sexy::H1CJKFontTable>> tables;
    static std::mutex mutex;std::scoped_lock lock(mutex);
    auto& cached=tables[path];if(auto value=cached.lock())return value;
    Sexy::Buffer buffer;if(!app->ReadBufferFromFile(Sexy::GetResourcePath(path),&buffer))return {};
    const auto* bytes=static_cast<const uint8_t*>(buffer.GetDataPtr());
    const auto size=buffer.GetDataLen();if(size<8||std::memcmp(bytes,"H1F1",4))return {};
    auto u32=[](const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;};
    const auto count=u32(bytes+4);if(!count||count>65536||size!=8+count*24)return {};
    auto table=std::make_shared<Sexy::H1CJKFontTable>();table->glyphs.reserve(count);
    uint32_t previous=0;
    for(uint32_t i=0;i<count;++i){
        const auto* p=bytes+8+i*24;Sexy::H1CJKFontTable::Glyph glyph{u32(p),u32(p+4),u32(p+8),u32(p+12),u32(p+16),u32(p+20)};
        if(glyph.code<=previous||glyph.code>0x10ffff||!glyph.width||glyph.width>8192||!glyph.w||!glyph.h||glyph.x>65535||glyph.y>65535||glyph.w>8192||glyph.h>8192)return {};
        previous=glyph.code;table->glyphs.push_back(glyph);
    }
    cached=table;return table;
}
bool H1CJKLookup(Sexy::FontLayer& layer,char32_t code,Sexy::CharData& output) {
    if(!layer.mH1CJKFont)return false;
    const auto& glyphs=layer.mH1CJKFont->glyphs;
    auto it=std::lower_bound(glyphs.begin(),glyphs.end(),uint32_t(code),[](const auto& g,uint32_t value){return g.code<value;});
    if(it==glyphs.end()||it->code!=uint32_t(code))return false;
    output.mWidth=it->width;output.mImageRect=Sexy::Rect(it->x,it->y,it->w,it->h);output.mOffset=Sexy::Point(0,layer.mH1CJKOffsetY);return true;
}
void H1CJKPopulateForScaledFont(Sexy::FontLayer& layer) {
    if(layer.mH1CJKFont)for(const auto& glyph:layer.mH1CJKFont->glyphs)layer.GetCharData(glyph.code);
}
}
