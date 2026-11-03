"""Compare pixel payload I/O using identical genuine saves, not game state edits."""
from pathlib import Path
import hashlib,json,os,shutil,subprocess,sys
from concurrent.futures import ThreadPoolExecutor

ROOT=Path(__file__).resolve().parents[2]
BUILD=ROOT/'build/host'
DEST=BUILD/'cache-benchmark'

def run(name,prefer):
    directory=DEST/name;save=directory/'save';output=directory/'output'
    shutil.copytree(BUILD/'save16',save);output.mkdir()
    env=os.environ.copy()
    env.update(PVZ_RESOURCE_DIR=(ROOT/'private/resources').as_posix()+'/',PVZ_SAVE_DIR=save.as_posix()+'/',
        PVZ_HOST_OUTPUT=output.as_posix(),PVZ_HOST_MAX_FRAMES='1500',PVZ_HOST_SPEED='3',PVZ_HOST_AUTOPLAY='1',
        PVZ_HOST_SCRIPT=(ROOT/'platform/host/cache-benchmark.script').as_posix(),
        PVZ_HOST_CACHE_PREFER_LARGE='1' if prefer else '0')
    with (directory/'runtime-output.txt').open('w') as log:
        result=subprocess.run([str(BUILD/'pvz-portable-host.exe')],env=env,cwd=ROOT,stdout=log,stderr=log)
    assert result.returncode==0
    states=[json.loads(line) for line in (output/'states.jsonl').read_text().splitlines()]
    entered=next(s['frame'] for s in states if s['event']=='scene' and s['scene']==3)
    sample=[s for s in states if s['scene']==3 and s['event']=='frame' and entered+60<=s['frame']<=entered+240 and not s.get('dialogs')]
    assert len(sample)>=3
    first,last=sample[0],sample[-1];frames=last['frame']-first['frame'];assert frames>=120
    payload=last['image_pixel_payload_bytes_loaded']-first['image_pixel_payload_bytes_loaded']
    return {'prefer_large_image':prefer,'exit_code':0,'source_save':'build/host/save16',
        'output':output.relative_to(ROOT).as_posix(),'sample_from':first,'sample_to':last,
        'measured_frames':frames,'pixel_payload_bytes_read':payload,'pixel_payload_bytes_per_frame':payload/frames,
        'image_loads':last['image_loads']-first['image_loads'],
        'image_evictions':last['image_evictions']-first['image_evictions'],
        'image_load_failures':max(s['image_load_failures'] for s in states)}

def main():
    if DEST.exists():raise SystemExit('Comparison output already exists; preserve its evidence before choosing a new destination.')
    DEST.mkdir()
    exe=BUILD/'pvz-portable-host.exe'
    with ThreadPoolExecutor(max_workers=2) as pool:
        results=list(pool.map(lambda pair:run(*pair),(('plain-lru',False),('prefer-large',True))))
    plain,preferred=results
    assert preferred['pixel_payload_bytes_per_frame']<plain['pixel_payload_bytes_per_frame']
    report={'scope':'Host I/O comparison, not H1 throughput measurement. Both runs use independent byte-for-byte copies of the same original saved game.',
        'executable_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'runs':results,
        'payload_reduction_percent':100*(1-preferred['pixel_payload_bytes_per_frame']/plain['pixel_payload_bytes_per_frame']),
        'pixel_regression':'Native MemoryImage draw output is byte-identical before/after largest-source retention in platform/graphics/tests/test_software.cpp; unused previous-frame large images remain evictable.',
        'budget':'The same completed-frame hard 6 MiB source budget is used in both runs; active frames may exceed the budget.'}
    target=DEST/'comparison.json';target.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'report':str(target),'payload_reduction_percent':report['payload_reduction_percent']}))

if __name__=='__main__':main()
