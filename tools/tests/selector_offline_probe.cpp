/* Real runtime loader/replay tests. No game or device services are executed. */
#include "../../platform/selector/SelectorOffline.cpp"
#include <vector>
#include <string>
#include <cstdlib>
#include <iostream>
extern "C" void pvzp_audio_service(){} // No audio device exists in this isolated loader test.
namespace {
unsigned checks=0;
void Check(bool value,const char* what){++checks;if(!value){std::cerr<<what<<"\n";std::exit(1);}}
void Put(std::vector<unsigned char>& bytes,size_t at,uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=(unsigned char)(value>>(i*8));}
std::vector<unsigned char> Bundle(){
    std::vector<unsigned char> bytes(64+40+3*12+6*4);
    uint32_t header[16]={Magic,1,64,(uint32_t)bytes.size(),Width,Height,800,600,1,64,40,40,48,0,0,0};
    for(unsigned i=0;i<16;++i)Put(bytes,i*4,header[i]);
    uint32_t entry[10]={1,5,0,0xffffffff,3,6,104,60,0,0};
    for(unsigned i=0;i<10;++i)Put(bytes,64+i*4,entry[i]);
    // Ordered overlap: opaque three pixels, partial two, opaque final pixel.
    uint32_t runs[9]={7,3,0,8,0x10002,3,9,1,5};
    for(unsigned i=0;i<9;++i)Put(bytes,104+i*4,runs[i]);
    uint32_t pixels[6]={0xff123456,0xffabcdef,0xff987654,0x7f38270e,0x40261701,0xff010203};
    for(unsigned i=0;i<6;++i)Put(bytes,140+i*4,pixels[i]);
    return bytes;
}
void Write(const std::string& path,const std::vector<unsigned char>& bytes){
    FILE* f=std::fopen(path.c_str(),"wb");Check(f!=nullptr,"create fixture");
    Check(std::fwrite(bytes.data(),1,bytes.size(),f)==bytes.size(),"write fixture");std::fclose(f);
}
uint32_t Partial(uint32_t p,uint32_t literal){
    unsigned inverse=literal>>24;
    return 0xff000000u|((literal&0xffffffu)+((((p&0xff00ffu)*inverse)>>8)&0xff00ffu)+((((p&0xff00u)*inverse)>>8)&0xff00u));
}
}
int main(int argc,char** argv){
    Check(argc==2,"fixture path argument");std::string path=argv[1];auto valid=Bundle();Write(path,valid);
    Check(Load(path.c_str()),"valid bundle");Check(state.loaded&&state.count==1,"loaded metadata");
    std::vector<uint32_t> pixels(Pixels),expected(Pixels);
    for(unsigned trial=0;trial<64;++trial){
        for(unsigned i=0;i<Pixels;++i)pixels[i]=expected[i]=0xff000000u|((i*1664525u+trial*1013904223u)&0xffffffu);
        expected[7]=0xff123456;expected[8]=0xffabcdef;expected[9]=0xff987654;
        expected[8]=Partial(expected[8],0x7f38270e);expected[9]=Partial(expected[9],0x40261701);expected[9]=0xff010203;
        Replay(state.entries[0],pixels.data());Check(pixels==expected,"literal exact ordered /256 replay");
    }
    Clear();Check(state.resident==0&&!state.entries&&!state.loaded,"release all runtime-owned buffers");
    struct Bad{unsigned offset;uint32_t value;const char* reason;};
    const Bad bads[]={
        {0,0,"wrong magic"},{4,2,"wrong version"},{12,1,"wrong file size"},{16,800,"wrong target"},
        {32,0,"empty directory"},{32,1025,"directory bound"},{36,0,"directory offset"},{40,41,"directory stride"},
        {48,129,"track bound"},{64,4,"unknown kind"},{80,97200*4+1,"run bound"},
        {88,0,"entry overlaps directory"},{92,59,"payload byte mismatch"},{104,359,"run crosses target row"},
        {108,0,"zero run"},{108,0x20003,"unknown run kind"},{112,6,"payload overrun"},
        {140,0x00123456,"opaque must retain alpha255"}};
    for(const auto& bad:bads){auto bytes=valid;Put(bytes,bad.offset,bad.value);Write(path,bytes);Check(!Load(path.c_str()),bad.reason);Check(!state.entries&&!state.resident,"failed load owns nothing");}
    for(unsigned size=0;size<valid.size();++size){auto bytes=valid;bytes.resize(size);Write(path,bytes);Check(!Load(path.c_str()),"all truncated input boundaries reject");}
    Write(path,valid);Check(Load(path.c_str()),"reload after all failures");Clear();
    std::cout<<"{\"passed\":true,\"checks\":"<<checks<<",\"runtime_resident\":"<<state.resident<<"}\n";
}
