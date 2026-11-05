#include "FontTileStorage.h"
#include "h1_resources.h"
#include "zlib.h"
#include <cstring>
#include <string>
#include <new>
namespace Sexy {
namespace {
const unsigned char zeroTile[256]={};
uint32_t U32(const unsigned char* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
}
H1FontTileStorage::~H1FontTileStorage(){
    if(tiles)for(uint32_t i=0;i<count;++i)if(tiles[i]!=zeroTile)delete[] tiles[i];
    if(file)fclose(file);
}
bool H1FontTileStorage::Open(const char* name,uint32_t width,uint32_t height){
    if(file||!name||!width||!height||width>8192||height>8192)return false;
    const std::string path=std::string(h1_resources_get_root())+"data/font-tiles.h1ft";
    file=fopen(path.c_str(),"rb");if(!file)return false;
    unsigned char header[32],directory[16*64];
    if(fread(header,1,32,file)!=32||memcmp(header,"H1FT",4)||U32(header+4)!=1||U32(header+12)!=64||U32(header+28)!=16)return false;
    uint32_t n=U32(header+8);fileBytes=U32(header+16);dataStart=U32(header+20);
    if(!n||n>16||fileBytes>16*1024*1024||dataStart<32+n*64||dataStart>fileBytes)return false;
    if(fread(directory,1,n*64,file)!=n*64||crc32(0,directory,n*64)!=U32(header+24))return false;
    const unsigned char* match=nullptr;
    for(uint32_t i=0;i<n;++i){const auto* r=directory+i*64;
        if(!memchr(r,0,32)||!r[0])return false;
        if(!strcmp((const char*)r,name)){if(match)return false;match=r;}}
    if(!match||U32(match+32)!=width||U32(match+36)!=height)return false;
    columns=U32(match+40);rows=U32(match+44);const auto offset=U32(match+48),bytes=U32(match+52);
    if(columns!=(width+15)/16||rows!=(height+15)/16)return false;
    count=columns*rows;
    if(bytes!=count*8||offset<32+n*64||offset>dataStart||bytes>dataStart-offset)return false;
    records.reset(new(std::nothrow) Record[count]);tiles.reset(new(std::nothrow) unsigned char*[count]());
    if(!records||!tiles||fseek(file,offset,SEEK_SET)||fread(records.get(),1,bytes,file)!=bytes||crc32(0,(const Bytef*)records.get(),bytes)!=U32(match+56))return false;
    static_assert(sizeof(Record)==8);
    for(uint32_t i=0;i<count;++i){auto& r=records[i];const auto* p=(const unsigned char*)&r;
        r={U32(p),U32(p+4)};
        if(!r.offset){if(r.crc!=crc32(0,zeroTile,256))return false;tiles[i]=const_cast<unsigned char*>(zeroTile);}
        else if(r.offset<dataStart||r.offset>fileBytes||256>fileBytes-r.offset||(r.offset-dataStart)%256)return false;}
    return true;
}
bool H1FontTileStorage::Prepare(int left,int top,int right,int bottom,uint32_t& readBytes){
    readBytes=0;
    if(!file||left<0||top<0||right<=left||bottom<=top||uint32_t(right)>columns*16||uint32_t(bottom)>rows*16)return false;
    for(int y=top>>4;y<=(bottom-1)>>4;++y)for(int x=left>>4;x<=(right-1)>>4;++x){
        const auto i=uint32_t(y)*columns+x;if(tiles[i])continue;
        auto data=std::unique_ptr<unsigned char[]>(new(std::nothrow) unsigned char[256]);
        if(!data||fseek(file,records[i].offset,SEEK_SET)||fread(data.get(),1,256,file)!=256||crc32(0,data.get(),256)!=records[i].crc)return false;
        tiles[i]=data.release();++allocated;readBytes+=256;
    }
    return true;
}
}
