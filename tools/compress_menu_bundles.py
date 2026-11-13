"""Losslessly package existing baked menu entries with the supported H1MB codec."""
from pathlib import Path
import hashlib,json,struct,zlib,importlib.util

ROOT=Path(__file__).resolve().parents[1]
sha=lambda data:hashlib.sha256(data).hexdigest()

def decoded_bundle(data):
    header=list(struct.unpack_from('<16I',data));directory=bytearray();payload=bytearray()
    offset=64+40*header[8]
    for i in range(header[8]):
        row=list(struct.unpack_from('<10I',data,64+i*40));raw=data[row[6]:row[6]+row[7]]
        if header[13]:raw=zlib.decompress(raw)
        assert len(raw)==row[4]*12+row[5]*4
        row[6:8]=offset,len(raw);directory+=struct.pack('<10I',*row);payload+=raw;offset+=len(raw)
    header[3]=offset;header[13]=0
    return struct.pack('<16I',*header)+directory+payload

def main():
    inventory=ROOT/'provenance/resources.json'
    meta=json.loads(inventory.read_text(encoding='utf8'));base=ROOT/meta['directory']
    changed={};proof=[]
    for relative in ('h1menu/selector-wide.h1mb','h1menu/adventure.h1mb'):
        path=base/relative;data=path.read_bytes();header=list(struct.unpack_from('<16I',data))
        assert header[0]==0x424d3148 and header[1:3]==[1,64] and header[3]==len(data)
        rows=[list(struct.unpack_from('<10I',data,64+i*40)) for i in range(header[8])]
        directory=bytearray();payload=bytearray();offset=64+40*len(rows);raw_bytes=0
        raw_directory=bytearray();raw_payload=bytearray();raw_offset=offset
        for row in rows:
            raw=data[row[6]:row[6]+row[7]]
            if header[13]:raw=zlib.decompress(raw) if raw else b''
            assert len(raw)==row[4]*12+row[5]*4
            raw_row=list(row);raw_row[6:8]=raw_offset,len(raw)
            raw_directory+=struct.pack('<10I',*raw_row);raw_payload+=raw;raw_offset+=len(raw)
            packed=zlib.compress(raw,9)
            assert zlib.decompress(packed)==raw
            raw_bytes+=len(raw);row[6:8]=offset,len(packed)
            directory+=struct.pack('<10I',*row);payload+=packed;offset+=len(packed)
        header[3]=offset;header[13]=1
        raw_header=list(header);raw_header[3]=raw_offset;raw_header[13]=0
        canonical=struct.pack('<16I',*raw_header)+raw_directory+raw_payload
        result=struct.pack('<16I',*header)+directory+payload
        path.write_bytes(result);changed[relative]=(len(data),len(result),sha(result))
        proof.append(dict(path=relative,input_sha256=sha(data),output_sha256=sha(result),
                          input_bytes=len(data),bytes=len(result),uncompressed_payload_bytes=raw_bytes,
                          uncompressed_bundle_bytes=len(canonical),uncompressed_sha256=sha(canonical),
                          entries=len(rows),all_decompressed_payloads_byte_exact=True))
    spec=importlib.util.spec_from_file_location('h1_file_index',ROOT/'tools/bake_file_index.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    index=base/module.NAME;old_size=index.stat().st_size
    data,entries=module.encode(base);index.write_bytes(data)
    changed[module.NAME]=(old_size,len(data),sha(data))
    for relative,(before,after,digest) in changed.items():
        meta['files'][relative]=digest;meta['bytes']+=after-before
    inventory.write_text(json.dumps(meta,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    index_proof=ROOT/'provenance/offline-file-index.json'
    index_meta=json.loads(index_proof.read_text(encoding='utf8'))
    index_meta.update(bytes=len(data),sha256=sha(data),entries=len(entries))
    index_proof.write_text(json.dumps(index_meta,indent=2)+'\n',encoding='utf8')
    proof=dict(codec='Existing H1MB v1 compressed-entry flag; no renderer or pixels changed',
               encoder_sha256=sha(Path(__file__).read_bytes()),bundles=proof,
               total_saved_bytes=sum(row['uncompressed_bundle_bytes']-row['bytes'] for row in proof))
    adventure=ROOT/'provenance/offline-adventure.json'
    original=json.loads(adventure.read_text(encoding='utf8'));row=proof['bundles'][1]
    original.update(bytes=row['bytes'],sha256=row['output_sha256'])
    original['runtime_packaging']=dict(encoder='tools/compress_menu_bundles.py',
        encoder_sha256=proof['encoder_sha256'],uncompressed_sha256=row['uncompressed_sha256'],
        all_payloads_byte_exact=True,hand_frames='Compressed in RAM; expand only the displayed frame')
    adventure.write_text(json.dumps(original,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    (ROOT/'provenance/compressed-menu-bundles.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8')
    print(json.dumps(proof));print('Runtime resource bytes',meta['bytes'])

if __name__=='__main__':main()
