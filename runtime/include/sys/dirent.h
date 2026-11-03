#ifndef PVZ_H1_DIRENT_H
#define PVZ_H1_DIRENT_H
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct pvz_h1_directory DIR;
struct dirent { unsigned long d_ino; unsigned short d_reclen; unsigned char d_type; char d_name[512]; };
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14
#define MAXNAMLEN 511
DIR *opendir(const char *);
struct dirent *readdir(DIR *);
int closedir(DIR *);
void rewinddir(DIR *);
void seekdir(DIR *,long);
long telldir(DIR *);
int dirfd(DIR *);
DIR *fdopendir(int);
#ifdef __cplusplus
}
#endif
#endif
