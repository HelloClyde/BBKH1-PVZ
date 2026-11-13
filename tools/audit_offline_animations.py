"""List animation routes separately from target-density image storage."""
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
def inventory():
    policy_path=ROOT/'provenance/animation-render-policy.json'
    policy=json.loads(policy_path.read_text(encoding='utf-8')) if policy_path.exists() else {}
    observed={row['type']:row for row in policy.get('realtime_observations',[])}
    params=re.findall(r'mReanimationType = ReanimationType::(REANIM_\w+), .mReanimFileName = "([^"]+)"',
        (ROOT/'upstream/src/PvzpLib/Reanimator.cpp').read_text(encoding='utf-8'))
    plants=set(re.findall(r'case ReanimationType::(REANIM_\w+)',
        (ROOT/'platform/actors/PlantPoseTypes.h').read_text(encoding='utf-8')))
    known={'REANIM_ZOMBIE':'zombie-hit.h1ap','REANIM_SUN':'sun-poses.h1ap',
        'REANIM_SODROLL':'sod-roll.h1ap','REANIM_SELECTOR_SCREEN':'h1menu/selector-wide.h1mb'}
    rows=[]
    for index,(name,source) in enumerate(params):
        if name in plants: coverage,asset='plant_bank','plant-poses.h1pp'
        elif name in known: coverage,asset='dedicated_bank',known[name]
        else: coverage,asset='extra_actor_bank',f'extra-actors/{index:03d}.h1ap'
        candidate=coverage
        if coverage=='extra_actor_bank':
            coverage='measured_preview_only' if name=='REANIM_POLEVAULTER' else 'realtime_unmeasured'
            if name in observed:
                coverage='realtime_'+observed[name]['status']
        rows.append(dict(id=index,type=name,source=source,coverage=coverage,
            candidate_coverage=candidate,asset='zombie-poses.h1pp' if name=='REANIM_POLEVAULTER' else None if candidate=='extra_actor_bank' else asset))
        if name in observed:
            rows[-1]['measurement']=observed[name]
    return dict(scope='Animation type routing. Target-density images do not imply pre-rendered animation; color, scale, blend and phase coverage require replay tests.',
        counts={key:sum(r['coverage']==key for r in rows) for key in sorted({r['coverage'] for r in rows})},types=rows)

if __name__=='__main__':
    report=inventory()
    target=ROOT/'build/offline-animation-audit.json'
    target.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(counts=report['counts'],report=str(target))))
