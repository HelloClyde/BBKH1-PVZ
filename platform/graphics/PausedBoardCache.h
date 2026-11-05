#pragma once
#include <cstddef>
class Board;
namespace Sexy {class Graphics;}
/* Cache only the paused adventure board. Dialog widgets, their children and
 * updates still execute normally. Dynamic cutscenes reject the cached view. */
bool H1PausedBoardReplay(Board*,Sexy::Graphics*);
void H1PausedBoardCapture(Board*,Sexy::Graphics*);
void H1PausedBoardForget(Board*);
extern "C" void pvzh1_paused_board_shutdown();
extern "C" std::size_t pvzh1_paused_board_release_pressure(std::size_t);
extern "C" volatile unsigned pvzh1_paused_board_diagnostic[4];
#ifdef PVZ_HOST_RUNNER
void H1PausedBoardTestEnable(bool);
#endif
void H1PausedBoardPrime(Board* board);
