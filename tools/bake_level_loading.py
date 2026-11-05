"""Losslessly pack the original loader at its final 480x272 LCD size."""
from pathlib import Path
import argparse,array,struct,zlib,hashlib,json
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser();p.add_argument('--input',type=Path,required=True);a=p.parse_args();data=a.input.read_bytes();assert struct.unpack_from('<4I',data)==(0x4c4c3148,1,480,272) and len(data)==16+480*272*8
 bg=data[16:16+480*272*4];full=data[16+480*272*4:];first=array.array('I');first.frombytes(bg);last=array.array('I');last.frombytes(full)
 diff=[i for i in range(480*272) if first[i]!=last[i]];assert diff
 x=min(i%480 for i in diff);right=max(i%480 for i in diff)+1;y=min(i//480 for i in diff);bottom=max(i//480 for i in diff)+1;w=right-x;h=bottom-y
 patch=b''.join(full[(row*480+x)*4:(row*480+right)*4] for row in range(y,bottom));base_z=zlib.compress(bg,9);patch_z=zlib.compress(patch,9)
 header=[0x4c4c3148,1,480,272,len(bg),len(base_z),x,y,w,h,len(patch_z),zlib.crc32(bg),zlib.crc32(patch),64+len(base_z)+len(patch_z),0,0]
 out=ROOT/'private/resources-h1-wide/level-loading.h1ll';out.write_bytes(struct.pack('<16I',*header)+base_z+patch_z)
 sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();m={'version':1,'runtime_path':out.name,'bytes':out.stat().st_size,'sha256':sha(out),'dimensions':[480,272],'background_bytes':len(bg),'patch_rectangle':[x,y,w,h],'patch_bytes':len(patch),'background_crc32':f'{header[11]:08x}','patch_crc32':f'{header[12]:08x}','lossless':True,'exporter_sha256':sha(ROOT/'tools/tests/level_loading_export.cpp'),'encoder_sha256':sha(Path(__file__)),'scope':'Original startup background, logo, dirt/grass bar and translated loading label; lossless final-LCD pixels. Runtime inflates into the existing screen buffer and reveals the precomposited grass patch.'}
 (ROOT/'provenance/offline-level-loading.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m))
if __name__=='__main__':main()
