/* Export image dependencies of real startup and the early lawn levels. */
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/System/PlayerInfo.h"
#include "graphics/GLImage.h"
#include "graphics/MemoryImageCache.h"
#include <set>
#include <string>
#include <filesystem>
#include <fstream>
extern "C" void pvzh1_common_images_probe(){
 std::set<std::string> paths;
 auto collect=[&](){for(const auto& entry:gLawnApp->mSharedImageMap){
  auto* image=entry.second.mImage;if(!image||image->mFilePath.empty())continue;
  bool live=image->mBits||image->mColorIndices||image->mNativeAlphaData||image->mRLAlphaData||image->mRLAdditiveData;
  Sexy::H1ImageCacheStorageStats s{};live|=Sexy::H1ImageCacheGetStorageStats(image,&s)&&s.bytes;
  Sexy::H1ImageCacheRGBStats rgb{};live|=Sexy::H1ImageCacheGetRGBStats(image,&rgb)&&rgb.bytes;
  Sexy::H1ImageCacheFontPageStats font{};live|=Sexy::H1ImageCacheGetFontPageStats(image,&font)&&font.bytes;
  if(live)paths.insert(image->mFilePath);
 }};
 collect();PlayerInfo profile;profile.mName="PACK_EXPORT";auto* saved=gLawnApp->mPlayerInfo;gLawnApp->mPlayerInfo=&profile;
 for(int level:{1,2,3,4}){profile.mLevel=level;gLawnApp->PreNewGame(GAMEMODE_ADVENTURE,false);collect();gLawnApp->KillSeedChooserScreen();gLawnApp->KillBoard();}
 gLawnApp->mPlayerInfo=saved;
 std::ofstream out(std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"common-images.txt");for(const auto& path:paths)out<<path<<'\n';
}
