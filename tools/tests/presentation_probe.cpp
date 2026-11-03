#include "graphics/PresentationMap.h"
#include <vector>
#include <cassert>
#include <cstdio>
#include <cstring>
static void Reference(uint32_t*out,const uint32_t*p,int w,int h,int px,int py){
 int dw=w*272/h,dh=272;if(dw>480){dw=480;dh=h*480/w;}int ox=(480-dw)/2,oy=(272-dh)/2;
 for(int y=0;y<272;++y){auto row=out+y*480;for(int x=0;x<ox;++x)row[x]=0x101b14;for(int x=ox+dw;x<480;++x)row[x]=0x101b14;if(y<oy||y>=oy+dh){for(int x=ox;x<ox+dw;++x)row[x]=0x101b14;continue;}const uint32_t*src=p+((y-oy)*h/dh)*w;for(int x=0;x<dw;++x)row[ox+x]=src[x*w/dw]&0xffffff;}
 int cx=ox+px*dw/800,cy=oy+py*dh/600;if(cx>=ox&&cx<ox+dw&&cy>=oy&&cy<oy+dh)for(int d=-3;d<=3;++d){if(cx+d>=0&&cx+d<480)out[cy*480+cx+d]=0xffec55;if(cy+d>=0&&cy+d<272)out[(cy+d)*480+cx]=0xffec55;}
}
int main(){H1PresentationMap map;unsigned checks=0;const int dims[][2]={{360,270},{800,600},{480,272},{272,480},{31,23},{721,162},{1,1},{1,999},{999,1},{360,270}};const int points[][2]={{-800,-600},{-1,-1},{0,0},{1,1},{400,300},{799,599},{800,600},{801,601},{123,456},{799,0},{0,599}};
 for(auto &dim:dims){int w=dim[0],h=dim[1];std::vector<uint32_t> input(w*h);for(size_t i=0;i<input.size();++i)input[i]=(uint32_t)(i*2654435761u+0x12345678u);for(auto &point:points){std::vector<uint32_t>a(480*272+8,0xfefefefe),b=a;Reference(a.data()+4,input.data(),w,h,point[0],point[1]);assert(H1PresentMapped(b.data()+4,input.data(),w,h,point[0],point[1],map));assert(a==b);++checks;}}
 std::vector<uint32_t> untouched(480*272,0xabcdef01),before=untouched;uint32_t input=0;assert(!H1PresentMapped(untouched.data(),&input,0,1,0,0,map));assert(!H1PresentMapped(untouched.data(),&input,1,0,0,0,map));assert(!H1PresentMapped(nullptr,&input,1,1,0,0,map));assert(!H1PresentMapped(untouched.data(),nullptr,1,1,0,0,map));assert(untouched==before);
 std::printf("{\"all_passed\":true,\"exact_framebuffer_cases\":%u,\"invalid_input_checks\":4,\"pointer_logical_size\":[800,600],\"guard_pixels_unchanged\":true}\n",checks);
}
