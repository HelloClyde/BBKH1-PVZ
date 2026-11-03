/* LGPL-3.0-or-later. H1 FAT filenames already compare case-insensitively. */
#include "fcaseopen/fcaseopen.h"
#include <string>
#include <unistd.h>
extern "C" FILE *fcaseopen(const char *path,const char *mode){return fopen(path,mode);}
extern "C" FILE *fcaseopenat(const char *base,const char *path,const char *mode){
    if(path[0]=='/'||!base||!base[0])return fopen(path,mode);
    std::string name(base);if(name.back()!='/')name+='/';name+=path;return fopen(name.c_str(),mode);
}
extern "C" int casechdir(const char *path){return chdir(path);}
