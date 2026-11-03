#ifndef PVZ_H1_SDL_COMPAT_H
#define PVZ_H1_SDL_COMPAT_H
/* The upstream engine only: H1 replacements for timing/logging/cursors.
   This is not an SDL emulator. Window, input and rasterization are native. */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint8_t Uint8; typedef uint16_t Uint16; typedef uint32_t Uint32; typedef uint64_t Uint64;
typedef int8_t Sint8; typedef int16_t Sint16; typedef int32_t Sint32; typedef int64_t Sint64;
typedef struct SDL_Window { int unused; } SDL_Window;
typedef struct SDL_Cursor { int unused; } SDL_Cursor;
typedef struct SDL_Surface { void *pixels; int w,h,pitch; } SDL_Surface;
typedef struct SDL_Rect { int x,y,w,h; } SDL_Rect;
typedef struct SDL_RWops { void *memory; int size; } SDL_RWops;
typedef enum SDL_SystemCursor { SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_IBEAM,
 SDL_SYSTEM_CURSOR_WAIT,SDL_SYSTEM_CURSOR_CROSSHAIR,SDL_SYSTEM_CURSOR_WAITARROW,
 SDL_SYSTEM_CURSOR_SIZENWSE,SDL_SYSTEM_CURSOR_SIZENESW,SDL_SYSTEM_CURSOR_SIZEWE,
 SDL_SYSTEM_CURSOR_SIZENS,SDL_SYSTEM_CURSOR_SIZEALL,SDL_SYSTEM_CURSOR_NO,SDL_SYSTEM_CURSOR_HAND } SDL_SystemCursor;
#define SDL_INIT_TIMER 1u
#define SDL_ENABLE 1
#define SDL_DISABLE 0
#define SDL_MESSAGEBOX_ERROR 1
#define SDL_PIXELFORMAT_BGRA32 1
#define SDL_LOG_CATEGORY_APPLICATION 0
#define SDL_LOG_PRIORITY_INFO 0
#define SDL_LOG_PRIORITY_ERROR 1
#define SDL_malloc malloc
#define SDL_calloc calloc
#define SDL_realloc realloc
#define SDL_free free
#define SDL_memcpy memcpy
#define SDL_memset memset
int SDL_Init(Uint32 flags);
Uint32 SDL_GetTicks(void);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);
void SDL_Delay(Uint32 ms);
char *SDL_GetBasePath(void);
char *SDL_GetPrefPath(const char *,const char *);
void SDL_LogMessage(int category,int priority,const char *fmt,...);
void SDL_Log(const char *fmt,...);
int SDL_ShowSimpleMessageBox(Uint32 flags,const char *title,const char *message,SDL_Window *window);
SDL_Cursor *SDL_CreateSystemCursor(SDL_SystemCursor id);
SDL_Cursor *SDL_CreateColorCursor(SDL_Surface *,int,int);
SDL_Cursor *SDL_GetDefaultCursor(void);
void SDL_FreeCursor(SDL_Cursor *);
void SDL_SetCursor(SDL_Cursor *);
int SDL_ShowCursor(int toggle);
SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels,int w,int h,int depth,int pitch,Uint32 format);
void SDL_FreeSurface(SDL_Surface *);
void SDL_MinimizeWindow(SDL_Window *);
int SDL_SetClipboardText(const char *);
char *SDL_GetClipboardText(void);
size_t SDL_strlcpy(char *,const char *,size_t);
char *SDL_iconv_string(const char *,const char *,const char *,size_t);
SDL_RWops *SDL_RWFromMem(void *,int);
SDL_RWops *SDL_RWFromConstMem(const void *,int);
const char *SDL_GetError(void);
#ifdef __cplusplus
}
#endif
#endif
