/* Exact changed-block updates while the native fullscreen window owns its
 * surface. Cached source pages are ordinary owned heap memory; every LCD
 * store remains volatile. LGPL-3.0-or-later. */
#pragma once
#include "PresentationMap.h"
#include <new>
#include <cstring>

struct H1DirtyPresentation {
    static constexpr unsigned Width=Sexy::H1ScreenWidth,Height=Sexy::H1ScreenHeight,
        RowsPerPage=Sexy::H1TileHeight,PageCount=Height/RowsPerPage,Block=Sexy::H1TileWidth;
    uint32_t* pages[PageCount]{};
    volatile uint32_t* surface=nullptr;
    bool attempted=false,valid=false,oldCursor=false;
    int oldX=0,oldY=0;
    uint32_t fullFrames=0,partialFrames=0,changedBlocks=0,skippedBlocks=0,allocationFailures=0;
    uint32_t partialPixelStores=0;
#ifdef PVZH1_DIRTY_PRESENT_TEST
    int failAfter=-1;
#endif
    bool Prepare(){
        if(attempted)return pages[PageCount-1]!=nullptr;
        attempted=true;
        for(unsigned i=0;i<PageCount;++i){
#ifdef PVZH1_DIRTY_PRESENT_TEST
            if(failAfter==0)pages[i]=nullptr;
            else {if(failAfter>0)--failAfter;
#endif
            pages[i]=new(std::nothrow) uint32_t[RowsPerPage*Width];
#ifdef PVZH1_DIRTY_PRESENT_TEST
            }
#endif
            if(!pages[i]){
                for(unsigned j=0;j<i;++j){delete[] pages[j];pages[j]=nullptr;}
                ++allocationFailures;return false;
            }
        }
        return true;
    }
    void Reset(){
        for(auto*& page:pages){delete[] page;page=nullptr;}
        surface=nullptr;valid=attempted=oldCursor=false;
    }
    uint32_t* Row(unsigned y){return pages[y/RowsPerPage]+(y%RowsPerPage)*Width;}
    void RestorePoint(volatile uint32_t* out,const uint32_t* pixels,const H1PresentationMap& map,int x,int y){
        if(x<0||x>=480||y<0||y>=272)return;
        uint32_t color=0x101b14u;
        if(x>=59&&x<421)color=pixels[map.row[y]+map.x[x-59]]&0xffffffu;
        out[y*480+x]=color;
    }
    static __attribute__((always_inline)) inline unsigned UpdateFour(volatile uint32_t* out,const uint32_t* input,uint32_t* previous){
        const uint32_t a=input[0],b=input[1],c=input[2],d=input[3];
        const uint32_t da=(a^previous[0])&0xffffffu,db=(b^previous[1])&0xffffffu,
            dc=(c^previous[2])&0xffffffu,dd=(d^previous[3])&0xffffffu;
        if(!(da|db|dc|dd))return 0;
        unsigned stores=0;
        if(da){previous[0]=a;out[0]=a&0xffffffu;++stores;}
        if(db){previous[1]=b;out[1]=b&0xffffffu;++stores;}
        if(dc){previous[2]=c;out[2]=c&0xffffffu;++stores;}
        if(dd){previous[3]=d;out[3]=d&0xffffffu;++stores;}
        return stores;
    }
    bool Present(volatile uint32_t* out,const uint32_t* pixels,int width,int height,int px,int py,H1PresentationMap& map,const uint32_t* changedTiles=nullptr){
#ifdef PVZH1_WIDESCREEN
        if(!out||!pixels||!map.Configure(width,height)||!H1CanPresent480(out,pixels,width,height,map)||!Prepare()){
            valid=false;return false;
        }
        bool fullDamage=changedTiles!=nullptr;
        if(fullDamage)for(unsigned row=0;row<PageCount;++row)
            if((changedTiles[row]&0x3fffffffu)!=0x3fffffffu){fullDamage=false;break;}
        if(fullDamage){
            // A full-screen effect dirties every tile. Copy directly rather
            // than compare and maintain an equally large previous picture.
            H1PresentFixed480(out,pixels);++fullFrames;surface=out;valid=false;
        }else if(!valid||surface!=out){
            H1PresentFixed480(out,pixels);
            for(unsigned y=0;y<Height;++y)std::memcpy(Row(y),pixels+y*Width,Width*4u);
            ++fullFrames;valid=true;surface=out;
        }else{
            if(oldCursor)for(int d=-3;d<=3;++d){
                for(unsigned axis=0;axis<2;++axis){const int x=oldX+(axis?0:d),y=oldY+(axis?d:0);
                    if(x>=0&&x<480&&y>=0&&y<272)out[y*480+x]=pixels[y*480+x]&0xffffffu;
                }
            }
            uint32_t changed=0,skipped=0,stores=0;
            for(unsigned y=0;y<Height;++y){
                const uint32_t* input=pixels+y*Width;uint32_t* previous=Row(y);volatile uint32_t* output=out+y*Width;
                for(unsigned x=0;x<Width;x+=Block){
                    if(changedTiles&&!(changedTiles[y/RowsPerPage]&(uint32_t(1)<<(x/Block)))){++skipped;continue;}
                    const unsigned n=UpdateFour(output+x,input+x,previous+x)+
                        UpdateFour(output+x+4,input+x+4,previous+x+4)+
                        UpdateFour(output+x+8,input+x+8,previous+x+8)+
                        UpdateFour(output+x+12,input+x+12,previous+x+12);
                    if(n){++changed;stores+=n;}else ++skipped;
                }
            }
            changedBlocks+=changed;skippedBlocks+=skipped;partialPixelStores+=stores;++partialFrames;
        }
        H1PresentCursorMapped(out,px,py,map);
        oldX=(int)((int64_t)px*480/Sexy::H1LogicalWidth);oldY=(int)((int64_t)py*272/Sexy::H1LogicalHeight);
        oldCursor=oldX>=0&&oldX<480&&oldY>=0&&oldY<272;
        return true;
#else
        if(!out||!pixels||!map.Configure(width,height)||!H1CanPresent360(out,pixels,width,height,map)||!Prepare()){
            valid=false;return false;
        }
        if(!valid||surface!=out){
            H1PresentFixed360(out,pixels);
            for(unsigned y=0;y<Height;++y)std::memcpy(Row(y),pixels+y*Width,Width*4u);
            ++fullFrames;valid=true;surface=out;
        }else{
            // The cursor is drawn after presenting and is deliberately absent
            // from the cached picture. Restore its old cross from this frame,
            // including the green margin when it touched a viewport edge.
            if(oldCursor)for(int d=-3;d<=3;++d){
                RestorePoint(out,pixels,map,oldX+d,oldY);
                RestorePoint(out,pixels,map,oldX,oldY+d);
            }
            uint32_t changed=0,skipped=0;
            for(unsigned y=0;y<Height;++y){
                const uint32_t* input=pixels+y*Width;uint32_t* previous=Row(y);
                const unsigned dy=y+1+(y>=135);
                volatile uint32_t* output=out+dy*480;
                volatile uint32_t* duplicate=y==0?out:(y==135?out+136*480:nullptr);
                for(unsigned x=0;x<Width;x+=Block){
                    if(changedTiles&&!(changedTiles[y/RowsPerPage]&(uint32_t(1)<<(x/Block)))){++skipped;continue;}
                    uint32_t difference=0;
                    for(unsigned j=0;j<Block;j+=4){
                        difference|=(input[x+j]^previous[x+j])|(input[x+j+1]^previous[x+j+1])|
                            (input[x+j+2]^previous[x+j+2])|(input[x+j+3]^previous[x+j+3]);
                    }
                    if(!(difference&0xffffffu)){++skipped;continue;}
                    ++changed;const unsigned dx=60+x+(x>=180);
                    for(unsigned j=0;j<Block;++j){
                        const uint32_t value=input[x+j];previous[x+j]=value;
                        output[dx+j]=value&0xffffffu;
                        if(duplicate)duplicate[dx+j]=value&0xffffffu;
                    }
                    if(x==0||x==180){const unsigned edge=x==0?59:240;
                        output[edge]=input[x]&0xffffffu;
                        if(duplicate)duplicate[edge]=input[x]&0xffffffu;
                    }
                }
            }
            changedBlocks+=changed;skippedBlocks+=skipped;++partialFrames;
        }
        H1PresentCursorMapped(out,px,py,map);
        oldX=map.ox+(int)((int64_t)px*map.dw/800);
        oldY=map.oy+(int)((int64_t)py*map.dh/600);
        oldCursor=oldX>=59&&oldX<421&&oldY>=0&&oldY<272;
        return true;
#endif
    }
    unsigned ResidentBytes()const{return pages[PageCount-1]?Width*Height*4u:0;}
};
