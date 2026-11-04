"""Actual memo candidate vs pristine libxmp, bounded streaming PCM comparison.
No whole PCM file is written; all generated source/objects stay below build/.
The production build hooks, vendor and resource files remain untouched.
"""
from pathlib import Path
import hashlib,importlib.util,json,os,subprocess
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'build/xmp-numeric-candidate'
OUT.mkdir(parents=True,exist_ok=True)
import shutil
for original,target in (('xmp_numeric_unit.c','unit.c'),('xmp_numeric_menu.c','menu.c')):
    shutil.copyfile(Path(__file__).with_name(original),OUT/target)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('h1_numeric_patch',ROOT/'platform/audio/xmp_numeric_patches.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
overrides=patch.apply(OUT)
before={p:sha(p) for p in overrides}
assert patch.apply(OUT)==overrides
# O2 build currently freezes the optional production patch file. Apply its
# agreed include-only correction to build copies first; synchronize after ACK.
pending_include_sync=False
for original,generated in overrides.items():
    text=generated.read_text()
    if '#include "common.h"' in text:
        header=os.path.relpath(original.parent/'common.h',generated.parent).replace('\\','/')
        generated.write_text(text.replace('#include "common.h"','#include "'+header+'"',1),newline='\n')
        pending_include_sync=True
config=json.loads((ROOT/'platform/music_sources.json').read_text())
flags=['-std=gnu99','-O2','-g','-Wall','-Wextra','-Wno-unused-parameter','-I'+str(ROOT/'platform/audio')]
flags+=['-D'+d for d in config['defines']]+['-I'+str(ROOT/p) for p in config['includes']]+['-I'+str(ROOT/'platform')]
env=os.environ.copy();env['PATH']='C:/msys64/ucrt64/bin;'+env.get('PATH','')
cc='C:/msys64/ucrt64/bin/gcc.exe';identities={}
def execute(cmd,log):
    r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,env=env)
    (OUT/log).write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(log+': '+r.stdout+r.stderr)
    return r.stdout
def compile(src,name,extra=()):
    obj=OUT/(name+'.o');execute([cc,*flags,*extra,'-c',src,'-o',obj],name+'.compile.log');return obj
common=[];pristine=[]
for name in config['sources']+['platform/xmp_shared.c']:
    src=ROOT/name;obj=ROOT/'build/xmp-shared-test'/(hashlib.sha1(str(src).encode()).hexdigest()[:10]+'.o')
    assert obj.exists(),obj;identities[name]=dict(source_sha256=sha(src),object_sha256=sha(obj))
    if src in overrides:pristine.append(obj)
    else:common.append(obj)
candidate=[compile(src,'candidate-'+src.stem,['-DH1_PVZ_PORTABLE','-DPVZH1_XMP_NUMERIC_STATS']) for src in overrides.values()]
# Generate observer from the unchanged 7200-block regression. Original JSON is
# stderr, while stdout carries exact PCM and logical state for pipe comparison.
test=(ROOT/'tools/test_xmp_shared.c').read_text()
test=test.replace('#define CHECK(c)', '#define CHECK(c)',1)
prefix='''#include <stdio.h>
#include <stdint.h>
#include <io.h>
#include <fcntl.h>
#define printf(...) fprintf(stderr,__VA_ARGS__)
#ifdef PVZH1_CANDIDATE
#include "xmp_numeric_cache.h"
#endif
static uint64_t proxy_pow_calls;
extern double __real_pow(double,double);
double __wrap_pow(double a,double b){++proxy_pow_calls;return __real_pow(a,b);}
static void trace_block(unsigned phase,unsigned index,unsigned player,int bytes,const unsigned char *pcm,xmp_context context);
'''
# xmp_context is defined by existing includes before the inserted prefix.
anchor='#define CHECK(c)';at=test.index(anchor);test=test[:at]+prefix+test[at:]
old='''            for(k=0;k<(unsigned)bytes;++k){pcm_hash^=actual[k];pcm_hash*=16777619u;}'''
assert test.count(old)==1;test=test.replace(old,'            trace_block(phase,j,i,bytes,actual,shared[i]);\n'+old)
tail='''
static void trace_block(unsigned phase,unsigned index,unsigned player,int bytes,const unsigned char *pcm,xmp_context context){
    struct xmp_frame_info frame;uint32_t header[11];xmp_get_frame_info(context,&frame);
    header[0]=phase;header[1]=index;header[2]=player;header[3]=(uint32_t)bytes;header[4]=(uint32_t)frame.pos;header[5]=(uint32_t)frame.row;header[6]=(uint32_t)frame.frame;header[7]=(uint32_t)frame.speed;header[8]=(uint32_t)frame.bpm;header[9]=(uint32_t)frame.time;header[10]=(uint32_t)frame.loop_count;
    CHECK(fwrite(header,sizeof(header),1,stdout)==1&&fwrite(pcm,1024,1,stdout)==1);
}
'''
old='    return 0;\n}';assert test.count(old)==1
report='''    fprintf(stderr,"{\\"pow_calls\\":%llu",(unsigned long long)proxy_pow_calls);
#ifdef PVZH1_CANDIDATE
    {H1XmpNumericStats n=pvzh1_xmp_note_cache_stats(),s=pvzh1_xmp_step_cache_stats();fprintf(stderr,",\\"note_calls\\":%llu,\\"note_hits\\":%llu,\\"step_calls\\":%llu,\\"step_hits\\":%llu",(unsigned long long)n.calls,(unsigned long long)n.hits,(unsigned long long)s.calls,(unsigned long long)s.hits);}
#endif
    fprintf(stderr,"}\\n");
'''
test=test.replace(old,report+old);(OUT/'observer.c').write_text(test+tail)
test=test.replace('int main(int argc,char **argv)\n{','int main(int argc,char **argv)\n{\n    _setmode(_fileno(stdout),_O_BINARY);')
(OUT/'observer.c').write_text(test+tail)
executables={}
for mode,core,extra in [('pristine',pristine,[]),('candidate',candidate,['-DH1_PVZ_PORTABLE','-DPVZH1_XMP_NUMERIC_STATS','-DPVZH1_CANDIDATE'])]:
    for kind,src in [('oracle',OUT/'observer.c'),('menu',OUT/'menu.c')]:
        obj=compile(src,mode+'-'+kind,extra);exe=OUT/(mode+'-'+kind+'.exe')
        execute([cc,*common,*core,obj,'-Wl,--wrap=pow','-lm','-o',exe],mode+'-'+kind+'.link.log');executables[mode,kind]=exe
def compare(kind,module):
    commands=[[str(executables[mode,kind]),str(module)] for mode in ('pristine','candidate')]
    processes=[subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env) for cmd in commands]
    digest=hashlib.sha256();count=0
    try:
        while True:
            a=processes[0].stdout.read(32768);b=processes[1].stdout.read(32768)
            if a!=b:
                offset=next((i for i in range(min(len(a),len(b))) if a[i]!=b[i]),min(len(a),len(b)))
                raise RuntimeError(f'{kind}/{module.name}: pristine/candidate difference at stream byte {count+offset}')
            if not a:break
            digest.update(a);count+=len(a)
        logs=[]
        for mode,p in zip(('pristine','candidate'),processes):
            err=p.stderr.read().decode();rc=p.wait(timeout=20);(OUT/(mode+'-'+kind+'-'+module.name+'.log')).write_text(err)
            assert not rc,(rc,err);logs.append([json.loads(line) for line in err.splitlines() if line.startswith('{')])
        expected=(1068 if kind=='oracle' else 1060)*7200
        assert count==expected,(count,expected)
        return dict(kind=kind,module=module.name,module_sha256=sha(module),compared_blocks=7200,compared_stream_bytes=count,
            PCM_and_order_row_frame_speed_BPM_time_loop_equal=True,stream_sha256=digest.hexdigest(),pristine=logs[0],candidate=logs[1])
    finally:
        for p in processes:
            if p.poll() is None:p.kill()
modules=sorted((ROOT/'private/resources-h1-wide/sounds').glob('*.xm'))
assert modules,'Runtime tracker modules missing'
reports=[compare('oracle',module) for module in modules]
reports.append(compare('menu',ROOT/'private/resources-h1-wide/sounds/mainmusic.xm'))
unit=compile(OUT/'unit.c','unit',['-DH1_PVZ_PORTABLE','-DPVZH1_XMP_NUMERIC_STATS','-Werror'])
unit_exe=OUT/'unit.exe';execute([cc,*common,*candidate,unit,'-lm','-o',unit_exe],'unit.link.log')
unit_result=json.loads(execute([unit_exe],'unit.log'))
assert all(sha(p)==digest for p,digest in before.items())
result=dict(passed=True,production_hooked=False,vendor_unchanged=True,build_copy_include_fix_pending_patch_sync=pending_include_sync,method='Separate pristine and candidate executables; PCM+logical-state streams compared incrementally without audio files.',
    reports=reports,unit=unit_result,patch_sha256=sha(ROOT/'platform/audio/xmp_numeric_patches.py'),header_sha256=sha(ROOT/'platform/audio/xmp_numeric_cache.h'),
    generated_sources={str(p.relative_to(ROOT)):sha(p) for p in overrides.values()},baseline_objects=identities,
    executables={mode+'-'+kind:sha(exe) for (mode,kind),exe in executables.items()})
(OUT/'report.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(passed=True,reports=reports,unit=unit_result,report_sha256=sha(OUT/'report.json')),indent=2))
