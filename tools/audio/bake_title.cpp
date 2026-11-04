// Offline rendering through the very same libxmp settings as the H1 mixer.
#include <xmp.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>
static void put(FILE* f,uint32_t n){unsigned char b[4];for(int i=0;i<4;i++)b[i]=n>>(i*8);fwrite(b,1,4,f);}
static bool init(xmp_context c,const char* path){return xmp_load_module(c,path)==0&&xmp_start_player(c,16000,XMP_FORMAT_MONO)==0&&xmp_set_player(c,XMP_PLAYER_INTERP,XMP_INTERP_LINEAR)==0&&xmp_set_player(c,XMP_PLAYER_DSP,0)==0&&xmp_set_position(c,0x98)>=-1&&xmp_set_row(c,0)>=0;}
int main(int argc,char** argv){
 if(argc!=3)return 2;auto c=xmp_create_context();if(!init(c,argv[1]))return 3;
 std::vector<int16_t> pcm;bool first=true;int firstOrder=-1,firstRow=-1,firstFrame=-1;
 for(unsigned frames=0;frames<100000;frames++){
  if(xmp_play_frame(c)<0)return 4;xmp_frame_info info{};xmp_get_frame_info(c,&info);
  if(first){firstOrder=info.pos;firstRow=info.row;firstFrame=info.frame;first=false;}
  else if(info.pos==firstOrder&&info.row==firstRow&&info.frame==firstFrame)break;
  const int16_t* p=(const int16_t*)info.buffer;pcm.insert(pcm.end(),p,p+info.buffer_size/2);
  if(pcm.size()>16000u*600u)return 5;
 }
 xmp_end_player(c);xmp_release_module(c);xmp_free_context(c);
 if(pcm.empty())return 6;
 // Independently replay the original stream API and compare every first-loop sample.
 c=xmp_create_context();if(!init(c,argv[1]))return 7;xmp_play_buffer(c,nullptr,0,0);
 int16_t block[256];for(size_t at=0;at<pcm.size();){unsigned n=std::min(size_t(256),pcm.size()-at);if(xmp_play_buffer(c,block,n*2,0)<0||memcmp(block,pcm.data()+at,n*2))return 8;at+=n;}
 xmp_end_player(c);xmp_release_module(c);xmp_free_context(c);
 FILE* f=fopen(argv[2],"wb");if(!f)return 9;fwrite("H1PC",1,4,f);put(f,1);put(f,16000);put(f,pcm.size());fwrite(pcm.data(),2,pcm.size(),f);fclose(f);
 printf("TITLE_PCM_OK samples=%zu seconds=%.3f sample_exact=true\n",pcm.size(),double(pcm.size())/16000);return 0;
}
