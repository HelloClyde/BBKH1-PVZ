#ifndef PVZH1_FONT_TILE_STORAGE_H
#define PVZH1_FONT_TILE_STORAGE_H
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <memory>
namespace Sexy {
/* Existing A8 glyph pixels, already scaled offline. Loading copies only the
 * tiles intersecting a glyph. No rasterization, rescaling or decompression. */
class H1FontTileStorage {
    FILE* file=nullptr;
    std::unique_ptr<uint32_t[]> records;
    std::unique_ptr<unsigned char*[]> tiles;
    uint32_t count=0,columns=0,rows=0,dataStart=0,fileBytes=0,allocated=0;
public:
    ~H1FontTileStorage();
    bool Open(const char* name,uint32_t width,uint32_t height);
    bool Prepare(int left,int top,int right,int bottom,uint32_t& readBytes);
    const unsigned char* const* View()const{return tiles.get();}
    uint32_t Columns()const{return columns;}
    size_t Bytes()const{return count*(sizeof(uint32_t)+sizeof(unsigned char*))+allocated*256u;}
    size_t Largest()const{return count?count*(sizeof(uint32_t)>sizeof(unsigned char*)?sizeof(uint32_t):sizeof(unsigned char*)):0;}
};
}
#endif
