"""Keep the exact old 8888 sample, without a triangle-sized temporary image."""
from pathlib import Path
def apply(root):
    graphics=Path(root)/'SexyAppFramework/graphics'
    p=graphics/'SWTri.h';s=p.read_text(encoding='utf8')
    s='#ifdef H1_PVZ_PORTABLE\nextern "C" unsigned int pvzh1_direct_additive;\nextern "C" const unsigned char* pvzh1_additive_lut;\nextern "C" unsigned int pvzh1_additive_span(unsigned int*,unsigned int,unsigned int*);\nextern "C" void pvzh1_additive_span_done(unsigned int*);\n#endif\n'+s
    p.write_text(s,encoding='utf8',newline='\n')
    p=graphics/'SWTri/SWTri_Pixel8888.inc';s=p.read_text(encoding='utf8')
    declarations='''#ifdef PVZ_HOST_RUNNER
extern "C" unsigned int pvzh1_text_capture_active;
extern "C" void (*pvzh1_host_offline_additive_capture)(unsigned int*,unsigned int);
#endif
'''
    header=graphics/'SWTri.h';header.write_text(declarations+header.read_text(encoding='utf8'),encoding='utf8',newline='\n')
    s='''#ifdef H1_PVZ_PORTABLE
    unsigned int* h1AdditiveDestination=pix;
    unsigned int h1TransparentSample=0;
    const bool h1NeedsAdd= !pvzh1_direct_additive||((*pix&0xffffffu)!=0xffffffu);
    if(pvzh1_direct_additive)pix=&h1TransparentSample;
    if(h1NeedsAdd){
#endif
'''+s+'''
#ifdef H1_PVZ_PORTABLE
    }
    if(pvzh1_direct_additive){
        pix=h1AdditiveDestination;
        const unsigned s=h1TransparentSample,a=s>>24,d=*pix;
        if(a){
#ifdef PVZ_HOST_RUNNER
            if(pvzh1_text_capture_active&&pvzh1_host_offline_additive_capture){
                pvzh1_host_offline_additive_capture(pix,(((((s>>16)&255)*a+127)/255)<<16)|(((((s>>8)&255)*a+127)/255)<<8)|(((s&255)*a+127)/255));
            }else
#endif
            {
            unsigned r=((d>>16)&255)+((((s>>16)&255)*a+127)/255);
            unsigned g=((d>>8)&255)+((((s>>8)&255)*a+127)/255);
            unsigned b=(d&255)+(((s&255)*a+127)/255);
            if(r>255)r=255;if(g>255)g=255;if(b>255)b=255;
            *pix=(d&0xff000000u)|(r<<16)|(g<<8)|b;
            }
        }
    }
#endif
'''
    needle='#include "SWTri_TexelARGB.inc"';assert s.count(needle)==1
    s=s.replace(needle,needle+'''
#if defined(H1_PVZ_PORTABLE) && !defined(LINEAR_BLEND)
            if(pvzh1_direct_additive&&pvzh1_additive_lut&&alpha>0){
                const auto* lut=pvzh1_additive_lut+unsigned(alpha)*256u;const unsigned d=*h1AdditiveDestination;
#ifdef PVZ_HOST_RUNNER
                if(pvzh1_text_capture_active&&pvzh1_host_offline_additive_capture){
                    pvzh1_host_offline_additive_capture(h1AdditiveDestination,(unsigned(lut[(tex>>16)&255])<<16)|(unsigned(lut[(tex>>8)&255])<<8)|lut[tex&255]);
                }else
#endif
                {
                unsigned r=((d>>16)&255)+lut[(tex>>16)&255];
                unsigned g=((d>>8)&255)+lut[(tex>>8)&255];
                unsigned b=(d&255)+lut[tex&255];
                if(r>255)r=255;if(g>255)g=255;if(b>255)b=255;
                *h1AdditiveDestination=(d&0xff000000u)|(r<<16)|(g<<8)|b;
                }
                alpha=0; // Exact zero-background sample already committed.
            }
#endif
''')
    p.write_text(s,encoding='utf8',newline='\n')

    p=graphics/'SWTri/SWTri_Loop.inc';s=p.read_text(encoding='utf8')
    needle='\twhile(width-- > 0)\n\t{';assert s.count(needle)==1
    s=s.replace(needle,'''#if defined(H1_PVZ_PORTABLE) && TRI0==0
    while(width>0){
        unsigned h1Span=width;
        if(pvzh1_direct_additive){
            unsigned skip=pvzh1_additive_span(reinterpret_cast<unsigned int*>(pix),width,&h1Span);
            if(skip){pix+=skip;width-=skip;
#if defined(MOD_ARGB)
                a+=da*skip;r+=dr*skip;g+=dg*skip;b+=db*skip;
#endif
#if defined(TEXTURED)
                u+=du*skip;v+=dv*skip;
#endif
                continue;
            }
        }
        auto* h1SpanStart=pix;int h1Remaining=width-int(h1Span);width=h1Span;
#endif
\twhile(width-- > 0)
\t{''')
    needle='\tlx += ldx;';assert s.count(needle)==1
    s=s.replace(needle,'''#if defined(H1_PVZ_PORTABLE) && TRI0==0
        if(pvzh1_direct_additive)pvzh1_additive_span_done(reinterpret_cast<unsigned int*>(h1SpanStart));
        width=h1Remaining;
    }
#endif
'''+needle)
    p.write_text(s,encoding='utf8',newline='\n')
