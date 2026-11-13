"""Offline integrity and structure checks for LCD-density combat banks."""
from pathlib import Path
import hashlib, json, struct, zlib
from pack_actor_pages import decode
ROOT = Path(__file__).resolve().parents[1]
def digest(b): return hashlib.sha256(b).hexdigest()
def verify(resources):
    proof=json.loads((ROOT/'provenance/offline-combat-sprites.json').read_text())
    assert proof['dimensions']==[480,272] and proof['splat_variants']==4
    for name, sha in proof['source_files'].items(): assert digest((ROOT/name).read_bytes())==sha,name
    for report in proof['export_proofs'].values(): assert report['passed']
    entries={}
    for name, info in proof['files'].items():
        blob=(resources/name).read_bytes();assert len(blob)==info['bytes'] and digest(blob)==info['sha256'],name
        raw=decode(blob) if blob[:4]==b'H1PZ' else blob
        assert len(raw)==info['decoded_bytes'] and digest(raw)==info['decoded_sha256']
        h=struct.unpack_from('<16I',raw);assert h[:4]==(0x50413148,1,480,272)
        assert h[9]==len(raw) and zlib.crc32(raw[64:])==h[10] and h[14]==0x314d4352
        previous=None
        for i in range(h[4]):
            key,frame,start,length,kind=struct.unpack_from('<Q4I',raw,64+24*i)
            assert previous is None or previous<(key,frame)
            previous=key,frame;assert h[8]<=start<=len(raw)-length and length>=16
            segments,mode=struct.unpack_from('<HH',raw,start+4)
            assert segments<=257 and mode==(1 if name=="zombie-hats.h1ap" else h[15])
            at=start+16
            for _ in range(segments):
                track,draw,runs,payload=struct.unpack_from('<hHII',raw,at);at+=12
                if mode&64:at+=4
                if track==-2:
                    assert mode&1024 and not runs and draw in (0,1)
                    first,count=payload&65535,payload>>16
                    assert count and first+count<=256
                    continue
                if payload&0x80000000:
                    p=h[8]+(payload&0x7fffffff);size,=struct.unpack_from('<I',raw,p)
                    assert p+4+size<=len(raw)
                else:assert at+payload<=start+length;at+=payload
            assert at==start+length
        entries[name]=h[4]
    assert entries=={'combat-sprites.h1ap':816,'zombie-hats.h1ap':3000,'pea-transitions.h1ap':2940}
    assert not (resources/'plant-shadows.h1ap').exists()
    return dict(passed=True,entries=entries,runtime_bytes=sum(r['bytes'] for r in proof['files'].values()),scope='Offline byte/packet integrity; pixel oracles are recorded separately, no runtime checks')
if __name__=='__main__': print(json.dumps(verify(ROOT/'private/resources-h1-wide')))
