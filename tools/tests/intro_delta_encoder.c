/* Offline exact RGB565 horizontal predictor; the H1 never searches motion. */
#include <stdint.h>
#include <string.h>
int intro_delta(const uint32_t *argb,uint16_t *current,const uint16_t *previous,uint16_t *delta){
 for(unsigned i=0;i<480*272;++i){unsigned c=argb[i];current[i]=((c>>8)&0xf800)|((c>>5)&0x7e0)|((c>>3)&31);}
 unsigned best=0;int shift=0;
 for(int dx=-3;dx<=3;++dx){unsigned equal=0;
  for(unsigned y=0;y<272;++y)for(int x=0;x<480;++x){int px=x-dx;uint16_t p=px>=0&&px<480?previous[y*480+px]:0;equal+=current[y*480+x]==p;}
  if(equal>best){best=equal;shift=dx;}
 }
 for(unsigned y=0;y<272;++y)for(int x=0;x<480;++x){int px=x-shift;delta[y*480+x]=current[y*480+x]^(px>=0&&px<480?previous[y*480+px]:0);}
 return shift;
}
