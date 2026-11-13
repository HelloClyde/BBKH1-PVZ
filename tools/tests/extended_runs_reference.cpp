/* Frozen V54 pixel oracle, used only by the host differential test. */
__attribute__((optimize("O3"))) static void DrawExtendedReference(Reader& stream,unsigned runs,uint32_t* pixels,int dx,int dy,int cx,int cy,int cr,int cb){
 const bool direct=cache.directLiterals;const unsigned sampleBytes=direct?4:2;
 auto read=[&](){return direct?stream.U32():cache.palette[stream.U16()];};
 for(unsigned run=0;run<runs;++run){int x,y;unsigned flags;stream.Run(false,x,y,flags);x+=dx;y+=dy;unsigned n=flags&4095;const bool constant=flags&4096;
  if(y<cy||y>=cb||x>=cr||x+int(n)<=cx){stream.Skip((constant?1:n)*sampleBytes);continue;}
  unsigned first=std::max(0,cx-x),last=std::min(int(n),cr-x);auto*dest=pixels+y*480+x+first;uint32_t value=0;
  if(constant)value=read();else stream.Skip(first*sampleBytes);
  for(unsigned i=first;i<last;++i){if(!constant)value=read();uint32_t p=*dest;
   if(flags&32768)*dest=value;
   else if(flags&16384){unsigned rb=(p&0xff00ffu)+(value&0xff00ffu),g=(p&0xff00u)+(value&0xff00u),carry=rb&0x1000100u;
    rb|=carry-(carry>>8);if(g&0x10000u)g=0xff00u;*dest=(p&0xff000000u)|(rb&0xff00ffu)|(g&0xff00u);}
   else if(flags&8192){unsigned a=value>>24,inv=256-a;*dest=0xff000000u|((((p&0xff00ffu)*inv+(value&0xff00ffu)*a)>>8)&0xff00ffu)|((((p&0xff00u)*inv+(value&0xff00u)*a)>>8)&0xff00u);}
   else {unsigned inv=value>>24;*dest=0xff000000u|((value&0xffffffu)+((((p&0xff00ffu)*inv)>>8)&0xff00ffu)+((((p&0xff00u)*inv)>>8)&0xff00u));}
   ++dest;
  }
  if(!constant)stream.Skip((n-last)*sampleBytes);
 }
}