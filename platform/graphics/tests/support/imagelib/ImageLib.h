#ifndef PVZH1_GRAPHICS_TEST_IMAGELIB_H
#define PVZH1_GRAPHICS_TEST_IMAGELIB_H
#include <cstdint>
#include <string>
namespace ImageLib {
class Image {
public:
    virtual ~Image()=default;
    int GetWidth(){return 0;}
    int GetHeight(){return 0;}
    uint32_t *GetBits(){return nullptr;}
};
inline Image *GetImage(const std::string&,bool=true){return nullptr;}
}
#endif
