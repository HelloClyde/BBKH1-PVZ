/* Lossless spatially aligned animation tiles and bounded temporal deltas.
 * Original colors, per-pixel blend order and every authored frame survive.
 * Offline encoder only. LGPL-3.0-or-later. */
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <vector>
using Bytes=std::vector<uint8_t>;
constexpr unsigned TileW=32,TileH=16,TilePixels=TileW*TileH,TileBytes=TilePixels*5,MaxDepth=3;
struct Op{int32_t x,y;uint32_t color,kind;};
struct Segment{unsigned track,drawn;std::vector<Op> ops;};
struct Frame{uint32_t h[7];std::vector<Segment> segments;};
struct Entry{uint64_t key;uint32_t frame,offset,bytes,type;};
struct Stored{Bytes raw;uint32_t offset;unsigned depth;bool opaque;};
void Check(bool ok,const char*why){if(!ok)throw std::runtime_error(why);}
void U16(Bytes&b,uint16_t n){b.push_back(n);b.push_back(n>>8);}
void U32(Bytes&b,uint32_t n){U16(b,n);U16(b,n>>16);}
uint32_t Word(const uint8_t*p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void SetWord(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=v>>(i*8);}
uint64_t Hash(const Bytes&b){uint64_t h=14695981039346656037ull;for(uint8_t v:b){h^=v;h*=1099511628211ull;}return h;}
Bytes Deflate(const Bytes&b){uLongf n=compressBound(b.size());Bytes out(n);Check(compress2(out.data(),&n,b.data(),b.size(),6)==Z_OK,"compress tile");out.resize(n);return out;}
Bytes Shuffle(const Bytes&b){Bytes out(TileBytes);for(unsigned c=0;c<4;++c)for(unsigned i=0;i<TilePixels;++i)out[c*TilePixels+i]=b[i*4+c];std::copy(b.begin()+TilePixels*4,b.end(),out.begin()+TilePixels*4);return out;}
Bytes Unshuffle(const Bytes&b){Bytes out(TileBytes);for(unsigned c=0;c<4;++c)for(unsigned i=0;i<TilePixels;++i)out[i*4+c]=b[c*TilePixels+i];std::copy(b.begin()+TilePixels*4,b.end(),out.begin()+TilePixels*4);return out;}
Bytes Green(const Bytes&b,bool restore=false){Bytes out=b;for(unsigned i=0;i<TilePixels;++i){uint32_t v=Word(b.data()+i*4),g=((v>>8)&255)*0x00010001u;
 uint32_t rb=restore?((v&0xff00ffu)+g)&0xff00ffu:((v&0xff00ffu)+0x1000100u-g)&0xff00ffu;SetWord(out.data()+i*4,(v&0xff00ff00u)|rb);}return out;}
Bytes Shift(const Bytes&b,int dx,int dy){Bytes out(TileBytes,0);for(int y=0;y<int(TileH);++y)for(int x=0;x<int(TileW);++x){int sx=x+dx,sy=y+dy;if(sx<0||sx>=int(TileW)||sy<0||sy>=int(TileH))continue;unsigned a=y*TileW+x,s=sy*TileW+sx;std::memcpy(out.data()+a*4,b.data()+s*4,4);out[TilePixels*4+a]=b[TilePixels*4+s];}return out;}
std::pair<int,int> Motion(const Bytes&a,const Bytes&b){
 auto score=[&](int dx,int dy){unsigned total=0;for(int y=2;y<int(TileH);y+=4)for(int x=2;x<int(TileW);x+=4){int sx=x+dx,sy=y+dy;if(sx<0||sx>=int(TileW)||sy<0||sy>=int(TileH)){total+=768;continue;}unsigned at=(y*TileW+x)*4,bt=(sy*TileW+sx)*4;for(unsigned c=0;c<3;++c)total+=unsigned(std::abs(int(a[at+c])-int(b[bt+c])));}return total;};
 int bx=0,by=0;unsigned best=score(0,0);if(best<128)return {0,0};
 for(int y=-4;y<=4;y+=2)for(int x=-4;x<=4;x+=2){unsigned s=score(x,y);if(s<best){best=s;bx=x;by=y;}}
 int cx=bx,cy=by;for(int y=cy-1;y<=cy+1;++y)for(int x=cx-1;x<=cx+1;++x){unsigned s=score(x,y);if(s<best){best=s;bx=x;by=y;}}
 return {bx,by};
}
bool Read(std::ifstream&f,Frame&r){f.read((char*)r.h,28);if(!f.gcount())return false;Check(f.gcount()==28,"frame header");r.segments.resize(r.h[6]&0x3fffffffu);
 for(auto&s:r.segments){uint32_t h[3];Check(bool(f.read((char*)h,12)),"track header");s.track=h[0];s.drawn=h[1];s.ops.resize(h[2]);Check(bool(f.read((char*)s.ops.data(),uint64_t(h[2])*16)),"operations");}return true;}
int main(int argc,char**argv){try{
 Check(argc==3,"usage: pack_extra_actor_tiles RAW OUTPUT.h1ap");std::ifstream f(argv[1],std::ios::binary);Check(bool(f),"open raw");
 Frame frame;Bytes body;std::vector<Entry> entries;std::vector<Stored> tiles;
 std::unordered_map<uint64_t,std::vector<unsigned>> tileHash;
 std::unordered_map<uint64_t,unsigned> previous;
 std::unordered_map<uint64_t,std::vector<uint32_t>> packets,streams;
 uint64_t operations=0,reused=0,deltaCount=0,shuffledCount=0,motionCount=0;unsigned maxDepth=0,maxFrameTiles=0;
 auto append=[&](const Bytes&data,bool stream){auto&bucket=(stream?streams:packets)[Hash(data)];for(auto offset:bucket)if(Word(body.data()+offset)==data.size()&&!memcmp(body.data()+offset+4,data.data(),data.size()))return offset;
  Check(body.size()+4+data.size()<0x80000000u,"body too large");uint32_t offset=body.size();U32(body,data.size());body.insert(body.end(),data.begin(),data.end());bucket.push_back(offset);return offset;};
 auto store=[&](const Bytes&raw,uint64_t motionKey,bool opaque){auto&bucket=tileHash[Hash(raw)];for(unsigned id:bucket)if(tiles[id].raw==raw){previous[motionKey]=id;++reused;return tiles[id].offset;}
  Bytes best=raw;uint32_t flags=0x80000000u,base=UINT32_MAX;unsigned depth=0;int shiftX=0,shiftY=0;
  auto attempt=[&](const Bytes&input,uint32_t extra,uint32_t reference,unsigned candidateDepth,int dx=0,int dy=0){auto packed=Deflate(input);if(packed.size()+((extra&0x08000000u)?4:0)<best.size()+((flags&0x08000000u)?4:0)){best=std::move(packed);flags=extra;base=reference;depth=candidateDepth;shiftX=dx;shiftY=dy;}};
  auto colorAttempt=[&](const Bytes&input,uint32_t extra,uint32_t reference,unsigned candidateDepth,int dx=0,int dy=0){attempt(input,extra,reference,candidateDepth,dx,dy);attempt(Shuffle(input),extra|0x20000000u,reference,candidateDepth,dx,dy);attempt(Shuffle(Green(input)),extra|0x24000000u,reference,candidateDepth,dx,dy);};
  colorAttempt(raw,0,UINT32_MAX,0);
  auto found=previous.find(motionKey);if(found!=previous.end()&&tiles[found->second].depth<MaxDepth){const auto&old=tiles[found->second];Bytes delta(TileBytes);for(unsigned i=0;i<TileBytes;++i)delta[i]=raw[i]^old.raw[i];
   colorAttempt(delta,0,old.offset,old.depth+1);
   for(unsigned i=0;i<TileBytes;++i)delta[i]=uint8_t(raw[i]-old.raw[i]);
   colorAttempt(delta,0x10000000u,old.offset,old.depth+1);
   if(opaque&&old.opaque){auto[dx,dy]=Motion(raw,old.raw);if(dx||dy){Bytes shifted=Shift(old.raw,dx,dy);
     for(unsigned i=0;i<TileBytes;++i)delta[i]=raw[i]^shifted[i];colorAttempt(delta,0x08000000u,old.offset,old.depth+1,dx,dy);
     for(unsigned i=0;i<TileBytes;++i)delta[i]=uint8_t(raw[i]-shifted[i]);colorAttempt(delta,0x18000000u,old.offset,old.depth+1,dx,dy);
   }}
  }
  Bytes restored(TileBytes);if(flags&0x80000000u)restored=best;else{uLongf n=TileBytes;Check(uncompress(restored.data(),&n,best.data(),best.size())==Z_OK&&n==TileBytes,"tile inflate roundtrip");}
  if(flags&0x20000000u)restored=Unshuffle(restored);
  if(flags&0x04000000u)restored=Green(restored,true);
  if(base!=UINT32_MAX){auto id=found->second;Check(tiles[id].offset==base,"predictor identity");auto prediction=(flags&0x08000000u)?Shift(tiles[id].raw,shiftX,shiftY):tiles[id].raw;
   for(unsigned i=0;i<TileBytes;++i)restored[i]=(flags&0x10000000u)?uint8_t(restored[i]+prediction[i]):restored[i]^prediction[i];++deltaCount;}
  Check(restored==raw,"tile color/mode roundtrip");if(flags&0x20000000u)++shuffledCount;maxDepth=std::max(maxDepth,depth);
  uint32_t offset=body.size();U32(body,uint32_t(best.size())|flags|(opaque?0x40000000u:0));U32(body,base);if(flags&0x08000000u){U32(body,uint8_t(shiftX)|(uint32_t(uint8_t(shiftY))<<8));++motionCount;}body.insert(body.end(),best.begin(),best.end());
  unsigned id=tiles.size();tiles.push_back({raw,offset,depth,opaque});bucket.push_back(id);previous[motionKey]=id;return offset;
 };
 while(Read(f,frame)){struct Part{unsigned track,drawn,count,reference;int x,y;};std::vector<Part> parts;unsigned frameTiles=0;int left=32767,top=32767,right=-32768,bottom=-32768;
  for(auto&s:frame.segments){int ox=0,oy=0;if(!s.ops.empty()){ox=s.ops[0].x;oy=s.ops[0].y;for(auto&o:s.ops){ox=std::min(ox,o.x);oy=std::min(oy,o.y);left=std::min(left,o.x);top=std::min(top,o.y);right=std::max(right,o.x+1);bottom=std::max(bottom,o.y+1);}}
   auto expected=s.ops;auto coordinate=[](const Op&a,const Op&b){return a.y!=b.y?a.y<b.y:a.x<b.x;};std::stable_sort(expected.begin(),expected.end(),coordinate);
   std::vector<std::vector<Op>> layers;for(size_t first=0;first<expected.size();){size_t end=first+1;while(end<expected.size()&&expected[end].x==expected[first].x&&expected[end].y==expected[first].y)++end;
    if(layers.size()<end-first)layers.resize(end-first);for(size_t i=first;i<end;++i)layers[i-first].push_back(expected[i]);first=end;}
   Bytes stream;std::vector<Op> actual;unsigned count=0;
   for(unsigned layer=0;layer<layers.size();++layer){std::map<unsigned,Bytes> cells;
    for(auto&o:layers[layer]){Check(o.kind<3,"blend kind");unsigned x=o.x-ox,y=o.y-oy,key=((y/TileH)<<16)|(x/TileW);auto[it,added]=cells.try_emplace(key);if(added)it->second.resize(TileBytes,0);
     unsigned at=(y%TileH)*TileW+x%TileW;SetWord(it->second.data()+at*4,o.color);it->second[TilePixels*4+at]=o.kind+1;
    }
    for(auto&[key,raw]:cells){unsigned tx=(key&65535)*TileW,ty=(key>>16)*TileH;bool opaque=true;
     for(unsigned i=0;i<TilePixels;++i){unsigned kind=raw[TilePixels*4+i];if(kind){uint32_t color=Word(raw.data()+i*4);actual.push_back({int32_t(ox+tx+i%TileW),int32_t(oy+ty+i/TileW),color,kind-1});}if(kind!=1||raw[i*4+3]!=255)opaque=false;}
     uint64_t motionKey=(uint64_t(s.track)<<48)|(uint64_t(layer)<<32)|key;uint32_t ref=store(raw,motionKey,opaque);U16(stream,tx);U16(stream,ty);U32(stream,ref);++count;
    }
   }
   std::stable_sort(actual.begin(),actual.end(),coordinate);Check(actual.size()==expected.size()&&(actual.empty()||!memcmp(actual.data(),expected.data(),actual.size()*sizeof(Op))),"per-pixel operation order changed");
   operations+=s.ops.size();uint32_t ref=append(stream,true);parts.push_back({s.track,s.drawn,count,ref,ox,oy});frameTiles+=count;
  }
  if(right<left)left=top=right=bottom=0;Bytes packet;U16(packet,frame.h[4]);U16(packet,frame.h[5]);U16(packet,parts.size());U16(packet,4290);
  for(int n:{left,top,right,bottom})U16(packet,uint16_t(n));for(auto&p:parts){U16(packet,p.track);U16(packet,p.drawn);U32(packet,p.count);U32(packet,p.reference|0x80000000u);U16(packet,p.x);U16(packet,p.y);}
  uint32_t ref=append(packet,false)+4;entries.push_back({uint64_t(frame.h[0])|(uint64_t(frame.h[1])<<32),frame.h[2],ref,uint32_t(packet.size()),frame.h[3]});maxFrameTiles=std::max(maxFrameTiles,frameTiles);
  if(entries.size()%200==0)std::fprintf(stderr,"TILE_PROGRESS type=%u frames=%zu tile_blobs=%zu bytes=%zu\n",frame.h[3],entries.size(),tiles.size(),body.size());
 }
 Check(!entries.empty(),"empty input");std::sort(entries.begin(),entries.end(),[](auto&a,auto&b){return a.key!=b.key?a.key<b.key:a.frame<b.frame;});
 unsigned dataOffset=64+entries.size()*24;Bytes data(64);for(auto&e:entries){U32(data,uint32_t(e.key));U32(data,uint32_t(e.key>>32));U32(data,e.frame);U32(data,dataOffset+e.offset);U32(data,e.bytes);U32(data,e.type);}data.insert(data.end(),body.begin(),body.end());
 Check(data.size()<512u*1024*1024,"logical file exceeds runtime range");uint32_t h[16]={0x50413148,1,480,272,uint32_t(entries.size()),64,0,dataOffset,dataOffset,uint32_t(data.size()),uint32_t(crc32(0,data.data()+64,data.size()-64)),0,0,0,0x314d4352,4290};std::memcpy(data.data(),h,64);
 Bytes output;for(uint32_t n:{0x5a503148u,3u,uint32_t(data.size()),uint32_t((data.size()+4095)/4096),4096u,uint32_t(data.size()),0u,uint32_t(crc32(0,data.data(),data.size()))})U32(output,n);output.insert(output.end(),data.begin(),data.end());
 std::ofstream out(argv[2],std::ios::binary);Check(bool(out.write((char*)output.data(),output.size())),"output write");
 std::cout<<"{\"type\":"<<entries[0].type<<",\"frames\":"<<entries.size()<<",\"operations\":"<<operations<<",\"bytes\":"<<output.size()<<",\"decoded_bytes\":"<<data.size()<<",\"tile_blobs\":"<<tiles.size()<<",\"reused_tiles\":"<<reused<<",\"delta_tiles\":"<<deltaCount<<",\"shuffled_tiles\":"<<shuffledCount<<",\"motion_tiles\":"<<motionCount<<",\"maximum_dependency_depth\":"<<maxDepth<<",\"maximum_frame_tiles\":"<<maxFrameTiles<<",\"pixel_roundtrip\":true,\"per_pixel_operation_order_preserved\":true,\"source_color_precision_unchanged\":true}\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
