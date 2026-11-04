// Compiled original font descriptors. Included in ImageFont.cpp after CompactFont.h.
// All integers are little endian; no pointers, native layouts or parser ASTs on disk.
#include <zlib.h>
#include <cstdio>
#include <cstdlib>
#ifdef PVZ_HOST_RUNNER
#include <filesystem>
#endif
extern "C" { volatile uint32_t pvzh1_font_cache_diagnostic[8]; }
namespace {
struct H1FontBytes {
    std::vector<uint8_t> b;size_t at=0;bool ok=true;
    void put(uint32_t n){for(int i=0;i<4;i++)b.push_back(uint8_t(n>>(8*i)));}
    uint32_t get(){if(at+4>b.size()){ok=false;return 0;}uint32_t n=0;for(int i=0;i<4;i++)n|=uint32_t(b[at++])<<(8*i);return n;}
    void putstr(const std::string& s){put(uint32_t(s.size()));b.insert(b.end(),s.begin(),s.end());}
    std::string str(){uint32_t n=get();if(n>1048576||at+n>b.size()){ok=false;return {};}std::string s((const char*)b.data()+at,n);at+=n;return s;}
    uint32_t count(uint32_t max=65536){uint32_t n=get();if(n>max){ok=false;return 0;}return n;}
};
void H1FontWriteTags(H1FontBytes& w,const std::vector<std::string>& v){w.put(v.size());for(const auto& s:v)w.putstr(s);}
void H1FontReadTags(H1FontBytes& r,std::vector<std::string>& v){auto n=r.count(1024);for(uint32_t i=0;i<n&&r.ok;i++)v.push_back(r.str());}
void H1FontWriteMap(H1FontBytes& w,const std::map<std::string,std::string>& v){w.put(v.size());for(const auto& e:v){w.putstr(e.first);w.putstr(e.second);}}
void H1FontReadMap(H1FontBytes& r,std::map<std::string,std::string>& v){auto n=r.count(8192);for(uint32_t i=0;i<n&&r.ok;i++){auto k=r.str();auto value=r.str();v.emplace(std::move(k),std::move(value));}}
H1FontBytes H1FontSerialize(Sexy::FontData& f){
    H1FontBytes w;w.put(f.mDefaultPointSize);w.put(f.mCharMap.size());
    for(const auto& e:f.mCharMap){w.put(e.first);w.put(e.second);}
    w.put(f.mDefineMap.size());for(const auto& e:f.mDefineMap){w.putstr(e.first);w.putstr(f.DataElementToString(e.second.get()));}
    w.put(f.mFontLayerList.size());for(const auto& l:f.mFontLayerList){
        std::string key;for(const auto& entry:f.mFontLayerMap)if(entry.second==&l){key=entry.first;break;}
        w.putstr(key);w.putstr(l.mLayerName);H1FontWriteMap(w,l.mExtendedInfo);H1FontWriteTags(w,l.mRequiredTags);H1FontWriteTags(w,l.mExcludedTags);
        w.putstr(l.mH1ImagePath);w.putstr(l.mH1CJKPath);w.put(l.mH1CJKOffsetY);
        for(int value:{l.mColorMult.mRed,l.mColorMult.mGreen,l.mColorMult.mBlue,l.mColorMult.mAlpha,l.mColorAdd.mRed,l.mColorAdd.mGreen,l.mColorAdd.mBlue,l.mColorAdd.mAlpha,
            l.mDrawMode,l.mOffset.mX,l.mOffset.mY,l.mSpacing,l.mMinPointSize,l.mMaxPointSize,l.mPointSize,l.mAscent,l.mAscentPadding,l.mHeight,l.mDefaultHeight,l.mLineSpacingOffset,l.mBaseOrder,int(l.mUseAlphaCorrection)})w.put(value);
        w.put(l.mCharDataMap.size());for(const auto& e:l.mCharDataMap){const auto& c=e.second;w.put(e.first);
            for(int value:{c.mImageRect.mX,c.mImageRect.mY,c.mImageRect.mWidth,c.mImageRect.mHeight,c.mOffset.mX,c.mOffset.mY,c.mWidth,c.mOrder})w.put(value);
            w.put(c.mKerningOffsets.size());for(const auto& k:c.mKerningOffsets){w.put(k.first);w.put(k.second);}
        }
    }return w;
}
bool H1FontDecode(H1FontBytes& r,Sexy::FontData& f){
    f.mDefaultPointSize=r.get();auto n=r.count();for(uint32_t i=0;i<n&&r.ok;i++){auto a=r.get();auto b=r.get();f.mCharMap.emplace(a,b);}
    n=r.count(8192);for(uint32_t i=0;i<n&&r.ok;i++){auto key=r.str();auto value=r.str();f.mDefineMap.emplace(std::move(key),std::make_unique<Sexy::SingleDataElement>(std::move(value)));}
    n=r.count(128);if(!n)return false;for(uint32_t i=0;i<n&&r.ok;i++){
        f.mFontLayerList.emplace_back(&f);auto& l=f.mFontLayerList.back();auto key=r.str();l.mLayerName=r.str();H1FontReadMap(r,l.mExtendedInfo);H1FontReadTags(r,l.mRequiredTags);H1FontReadTags(r,l.mExcludedTags);
        l.mH1ImagePath=r.str();l.mH1CJKPath=r.str();l.mH1CJKOffsetY=r.get();
        int* fields[]={&l.mColorMult.mRed,&l.mColorMult.mGreen,&l.mColorMult.mBlue,&l.mColorMult.mAlpha,&l.mColorAdd.mRed,&l.mColorAdd.mGreen,&l.mColorAdd.mBlue,&l.mColorAdd.mAlpha,
            &l.mDrawMode,&l.mOffset.mX,&l.mOffset.mY,&l.mSpacing,&l.mMinPointSize,&l.mMaxPointSize,&l.mPointSize,&l.mAscent,&l.mAscentPadding,&l.mHeight,&l.mDefaultHeight,&l.mLineSpacingOffset,&l.mBaseOrder};
        for(auto p:fields)*p=int32_t(r.get());l.mUseAlphaCorrection=r.get()!=0;
        auto chars=r.count();for(uint32_t j=0;j<chars&&r.ok;j++){auto code=r.get();Sexy::CharData c;
            int* cf[]={&c.mImageRect.mX,&c.mImageRect.mY,&c.mImageRect.mWidth,&c.mImageRect.mHeight,&c.mOffset.mX,&c.mOffset.mY,&c.mWidth,&c.mOrder};for(auto p:cf)*p=int32_t(r.get());
            auto kern=r.count();for(uint32_t k=0;k<kern&&r.ok;k++){auto ch=r.get();auto value=r.get();c.mKerningOffsets.emplace(ch,int32_t(value));}l.mCharDataMap.emplace(code,std::move(c));}
        if(!r.ok||l.mH1ImagePath.empty())return false;
        bool isNew=false;l.mImage=f.mApp->GetSharedImage(l.mH1ImagePath,"",&isNew);if((Sexy::Image*)l.mImage==nullptr)return false;if(isNew)l.mImage->Palletize();
        if(!l.mH1CJKPath.empty()){
            l.mH1CJKFont=H1LoadCJKTable(f.mApp,l.mH1CJKPath);if(!l.mH1CJKFont)return false;
            for(const auto& g:l.mH1CJKFont->glyphs)if(g.x+g.w>uint32_t(l.mImage->GetWidth())||g.y+g.h>uint32_t(l.mImage->GetHeight()))return false;
        }
        if(!f.mFontLayerMap.emplace(key,&l).second)return false;
    }return r.ok&&r.at==r.b.size();
}
bool H1FontCacheLoad(Sexy::FontData& target){
#ifdef PVZ_HOST_RUNNER
    if(std::getenv("PVZ_H1_EXPORT_FONTS"))return false;
#endif
    auto path=Sexy::GetResourcePath(Sexy::StringToLower(target.mSourceFile+".h1fc"));FILE* file=fopen(path.c_str(),"rb");if(!file)return false;
    uint8_t header[16];bool valid=fread(header,1,16,file)==16&&!memcmp(header,"H1FC",4);
    auto u32=[](const uint8_t* p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;};
    uint32_t size=valid?u32(header+8):0;valid=valid&&u32(header+4)==1&&size&&size<=2097152;
    H1FontBytes r;if(valid){r.b.resize(size);valid=fread(r.b.data(),1,size,file)==size&&fgetc(file)==EOF&&crc32(0,r.b.data(),size)==u32(header+12);}fclose(file);
    if(!valid){++pvzh1_font_cache_diagnostic[2];return false;}
    Sexy::FontData decoded;decoded.mApp=target.mApp;if(!H1FontDecode(r,decoded)){++pvzh1_font_cache_diagnostic[2];return false;}
    target.mDefaultPointSize=decoded.mDefaultPointSize;target.mCharMap.swap(decoded.mCharMap);target.mDefineMap.swap(decoded.mDefineMap);
    target.mFontLayerList.swap(decoded.mFontLayerList);target.mFontLayerMap.swap(decoded.mFontLayerMap);for(auto& l:target.mFontLayerList)l.mFontData=&target;
    target.mInitialized=true;++pvzh1_font_cache_diagnostic[0];pvzh1_font_cache_diagnostic[1]+=size;return true;
}
void H1FontCacheExport(Sexy::FontData& f){
#ifdef PVZ_HOST_RUNNER
    const char* directory=std::getenv("PVZ_H1_EXPORT_FONTS");if(!directory)return;
    auto w=H1FontSerialize(f);H1FontBytes r;r.b=w.b;Sexy::FontData decoded;decoded.mApp=f.mApp;
    if(!H1FontDecode(r,decoded)||H1FontSerialize(decoded).b!=w.b){fprintf(stderr,"H1FC roundtrip failed: %s\n",f.mSourceFile.c_str());std::abort();}
    auto relative=f.mSourceFile;while(relative.size()&&relative[0]=='/')relative.erase(0,1);if(relative.rfind("PVZ/",0)==0)relative.erase(0,4);
    relative=Sexy::StringToLower(relative);
    std::filesystem::path path=std::filesystem::path(directory)/(relative+".h1fc");std::filesystem::create_directories(path.parent_path());
    FILE* file=fopen(path.string().c_str(),"wb");if(!file)std::abort();H1FontBytes header;header.b={'H','1','F','C'};header.put(1);header.put(w.b.size());header.put(crc32(0,w.b.data(),w.b.size()));
    if(fwrite(header.b.data(),1,16,file)!=16||fwrite(w.b.data(),1,w.b.size(),file)!=w.b.size())std::abort();fclose(file);printf("H1FC_EXPORT_OK %s %zu\n",relative.c_str(),w.b.size());
#endif
}
}
