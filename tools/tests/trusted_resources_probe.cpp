/* Resource checksums belong to the offline pipeline. LGPL-3.0-or-later. */
#include "TrustedResources.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
int main(){
 std::array<unsigned char,65536> source{},decoded{};
 for(unsigned i=0;i<source.size();++i)source[i]=(i*13+i/19)&255;
 uLongf packedBytes=compressBound(source.size());std::vector<unsigned char> packed(packedBytes);
 assert(compress2(packed.data(),&packedBytes,source.data(),source.size(),9)==Z_OK);packed.resize(packedBytes);
 // Alter only the Adler trailer. The ordinary decoder rejects this payload.
 packed.back()^=1;uLongf count=decoded.size();
 assert(uncompress(decoded.data(),&count,packed.data(),packed.size())==Z_DATA_ERROR);
 count=decoded.size();assert(H1ResourceUncompress(decoded.data(),&count,packed.data(),packed.size())==Z_OK);
 assert(count==source.size()&&decoded==source);
 z_stream stream{};assert(H1ResourceInflateInit(&stream)==Z_OK);
 for(unsigned pass=0;pass<2;++pass){
  if(pass)assert(H1ResourceInflateReset(&stream)==Z_OK);
  stream.next_in=packed.data();stream.avail_in=packed.size();stream.next_out=decoded.data();stream.avail_out=decoded.size();
  assert(inflate(&stream,Z_FINISH)==Z_STREAM_END);assert(decoded==source);
 }
 inflateEnd(&stream);std::puts("PASS: one-shot, streaming and reset ignore resource checksums");
}
