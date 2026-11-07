/* Conservative absence hints; present supports keep the original query. */
#pragma once
#include "ConstEnums.h"
class Board;
void H1BoardSupportChanged();
bool H1BoardMayHaveSupport(Board*,SeedType);
#ifdef PVZ_HOST_RUNNER
void H1BoardLookupTestEnable(bool);
#endif
