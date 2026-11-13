/* Offline-verified, sorted directory of immutable names. No filesystem IO.
 * SPDX-License-Identifier: LGPL-3.0-or-later */
#include "asset_index.h"
#include <string.h>
static uint32_t word(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int pvz_asset_index_parse(PvzAssetIndex *out,const void *source,size_t size){
    const unsigned char *b=source;PvzAssetIndex next={0};uint32_t count,strings,bytes;
    if(!out||!b||size<32||size>2097152||memcmp(b,"H1FI",4)||word(b+4)!=1||word(b+12)!=12||word(b+28))return 0;
    count=word(b+8);strings=word(b+16);bytes=word(b+20);
    if(!count||count>32768||bytes!=size-32||strings>bytes||count*12u!=bytes-strings)return 0;
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
    static const char *folders[]={"data","images","particles","reanim","sounds","trails","properties","h1menu","extra-actors"};
    for(unsigned i=0;i<sizeof(folders)/sizeof(*folders);i++){size_t n=strlen(folders[i]);if(!strncmp(name,folders[i],n)&&(!name[n]||name[n]=='/'))return 1;}
    /* DefinitionLoadImage first tries IMAGE_* aliases at the resource root.
       Converted image formats there are immutable too; savedata is excluded. */
    if(!strchr(name,'/')){size_t n=strlen(name);
        if(n>4&&(!strcmp(name+n-4,".h1i")||!strcmp(name+n-4,".h1a")))return 1;}
    return !strcmp(name,"image-metadata.h1x")||!strcmp(name,"audio-index.json");
}
