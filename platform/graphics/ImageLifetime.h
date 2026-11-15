/* Explicit owners of immutable image storage. LGPL-3.0-or-later. */
#pragma once
#include <cstddef>
#include <cstdint>
namespace Sexy {
class MemoryImage;
class WidgetContainer;
enum class H1ImageOwner : unsigned {Scene,Widget,Animation,Particle,Trail,CommonUI,Operation};
enum class H1ImageScene : unsigned {Startup,Menu,Board,Award,Credits,Opening,Closed};
// A scope selects the actual user of subsequent image accesses. Its resources
// remain shared until ReleaseOwner, object destruction or that scene's exit.
class H1ImageUseScope {
    void* previous_;
    uintptr_t previousObject_;
    unsigned previousKind_;
    uint64_t previousSerial_,epoch_,scene_;
public:
    H1ImageUseScope(H1ImageOwner kind,const void* object);
    ~H1ImageUseScope();
    H1ImageUseScope(const H1ImageUseScope&)=delete;
    H1ImageUseScope& operator=(const H1ImageUseScope&)=delete;
};
void H1ImageResourcesBeginScene(H1ImageScene scene);
void H1ImageResourcesReleaseOwner(H1ImageOwner kind,const void* object);
void H1ImageResourcesReleaseWidgetTree(WidgetContainer* widget);
void H1ImageResourcesShutdown();
struct H1ImageLifetimeStats {
    size_t owners,references,ownedSources,residentBytes;
    uint64_t sceneGeneration,ownerReleases,sourceReleases,releasedBytes;
    H1ImageScene scene;
};
H1ImageLifetimeStats H1ImageResourcesStats();
unsigned H1ImageResourceOwnerCount(MemoryImage* image);
}
