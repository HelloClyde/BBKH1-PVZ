#ifndef PVZ_H1_MIXER_H
#define PVZ_H1_MIXER_H
#include "SDL.h"
#define MIX_MAX_VOLUME 128
#define AUDIO_S16SYS 0x8010
typedef struct Mix_Chunk { char path[256]; uint32_t samples,rate; const int16_t *cached; } Mix_Chunk;
typedef struct Mix_Music Mix_Music;
#ifdef __cplusplus
extern "C" {
#endif
Mix_Music *Mix_LoadMUS(const char *);
Mix_Music *Mix_LoadMUS_RW(SDL_RWops *,int);
void Mix_FreeMusic(Mix_Music *);
int Mix_PlayMusicStream(Mix_Music *,int loops);
int Mix_HaltMusicStream(Mix_Music *);
int Mix_HaltMusic(void);
void Mix_PauseMusicStream(Mix_Music *);
void Mix_ResumeMusicStream(Mix_Music *);
int Mix_PlayingMusicStream(Mix_Music *);
int Mix_PausedMusicStream(Mix_Music *);
int Mix_VolumeMusicStream(Mix_Music *,int volume);
int Mix_VolumeMusic(int volume);
int Mix_VolumeMusicGeneral(int volume);
int Mix_ModMusicStreamJumpToOrder(Mix_Music *,int order);
int Mix_ModMusicStreamGetOrder(Mix_Music *,int *order);
int Mix_ModMusicStreamSetChannelVolume(Mix_Music *,int channel,int volume);
#ifdef __cplusplus
}
#endif
#endif
