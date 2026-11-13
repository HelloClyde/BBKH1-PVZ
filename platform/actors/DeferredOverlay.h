/* Exact lazy matrices for leaf plant attachments. LGPL-3.0-or-later. */
#pragma once
#include <cstddef>
class Reanimation;
class ReanimatorDefinition;
bool H1DeferredOverlayPrepare();
bool H1DeferredAttachmentUpdate(Reanimation*,int track);
void H1ResolveActorOverlay(Reanimation*);
void H1DropActorOverlay(Reanimation*);
void H1DeferredOverlayForget(Reanimation*);
void H1DeferredOverlayForgetDefinition(ReanimatorDefinition*);
void H1DeferredOverlayFlush();
void H1DeferredOverlayShutdown();
std::size_t H1DeferredOverlayResident();
#ifdef PVZ_HOST_RUNNER
void H1DeferredOverlayTestEnable(bool);
#endif
