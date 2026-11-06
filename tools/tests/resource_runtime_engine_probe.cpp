/* Exercise every declared runtime resource through the complete real engine. */
#include "LawnApp.h"
#include "Resources.h"
#include "misc/ResourceManager.h"
#include "graphics/GLInterface.h"
#include "graphics/Graphics.h"
#include "graphics/GLImage.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/DirtyCanvas.h"
#include "graphics/RenderPlanCache.h"
#include "graphics/Font.h"
#include "PvzpLib/Reanimator.h"
#include "h1_resources.h"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <set>
extern "C" volatile uint32_t pvzh1_font_cache_diagnostic[8];
extern "C" volatile uint32_t pvzh1_font_tile_diagnostic[8];
namespace {
void Require(bool ok,const std::string& what){if(!ok)throw std::runtime_error(what);}
unsigned nonAlphaOpens=0,nonAlphaProbes=0;
}
extern "C" int __real_h1_image_open(H1Image*,const char*,int,uint32_t);
extern "C" int __wrap_h1_image_open(H1Image* image,const char* path,int alpha,uint32_t color){if(!alpha){++nonAlphaOpens;fprintf(stderr,"NON_ALPHA_OPEN %s\n",path);}return __real_h1_image_open(image,path,alpha,color);}
extern "C" int __real_h1_image_probe(const char*,int,H1ImageInfo*);
extern "C" int __wrap_h1_image_probe(const char* path,int alpha,H1ImageInfo* info){if(!alpha){++nonAlphaProbes;fprintf(stderr,"NON_ALPHA_PROBE %s\n",path);}return __real_h1_image_probe(path,alpha,info);}
extern "C" void pvzh1_resource_runtime_probe(){
    using namespace Sexy;
    auto* manager=gLawnApp->mResourceManager.get();
    auto* screen=gLawnApp->mGLInterface->GetScreenImage();
    auto* physical=H1GetPhysicalScreen(screen);auto* target=physical->Target();
    h1DirtyCanvas.enabled=false;H1RenderPlanTestEnable(false);
    auto out=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"));
    unsigned groups=0,images=0,fonts=0,sounds=0;
    std::set<std::string> declaredImages;
    for(const auto& [name,resources]:manager->mResGroupMap){
        for(auto* r:resources)if(r->mType==ResourceManager::ResType_Image&&!r->mFromProgram)declaredImages.insert(r->mId);
        fprintf(stderr,"RESOURCE_GROUP %s\n",name.c_str());
        Require(manager->LoadResources(name),"resource group failed: "+name+" "+manager->mError);++groups;
    }
    for(const auto& name:declaredImages){
        auto image=manager->GetImage(name);Require(static_cast<Image*>(image)!=nullptr,"image missing: "+name);
        h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();Graphics g(screen);
        std::fill_n(target->GetBits(),480*272,0xffc8bb9cu);
        // Rendering forces lazy file/pack reads, not only descriptor creation.
        g.DrawImage(image,0,0);++images;
        if(name.find("ZOMBIE_NOTE")!=std::string::npos||name.find("FINAL_NOTE")!=std::string::npos||name.find("CREDITS_ZOMBIENOTE")!=std::string::npos||name.find("POOL_BASE")!=std::string::npos||name.find("POOL_SHADING")!=std::string::npos){
            auto* bits=target->GetBits();std::ofstream f(out/(name+".argb"),std::ios::binary);f.write(reinterpret_cast<char*>(bits),480*272*4);
        }
        H1ImageCacheEndFrame();
    }
    for(const auto& [name,resource]:manager->mFontMap){
        auto* res=static_cast<ResourceManager::FontRes*>(resource.get());if(res->mFromProgram)continue;
        auto* font=manager->GetFont(name);Require(font!=nullptr,"font missing: "+name);
        h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();Graphics g(screen);g.SetFont(font);g.SetColor(Color::White);
        g.DrawString("ABC xyz 0123 !,.;: 读取中植物僵尸",15,150);++fonts;
        auto* bits=target->GetBits();std::ofstream f(out/(name+".argb"),std::ios::binary);f.write(reinterpret_cast<char*>(bits),480*272*4);
        H1ImageCacheEndFrame();
    }
    for(const auto& [name,resource]:manager->mSoundMap){auto* res=static_cast<ResourceManager::SoundRes*>(resource.get());if(res->mFromProgram)continue;Require(manager->GetSound(name)>=0,"sound missing: "+name);++sounds;}
    for(unsigned i=0;i<gReanimatorDefCount;++i)ReanimatorEnsureDefinitionLoaded(static_cast<ReanimationType>(i),false);
    Require(!pvzh1_font_cache_diagnostic[2]&&!pvzh1_font_cache_diagnostic[3],"font descriptor fallback occurred");
    Require(!pvzh1_font_tile_diagnostic[3]&&!pvzh1_font_tile_diagnostic[7],"tile read failed");
    Require(!nonAlphaOpens&&!nonAlphaProbes,"Production reader bypassed the preferred alpha variant");
    std::ofstream report(out/"resource-runtime-verification.json");
    report<<"{\"passed\":true,\"groups\":"<<groups<<",\"rendered_images\":"<<images<<",\"rendered_fonts\":"<<fonts<<",\"sounds\":"<<sounds<<",\"reanimation_definitions\":"<<gReanimatorDefCount<<",\"font_descriptor_fallbacks\":0,\"font_tile_errors\":0,\"non_alpha_image_opens\":0,\"non_alpha_image_probes\":0,\"scope\":\"complete host game with real production resource readers; not physical H1\"}\n";
}
