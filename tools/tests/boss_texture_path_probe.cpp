/* Observe the real reduced Boss texture before and after physical draws. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/Zombie.h"
#include "Lawn/System/PlayerInfo.h"
#include "PvzpLib/Reanimator.h"
#include "graphics/Graphics.h"
#include "graphics/GLInterface.h"
#include "graphics/PhysicalScreen.h"
#include "graphics/MemoryImage.h"
#include "graphics/MemoryImageCache.h"
#include "graphics/DirtyCanvas.h"
#include <fstream>
#include <filesystem>
#include <cctype>
#include <stdexcept>
#include <set>
namespace {
void Dump(std::ostream&out,Sexy::MemoryImage*image,const char*phase){
 Sexy::H1ImageCacheStorageStats s{};const bool found=Sexy::H1ImageCacheGetStorageStats(image,&s);auto all=Sexy::H1ImageCacheGetStats();
 out<<"{\"phase\":\""<<phase<<"\",\"registered\":"<<Sexy::H1ImageCacheContains(image)<<",\"reduced\":"<<Sexy::H1ImageCacheIsReduced(image)<<",\"storage_found\":"<<found<<",\"dimensions\":["<<image->mWidth<<','<<image->mHeight<<"],\"bits\":"<<bool(image->mBits)<<",\"indices\":"<<bool(image->mColorIndices)<<",\"palette\":"<<bool(image->mColorTable)<<",\"native_alpha\":"<<bool(image->mNativeAlphaData)<<",\"rl_alpha\":"<<bool(image->mRLAlphaData)<<",\"forced\":"<<image->mForcedMode<<",\"alpha\":"<<image->mHasAlpha<<",\"transparent\":"<<image->mHasTrans<<",\"storage\":["<<s.storageWidth<<','<<s.storageHeight<<"],\"storage_bytes\":"<<s.bytes<<",\"storage_loads\":"<<s.loads<<",\"argb_loads\":"<<s.argbLoads<<",\"cache_resident\":"<<all.residentBytes<<",\"cache_peak\":"<<all.peakBytes<<'}';
}
}
extern "C" void pvzh1_boss_texture_path_probe(){
 using namespace Sexy;PlayerInfo profile;profile.mLevel=50;profile.mName="BOSS_TEXTURE_PATH";auto*old=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);
 auto*z=gLawnApp->mBoard->GetBossZombie();if(!z)z=gLawnApp->mBoard->AddZombieInRow(ZOMBIE_BOSS,0,-2);auto*r=gLawnApp->ReanimationGet(z->mBodyReanimID);
 std::set<MemoryImage*> bodies;
 for(int t=0;t<r->mDefinition->mTracks.count;++t)for(int f=0;f<r->mDefinition->mTracks.tracks[t].mTransforms.count;++f){auto*i=r->mDefinition->mTracks.tracks[t].mTransforms.mTransforms[f].mImage;if(uintptr_t(i)<1000)continue;auto*m=dynamic_cast<MemoryImage*>(i);if(!m)continue;auto name=m->mFilePath;for(char&c:name)c=std::tolower((unsigned char)c);if(name.find("boss_upperbody")!=std::string::npos)bodies.insert(m);}
 if(bodies.empty())throw std::runtime_error("No original Boss upperbody image");auto*screen=gLawnApp->mGLInterface->GetScreenImage();auto*target=H1GetPhysicalScreen(screen)->Target();
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"boss-texture-path.json");out<<"{\"images\":[";bool first=true;
 for(auto*image:bodies){if(!first)out<<',';first=false;out<<"{\"path\":\""<<image->mFilePath<<"\",\"samples\":[";Dump(out,image,"preloaded");
  for(unsigned frame=0;frame<5;++frame){h1DirtyCanvas.Begin(target);h1DirtyCanvas.RequireClear();Graphics g(screen);g.DrawImage(image,0,0);H1ImageCacheEndFrame();out<<',';Dump(out,image,"after_draw");}out<<"]}";
 }out<<"]}\n";gLawnApp->KillBoard();gLawnApp->mPlayerInfo=old;
}
