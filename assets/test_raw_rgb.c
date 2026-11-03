#include "h1_resources.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static unsigned seeks, reads, checks, short_next, fail_seek_next;
int pvz_test_fseek(FILE *fp,long offset,int origin) {
    ++seeks;
    if (fail_seek_next) { fail_seek_next=0;return -1; }
    return fseek(fp,offset,origin);
}
size_t pvz_test_fread(void *out,size_t size,size_t count,FILE *fp) {
    ++reads;
    if (short_next && count) {
        short_next=0;
        return fread(out,size,count-(size==1 && count>=3?3:1),fp);
    }
    return fread(out,size,count,fp);
}
#define CHECK(x) do { ++checks;if (!(x)) { fprintf(stderr,"check %u failed, line %d: %s\n",checks,__LINE__,#x);return 3; } } while(0)
static int guarded(const unsigned char *b,size_t n) {
    size_t i;for(i=0;i<16;i++)if(b[i]!=0xa5 || b[16+n+i]!=0xa5)return 0;return 1;
}
static uint32_t color(const unsigned char *b) {
    return 0xff000000u|((uint32_t)b[0]<<16)|((uint32_t)b[1]<<8)|b[2];
}
int main(int argc,char **argv) {
    H1Image image;FILE *fp;long length;unsigned char *expected,*all;
    unsigned char page[3072+32],small[96];uint32_t *argb;
    uint32_t bytes,offset,n,pixels;unsigned old_seeks,old_reads,pages=0,i;
    if(argc!=5)return 2;
    fp=fopen(argv[3],"rb");if(!fp)return 2;
    fseek(fp,0,SEEK_END);length=ftell(fp);rewind(fp);
    if(length<=0 || (unsigned long)length>UINT32_MAX)return 2;
    bytes=(uint32_t)length;expected=(unsigned char*)malloc(bytes);all=(unsigned char*)malloc(bytes);
    if(!expected || !all || fread(expected,1,bytes,fp)!=bytes)return 2;
    fclose(fp);h1_resources_set_root(argv[1]);
    CHECK(h1_image_open(&image,argv[4],1,0xffffffu));
    CHECK(image.info.width==1400 && image.info.height==600 && image.info.channels==3 && image.info.flags==0);
    CHECK(image.info.pixel_bytes==bytes && bytes==2520000u);pixels=bytes/3u;
    old_seeks=seeks;old_reads=reads;
    for(offset=0;offset<bytes;offset+=n) {
        n=bytes-offset>3072u?3072u:bytes-offset;memset(page,0xa5,sizeof(page));
        CHECK(h1_image_read_rgb_bytes(&image,offset,n,page+16));
        CHECK(guarded(page,n));CHECK(!memcmp(page+16,expected+offset,n));
        memcpy(all+offset,page+16,n);++pages;
    }
    CHECK(pages==821 && n==960);CHECK(seeks==old_seeks);CHECK(reads-old_reads==821);
    CHECK(!memcmp(all,expected,bytes));
    old_seeks=seeks;old_reads=reads;memset(small,0x66,sizeof(small));
    CHECK(!h1_image_read_rgb_bytes(&image,1,3,small));
    CHECK(!h1_image_read_rgb_bytes(&image,0,1,small));
    CHECK(!h1_image_read_rgb_bytes(&image,bytes+3,0,small));
    CHECK(!h1_image_read_rgb_bytes(&image,bytes,3,small));
    CHECK(!h1_image_read_rgb_bytes(&image,UINT32_MAX,3,small));
    CHECK(!h1_image_read_rgb_bytes(&image,0,UINT32_MAX,small));
    CHECK(!h1_image_read_rgb_bytes(&image,0,3,NULL));
    CHECK(!h1_image_read_rgb_span(&image,UINT32_MAX,1,small));
    CHECK(!h1_image_read_rgb_span(&image,0,UINT32_MAX,small));
    CHECK(h1_image_read_rgb_bytes(&image,bytes,0,NULL));
    CHECK(h1_image_read_rgb_span(&image,pixels,0,NULL));
    CHECK(seeks==old_seeks && reads==old_reads);
    for(i=0;i<sizeof(small);i++)CHECK(small[i]==0x66);
    /* Cross a 1024-pixel page edge, a 1400-pixel row, and the final page. */
    CHECK(h1_image_read_rgb_span(&image,1018,20,small));CHECK(!memcmp(small,expected+1018*3,60));
    CHECK(h1_image_read_rgb_span(&image,1395,20,small));CHECK(!memcmp(small,expected+1395*3,60));
    CHECK(h1_image_read_rgb_span(&image,pixels-20,20,small));CHECK(!memcmp(small,expected+bytes-60,60));
    { uint32_t row[10];CHECK(h1_image_read_span(&image,1390,17,10,row));
      for(i=0;i<10;i++)CHECK(row[i]==color(expected+((17*1400+1390+i)*3))); }
    old_seeks=seeks;CHECK(h1_image_read_rgb_bytes(&image,3072,60,small));
    CHECK(seeks==old_seeks+1 && !memcmp(small,expected+3072,60));
    argb=(uint32_t*)malloc((size_t)pixels*4);CHECK(argb!=NULL);
    CHECK(h1_image_load_all(&image,argb,pixels));
    for(i=0;i<pixels;i++)CHECK(argb[i]==color(expected+i*3));
    free(argb);
    old_seeks=seeks;CHECK(h1_image_read_rgb_span(&image,1395,20,small));
    CHECK(seeks==old_seeks+1 && !memcmp(small,expected+1395*3,60));
    /* A partial read and failed seek must each force a known-position retry. */
    short_next=1;CHECK(!h1_image_read_rgb_bytes(&image,6144,60,small));
    old_seeks=seeks;CHECK(h1_image_read_rgb_bytes(&image,6144,60,small));
    CHECK(seeks==old_seeks+1 && !memcmp(small,expected+6144,60));
    fail_seek_next=1;CHECK(!h1_image_read_rgb_bytes(&image,0,60,small));
    old_seeks=seeks;CHECK(h1_image_read_rgb_bytes(&image,0,60,small));
    CHECK(seeks==old_seeks+1 && !memcmp(small,expected,60));
    h1_image_close(&image);CHECK(!h1_image_read_rgb_bytes(&image,0,3,small));
    memset(&image,0,sizeof(image));CHECK(!h1_image_read_rgb_bytes(&image,0,3,small));
    CHECK(!h1_image_read_rgb_bytes(NULL,0,3,small));
    h1_resources_set_root(argv[2]);
    { const char *bad[]={"badmagic","badsize","badwidth","badflags","empty","shortheader"};
      for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++)CHECK(!h1_image_open(&image,bad[i],0,0xffffffu)); }
    { const char *not_rgb[]={"rgba","mask","rgbmask","properties/partner_logo"};H1ImageInfo hint;
      CHECK(h1_image_probe("rgba",0,&hint) && hint.channels==3); /* Deliberately stale index. */
      for(i=0;i<sizeof(not_rgb)/sizeof(not_rgb[0]);i++) {
          CHECK(h1_image_open(&image,not_rgb[i],0,0xffffffu));
          CHECK(!h1_image_read_rgb_bytes(&image,0,3,small));
          /* Metadata edits cannot turn an actual RGBA/mask header into RGB. */
          image.info.channels=3;image.info.flags=0;
          image.info.pixel_bytes=image.info.width*image.info.height*3;
          CHECK(!h1_image_read_rgb_bytes(&image,0,3,small));h1_image_close(&image);
      }
    }
    CHECK(h1_image_open(&image,"autoalias",1,0xffffffu));
    CHECK(image.info.channels==4 && !h1_image_read_rgb_bytes(&image,0,3,small));h1_image_close(&image);
    CHECK(h1_image_open(&image,"autoalias",0,0xffffffu));
    CHECK(h1_image_read_rgb_span(&image,0,6,small));
    { const unsigned char high[]={255,128,0,0,255,255,1,254,129,255,255,255,0,0,0,128,0,255};
      uint32_t decoded[3];CHECK(!memcmp(small,high,sizeof(high)));
      CHECK(h1_image_read_span(&image,0,0,3,decoded));
      for(i=0;i<3;i++)CHECK(decoded[i]==color(high+i*3)); }
    h1_image_close(&image);
    CHECK(h1_image_open(&image,"truncated",0,0xffffffu));
    CHECK(!h1_image_read_rgb_span(&image,0,8,small));
    CHECK(h1_image_read_rgb_span(&image,0,1,small));
    CHECK(small[0]==255 && small[1]==128 && small[2]==0);h1_image_close(&image);
    free(expected);free(all);
    printf("{\"checks\":%u,\"rgb_bytes_compared\":%u,\"sequential_pages\":%u,\"last_page_bytes\":960,\"sequential_fseek_calls\":0,\"sequential_fread_calls\":821,\"result\":\"PASS\"}\n",checks,bytes,pages);
    return 0;
}
