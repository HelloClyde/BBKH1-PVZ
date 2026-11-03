/* Actual resource/raster coverage at the original campaign camera endpoints. */
#include "LawnApp.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/ScreenLayout.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/Graphics.h"
#include <fstream>
#include <filesystem>
#include <memory>
#include <stdexcept>
using namespace Sexy;
extern "C" void pvzh1_widescreen_backdrops_probe(){
    auto* device=gLawnApp->mGLInterface.get();auto saved=std::move(device->mScreenImage);
    unsigned cases=0;
    for(const char* name:{"background1","background1unsodded","background2","background3","background4","background5","background6boss"}){
        std::unique_ptr<GLImage> image(gLawnApp->GetImage(std::string("images/")+name));
        if(!image||image->mWidth<1400||image->mHeight!=600)throw std::runtime_error("campaign backdrop dimensions");
        for(int position:{-380,0,220}){
            device->mScreenImage=std::make_unique<H1PhysicalScreen>(device,H1LogicalWidth,H1LogicalHeight,H1ScreenWidth,H1ScreenHeight);
            auto* screen=device->GetScreenImage();auto* target=H1ScreenTarget(screen);
            for(unsigned i=0;i<H1ScreenWidth*H1ScreenHeight;++i)target->mBits[i]=0x11223344u;
            Graphics graphics(screen);graphics.Translate(H1BoardCameraX(position),0);graphics.DrawImage(image.get(),-220,0);
            unsigned untouchedColumns=0;
            for(unsigned x=0;x<H1ScreenWidth;++x){unsigned painted=0;
                for(unsigned y=0;y<H1ScreenHeight;++y)painted+=target->mBits[y*H1ScreenWidth+x]!=0x11223344u;
                if(painted<H1ScreenHeight-2)++untouchedColumns;
            }
            if(untouchedColumns>1)throw std::runtime_error("unpainted widescreen backdrop columns");
            std::filesystem::path path=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/(std::string(name)+"-camera-"+std::to_string(position)+".ppm");
            std::ofstream out(path,std::ios::binary);out<<"P6\n480 272\n255\n";
            for(unsigned i=0;i<H1ScreenWidth*H1ScreenHeight;++i){uint32_t p=target->mBits[i];const char rgb[]={(char)(p>>16),(char)(p>>8),(char)p};out.write(rgb,3);}
            ++cases;device->mScreenImage.reset();
        }
    }
    device->mScreenImage=std::move(saved);
    std::ofstream(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"widescreen-backdrops-verification.json")<<"{\"passed\":true,\"campaign_backgrounds\":7,\"camera_positions_per_background\":3,\"actual_raster_cases\":"<<cases<<",\"wide_unpainted_bars\":false}\n";
}
