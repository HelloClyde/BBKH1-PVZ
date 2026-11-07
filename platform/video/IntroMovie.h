#pragma once
#include <stdint.h>
class Board;
namespace Sexy {class Graphics;}
bool H1IntroMovieRequired();
bool H1IntroMoviePrepare(Board* board);
bool H1IntroMovieActive(Board* board);
bool H1IntroMovieDraw(Board* board,Sexy::Graphics* graphics);
void H1IntroMovieRelease(Board* board=nullptr);
void H1IntroMovieCompleted(Board* board);
const uint16_t* H1IntroMovieNativePixels(uint32_t* generation,uint32_t* frame);
void H1IntroMoviePresented();
#ifdef PVZ_HOST_RUNNER
void H1IntroMovieTestEnable(bool enabled);
#endif
