/* Compile the existing startup illustration/bar at final LCD resolution. */
#include "LawnApp.h"
#include "Resources.h"
#include "graphics/GLInterface.h"
#include "graphics/GLImage.h"
#include "graphics/Graphics.h"
#include "graphics/Font.h"
#include "graphics/PhysicalScreen.h"
#include <filesystem>
#include <fstream>
extern "C" void pvzh1_level_loading_export(){
 auto* device=gLawnApp->mGLInterface.get();auto* screen=device->GetScreenImage();auto* target=Sexy::H1GetPhysicalScreen(screen)->Target();
 auto path=std::filesystem::path(getenv("PVZ_HOST_OUTPUT"))/"level-loading.raw";std::ofstream out(path,std::ios::binary);uint32_t header[]={0x4c4c3148,1,480,272};out.write((char*)header,sizeof(header));
 for(unsigned stage:{0u,100u}){
  Sexy::Graphics g(screen);g.SetColor(Sexy::Color::Black);g.FillRect(0,0,gLawnApp->mWidth,gLawnApp->mHeight);
  g.DrawImage(Sexy::IMAGE_TITLESCREEN,0,0);g.DrawImage(Sexy::IMAGE_PVZ_LOGO,(gLawnApp->mWidth-Sexy::IMAGE_PVZ_LOGO->GetWidth())/2,15);
  const int x=(gLawnApp->mWidth-Sexy::IMAGE_LOADBAR_DIRT->GetWidth())/2,y=512;g.DrawImage(Sexy::IMAGE_LOADBAR_DIRT,x,y+18);
  Sexy::Graphics grass(g);grass.ClipRect(x,y,314*stage/100,Sexy::IMAGE_LOADBAR_GRASS->GetHeight());grass.DrawImage(Sexy::IMAGE_LOADBAR_GRASS,x,y);
  g.SetFont(Sexy::FONT_DWARVENTODCRAFT18);g.SetColor(Sexy::Color(218,184,33));const auto label=gLawnApp->GetString("LOADING","Loading...");g.DrawString(label,x+(314-g.GetFont()->StringWidth(label))/2,560);
  out.write((char*)target->GetBits(),480*272*4);
 }
}
