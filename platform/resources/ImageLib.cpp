/* H1 image decoder replacement. Original game dimensions and 8-bit alpha are
 * retained; platform image files are prepared by tools/prepare_assets.py. */
#include "imagelib/ImageLib.h"
#include "h1_resources.h"
#include <new>

namespace ImageLib {
int gAlphaComposeColor=0xffffff;
bool gAutoLoadAlpha=true;
bool gIgnoreJPEG2000Alpha=true;
Image::Image():mWidth(0),mHeight(0) {}
Image::~Image()=default;
int Image::GetWidth() { return mWidth; }
int Image::GetHeight() { return mHeight; }
uint32_t *Image::GetBits() { return mBits.get(); }

Image *GetImage(const std::string &path,bool auto_alpha) {
    H1Image source;
    if (!h1_image_open(&source,path.c_str(),auto_alpha&&gAutoLoadAlpha,(uint32_t)gAlphaComposeColor)) return nullptr;
    const size_t count=(size_t)source.info.width*source.info.height;
    Image *image=new(std::nothrow) Image();
    if (!image) { h1_image_close(&source); return nullptr; }
    image->mWidth=(int)source.info.width; image->mHeight=(int)source.info.height;
    image->mBits.reset(new(std::nothrow) uint32_t[count]);
    if (!image->mBits || !h1_image_load_all(&source,image->mBits.get(),count)) {
        h1_image_close(&source); delete image; return nullptr;
    }
    h1_image_close(&source);
    return image;
}
/* File-format export is outside this decoder backend. */
bool WriteJPEGImage(const std::string&,Image*) { return false; }
bool WritePNGImage(const std::string&,Image*) { return false; }
bool WriteTGAImage(const std::string&,Image*) { return false; }
}
