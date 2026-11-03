#include "h1_resources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define H1I_HEADER_BYTES 24u
#define H1I2_HEADER_BYTES 32u
#define H1I_MASK_ONLY 1u
static char resource_root[320] = "/PVZ/";
static H1ResourceLogger resource_logger;
static int optional_logo_reported;
static unsigned char *metadata_data;
static uint32_t metadata_count;
static int metadata_state;
static uint32_t metadata_record_bytes;

void h1_resources_set_logger(H1ResourceLogger logger) { resource_logger=logger; }

void h1_resources_set_root(const char *root) {
    size_t n;
    if (!root) return;
    n=strlen(root);
    if (n>=sizeof(resource_root)-2) return;
    memcpy(resource_root,root,n+1);
    if (n && root[n-1]!='/' && root[n-1]!='\\') {
        resource_root[n++]='/'; resource_root[n]=0;
    }
    free(metadata_data);metadata_data=0;metadata_count=0;metadata_state=0;metadata_record_bytes=0;
    optional_logo_reported=0;
}
const char *h1_resources_get_root(void) { return resource_root; }
static uint32_t little32(const unsigned char *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int make_path(char *out,size_t capacity,const char *logical,const char *ext) {
    size_t used=strlen(resource_root), base, i;
    const char *p=logical;
    if (!logical || used>=capacity) return 0;
    memcpy(out,resource_root,used);
    /* Font definitions may already carry the application's resource root. */
    for (i=0;i<used && p[i];i++) {
        unsigned char a=(unsigned char)p[i],b=(unsigned char)resource_root[i];
        if (a=='\\') a='/';
        if (b=='\\') b='/';
        if (a>='A' && a<='Z') a=(unsigned char)(a-'A'+'a');
        if (b>='A' && b<='Z') b=(unsigned char)(b-'A'+'a');
        if (a!=b) break;
    }
    if (i==used) p+=used;
    while (p[0]=='.' && (p[1]=='/' || p[1]=='\\')) p+=2;
    if (*p=='/' || *p=='\\' || strchr(p,':')) return 0;
    base=used;
    for (i=0;p[i];i++) {
        unsigned char ch=(unsigned char)p[i];
        if (ch=='\\') ch='/';
        if (ch>='A' && ch<='Z') ch=(unsigned char)(ch-'A'+'a');
        if (used+6>=capacity || ch<32) return 0;
        out[used++]=(char)ch;
    }
    out[used]=0;
    if (strstr(out+base,"../") || !strcmp(out+base,"..")) return 0;
    /* Callers may include a standard source image extension. */
    if (used>=base+4 && (!strcmp(out+used-4,".png") || !strcmp(out+used-4,".jpg") ||
                        !strcmp(out+used-4,".gif") || !strcmp(out+used-4,".tga"))) used-=4;
    if (used+strlen(ext)+1>capacity) return 0;
    strcpy(out+used,ext);
    return 1;
}
static int valid_info(const H1ImageInfo *info) {
    return info->width && info->width<=8192 && info->height && info->height<=8192 &&
           info->storage_width && info->storage_width<=info->width &&
           info->storage_height && info->storage_height<=info->height &&
           (info->channels==1 || info->channels==3 || info->channels==4) &&
           !(info->flags&~H1I_MASK_ONLY) && (info->channels!=1 || (info->flags&H1I_MASK_ONLY)) &&
           info->storage_width*info->storage_height*info->channels==info->pixel_bytes;
}
static void index_info(const unsigned char *record,uint32_t stride,H1ImageInfo *info) {
    info->width=little32(record+4);info->height=little32(record+8);
    info->channels=little32(record+12);info->flags=little32(record+16);info->pixel_bytes=little32(record+20);
    info->storage_width=stride==32?little32(record+24):info->width;
    info->storage_height=stride==32?little32(record+28):info->height;
}
static void metadata_init(void) {
    unsigned char header[28],*data=0;char path[640];FILE *fp;
    uint32_t count,strings,bytes,stride,hash=2166136261u,i;const char *previous=0;
    if (metadata_state) return;
    metadata_state=-1;
    if (strlen(resource_root)+sizeof("image-metadata.h1x")>sizeof(path)) return;
    strcpy(path,resource_root);strcat(path,"image-metadata.h1x");
    fp=fopen(path,"rb");if (!fp) return;
    if (fread(header,1,sizeof(header),fp)!=sizeof(header)) goto done;
    stride=little32(header+12);
    if (!((!memcmp(header,"H1X1",4) && little32(header+4)==1 && stride==24) ||
          (!memcmp(header,"H1X2",4) && little32(header+4)==2 && stride==32))) goto done;
    count=little32(header+8);strings=little32(header+16);bytes=little32(header+20);
    if (!count || count>16384 || !strings || bytes>1048576 || strings>bytes ||
        count*stride>bytes || strings!=bytes-count*stride) goto done;
    data=(unsigned char*)malloc(bytes);if (!data) goto done;
    if (fread(data,1,bytes,fp)!=bytes || fgetc(fp)!=EOF) goto done;
    for (i=0;i<bytes;i++) hash=(hash^data[i])*16777619u;
    if (hash!=little32(header+24)) goto done;
    for (i=0;i<count;i++) {
        const unsigned char *record=data+i*stride;uint32_t offset=little32(record);H1ImageInfo info;
        const char *name;
        if (offset>=strings) goto done;
        name=(const char*)data+count*stride+offset;
        if (!memchr(name,0,strings-offset) || !*name || (previous && strcmp(previous,name)>=0)) goto done;
        index_info(record,stride,&info);if (!valid_info(&info)) goto done;
        previous=name;
    }
    metadata_data=data;metadata_count=count;metadata_record_bytes=stride;metadata_state=1;data=0;
done:
    free(data);fclose(fp);
}
static int metadata_variant(const char *logical,const char *extension,H1ImageInfo *info) {
    char path[640];uint32_t low=0,high=metadata_count;const char *key;
    if (metadata_state!=1 || !make_path(path,sizeof(path),logical,extension)) return 0;
    key=path+strlen(resource_root);
    while (low<high) {
        uint32_t middle=low+(high-low)/2;const unsigned char *record=metadata_data+middle*metadata_record_bytes;
        const char *name=(const char*)metadata_data+metadata_count*metadata_record_bytes+little32(record);
        int order=strcmp(key,name);
        if (!order) { index_info(record,metadata_record_bytes,info);return 1; }
        if (order<0) high=middle;else low=middle+1;
    }
    return 0;
}
static int open_variant(H1Image *image,const char *logical,const char *ext,uint32_t color) {
    char path[640]; unsigned char header[H1I2_HEADER_BYTES]; FILE *fp; uint32_t expected;
    if (!make_path(path,sizeof(path),logical,ext)) return 0;
    fp=fopen(path,"rb");
    if (!fp) return 0;
    if (fread(header,1,H1I_HEADER_BYTES,fp)!=H1I_HEADER_BYTES ||
        (memcmp(header,"H1I1",4) && memcmp(header,"H1I2",4))) {
        fclose(fp); return 0;
    }
    image->info.width=little32(header+4); image->info.height=little32(header+8);
    image->info.channels=little32(header+12); image->info.flags=little32(header+16);
    image->info.pixel_bytes=little32(header+20);
    image->payload_offset=H1I_HEADER_BYTES;
    image->info.storage_width=image->info.width;image->info.storage_height=image->info.height;
    if (!memcmp(header,"H1I2",4)) {
        if (fread(header+H1I_HEADER_BYTES,1,8,fp)!=8) { fclose(fp);return 0; }
        image->payload_offset=H1I2_HEADER_BYTES;
        image->info.storage_width=little32(header+24);image->info.storage_height=little32(header+28);
    }
    if (!valid_info(&image->info)) { fclose(fp);return 0; }
    expected=image->info.pixel_bytes;
    image->file=fp; image->alpha_color=color&0xffffffu;
    /* This proof comes from the actual opened header, never the optional
     * metadata index. fread above left the stream at payload byte zero. */
    image->rgb_source_bytes=(image->info.channels==3 && image->info.flags==0)?expected:0;
    image->rgb_next_byte=0;image->rgb_stream_valid=1;
    return 1;
}
int h1_image_open(H1Image *image,const char *logical,int auto_alpha,uint32_t color) {
    char path[640];int tried_image=0;H1ImageInfo indexed;
    if (!image) return 0;
    memset(image,0,sizeof(*image));
    if (auto_alpha) {
        metadata_init();
        /* The index already determines h1_image_probe's preferred variant.
         * If it records only the ordinary image, avoid a slow negative alpha
         * lookup in the native FAT directory. Both variants remain separate
         * records, so a recorded alpha image retains its original priority.
         * Open and validate the actual file; a stale/missing preferred image
         * still falls back to the unindexed alpha variant. An alpha image
         * added beside a valid indexed ordinary image requires an index update
         * to take priority, matching the existing metadata-probe contract. */
        if (!metadata_variant(logical,".h1a",&indexed) &&
            metadata_variant(logical,".h1i",&indexed)) {
            tried_image=1;
            if (open_variant(image,logical,".h1i",color)) return 1;
        }
    }
    if (auto_alpha && open_variant(image,logical,".h1a",color)) return 1;
    if (!tried_image && open_variant(image,logical,".h1i",color)) return 1;
    /* The macOS resource tree omits its publisher-brand placeholder. Keep this
     * one optional slot transparent, explicitly marked as a platform fallback;
     * never substitute a different publisher's artwork or hide other misses. */
    if (make_path(path,sizeof(path),logical,".h1i") &&
        !strcmp(path+strlen(resource_root),"properties/partner_logo.h1i")) {
        memset(image,0,sizeof(*image));
        image->info.width=image->info.height=1;
        image->info.storage_width=image->info.storage_height=1;
        image->info.channels=4; image->info.flags=H1_IMAGE_OPTIONAL_TRANSPARENT;
        image->info.pixel_bytes=4;
        image->payload_offset=H1I_HEADER_BYTES;
        if (!optional_logo_reported) {
            const char *message="H1PVZ: optional properties/partner_logo is absent; explicit transparent publisher-slot fallback.\n";
            if (resource_logger) resource_logger(message); else fputs(message,stderr);
            optional_logo_reported=1;
        }
        return 1;
    }
    return 0;
}
void h1_image_close(H1Image *image) {
    if (image && image->file) { fclose((FILE*)image->file); image->file=0; }
    if (image) image->rgb_source_bytes=image->rgb_next_byte=image->rgb_stream_valid=0;
}
int h1_image_load_storage_alpha(H1Image *image,unsigned char *alpha,size_t capacity) {
    if(!image||!image->file||!alpha||image->info.channels!=1||image->info.flags!=H1_IMAGE_MASK_ONLY)return 0;
    const uint64_t area=(uint64_t)image->info.storage_width*image->info.storage_height;
    if(!area||area!=image->info.pixel_bytes||area>capacity)return 0;
    FILE *fp=(FILE*)image->file;image->rgb_stream_valid=0;
    return !fseek(fp,(long)image->payload_offset,SEEK_SET)&&fread(alpha,1,(size_t)area,fp)==area;
}
static void decode_pixels(const H1Image *image,const unsigned char *buffer,uint32_t count,uint32_t *argb) {
    uint32_t i,channels=image->info.channels;
    for (i=0;i<count;i++) {
        const unsigned char *p=buffer+i*channels;
        if (channels==1) argb[i]=image->alpha_color|((uint32_t)p[0]<<24);
        else argb[i]=((uint32_t)(channels==4?p[3]:255)<<24)|((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2];
    }
}
int h1_image_probe(const char *path,int auto_alpha,H1ImageInfo *info) {
    H1Image image;
    if (!info) return 0;
    metadata_init();
    if ((auto_alpha && metadata_variant(path,".h1a",info)) || metadata_variant(path,".h1i",info)) return 1;
    /* An index miss must never hide a real file (e.g. after a partial update).
     * Missing/corrupt index and unknown paths retain the complete old loader. */
    if (!h1_image_open(&image,path,auto_alpha,0xffffffu)) return 0;
    *info=image.info; h1_image_close(&image); return 1;
}
int h1_image_read_storage_span(H1Image *image,uint32_t x,uint32_t y,uint32_t count,uint32_t *argb) {
    unsigned char buffer[3072]; uint32_t channels,offset,n,i; FILE *fp;
    if (image) image->rgb_stream_valid=0;
    if (!image || (!argb && count) || y>=image->info.storage_height ||
        x>image->info.storage_width || count>image->info.storage_width-x) return 0;
    if (image->info.flags&H1_IMAGE_OPTIONAL_TRANSPARENT) {
        for (i=0;i<count;i++) argb[i]=0;
        return 1;
    }
    if (!image->file) return 0;
    fp=(FILE*)image->file; channels=image->info.channels;
    offset=image->payload_offset+(y*image->info.storage_width+x)*channels;
    if (fseek(fp,(long)offset,SEEK_SET)) return 0;
    while (count) {
        n=count>sizeof(buffer)/channels?(uint32_t)(sizeof(buffer)/channels):count;
        if (fread(buffer,channels,n,fp)!=n) return 0;
        decode_pixels(image,buffer,n,argb);
        count-=n; argb+=n;
    }
    return 1;
}
int h1_image_load_storage(H1Image *image,uint32_t *argb,size_t capacity) {
    unsigned char buffer[3072];uint32_t count,n,channels;FILE *fp;
    if (image) image->rgb_stream_valid=0;
    if (!image || !argb || capacity<(size_t)image->info.storage_width*image->info.storage_height) return 0;
    count=image->info.storage_width*image->info.storage_height;
    if (image->info.flags&H1_IMAGE_OPTIONAL_TRANSPARENT) {
        memset(argb,0,(size_t)count*sizeof(*argb));return 1;
    }
    if (!image->file) return 0;
    fp=(FILE*)image->file;channels=image->info.channels;
    if (fseek(fp,(long)image->payload_offset,SEEK_SET)) return 0;
    /* One sequential stream avoids hundreds of native filesystem seeks for
     * full-screen images. Span reads remain available for bounded callers. */
    while (count) {
        n=count>sizeof(buffer)/channels?(uint32_t)(sizeof(buffer)/channels):count;
        if (fread(buffer,channels,n,fp)!=n) return 0;
        decode_pixels(image,buffer,n,argb);argb+=n;count-=n;
    }
    return 1;
}
int h1_image_read_span(H1Image *image,uint32_t x,uint32_t y,uint32_t count,uint32_t *argb) {
    uint32_t pixels[768],sy,lx,end;
    if (!image || !argb || y>=image->info.height || x>image->info.width || count>image->info.width-x) return 0;
    if (image->info.width==image->info.storage_width && image->info.height==image->info.storage_height)
        return h1_image_read_storage_span(image,x,y,count,argb);
    sy=y*image->info.storage_height/image->info.height;lx=x;end=x+count;
    while (lx<end) {
        uint32_t first=lx*image->info.storage_width/image->info.width;
        uint32_t last=(end-1u)*image->info.storage_width/image->info.width;
        uint32_t n=last-first+1u;if(n>768u)n=768u;
        if (!h1_image_read_storage_span(image,first,sy,n,pixels)) return 0;
        while (lx<end) {
            uint32_t sx=lx*image->info.storage_width/image->info.width;
            if (sx-first>=n) break;
            *argb++=pixels[sx-first];++lx;
        }
    }
    return 1;
}
int h1_image_load_all(H1Image *image,uint32_t *argb,size_t capacity) {
    uint32_t y;
    if (!image || !argb || capacity<(size_t)image->info.width*image->info.height) return 0;
    if (image->info.width==image->info.storage_width && image->info.height==image->info.storage_height)
        return h1_image_load_storage(image,argb,capacity);
    for(y=0;y<image->info.height;++y)
        if(!h1_image_read_span(image,0,y,image->info.width,argb+(size_t)y*image->info.width))return 0;
    return 1;
}
int h1_image_read_rgb_bytes(H1Image *image,uint32_t offset,uint32_t count,unsigned char *rgb) {
    FILE *fp;
    if (!image || !image->file || !image->rgb_source_bytes ||
        image->info.channels!=3 || image->info.flags!=0 ||
        image->info.pixel_bytes!=image->rgb_source_bytes ||
        (uint64_t)image->info.storage_width*image->info.storage_height*3u!=image->rgb_source_bytes ||
        offset%3u || count%3u || offset>image->rgb_source_bytes ||
        count>image->rgb_source_bytes-offset || (count && !rgb)) return 0;
    if (!count) return 1;
    fp=(FILE*)image->file;
    if (!image->rgb_stream_valid || image->rgb_next_byte!=offset) {
        image->rgb_stream_valid=0;
        clearerr(fp);
        if (fseek(fp,(long)(image->payload_offset+offset),SEEK_SET)) return 0;
    }
    if (fread(rgb,1,count,fp)!=count) { image->rgb_stream_valid=0;return 0; }
    image->rgb_next_byte=offset+count;image->rgb_stream_valid=1;
    return 1;
}
int h1_image_read_rgb_span(H1Image *image,uint32_t first,uint32_t count,unsigned char *rgb) {
    if (first>UINT32_MAX/3u || count>UINT32_MAX/3u) return 0;
    return h1_image_read_rgb_bytes(image,first*3u,count*3u,rgb);
}
