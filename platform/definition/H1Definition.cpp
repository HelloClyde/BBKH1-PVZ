#include "TrustedResources.h"
/* Portable offline definitions. No native pointers or host struct layouts on disk.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "PvzpLib/Definition.h"
#include "PvzpLib/Trail.h"
#include "PvzpLib/Reanimator.h"
#include "SexyAppFramework/Common.h"
#include "zlib.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <map>
#include <cstddef>
#include <cerrno>
#ifdef PVZ_HOST_RUNNER
#include <filesystem>
#include <map>
#include <cstdlib>
#include "resources/h1_resources.h"
#endif
extern "C" { volatile uint32_t pvzh1_definition_diagnostic[8]; }
extern "C" {volatile uint32_t pvzh1_definition_open_diagnostic[4];char pvzh1_definition_open_path[192];}
extern "C" unsigned pvz_runtime_file_occupancy(void) __attribute__((weak));
extern "C" void pvzp_audio_service();
namespace {
constexpr uint32_t Limit=16*1024*1024;
static std::map<std::string,std::vector<unsigned char>> startupPack;
static bool startupPackAttempted;
static uint32_t ReadWord(const unsigned char* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
static void LoadStartupPack(){
    if(startupPackAttempted)return;startupPackAttempted=true;
    auto path=Sexy::GetResourcePath("particles/startup.h1dp");FILE* file=fopen(path.c_str(),"rb");if(!file)return;
    unsigned char header[16];bool ok=fread(header,1,16,file)==16&&!memcmp(header,"H1DP",4)&&ReadWord(header+4)==1;
    uint32_t count=ok?ReadWord(header+8):0,bytes=ok?ReadWord(header+12):0;ok=ok&&count&&count<=512&&bytes<=1048576;
    std::map<std::string,std::vector<unsigned char>> entries;uint32_t consumed=0;
    for(unsigned i=0;ok&&i<count;i++){
        unsigned char h[12];ok=fread(h,1,12,file)==12;if(!ok)break;consumed+=12;
        uint32_t length=ReadWord(h),size=ReadWord(h+4);
        ok=length&&length<=256&&size>=24&&size<=1048576&&uint64_t(consumed)+length+size<=bytes;if(!ok)break;
        std::string name(length,'\0');std::vector<unsigned char> data(size);
        ok=fread(name.data(),1,length,file)==length&&fread(data.data(),1,size,file)==size;
        consumed+=length+size;if(ok)ok=entries.emplace(std::move(name),std::move(data)).second;
    }
    ok=ok&&consumed==bytes&&fgetc(file)==EOF;fclose(file);
    if(ok){startupPack.swap(entries);pvzh1_definition_diagnostic[4]=count;pvzh1_definition_diagnostic[5]=bytes+16;}
    else ++pvzh1_definition_diagnostic[7];
}
const void* exactLoadedDefinition=nullptr;
const void* numericLoadedDefinition=nullptr;
#ifdef PVZ_HOST_RUNNER
bool fastTransformDecode=true;
#endif
#ifdef PVZ_HOST_RUNNER
uint32_t Hash(const DefMap* map,uint32_t h=2166136261u) {
    auto byte=[&](unsigned char b){h=(h^b)*16777619u;};
    for(auto f=map->mMapFields;*f->mFieldName;++f){
        for(auto p=f->mFieldName;*p;++p)byte(*p);byte(0);byte((unsigned)f->mFieldType);
        if(f->mFieldType==DefFieldType::DT_ARRAY)h=Hash((const DefMap*)f->mExtraData,h);
        if(f->mFieldType==DefFieldType::DT_ENUM||f->mFieldType==DefFieldType::DT_FLAGS)
            for(auto s=(const DefSymbol*)f->mExtraData;s->mSymbolName;++s){
                for(auto p=s->mSymbolName;*p;++p)byte(*p);byte(0);
                uint32_t v=s->mSymbolValue;for(int i=0;i<4;++i)byte(v>>(i*8));
            }
    }return h;
}
#endif
// Inflate into a small reusable page. The selector can be reloaded while game
// resources still occupy the heap; a second whole-definition buffer is wasteful.
struct Input {
    const unsigned char* source=nullptr;size_t sourceSize=0,limit=0,position=0,produced=0;
    const unsigned char* current=nullptr;size_t available=0;
    unsigned char page[4096];z_stream z{};bool compressed=false,initialized=false,ended=false,ok=true;
    explicit Input(const std::vector<unsigned char>& data){
        if(data.size()>=8&&!memcmp(data.data(),"H1DZ",4)){
            compressed=true;source=data.data()+8;sourceSize=data.size()-8;
            limit=data[4]|uint32_t(data[5])<<8|uint32_t(data[6])<<16|uint32_t(data[7])<<24;
        }else{source=data.data();sourceSize=limit=data.size();}
        if(limit<24||limit>Limit)ok=false;
        if(ok)reset();
    }
    ~Input(){if(initialized)inflateEnd(&z);}
    bool reset(){
        if(initialized)inflateEnd(&z);initialized=false;z={};
        position=produced=available=0;current=nullptr;ended=false;
        if(compressed){
            if(H1ResourceInflateInit(&z)!=Z_OK)return ok=false;
            initialized=true;z.next_in=const_cast<Bytef*>(source);z.avail_in=sourceSize;
        }return ok;
    }
    bool fill(){
        pvzp_audio_service(); // Foreground, outside filesystem locks; never recursive audio IO.
        size_t cap=std::min(sizeof(page),limit-produced),n=cap;
        if(!cap)return ok=false;
        if(compressed){
            z.next_out=page;z.avail_out=cap;int result=inflate(&z,Z_NO_FLUSH);
            if(result!=Z_OK&&result!=Z_STREAM_END)return ok=false;
            ended=result==Z_STREAM_END;n=cap-z.avail_out;current=page;
        }else current=source+produced;
        if(!n)return ok=false;
        produced+=n;available=n;return true;
    }
    bool read(void* out,size_t n){
        if(!ok||n>limit-position)return ok=false;
        auto dest=(unsigned char*)out;
        while(n){if(!available&&!fill())return false;size_t count=std::min(n,available);
            if(dest){memcpy(dest,current,count);dest+=count;}
            current+=count;available-=count;position+=count;n-=count;}
        return true;
    }
    bool finish(){
        if(!ok||position!=limit)return false;
        if(compressed&&!ended){
            unsigned char extra;z.next_out=&extra;z.avail_out=1;
            int result=inflate(&z,Z_FINISH);
            if(result!=Z_STREAM_END||z.avail_out!=1)return ok=false;ended=true;
        }
        return !compressed||(ended&&z.total_out==limit&&z.total_in==sourceSize);
    }
};
struct Reader {
    Input& input;bool ok=true,numeric=false;uint32_t budget=Limit;
    std::vector<std::string> strings;
    std::vector<Image*> images;std::vector<_Font*> fonts;
    std::vector<unsigned char> imageReady,fontReady;
    uint32_t word(){unsigned char p[4]{};raw(p,4);return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
    bool spend(uint64_t n){if(n>budget){ok=false;return false;}budget-=(uint32_t)n;return true;}
    bool raw(void* out,size_t n){if(!ok||!input.read(out,n))return ok=false;return true;}
    bool walk(const DefMap* map,void* object,bool apply,unsigned depth=0){
        if(depth>8)return ok=false;
        const bool transform=numeric&&map==&gReanimatorTransformDefMap;
        if(apply&&!transform){if(map->mConstructorFunc)map->mConstructorFunc(object);else DefinitionFillWithDefaults(map,object);}
        auto fields=map->mMapFields;
        if(transform){
            // H1D2 already contains resolved numeric fields in pinned map order.
            // Copy its eight floats together; retain the ordinary dictionary
            // and ownership handling for image/font/text below. No second
            // definition buffer or per-float map traversal is needed.
            static_assert(offsetof(ReanimatorTransform,mTransX)==0&&
                          offsetof(ReanimatorTransform,mAlpha)==28&&
                          offsetof(ReanimatorTransform,mImage)==32);
            if(!raw(apply?object:nullptr,32))return false;
            fields+=8;
        }
        for(auto f=fields;*f->mFieldName&&ok;++f){
            void* dst=apply?(unsigned char*)object+f->mFieldOffset:nullptr;
            switch(f->mFieldType){
            case DefFieldType::DT_ARRAY:{
                uint32_t n=word();auto sub=(const DefMap*)f->mExtraData;
                if(!spend(uint64_t(n)*sub->mDefSize)||n>1000000)return ok=false;
                unsigned char* data=nullptr;
                if(apply){auto a=(DefinitionArrayDef*)dst;a->mArrayCount=n;
                    a->mArrayData=data=n?(unsigned char*)DefinitionAlloc(n*sub->mDefSize):nullptr;}
                for(uint32_t i=0;i<n&&ok;++i)walk(sub,apply?data+i*sub->mDefSize:nullptr,apply,depth+1);
                break;}
            case DefFieldType::DT_TRACK_FLOAT:{
                static_assert(sizeof(FloatParameterTrackNode)==20);
                uint32_t n=word();if(n>1000000||!spend(uint64_t(n)*20))return ok=false;
                void* data=nullptr;if(apply){auto t=(FloatParameterTrack*)dst;t->mCountNodes=n;
                    t->mNodes=n?(FloatParameterTrackNode*)DefinitionAlloc(n*20):nullptr;data=t->mNodes;}
                raw(data,n*20);break;}
            case DefFieldType::DT_STRING:
            case DefFieldType::DT_IMAGE:
            case DefFieldType::DT_FONT:{
                uint32_t id=word();if(id>=strings.size())return ok=false;
                if(f->mFieldType==DefFieldType::DT_STRING){
                    if(!spend(strings[id].size()+1))return false;
                    if(apply){const char* text="";if(id){char* copy=(char*)DefinitionAlloc(strings[id].size()+1);
                        memcpy(copy,strings[id].c_str(),strings[id].size()+1);text=copy;}*(const char**)dst=text;}
                }else if(apply&&f->mFieldType==DefFieldType::DT_IMAGE){
                    if(!imageReady[id]){if(id&&!DefinitionLoadImage(&images[id],strings[id]))return ok=false;imageReady[id]=1;}
                    *(Image**)dst=images[id];
                }else if(apply){
                    if(!fontReady[id]){if(id&&!DefinitionLoadFont(&fonts[id],strings[id]))return ok=false;fontReady[id]=1;}
                    *(_Font**)dst=fonts[id];
                }break;}
            case DefFieldType::DT_VECTOR2:raw(dst,8);break;
            case DefFieldType::DT_INT:case DefFieldType::DT_FLOAT:
            case DefFieldType::DT_ENUM:case DefFieldType::DT_FLAGS:raw(dst,4);break;
            default:return ok=false;
            }
        }return ok;
    }
};
bool Decode(const std::vector<unsigned char>& bytes,const DefMap* map,void* object){
    if(bytes.size()<8||bytes.size()>Limit)return false;
    Input input(bytes);if(!input.ok)return false;
    Reader r{input};unsigned char magic[4]{};r.raw(magic,4);
    const bool numeric=!memcmp(magic,"H1D2",4);
    r.numeric=numeric;
#ifdef PVZ_HOST_RUNNER
    r.numeric=r.numeric&&fastTransformDecode;
#endif
    // The schema fingerprint, payload length and checksum were verified by
    // the resource builder. Consume their slots without rehashing the schema.
    if((!numeric&&memcmp(magic,"H1D1",4))||(numeric&&map!=&gReanimatorDefMap))return false;
    r.word();r.word();r.word();
    uint32_t count=r.word();if(!count||count>65536)return false;
    r.strings.reserve(count);
    for(uint32_t i=0;i<count&&r.ok;++i){uint32_t n=r.word();if(n>65536||n>input.limit-input.position)return false;
        r.strings.emplace_back(n,'\0');r.raw(r.strings.back().data(),n);
        if(r.strings.back().find('\0')!=std::string::npos)return false;}
    if(!r.ok||!r.strings[0].empty())return false;
    // Offline tools already checked the immutable dictionary and object graph.
    // Decode once directly into its final storage.
    r.budget=Limit;r.images.resize(count);r.fonts.resize(count);
    r.imageReady.resize(count);r.fontReady.resize(count);
    if(!r.walk(map,object,true)||!input.finish())return false;
    numericLoadedDefinition=numeric?object:nullptr;
    return true;
}
bool DecodeFile(const std::vector<unsigned char>& bytes,const DefMap* map,void* object){
    return Decode(bytes,map,object);
}
#ifdef PVZ_HOST_RUNNER
struct Writer {
    std::vector<unsigned char> data;
    std::vector<std::string> strings{std::string()};
    std::map<std::string,uint32_t> ids{{"",0}};
    void word(uint32_t n){for(int i=0;i<4;++i)data.push_back(n>>(i*8));}
    void raw(const void* p,size_t n){auto b=(const unsigned char*)p;if(n)data.insert(data.end(),b,b+n);}
    void text(const std::string& s){auto [it,inserted]=ids.emplace(s,strings.size());if(inserted)strings.push_back(s);word(it->second);}
    void walk(const DefMap* map,void* object){
        for(auto f=map->mMapFields;*f->mFieldName;++f){void* v=(unsigned char*)object+f->mFieldOffset;
            switch(f->mFieldType){
            case DefFieldType::DT_ARRAY:{auto a=(DefinitionArrayDef*)v;auto sub=(const DefMap*)f->mExtraData;word(a->mArrayCount);
                for(int i=0;i<a->mArrayCount;++i)walk(sub,(unsigned char*)a->mArrayData+i*sub->mDefSize);break;}
            case DefFieldType::DT_TRACK_FLOAT:{auto t=(FloatParameterTrack*)v;word(t->mCountNodes);raw(t->mNodes,t->mCountNodes*20);break;}
            case DefFieldType::DT_STRING:text(*(const char**)v?*(const char**)v:"");break;
            case DefFieldType::DT_IMAGE:{std::string s;if(*(Image**)v&&!PvzpFindImagePath(*(Image**)v,&s))throw std::runtime_error("Unresolved image");text(s);break;}
            case DefFieldType::DT_FONT:{std::string s;if(*(_Font**)v&&!PvzpFindFontPath(*(_Font**)v,&s))throw std::runtime_error("Unresolved font");text(s);break;}
            case DefFieldType::DT_VECTOR2:raw(v,8);break;
            default:raw(v,4);break;
            }
        }
    }
};
std::vector<unsigned char> Encode(const DefMap* map,void* object){
    Writer w;w.walk(map,object);Writer p;p.word(w.strings.size());
    for(auto& s:w.strings){p.word(s.size());p.raw(s.data(),s.size());}p.raw(w.data.data(),w.data.size());
    Writer file;file.raw(map==&gReanimatorDefMap?"H1D2":"H1D1",4);file.word(Hash(map));file.word(p.data.size());file.word(crc32(0,p.data.data(),p.data.size()));
    file.raw(p.data.data(),p.data.size());return file.data;
}
void FreeStrings(const DefMap* map,void* object){
    for(auto f=map->mMapFields;*f->mFieldName;++f){void* v=(unsigned char*)object+f->mFieldOffset;
        if(f->mFieldType==DefFieldType::DT_STRING){auto s=*(const char**)v;if(s&&*s)delete[] s;*(const char**)v="";}
        else if(f->mFieldType==DefFieldType::DT_ARRAY){auto a=(DefinitionArrayDef*)v;auto sub=(const DefMap*)f->mExtraData;
            for(int i=0;i<a->mArrayCount;++i)FreeStrings(sub,(unsigned char*)a->mArrayData+i*sub->mDefSize);}
    }
}
#endif
}
bool H1DefinitionLoad(const std::string& xml,const DefMap* map,void* object){
    exactLoadedDefinition=nullptr;
    numericLoadedDefinition=nullptr;
    LoadStartupPack();std::string key=xml;for(auto& c:key){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}
    auto packed=startupPack.find(key);
    if(packed!=startupPack.end()){
        if(!DecodeFile(packed->second,map,object)){++pvzh1_definition_diagnostic[3];throw std::runtime_error("Invalid packed H1 definition: "+xml);}
        exactLoadedDefinition=object;++pvzh1_definition_diagnostic[0];++pvzh1_definition_diagnostic[6];pvzh1_definition_diagnostic[1]+=packed->second.size();return true;
    }
    // The offline tree uses canonical lowercase paths, as does the packed
    // dictionary above. Lazy reloads must use the same spelling as preloads.
    std::string name=Sexy::GetResourcePath(key+".h1d");FILE* f=fopen(name.c_str(),"rb");
    if(!f){
        const unsigned error=errno;++pvzh1_definition_diagnostic[2];++pvzh1_definition_open_diagnostic[0];
        pvzh1_definition_open_diagnostic[1]=error;
        pvzh1_definition_open_diagnostic[2]=pvz_runtime_file_occupancy?pvz_runtime_file_occupancy():UINT32_MAX;
        pvzh1_definition_open_diagnostic[3]=name.size();
        std::strncpy(pvzh1_definition_open_path,name.c_str(),sizeof(pvzh1_definition_open_path)-1);
        return false;
    }
    bool ok=false;std::vector<unsigned char> data;
    if(!fseek(f,0,SEEK_END)){long n=ftell(f);if(n>=24&&n<=Limit&&!fseek(f,0,SEEK_SET)){
        data.resize(n);ok=fread(data.data(),1,n,f)==(size_t)n;}}
    fclose(f);if(ok)ok=DecodeFile(data,map,object);
    if(ok){exactLoadedDefinition=object;++pvzh1_definition_diagnostic[0];pvzh1_definition_diagnostic[1]+=data.size();}
    else {++pvzh1_definition_diagnostic[3];throw std::runtime_error("Invalid H1 definition: "+xml);}
    return ok;
}
bool H1DefinitionTakeExact(const void* object){
    bool exact=object==exactLoadedDefinition;exactLoadedDefinition=nullptr;return exact;
}
bool H1DefinitionNumericResolved(const void* object){return object==numericLoadedDefinition;}
#ifdef PVZ_HOST_RUNNER
void H1DefinitionFastTransformTestEnable(bool value){fastTransformDecode=value;}
extern "C" void pvzh1_export_definitions(){
    const char* output=getenv("PVZ_H1_EXPORT_DEFINITIONS");if(!output||!*output)return;
    namespace fs=std::filesystem;fs::path root=h1_resources_get_root();unsigned count=0;uint64_t bytes=0;
    for(const char* folder:{"reanim","particles","trails"}){
        if(!fs::exists(root/folder))continue;
        for(auto& entry:fs::recursive_directory_iterator(root/folder)){
            auto ext=entry.path().extension().string();const DefMap* map=nullptr;
            if(ext==".reanim")map=&gReanimatorDefMap;else if(ext==".xml")map=&gParticleDefMap;else if(ext==".trail")map=&gTrailDefMap;
            if(!map)continue;
            std::string relative=entry.path().lexically_relative(root).generic_string();
            // Several bundled particle XML files are unused editor examples and
            // refer to absent images. Export every real engine particle only.
            if(ext==".xml"){
                bool used=false;
                for(const auto& p:gLawnParticleArray){std::string name=p.mParticleFileName;
                    for(auto& c:name){if(c=='\\')c='/';if(c>='A'&&c<='Z')c+=32;}
                    if(name==relative){used=true;break;}}
                if(!used)continue;
            }
            fprintf(stderr,"H1D_EXPORT_BEGIN %s\n",relative.c_str());
            std::vector<unsigned char> a(map->mDefSize),b(map->mDefSize);
            if(!DefinitionCompileFile(relative,"",map,a.data()))throw std::runtime_error("Compile failed: "+relative);
            if(map==&gReanimatorDefMap){
                auto* definition=(ReanimatorDefinition*)a.data();
                for(int ti=0;ti<definition->mTracks.count;++ti){
                    float previous[8]={0,0,0,0,1,1,0,1};
                    auto& track=definition->mTracks.tracks[ti];
                    for(int fi=0;fi<track.mTransforms.count;++fi){auto& t=track.mTransforms.mTransforms[fi];
                        float* fields[]={&t.mTransX,&t.mTransY,&t.mSkewX,&t.mSkewY,&t.mScaleX,&t.mScaleY,&t.mFrame,&t.mAlpha};
                        for(int k=0;k<8;++k){if(*fields[k]==DEFAULT_FIELD_PLACEHOLDER)*fields[k]=previous[k];else previous[k]=*fields[k];}
                    }
                }
            }
            auto encoded=Encode(map,a.data());
            if(!Decode(encoded,map,b.data())||Encode(map,b.data())!=encoded)throw std::runtime_error("Roundtrip mismatch: "+relative);
            if(ReadWord(encoded.data()+12)!=crc32(0,encoded.data()+16,encoded.size()-16))
                throw std::runtime_error("Offline definition CRC mismatch");
            uLongf size=compressBound(encoded.size());std::vector<unsigned char> compressed(size);
            if(compress2(compressed.data(),&size,encoded.data(),encoded.size(),9)!=Z_OK)throw std::runtime_error("Compression failed");
            Writer packed;packed.raw("H1DZ",4);packed.word(encoded.size());packed.raw(compressed.data(),size);
            std::vector<unsigned char> c(map->mDefSize);
            if(!DecodeFile(packed.data,map,c.data())||Encode(map,c.data())!=encoded)throw std::runtime_error("Compressed roundtrip mismatch");
            FreeStrings(map,c.data());DefinitionFreeMap(map,c.data());
            auto dest=fs::path(output)/(relative+".h1d");fs::create_directories(dest.parent_path());
            FILE* f=fopen(dest.string().c_str(),"wb");if(!f||fwrite(packed.data.data(),1,packed.data.size(),f)!=packed.data.size())throw std::runtime_error("Export write failed");fclose(f);
            FreeStrings(map,a.data());DefinitionFreeMap(map,a.data());FreeStrings(map,b.data());DefinitionFreeMap(map,b.data());
            ++count;bytes+=packed.data.size();
        }
    }
    fprintf(stderr,"H1D_EXPORT_OK files=%u bytes=%llu\n",count,(unsigned long long)bytes);
}
#endif
