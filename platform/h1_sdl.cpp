/* LGPL-3.0-or-later. Native H1 services used by the unmodified game engine. */
#include "SDL.h"
#include "h1_host.h"
#include <stdio.h>
#include <stdarg.h>

static SDL_Cursor default_cursor;
static char *copy_string(const char *p){size_t n=strlen(p)+1;char *s=(char*)malloc(n);if(s)memcpy(s,p,n);return s;}
extern "C" {
int SDL_Init(Uint32){return 0;}
Uint32 SDL_GetTicks(void){return pvzp_ticks_ms();}
Uint64 SDL_GetPerformanceCounter(void){return pvzp_ticks_ms();}
Uint64 SDL_GetPerformanceFrequency(void){return 1000;}
void SDL_Delay(Uint32 ms){Uint32 start=SDL_GetTicks();while(SDL_GetTicks()-start<ms)pvzp_audio_pump();}
char *SDL_GetBasePath(void){const char *p=getenv("PVZ_RESOURCE_DIR");return copy_string(p?p:"/PVZ/");}
char *SDL_GetPrefPath(const char *,const char *){const char *p=getenv("PVZ_SAVE_DIR");return copy_string(p?p:"/PVZSAVE/");}
void SDL_LogMessage(int,int,const char *fmt,...){char b[768];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);pvzp_log_line(b);}
void SDL_Log(const char *fmt,...){char b[768];va_list a;va_start(a,fmt);vsnprintf(b,sizeof(b),fmt,a);va_end(a);pvzp_log_line(b);}
int SDL_ShowSimpleMessageBox(Uint32,const char *title,const char *message,SDL_Window *){pvzp_log_line(title);pvzp_log_line(message);pvzp_diagnostic[3]=1;return 0;}
SDL_Cursor *SDL_CreateSystemCursor(SDL_SystemCursor){return new SDL_Cursor{};}
SDL_Cursor *SDL_CreateColorCursor(SDL_Surface *,int,int){return new SDL_Cursor{};}
SDL_Cursor *SDL_GetDefaultCursor(void){return &default_cursor;}
void SDL_FreeCursor(SDL_Cursor *p){if(p!=&default_cursor)delete p;}
void SDL_SetCursor(SDL_Cursor *){}
int SDL_ShowCursor(int toggle){return toggle;}
SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *p,int w,int h,int,int pitch,Uint32){return new SDL_Surface{p,w,h,pitch};}
void SDL_FreeSurface(SDL_Surface *p){delete p;}
void SDL_MinimizeWindow(SDL_Window *){}
int SDL_SetClipboardText(const char *){return -1;}
char *SDL_GetClipboardText(void){return copy_string("");}
size_t SDL_strlcpy(char *d,const char *s,size_t n){size_t len=strlen(s);if(n){size_t k=len<n-1?len:n-1;memcpy(d,s,k);d[k]=0;}return len;}
SDL_RWops *SDL_RWFromMem(void *p,int n){return new SDL_RWops{p,n};}
SDL_RWops *SDL_RWFromConstMem(const void *p,int n){return new SDL_RWops{const_cast<void*>(p),n};}
const char *SDL_GetError(void){return "H1 native backend";}
char *SDL_iconv_string(const char *to,const char *from,const char *s,size_t n){
    if(strcmp(to,"UTF-8")!=0)return nullptr;
    char *out=(char*)malloc(n*2+4);if(!out)return nullptr;size_t used=0;
    if(strcmp(from,"UTF-16LE")==0||strcmp(from,"UTF-16BE")==0){
        int be=strcmp(from,"UTF-16BE")==0;
        for(size_t i=0;i+1<n;i+=2){const unsigned char *p=(const unsigned char*)s+i;unsigned c=be?(p[0]<<8|p[1]):(p[1]<<8|p[0]);
            if(c>=0xd800&&c<=0xdbff&&i+3<n){p+=2;unsigned d=be?(p[0]<<8|p[1]):(p[1]<<8|p[0]);if(d>=0xdc00&&d<=0xdfff){c=0x10000+((c-0xd800)<<10)+(d-0xdc00);i+=2;}}
            if(c<128)out[used++]=(char)c;
            else if(c<2048){out[used++]=(char)(0xc0|(c>>6));out[used++]=(char)(0x80|(c&63));}
            else if(c<65536){out[used++]=(char)(0xe0|(c>>12));out[used++]=(char)(0x80|((c>>6)&63));out[used++]=(char)(0x80|(c&63));}
            else{out[used++]=(char)(0xf0|(c>>18));out[used++]=(char)(0x80|((c>>12)&63));out[used++]=(char)(0x80|((c>>6)&63));out[used++]=(char)(0x80|(c&63));}
        }
    }else{memcpy(out,s,n);used=n;}
    out[used]=out[used+1]=out[used+2]=out[used+3]=0;return out;
}
}
