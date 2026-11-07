/* Offline opaque early-lawn composites. LGPL-3.0-or-later. */
#pragma once
class Board;namespace Sexy{class Graphics;}
bool H1EarlyLawnPrepare(Board*);
bool H1EarlyLawnDraw(Board*,Sexy::Graphics*);
extern "C" void pvzh1_early_lawn_shutdown();
#ifdef PVZ_HOST_RUNNER
void H1EarlyLawnTestEnable(bool);
#endif
