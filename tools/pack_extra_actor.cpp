/* Exact translated-track sharing and page compression, offline only.
 * LGPL-3.0-or-later. */
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
using Bytes=std::vector<uint8_t>;
struct Op{int32_t x,y;uint32_t color,kind;};
struct Segment{uint32_t track,drawn;std::vector<Op> ops;};
struct Frame{uint32_t h[7];std::vector<Segment> segments;};
struct Entry{uint64_t key;uint32_t frame,offset,bytes,type;};
void Require(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
void U16(Bytes&b,uint16_t n){b.push_back(n);b.push_back(n>>8);}
void U32(Bytes&b,uint32_t n){U16(b,n);U16(b,n>>16);}
uint32_t Word(const uint8_t*p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
uint16_t Short(const uint8_t*p){return p[0]|unsigned(p[1])<<8;}
bool Read(std::ifstream&f,Frame&r){
 f.read(reinterpret_cast<char*>(r.h),28);if(!f.gcount())return false;Require(f.gcount()==28,"truncated frame header");
 Require((r.h[6]&0xc0000000u)==0x40000000u,"wrong raw schema");r.segments.resize(r.h[6]&0x3fffffffu);
 for(auto&s:r.segments){uint32_t h[3];Require(bool(f.read(reinterpret_cast<char*>(h),12)),"truncated segment");s.track=h[0];s.drawn=h[1];s.ops.resize(h[2]);Require(bool(f.read(reinterpret_cast<char*>(s.ops.data()),uint64_t(h[2])*16)),"truncated operations");}return true;
}
uint64_t Hash(const Bytes&b){uint64_t h=14695981039346656037ull;for(auto v:b){h^=v;h*=1099511628211ull;}return h;}
void VerifyStream(const Bytes&b,int ox,int oy,const std::vector<Op>&ops,bool direct,const std::vector<uint32_t>&palette){
 size_t at=0,index=0;
 while(at<b.size()){Require(b.size()-at>=6,"run header");int x=int16_t(Short(b.data()+at))+ox,y=int16_t(Short(b.data()+at+2))+oy;unsigned flags=Short(b.data()+at+4),n=flags&4095;at+=6;
  Require(n&&n<=480&&index+n<=ops.size(),"run count");unsigned kind=flags&0x4000?1:flags&0x2000?2:0,constant=flags&0x1000,width=direct?4:2;uint32_t value=0;
  for(unsigned j=0;j<n;++j){if(!constant||!j){Require(at+width<=b.size(),"run payload");value=direct?Word(b.data()+at):palette.at(Short(b.data()+at));at+=width;}
   const auto&o=ops[index++];Require(o.x==x+int(j)&&o.y==y&&o.color==value&&o.kind==kind,"pixel roundtrip");}
 }Require(index==ops.size(),"operation roundtrip");
}
int main(int argc,char**argv){try{
 Require(argc==3,"usage: pack_extra_actor INPUT.raw OUTPUT.h1ap");
 std::unordered_map<uint32_t,uint32_t> colors;std::vector<uint32_t> palette;Frame r;bool direct=false;
 {std::ifstream f(argv[1],std::ios::binary);Require(bool(f),"open raw");
  while(!direct&&Read(f,r))for(auto&s:r.segments){for(auto&o:s.ops){if(!colors.contains(o.color)){colors[o.color]=palette.size();palette.push_back(o.color);if(palette.size()>65536){direct=true;break;}}}if(direct)break;}}
 if(!direct&&palette.empty()){palette.push_back(0);colors[0]=0;}
 Bytes body;std::vector<Entry> entries;std::unordered_map<uint64_t,std::vector<uint32_t>> streams,packets;
 uint64_t operations=0,savedStreamBytes=0;unsigned uniqueStreams=0;
 auto share=[&](const Bytes&data,bool stream){auto&dict=stream?streams:packets;uint64_t h=Hash(data);auto&bucket=dict[h];
  for(auto offset:bucket){size_t size=Word(body.data()+offset);if(size==data.size()&&!memcmp(body.data()+offset+4,data.data(),size)){if(stream)savedStreamBytes+=size;return offset;}}
  Require(body.size()+data.size()+4<0x80000000ull,"bank exceeds offset range");uint32_t offset=body.size();U32(body,data.size());body.insert(body.end(),data.begin(),data.end());bucket.push_back(offset);if(stream)++uniqueStreams;return offset;};
 std::ifstream f(argv[1],std::ios::binary);Require(bool(f),"reopen raw");
 while(Read(f,r)){
  struct Packed{uint32_t track,drawn,runs,reference;int x,y;};std::vector<Packed> segments;
  int left=32767,top=32767,right=-32768,bottom=-32768;
  for(auto&s:r.segments){int ox=0,oy=0;if(!s.ops.empty()){ox=s.ops[0].x;oy=s.ops[0].y;for(auto&o:s.ops){ox=std::min(ox,o.x);oy=std::min(oy,o.y);left=std::min(left,o.x);top=std::min(top,o.y);right=std::max(right,o.x+1);bottom=std::max(bottom,o.y+1);}}
   Bytes data;unsigned runs=0;
   for(size_t i=0;i<s.ops.size();){const auto&first=s.ops[i];Require(first.kind<=2,"operation kind");bool opaque=first.kind!=1&&(first.color>>24)==255;unsigned n=1;
    while(i+n<s.ops.size()&&n<480){auto&o=s.ops[i+n];if(o.x!=first.x+int(n)||o.y!=first.y||o.kind!=first.kind||(o.kind!=1&&(o.color>>24)==255)!=opaque)break;++n;}
    bool constant=n>1;for(unsigned j=1;j<n&&constant;++j)constant=s.ops[i+j].color==first.color;
    unsigned flags=n|(opaque?0x8000:0)|(first.kind==1?0x4000:first.kind==2?0x2000:0)|(constant?0x1000:0);
    Require(first.x-ox<=32767&&first.y-oy<=32767,"coordinate range");U16(data,uint16_t(first.x-ox));U16(data,uint16_t(first.y-oy));U16(data,flags);
    for(unsigned j=0;j<(constant?1u:n);++j){uint32_t color=s.ops[i+j].color;if(direct)U32(data,color);else U16(data,colors.at(color));}i+=n;++runs;
   }
   VerifyStream(data,ox,oy,s.ops,direct,palette);operations+=s.ops.size();uint32_t ref=share(data,true);
   segments.push_back({s.track,s.drawn,runs,ref,ox,oy});
  }
  if(right<left)left=top=right=bottom=0;
  Bytes packet;U16(packet,r.h[4]);U16(packet,r.h[5]);U16(packet,segments.size());unsigned mode=194|(direct?4:0);U16(packet,mode);
  for(int n:{left,top,right,bottom})U16(packet,uint16_t(n));
  for(auto&s:segments){U16(packet,s.track);U16(packet,s.drawn);U32(packet,s.runs);U32(packet,s.reference|0x80000000u);U16(packet,uint16_t(s.x));U16(packet,uint16_t(s.y));}
  uint32_t offset=share(packet,false)+4;entries.push_back({uint64_t(r.h[0])|(uint64_t(r.h[1])<<32),r.h[2],offset,uint32_t(packet.size()),r.h[3]});
 }
 Require(!entries.empty(),"empty bank");std::sort(entries.begin(),entries.end(),[](auto&a,auto&b){return a.key!=b.key?a.key<b.key:a.frame<b.frame;});
 unsigned paletteCount=direct?0:palette.size(),paletteOffset=64+entries.size()*24,dataOffset=paletteOffset+paletteCount*4;Bytes data(64);
 for(auto&e:entries){U32(data,uint32_t(e.key));U32(data,uint32_t(e.key>>32));U32(data,e.frame);U32(data,dataOffset+e.offset);U32(data,e.bytes);U32(data,e.type);}
 if(!direct)for(auto v:palette)U32(data,v);data.insert(data.end(),body.begin(),body.end());
 uint32_t header[16]={0x50413148,1,480,272,uint32_t(entries.size()),64,paletteCount,paletteOffset,dataOffset,uint32_t(data.size()),uint32_t(crc32(0,data.data()+64,data.size()-64)),0,0,0,0x314d4352,uint32_t(194|(direct?4:0))};memcpy(data.data(),header,64);
 Bytes encoded,table;unsigned pages=(data.size()+4095)/4096;
 for(unsigned page=0;page<pages;++page){unsigned n=std::min(size_t(4096),data.size()-page*4096);uLongf cap=compressBound(n);Bytes compressed(cap);
  Require(compress2(compressed.data(),&cap,data.data()+page*4096,n,6)==Z_OK,"compress page");compressed.resize(cap);uint8_t restored[4096];uLongf bytes=n;
  Require(uncompress(restored,&bytes,compressed.data(),compressed.size())==Z_OK&&bytes==n&&!memcmp(restored,data.data()+page*4096,n),"page roundtrip");
  U32(table,encoded.size());U32(table,compressed.size());U32(table,crc32(0,restored,n));encoded.insert(encoded.end(),compressed.begin(),compressed.end());}
 Bytes output;for(uint32_t n:{0x5a503148u,1u,uint32_t(data.size()),pages,4096u,uint32_t(encoded.size()),uint32_t(table.size()),uint32_t(crc32(0,data.data(),data.size()))})U32(output,n);
 output.insert(output.end(),table.begin(),table.end());output.insert(output.end(),encoded.begin(),encoded.end());std::ofstream out(argv[2],std::ios::binary);Require(bool(out.write(reinterpret_cast<const char*>(output.data()),output.size())),"write bank");
 std::cout<<"{\"type\":"<<entries[0].type<<",\"frames\":"<<entries.size()<<",\"operations\":"<<operations<<",\"bytes\":"<<output.size()<<",\"decoded_bytes\":"<<data.size()<<",\"unique_track_streams\":"<<uniqueStreams<<",\"shared_stream_bytes_saved\":"<<savedStreamBytes<<",\"direct_literals\":"<<(direct?"true":"false")<<",\"pixel_roundtrip\":true,\"page_roundtrip\":true}\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
