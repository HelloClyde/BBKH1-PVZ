/* Fixed ASCII definition tag; avoid a locale lookup on every track update. */
#pragma once
inline bool H1IsAttacherTrack(const char* name){
 return (name[0]|32)=='a'&&(name[1]|32)=='t'&&(name[2]|32)=='t'&&
        (name[3]|32)=='a'&&(name[4]|32)=='c'&&(name[5]|32)=='h'&&
        (name[6]|32)=='e'&&(name[7]|32)=='r'&&name[8]=='_'&&name[9]=='_';
}
