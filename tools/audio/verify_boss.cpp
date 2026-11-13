// Compare original and offline-pruned final-Boss XM through the pinned mixer.
#include <xmp.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>
static bool init(xmp_context c,const char* path,int order,int row){
 return xmp_load_module(c,path)==0&&xmp_start_player(c,16000,XMP_FORMAT_MONO)==0&&
 xmp_set_player(c,XMP_PLAYER_INTERP,XMP_INTERP_LINEAR)==0&&xmp_set_player(c,XMP_PLAYER_DSP,0)==0&&
 xmp_set_position(c,order)>=-1&&xmp_set_row(c,row)>=0;
}
static void done(xmp_context c){xmp_end_player(c);xmp_release_module(c);xmp_free_context(c);}
int main(int argc,char** argv){
 if(argc!=3)return 2;uint64_t compared=0;unsigned totalFrames=0;bool visited[256]{};
 for(unsigned seek=0;seek<6;++seek){
  int order=0x9e + seek,row=seek?11:0;
  auto a=xmp_create_context(),b=xmp_create_context();if(!init(a,argv[1],order,row)||!init(b,argv[2],order,row))return 3;
  unsigned repeats=0;bool seen[8][256][32]{};
  for(unsigned frame=0;frame<100000;++frame){
   // Exercise all original dynamic channels, including drums and hi-hat.
   if(frame%127==0)for(int channel=0;channel<30;++channel){int gain=int((frame/127*37+channel*19)%101);xmp_channel_vol(a,channel,gain);xmp_channel_vol(b,channel,gain);}
   if(xmp_play_frame(a)<0||xmp_play_frame(b)<0)return 4;
   xmp_frame_info x{},y{};xmp_get_frame_info(a,&x);xmp_get_frame_info(b,&y);
   if(x.pos<0x9e||x.pos>=0xa6||x.pos!=y.pos||x.row!=y.row||x.frame!=y.frame||x.speed!=y.speed||x.bpm!=y.bpm||x.buffer_size!=y.buffer_size||memcmp(x.buffer,y.buffer,x.buffer_size)){
    fprintf(stderr,"MISMATCH seek=%u frame=%u pos=%d/%d row=%d/%d\n",seek,frame,x.pos,y.pos,x.row,y.row);return 5;
   }
   visited[x.pos]=true;compared+=x.buffer_size/2;++totalFrames;
   if(!seek){
    if(x.row>=256||x.frame>=32)return 7;
    if(seen[x.pos-0x9e][x.row][x.frame]){if(++repeats==10000)break;}
    seen[x.pos-0x9e][x.row][x.frame]=true;
   }else if(frame>=1999)break;
   if(frame==99999)return 6;
  }
  done(a);done(b);
 }
 unsigned orders=0;for(bool v:visited)orders+=v;
 printf("{\"passed\":true,\"sample_exact\":true,\"samples_compared\":%llu,\"frames_compared\":%u,\"orders_visited\":%u,\"seek_cases\":6,\"dynamic_channels\":30}\n",(unsigned long long)compared,totalFrames,orders);
 return 0;
}
