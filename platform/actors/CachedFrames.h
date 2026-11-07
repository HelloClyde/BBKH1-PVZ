#pragma once
#include <memory>
namespace Sexy {class MemoryImage;}
std::unique_ptr<Sexy::MemoryImage> H1CachedFrameLoad(int kind,int id,int variation=0);
void H1CachedFramesPrepare(const bool* plants,const bool* filtered,bool packetsChosen);
void H1CachedFramesRelease();
#ifdef PVZ_HOST_RUNNER
bool H1CachedFramesTestEnabled();
void H1CachedFramesTestEnable(bool);
#endif
