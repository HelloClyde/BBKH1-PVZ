"""Optional H1 libxmp numerical-cache adaptation. NOT production-hooked.

apply(output_root) writes only build copies, returning {original: replacement}.
The caller must explicitly select the two replacements; vendor is never edited.
Miss expressions retain pinned spelling/order. No game or audio clock changes.
"""
from pathlib import Path
import hashlib
import os

ROOT=Path(__file__).resolve().parents[2]
PINNED={
    'period.c':'23cc96ef90974a6fffb213f6b5412257cca80ade143ad568157ee580a66c3f75',
    'mixer.c':'72ff837d252050bb8cc93471ff955d17d109801c4d30e70d7a896f7bdd6553ad',
}

def apply(output_root):
    output_root=Path(output_root).resolve()
    if not output_root.is_relative_to((ROOT/'build').resolve()):
        raise ValueError('Numerical adaptation output must stay in project build/')
    output=output_root/'h1-xmp-numeric';output.mkdir(parents=True,exist_ok=True)
    replacements={}
    for name,expected in PINNED.items():
        original=ROOT/'vendor/libxmp/src'/name
        if hashlib.sha256(original.read_bytes()).hexdigest()!=expected:
            raise RuntimeError('Pinned libxmp numeric source changed: '+name)
        text=original.read_text(encoding='utf8')
        # Copied C files have no adjacent vendor headers. On case-insensitive
        # H1/Windows trees, a generic common.h include could select Sexy's C++
        # Common.h before libxmp's own C header. Bind just this include to the
        # original vendor location; do not reorder global framework includes.
        common=os.path.relpath(original.parent/'common.h',output).replace('\\','/')
        if text.count('#include "common.h"')!=1:
            raise RuntimeError('Pinned libxmp common.h include changed: '+name)
        text=text.replace('#include "common.h"','#include "'+common+'"',1)
        text='#include "xmp_numeric_cache.h" /* PVZH1_EXACT_XMP_NUMERIC_CACHE */\n'+text
        if name=='period.c':
            old='''double libxmp_note_to_period_mix(int n, int b)
{
	double d = (double)n + (double)b / 12800;
	return PERIOD_BASE / pow(2, d / 12);
}'''
            new='''#ifdef H1_PVZ_PORTABLE
static H1XmpNoteCache h1_note_cache;
void pvzh1_xmp_note_cache_reset(void) { __builtin_memset(&h1_note_cache,0,sizeof h1_note_cache); }
H1XmpNumericStats pvzh1_xmp_note_cache_stats(void) {
#ifdef PVZH1_XMP_NUMERIC_STATS
    return h1_note_cache.stats;
#else
    const H1XmpNumericStats empty={0,0,0,0};return empty;
#endif
}
#endif
double libxmp_note_to_period_mix(int n, int b)
{
	double result;
#ifdef H1_PVZ_PORTABLE
	if (h1_xmp_note_read(&h1_note_cache,n,b,&result)) return result;
#endif
	double d = (double)n + (double)b / 12800;
	result = PERIOD_BASE / pow(2, d / 12);
#ifdef H1_PVZ_PORTABLE
	h1_xmp_note_write(&h1_note_cache,n,b,result);
#endif
	return result;
}'''
            assert text.count(old)==1
            text=text.replace(old,new)
        else:
            old='\t\tstep = C4_PERIOD * c5spd / s->freq / vi->period;'
            new='''#ifdef H1_PVZ_PORTABLE
		if (!h1_xmp_step_read(&h1_step_cache,c5spd,s->freq,vi->period,&step)) {
#endif
		step = C4_PERIOD * c5spd / s->freq / vi->period;
#ifdef H1_PVZ_PORTABLE
			h1_xmp_step_write(&h1_step_cache,c5spd,s->freq,vi->period,step);
		}
#endif'''
            assert text.count(old)==1;text=text.replace(old,new)
            anchor='/* Fill the output buffer calling one of the handlers.'
            prelude='''#ifdef H1_PVZ_PORTABLE
static H1XmpStepCache h1_step_cache;
void pvzh1_xmp_step_cache_reset(void) { __builtin_memset(&h1_step_cache,0,sizeof h1_step_cache); }
H1XmpNumericStats pvzh1_xmp_step_cache_stats(void) {
#ifdef PVZH1_XMP_NUMERIC_STATS
    return h1_step_cache.stats;
#else
    const H1XmpNumericStats empty={0,0,0,0};return empty;
#endif
}
#endif

'''
            assert text.count(anchor)==1;text=text.replace(anchor,prelude+anchor)
        target=output/name;target.write_text(text,encoding='utf8',newline='\n')
        replacements[original]=target
    return replacements
