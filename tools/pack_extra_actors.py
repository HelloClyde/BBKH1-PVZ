"""Pack successful original-shader captures; retain independent offline proofs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from audit_offline_animations import inventory

ROOT=Path(__file__).resolve().parents[1]
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'build/extra-actor-packed')
    p.add_argument('--cxx',default='C:/msys64/ucrt64/bin/g++.exe')
    p.add_argument('--type',type=int,action='append',dest='selected_types',help='Explicit measured hotspot type; may be repeated')
    p.add_argument('--experimental-all',action='store_true',help='Build all candidates for experiments, never select runtime policy')
    p.add_argument('--capture',nargs=2,action='append',metavar=('PROOF','RAW_DIRECTORY'),
        help='Successful exporter report and its actor-raw directory; may be repeated for resumed captures')
    a=p.parse_args()
    if not a.selected_types and not a.experimental_all:p.error('Select --type for measured hotspots, or explicitly use --experimental-all')
    output=a.output.resolve();output.mkdir(parents=True,exist_ok=True)
    proofs=output/'pack-proofs';proofs.mkdir(exist_ok=True)
    inputs={}
    captures=a.capture
    if captures is None:
        complete=ROOT/'build/extra-actor-offline-export/run/extra-actor-export-verification.json'
        try: complete_passed=json.loads(complete.read_text()).get('passed')
        except (OSError,ValueError): complete_passed=False
        captures=[(str(complete),str(complete.parent/'actor-raw'))] if complete_passed else [
        ('build/extra-actor-offline-export/partial-verified-types.json','build/extra-actor-offline-export/run/actor-raw'),
        ('build/extra-actor-offline-export-tail/run/extra-actor-export-verification.json','build/extra-actor-offline-export-tail/run/actor-raw'),
        ('build/extra-actor-offline-export-fill/run/extra-actor-export-verification.json','build/extra-actor-offline-export-fill/run/actor-raw')]
    for report,raw in captures:
        for row in json.loads((ROOT/report).read_text())['types']:
            if row.get('passed',True) and row.get('frames'):
                assert row['comparisons']==row['frames']*3
                inputs[row['type']]=(ROOT/raw/(str(row['type'])+'.raw'),report,row)
    types={r['id']:r for r in inventory()['types']}
    expected={n for n,r in types.items() if r.get('candidate_coverage',r['coverage'])=='extra_actor_bank'}
    if a.selected_types:
        assert set(a.selected_types)<=expected
        expected&=set(a.selected_types)
        inputs={n:v for n,v in inputs.items() if n in expected}
        existing={int(f.stem) for f in output.glob('*.h1ap')}
        if existing-expected:p.error('Output contains unselected banks; choose a fresh --output directory')
    assert set(inputs)==expected
    encoder=ROOT/'tools/pack_extra_actor.cpp';encoder_sha=sha(encoder)
    executable=ROOT/'build/pack-extra-actor.exe';stamp=executable.with_suffix('.source-sha256')
    env=os.environ.copy();env['PATH']=str(Path(a.cxx).parent)+os.pathsep+env['PATH']
    if not executable.exists() or not stamp.exists() or stamp.read_text()!=encoder_sha:
        subprocess.run([a.cxx,'-std=c++20','-O2',str(encoder),'-lz','-o',str(executable)],env=env,check=True)
        stamp.write_text(encoder_sha)
    records=[]
    for type_id,(raw,report,source) in sorted(inputs.items()):
        target=output/f'{type_id:03d}.h1ap';proof=proofs/f'{type_id:03d}.json'
        reusable=False
        if proof.exists() and target.exists():
            entry=json.loads(proof.read_text())
            reusable=(entry['encoder_sha256']==encoder_sha and entry['source_proof']==report and
                entry['source_frames']==source['frames'] and entry['raw_bytes']==raw.stat().st_size and
                entry['bytes']==target.stat().st_size and entry['sha256']==sha(target))
        if not reusable:
            r=subprocess.run([str(executable),str(raw),str(target)],env=env,capture_output=True,text=True,encoding='utf-8')
            if r.returncode:raise RuntimeError(f'{type_id}: {r.stderr}')
            entry=json.loads(r.stdout)
            assert entry['pixel_roundtrip'] and entry['page_roundtrip']
            entry.update(encoder_sha256=encoder_sha,source_proof=report,source_frames=source['frames'],
                raw_bytes=raw.stat().st_size,sha256=sha(target),name=types[type_id]['type'],runtime_path=f'extra-actors/{type_id:03d}.h1ap')
            proof.write_text(json.dumps(entry,indent=2)+'\n')
        entry['raw_input']=str(raw.relative_to(ROOT)) if raw.is_relative_to(ROOT) else str(raw)
        proof.write_text(json.dumps(entry,indent=2)+'\n')
        records.append(entry)
        print(f'{type_id:03d} {types[type_id]["type"]}: {entry["bytes"]} bytes'+(' (reused)' if reusable else ''),flush=True)
    result=dict(passed=True,types=len(records),frames=sum(r['frames'] for r in records),
        bytes=sum(r['bytes'] for r in records),max_bank_bytes=max(r['bytes'] for r in records),
        encoder_sha256=encoder_sha,files=records,all_pixels_and_pages_roundtrip=True)
    (output/'verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='files'}))

if __name__=='__main__':
    main()
