/* Exact profile ownership matching, shared by host deletion and SDK worker. */
#ifndef PVZ_PROFILE_FILES_H
#define PVZ_PROFILE_FILES_H
#include <string.h>
static int pvz_profile_prefix(const char *a,const char *b,size_t n){
    while(n--){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;
        if(x>='A'&&x<='Z')x+='a'-'A';if(y>='A'&&y<='Z')y+='a'-'A';
        if(x!=y)return 0;if(!x)return 1;}
    return 1;
}
static int pvz_profile_equal(const char *a,const char *b){size_t n=strlen(b);return strlen(a)==n&&pvz_profile_prefix(a,b,n);}
static int pvz_profile_suffix(const char *p){
    return !*p||pvz_profile_equal(p,".h1tmp")||pvz_profile_equal(p,".h1bak");
}
static int pvz_profile_file_matches(const char *name,const char *user_file,
                                    const char *game_prefix,unsigned modes){
    size_t n=strlen(user_file);
    if(strlen(name)>=n&&pvz_profile_prefix(name,user_file,n)&&pvz_profile_suffix(name+n))return 1;
    n=strlen(game_prefix);if(strlen(name)<n||!pvz_profile_prefix(name,game_prefix,n))return 0;
    const char *p=name+n;if(*p<'0'||*p>'9')return 0;
    if(*p=='0'&&p[1]>='0'&&p[1]<='9')return 0;
    unsigned mode=0;
    while(*p>='0'&&*p<='9'){
        if(mode>=modes)return 0;
        mode=mode*10u+(unsigned)(*p++-'0');
    }
    if(mode>=modes)return 0;
    if(strlen(p)>=3&&pvz_profile_prefix(p,".v4",3))return pvz_profile_suffix(p+3);
    if(strlen(p)>=4&&pvz_profile_prefix(p,".dat",4))return pvz_profile_suffix(p+4);
    return 0;
}
#endif
