/* Read-only semantic probe around actual original FontData and ImageFont.
 * This translation unit is host-only; it replaces no game or parser logic. */
#include "LawnApp.h"
#include "graphics/ImageFont.h"
#include "graphics/MemoryImage.h"
#include "graphics/Graphics.h"
#include "misc/ResourceManager.h"
#include "h1_host.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <stdexcept>
#include <cstring>

void H1CompactFontParserData(Sexy::FontData&);
using namespace Sexy;
namespace {
struct Count {
    uint64_t nodes=0,singles=0,lists=0,vector_slots=0,string_bytes=0,string_heap_bytes=0;
    uint64_t native_payload=0,native_blocks=0,host_payload=0,defines=0;
    void Allocation(uint64_t native,uint64_t host){native_payload+=native;host_payload+=host;++native_blocks;}
    void String(const std::string& text){
        string_bytes+=text.size();
        if(text.capacity()>15){string_heap_bytes+=text.capacity()+1;Allocation(text.capacity()+1,text.capacity()+1);}
    }
    void Node(const DataElement *element){
        if(!element)return;++nodes;
        if(element->mIsList){
            ++lists;Allocation(20,sizeof(ListDataElement));
            const auto& children=static_cast<const ListDataElement*>(element)->mElementVector;
            vector_slots+=children.capacity();
            if(children.capacity())Allocation(children.capacity()*4,children.capacity()*sizeof(void*));
            for(auto& child:children)Node(child.get());
        }else{
            ++singles;Allocation(32,sizeof(SingleDataElement));
            String(static_cast<const SingleDataElement*>(element)->mString);
        }
    }
    void Font(const FontData& data){
        for(auto& entry:data.mDefineMap){
            ++defines;Allocation(44,sizeof(std::_Rb_tree_node<DataElementMap::value_type>));
            String(entry.first);Node(entry.second.get());
        }
        String(data.mCurrentLine);
    }
    uint64_t NativeRawEstimate()const{return native_payload+native_blocks*31;}
    Count& operator+=(const Count& b){
        nodes+=b.nodes;singles+=b.singles;lists+=b.lists;vector_slots+=b.vector_slots;
        string_bytes+=b.string_bytes;string_heap_bytes+=b.string_heap_bytes;
        native_payload+=b.native_payload;native_blocks+=b.native_blocks;host_payload+=b.host_payload;defines+=b.defines;return *this;
    }
};
void WriteCount(std::ostream& out,const Count& c){
    out<<"{\"nodes\":"<<c.nodes<<",\"single_nodes\":"<<c.singles<<",\"list_nodes\":"<<c.lists
       <<",\"defines\":"<<c.defines<<",\"vector_capacity_slots\":"<<c.vector_slots
       <<",\"string_length_bytes\":"<<c.string_bytes<<",\"string_heap_capacity_bytes\":"<<c.string_heap_bytes
       <<",\"host_requested_payload_bytes\":"<<c.host_payload<<",\"native_requested_payload_bytes\":"<<c.native_payload
       <<",\"native_allocations\":"<<c.native_blocks<<",\"native_raw_request_estimate\":"<<c.NativeRawEstimate()<<'}';
}
struct Hash {
    uint64_t value=1469598103934665603ull;
    void Bytes(const void *p,size_t n){const unsigned char *s=(const unsigned char*)p;while(n--){value^=*s++;value*=1099511628211ull;}}
    template<class T>void Add(const T& x){Bytes(&x,sizeof(x));}
    void Text(const std::string& x){uint64_t n=x.size();Add(n);Bytes(x.data(),x.size());}
};
void RectHash(Hash& h,const Rect& r){h.Add(r.mX);h.Add(r.mY);h.Add(r.mWidth);h.Add(r.mHeight);}
void ColorHash(Hash&h,const Color&c){h.Add(c.mRed);h.Add(c.mGreen);h.Add(c.mBlue);h.Add(c.mAlpha);}
uint64_t CompiledHash(const FontData& data){
    Hash h;h.Add(data.mDefaultPointSize);
    for(auto& p:data.mCharMap){h.Add(p.first);h.Add(p.second);}
    for(auto& l:data.mFontLayerList){
        h.Text(l.mLayerName);for(auto& p:l.mExtendedInfo){h.Text(p.first);h.Text(p.second);}
        for(auto& p:l.mRequiredTags)h.Text(p);for(auto& p:l.mExcludedTags)h.Text(p);
        ColorHash(h,l.mColorMult);ColorHash(h,l.mColorAdd);h.Add(l.mDrawMode);h.Add(l.mOffset.mX);h.Add(l.mOffset.mY);
        h.Add(l.mSpacing);h.Add(l.mMinPointSize);h.Add(l.mMaxPointSize);h.Add(l.mPointSize);h.Add(l.mAscent);
        h.Add(l.mAscentPadding);h.Add(l.mHeight);h.Add(l.mDefaultHeight);h.Add(l.mLineSpacingOffset);h.Add(l.mBaseOrder);h.Add(l.mUseAlphaCorrection);
        Image *image=const_cast<SharedImageRef&>(l.mImage);
        if(image){h.Add(image->mWidth);h.Add(image->mHeight);h.Add(image->mNumRows);h.Add(image->mNumCols);h.Text(image->mFilePath);}
        for(auto& p:l.mCharDataMap){
            h.Add(p.first);const auto& c=p.second;RectHash(h,c.mImageRect);h.Add(c.mOffset.mX);h.Add(c.mOffset.mY);h.Add(c.mWidth);h.Add(c.mOrder);
            for(auto& k:c.mKerningOffsets){h.Add(k.first);h.Add(k.second);}
        }
    }
    for(auto& l:data.mFontLayerMap)h.Text(l.first);
    return h.value;
}
uint64_t Render(ImageFont& font,double scale,int pointSize){
    font.SetPointSize(pointSize);font.SetScale(scale);font.Prepare();
    MemoryImage surface(gLawnApp);surface.Create(800,180);
    Graphics graphics(&surface);graphics.SetColor(Color(24,35,46));graphics.FillRect(0,0,800,180);
    const std::string text="AV WA To fi 0123456789 !? Plants vs Zombies";
    int width=0;font.DrawStringEx(&graphics,12,110,text,Color(225,241,199),nullptr,&width);
    Hash h;h.Add(width);h.Bytes(surface.GetBits(),800*180*4);return h.value;
}
std::map<std::string,std::string> Defines(ImageFont& font){
    std::map<std::string,std::string> values;
    for(auto& p:font.mFontData->mDefineMap)values.emplace(p.first,font.GetDefine(p.first));
    return values;
}
}

extern "C" void pvzh1_host_probe_fonts(){
    static bool ran=false;if(ran||!gLawnApp||!gLawnApp->mResourceManager)return;ran=true;
    const char *setting=getenv("PVZ_HOST_FONT_PROBE");if(!setting||setting[0]!='1')return;
    const char *directory=getenv("PVZ_HOST_OUTPUT");
    std::ofstream output(std::filesystem::path(directory?directory:".")/"font-parser-verification.json");
    Count totalBefore,totalAfter;unsigned tested=0;bool comma=false;
    output<<"{\"scope\":\"Host probe of original FontData::LoadDescriptor and ImageFont renderer, not a replacement parser\","
        "\"native_estimate_note\":\"Exact MIPS layouts Single32/List20/mapnode44/pointer4/SSO15; each native allocation adds31 raw-request bytes; firmware heap rounding/metadata excluded. Host string/vector capacities are measured.\",\"fonts\":[";
    for(auto& entry:gLawnApp->mResourceManager->mFontMap){
        auto *resource=static_cast<ResourceManager::FontRes*>(entry.second.get());
        auto *loaded=dynamic_cast<ImageFont*>(resource->mFont.get());
        if(!loaded||!loaded->mFontData||!loaded->mFontData->mInitialized)continue;
        auto data=std::make_unique<FontData>();data->mApp=gLawnApp;data->mCurrentLine="";
        data->mSourceFile=loaded->mFontData->mSourceFile;
        data->mFontErrorHeader="Font Descriptor Error in "+data->mSourceFile+"\r\n";
        data->mInitialized=data->LoadDescriptor(data->mSourceFile);
        if(!data->mInitialized)throw std::runtime_error("Original font descriptor failed in probe: "+data->mSourceFile);
        ImageFont probe(*loaded);probe.mActiveLayerList.clear();probe.mFontData->DeRef();
        probe.mFontData=data.release();probe.mFontData->Ref();probe.mActiveListValid=false;probe.Prepare();
        Count before;before.Font(*probe.mFontData);
        auto defineBefore=Defines(probe);
        const int pointSize=loaded->mPointSize;
        const uint64_t renderBefore=Render(probe,1.0,pointSize),scaledBefore=Render(probe,0.75,pointSize);
        const uint64_t resizedBefore=Render(probe,1.0,std::max(1,pointSize+3));
        const uint64_t compiledBefore=CompiledHash(*probe.mFontData);
        H1CompactFontParserData(*probe.mFontData);
        Count after;after.Font(*probe.mFontData);
        auto defineAfter=Defines(probe);const uint64_t compiledAfter=CompiledHash(*probe.mFontData);
        const uint64_t renderAfter=Render(probe,1.0,pointSize),scaledAfter=Render(probe,0.75,pointSize);
        const uint64_t resizedAfter=Render(probe,1.0,std::max(1,pointSize+3));
        if(defineBefore!=defineAfter||compiledBefore!=compiledAfter||renderBefore!=renderAfter||scaledBefore!=scaledAfter||resizedBefore!=resizedAfter)
            throw std::runtime_error("Font compaction changed original defines/compiled fields/render: "+resource->mPath);
        Hash defineHash;for(auto& p:defineBefore){defineHash.Text(p.first);defineHash.Text(p.second);}
        if(comma)output<<',';comma=true;
        output<<"{\"id\":"<<std::quoted(entry.first)<<",\"source\":"<<std::quoted(probe.mFontData->mSourceFile)<<",\"before\":";
        WriteCount(output,before);output<<",\"after\":";WriteCount(output,after);
        output<<",\"all_define_bytes_equal\":true,\"define_hash\":"<<defineHash.value
              <<",\"compiled_glyph_kerning_layer_hash\":"<<compiledBefore<<",\"compiled_hash_after\":"<<compiledAfter
              <<",\"render_hash\":"<<renderBefore<<",\"render_hash_after\":"<<renderAfter
              <<",\"scaled_render_hash\":"<<scaledBefore<<",\"scaled_render_hash_after\":"<<scaledAfter
              <<",\"changed_point_size_render_hash\":"<<resizedBefore<<",\"changed_point_size_render_hash_after\":"<<resizedAfter<<'}';
        totalBefore+=before;totalAfter+=after;++tested;
    }
    if(tested<17)throw std::runtime_error("Font probe did not exercise all original LoadingFonts");
    output<<"],\"font_count\":"<<tested<<",\"total_before\":";WriteCount(output,totalBefore);
    output<<",\"total_after\":";WriteCount(output,totalAfter);
    output<<",\"native_raw_request_estimated_saving\":"<<totalBefore.NativeRawEstimate()-totalAfter.NativeRawEstimate()
          <<",\"all_passed\":true}\n";output.flush();
    pvzp_log_line("HOST FONT PROBE: original define/glyph/kerning/layer and normal/scaled pixel hashes all match");
}
