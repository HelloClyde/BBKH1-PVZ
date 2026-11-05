/* Sample-exact PCM in ordinary heap pages; LGPL-3.0-or-later. */
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <new>
class H1PagedTitlePCM {
 static constexpr unsigned SamplesPerPage=2048;
 int16_t** pages=nullptr;unsigned pageCount=0,samples=0;bool loading=false;
public:
 H1PagedTitlePCM()=default;H1PagedTitlePCM(const H1PagedTitlePCM&)=delete;
 ~H1PagedTitlePCM(){Clear();}
 bool empty()const{return !samples;}
 unsigned size()const{return samples;}
 size_t capacity_bytes()const{return size_t(samples)*2+size_t(pageCount)*sizeof(void*);}
 bool busy()const{return loading;}
 void Clear(){if(pages){for(unsigned i=0;i<pageCount;++i)delete[] pages[i];delete[] pages;}pages=nullptr;pageCount=samples=0;}
 bool Load(FILE* file,unsigned count){
  Clear();loading=true;pageCount=(count+SamplesPerPage-1)/SamplesPerPage;
  pages=new(std::nothrow) int16_t*[pageCount]{};bool okay=pages!=nullptr;
  for(unsigned i=0;okay&&i<pageCount;++i){const unsigned n=std::min(SamplesPerPage,count-i*SamplesPerPage);
   pages[i]=new(std::nothrow) int16_t[n];okay=pages[i]&&fread(pages[i],2,n,file)==n;}
  if(okay)okay=fgetc(file)==EOF;
  if(okay)samples=count;else Clear();loading=false;return okay;
 }
 void Copy(int16_t* out,unsigned offset,unsigned count)const{
  while(count){unsigned n=std::min(count,SamplesPerPage-offset%SamplesPerPage);std::memcpy(out,pages[offset/SamplesPerPage]+offset%SamplesPerPage,n*2);out+=n;offset+=n;count-=n;}
 }
};
