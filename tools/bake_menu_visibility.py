"""Cull selector fragments hidden by invariant later opaque tracks, offline.

Attachments stay at their original track boundary. Runtime checks the invariant
tracks before using this bundle; unsupported poses use the original renderer.
"""
from pathlib import Path
import argparse
import hashlib
import itertools
import json
import struct
import zlib

FIXED = (22,23,25,27,29,45,46,47)
TAG = 0x31434c56  # VLC1


def decode(data, row, compressed):
    kind, ident, variant, color, runs, pixels, offset, size, pos, dims = row
    raw = data[offset:offset+size]
    if compressed: raw=zlib.decompress(raw)
    assert len(raw)==runs*12+pixels*4
    words=struct.unpack_from('<'+'I'*pixels,raw,runs*12) if pixels else ()
    ops=[]
    for dst,count,blend,word in struct.iter_unpack('<IHHI',raw[:runs*12]):
        ops.extend((dst+i,blend,words[word+i]) for i in range(count))
    return ops


def pack(ops, width):
    runs=[];words=[];last=-2
    for dst,kind,word in ops:
        if runs and dst==last+1 and dst//width==last//width and runs[-1][2]==kind:runs[-1][1]+=1
        else:runs.append([dst,1,kind,len(words)])
        words.append(word);last=dst
    return b''.join(struct.pack('<IHHI',*r) for r in runs)+struct.pack('<'+'I'*len(words),*words),len(runs),len(words)


def replay(entries, selected, seed, pixels):
    out=[0xff000000|((i*0x164b35+seed*0x213141)&0xffffff) for i in range(pixels)]
    for key in selected:
        for dst,kind,value in entries[key]:
            if not kind:out[dst]=value
            else:
                p=out[dst];inv=value>>24
                out[dst]=0xff000000|((value&0xffffff)+((((p&0xff00ff)*inv)>>8)&0xff00ff)+((((p&0xff00)*inv)>>8)&0xff00))
    return out


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--adventure-input',type=Path)
    parser.add_argument('--adventure-output',type=Path)
    args=parser.parse_args();data=args.input.read_bytes();h=list(struct.unpack_from('<16I',data))
    rows=[list(struct.unpack_from('<10I',data,64+i*40)) for i in range(h[8])]
    original={tuple(r[:4]):decode(data,r,h[13]) for r in rows};cull={}
    opaque={}
    for ident in FIXED:
        variants=[{dst for dst,kind,_ in ops if not kind} for key,ops in original.items() if key[0]==1 and key[1]==ident]
        assert variants
        opaque[ident]=set.intersection(*variants)
    for key,ops in original.items():
        if key[0]!=1:cull[key]=ops;continue
        covered=set().union(*(opaque[i] for i in FIXED if key[1]==14 or i>key[1]))
        cull[key]=[op for op in ops if op[0] not in covered]
    # Test every authored track variant against unchanged ordered replay.
    # Non-invariant conditional tracks may independently be absent/present.
    groups={i:[k for k in original if k[0]==1 and k[1]==i] for i in range(48)}
    active=[14,21,22,23,24,25,27,29,31,33,45,46,47]
    choices=[groups[i]+([None] if i in (21,24,31,33) else []) for i in active]
    checks=0
    for selection in itertools.product(*choices):
        selected=[k for k in selection if k is not None]
        # Every authored press/shadow combination and both adventure shadows.
        assert replay(original,selected,17,h[4]*h[5])==replay(cull,selected,17,h[4]*h[5])
        checks+=1
    directory=bytearray();payload=bytearray();at=64+40*len(rows)
    for row in rows:
        raw,runs,pixels=pack(cull[tuple(row[:4])],h[4]);row[4:8]=runs,pixels,at,len(raw)
        directory+=struct.pack('<10I',*row);payload+=raw;at+=len(raw)
    h[3]=at;h[13]=0;h[14]=TAG
    result=struct.pack('<16I',*h)+directory+payload
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(result)
    manifest=dict(input_sha256=hashlib.sha256(data).hexdigest(),output_sha256=hashlib.sha256(result).hexdigest(),
                  input_bytes=len(data),bytes=len(result),fixed_tracks=FIXED,variant_replay_checks=checks,
                  runtime_path='h1menu/selector-wide.h1mb',encoder_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    if args.adventure_input:
        assert args.adventure_output
        packed=args.adventure_input.read_bytes();ah=list(struct.unpack_from('<16I',packed));payload=bytearray();directory=bytearray();at=64+40*ah[8]
        for i in range(ah[8]):
            row=list(struct.unpack_from('<10I',packed,64+i*40));raw=packed[row[6]:row[6]+row[7]]
            if ah[13]:raw=zlib.decompress(raw)
            assert len(raw)==row[4]*12+row[5]*4
            row[6:8]=at,len(raw);directory+=struct.pack('<10I',*row);payload+=raw;at+=len(raw)
        ah[3]=at;ah[13]=0;expanded=struct.pack('<16I',*ah)+directory+payload
        args.adventure_output.parent.mkdir(parents=True,exist_ok=True);args.adventure_output.write_bytes(expanded)
        manifest['adventure']=dict(input_sha256=hashlib.sha256(packed).hexdigest(),output_sha256=hashlib.sha256(expanded).hexdigest(),bytes=len(expanded),entries=ah[8],all_payloads_byte_exact=True)
    args.output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf8')
    print(json.dumps(manifest))


if __name__=='__main__':main()
