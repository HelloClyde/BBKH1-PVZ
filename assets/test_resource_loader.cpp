#include "h1_resources.h"
#include "imagelib/ImageLib.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

static unsigned hash_pixels(const uint32_t *pixels,size_t count) {
    unsigned hash=2166136261u;
    for (size_t i=0;i<count;i++) hash=(hash^pixels[i])*16777619u;
    return hash;
}
int main(int argc,char **argv) {
    if (argc!=3) return 2;
    h1_resources_set_root(argv[1]);
    std::ifstream cases(argv[2]);
    std::string path;
    unsigned color,width,height,expected,flags;
    int auto_alpha,count=0;
    while (cases>>path>>auto_alpha>>std::hex>>color>>std::dec>>width>>height>>std::hex>>expected>>std::dec>>flags) {
        H1ImageInfo metadata;
        if (!h1_image_probe(path.c_str(),auto_alpha,&metadata) || metadata.width!=width || metadata.height!=height || metadata.flags!=flags) {
            std::fprintf(stderr,"metadata mismatch: %s\n",path.c_str()); return 3;
        }
        H1Image input;
        if (!h1_image_open(&input,path.c_str(),auto_alpha,color)) return 4;
        std::vector<uint32_t> pixels((size_t)width*height);
        if (!h1_image_load_all(&input,pixels.data(),pixels.size()) || hash_pixels(pixels.data(),pixels.size())!=expected) {
            std::fprintf(stderr,"ARGB mismatch: %s\n",path.c_str()); return 5;
        }
        if (width>2) {
            std::vector<uint32_t> row(width-2);
            if (!h1_image_read_span(&input,1,height-1,width-2,row.data())) return 6;
            for (unsigned i=0;i<width-2;i++) if (row[i]!=pixels[(height-1)*width+i+1]) return 7;
        }
        uint32_t guard=0xdeadbeefu;
        if (h1_image_read_span(&input,width,0,1,&guard) || guard!=0xdeadbeefu) return 8;
        h1_image_close(&input);
        ImageLib::gAlphaComposeColor=(int)color;
        auto image=std::unique_ptr<ImageLib::Image>(ImageLib::GetImage(path,auto_alpha));
        if (!image || image->mWidth!=(int)width || image->mHeight!=(int)height ||
            hash_pixels(image->GetBits(),pixels.size())!=expected) return 9;
        count++;
    }
    if (!cases.eof() || count==0) { std::fprintf(stderr,"case parse failure after %d, state=%d\n",count,(int)cases.rdstate()); return 10; }
    H1Image missing;
    if (h1_image_open(&missing,"images/this_resource_is_not_present",1,0xffffffu)) return 11;
    if (h1_image_open(&missing,"../../outside_resource_root",1,0xffffffu)) return 12;
    std::printf("Passed %d image metadata/full-load/span/alpha comparisons.\n",count);
    return 0;
}
