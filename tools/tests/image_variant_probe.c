/* Test-only observer: compile the actual reader, preserving its private state.
 * The legacy entry point retains exactly the former variant search order. */
#include <stdio.h>
#include <string.h>
static FILE *variant_fopen(const char *,const char *);
static int variant_fclose(FILE *);
static unsigned open_count,opened_files,closed_files;
static char open_paths[32][1024];
#define fopen variant_fopen
#define fclose variant_fclose
#include "h1_resources.c"
#undef fopen
#undef fclose
static FILE *variant_fopen(const char *path,const char *mode) {
    FILE *file;
    if(open_count<32) {
        strncpy(open_paths[open_count],path,sizeof(open_paths[0])-1);
        open_paths[open_count][sizeof(open_paths[0])-1]=0;
    }
    ++open_count;file=fopen(path,mode);if(file)++opened_files;return file;
}
static int variant_fclose(FILE *file) {++closed_files;return fclose(file);}
void test_reset_open_log(void) {open_count=opened_files=closed_files=0;}
unsigned test_open_count(void) {return open_count;}
const char *test_open_path(unsigned index) {return index<open_count&&index<32?open_paths[index]:"";}
int test_balanced_files(void) {return opened_files==closed_files;}
int test_metadata_state(void) {return metadata_state;}
int test_legacy_open(H1Image *image,const char *path,int alpha,uint32_t color) {
    if(!image)return 0;
    memset(image,0,sizeof(*image));
    return (alpha&&open_variant(image,path,".h1a",color))||open_variant(image,path,".h1i",color);
}
