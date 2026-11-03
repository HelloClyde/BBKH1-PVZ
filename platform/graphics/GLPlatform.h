#ifndef __GLPLATFORM_H__
#define __GLPLATFORM_H__
/* The software backend retains the public GLInterface ABI but has no GL or
 * SDL graphics dependency. Texture identifiers are never allocated. */
typedef unsigned int GLuint;
extern bool gDesktopGLFallback;
inline void PlatformGLInit() {}
#endif
