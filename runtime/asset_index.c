/* Validated, sorted directory of immutable resource names. No filesystem IO.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "asset_index.h"
#include <string.h>
static uint32_t word(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int valid_name(const char *name){
    const char *part=name;size_t length=strlen(name);if(!length||length>=512||*name=='/')return 0;
    for(const char *p=name;;p++){
        unsigned c=(unsigned char)*p;if(c=='\\'||c==':'||(c&&c<32)||(c>='A'&&c<='Z'))return 0;
        if(!c||c=='/'){size_t n=(size_t)(p-part);if(!n||(n==1&&*part=='.')||(n==2&&part[0]=='.'&&part[1]=='.'))return 0;part=p+1;}
        if(!c)return 1;
    }
}
int pvz_asset_index_parse(PvzAssetIndex *out,const void *source,size_t size){
    const unsigned char *b=source;PvzAssetIndex next={0};uint32_t count,strings,bytes,hash=2166136261u;const char *previous=0;
    if(!out||!b||size<32||size>2097152||memcmp(b,"H1FI",4)||word(b+4)!=1||word(b+12)!=12||word(b+28))return 0;
    count=word(b+8);strings=word(b+16);bytes=word(b+20);
    if(!count||count>32768||bytes!=size-32||strings>bytes||count*12u!=bytes-strings)return 0;
    for(uint32_t i=32;i<size;i++)hash=(hash^b[i])*16777619u;
    if(hash!=word(b+24))return 0;
    for(uint32_t i=0;i<count;i++){
        const unsigned char *r=b+32+i*12;uint32_t offset=word(r),length=word(r+4),flags=word(r+8);const char *name;
        if(offset>=strings||flags>1||length>0x7fffffffu||(flags&&length))return 0;
        name=(const char*)b+32+count*12+offset;
        if(!memchr(name,0,strings-offset)||!valid_name(name)||(previous&&strcmp(previous,name)>=0))return 0;
        previous=name;
    }
    next.data=b+32;next.count=count;next.strings=count*12;next.bytes=bytes;*out=next;return 1;
}
int pvz_asset_index_find(const PvzAssetIndex *index,const char *name,uint32_t *size,int *directory){
    uint32_t low=0,high=index->count;
    while(low<high){uint32_t mid=low+(high-low)/2;const unsigned char *r=index->data+mid*12;
        int order=strcmp(name,(const char*)index->data+index->strings+word(r));
        if(!order){if(size)*size=word(r+4);if(directory)*directory=(int)word(r+8);return 1;}
        if(order<0)high=mid;else low=mid+1;
    }return 0;
}
int pvz_asset_index_managed(const char *name){
    static const char *folders[]={"data","images","particles","reanim","sounds","trails","properties","h1menu"};
    for(unsigned i=0;i<sizeof(folders)/sizeof(*folders);i++){size_t n=strlen(folders[i]);if(!strncmp(name,folders[i],n)&&(!name[n]||name[n]=='/'))return 1;}
    /* DefinitionLoadImage first tries IMAGE_* aliases at the resource root.
       Converted image formats there are immutable too; savedata is excluded. */
    if(!strchr(name,'/')){size_t n=strlen(name);
        if(n>4&&(!strcmp(name+n-4,".h1i")||!strcmp(name+n-4,".h1a")))return 1;}
    return !strcmp(name,"image-metadata.h1x")||!strcmp(name,"audio-index.json");
}
