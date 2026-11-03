/* Actual original SexyMatrix generic multiply versus the narrow candidate. */
#include "SexyMatrix.h"
#include "PhysicalMatrixScale.h"
#include <stdint.h>
using namespace Sexy;
static inline float fromBits(uint32_t x){float f;__builtin_memcpy(&f,&x,4);return f;}
extern "C" __attribute__((noinline)) void original_scale(float *m,float sx,float sy) {
    SexyMatrix3 matrix;for(int i=0;i<9;++i)matrix.m[i/3][i%3]=m[i];
    SexyTransform2D scale;scale.Scale(sx,sy);matrix=scale*matrix;
    for(int i=0;i<9;++i)m[i]=matrix.m[i/3][i%3];
}
extern "C" __attribute__((noinline)) int candidate_scale(float *m,float sx,float sy) {
    if(H1TryScaleAffineMatrix(m,sx,sy))return 1;
    original_scale(m,sx,sy);return 0;
}
/* Record=scaleX,scaleY,nine original matrix words. Output=accepted,9 old,9 new.
 * This same entry is executed as real o32 MIPS code under isolated Unicorn. */
extern "C" void matrix_evaluate(const uint32_t *input,uint32_t *output,unsigned count) {
    for(unsigned k=0;k<count;++k,input+=11,output+=19){
        float old[9],current[9];for(int i=0;i<9;++i)old[i]=current[i]=fromBits(input[i+2]);
        const float sx=fromBits(input[0]),sy=fromBits(input[1]);
        original_scale(old,sx,sy);output[0]=(uint32_t)candidate_scale(current,sx,sy);
        for(int i=0;i<9;++i){output[1+i]=H1ScaleFloatBits(old[i]);output[10+i]=H1ScaleFloatBits(current[i]);}
    }
}
#ifdef PVZH1_MATRIX_HOST
#include <cstdio>
#include <cstdlib>
#include <vector>
int main(int argc,char **argv){
    if(argc!=3)return 2;
    FILE *in=std::fopen(argv[1],"rb");if(!in)return 3;
    std::fseek(in,0,SEEK_END);long size=std::ftell(in);std::rewind(in);
    if(size<0||size%44)return 4;
    std::vector<uint32_t> input((size_t)size/4),output((size_t)size/44*19);
    if(std::fread(input.data(),1,(size_t)size,in)!=(size_t)size)return 5;std::fclose(in);
    matrix_evaluate(input.data(),output.data(),(unsigned)(size/44));
    FILE *out=std::fopen(argv[2],"wb");if(!out)return 6;
    if(std::fwrite(output.data(),4,output.size(),out)!=output.size())return 7;
    return std::fclose(out)!=0;
}
#endif
