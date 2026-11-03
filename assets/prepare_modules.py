"""Decode original MO3 modules, then adapt XM sample storage for H1 RAM.

Orders, patterns, effects, instrument maps and envelopes are retained byte for
byte. Sample PCM is decimated 2:1 with FFmpeg's band-limited resampler, quantized
to signed 8-bit, and XM relativeNote is decreased by 12 semitones to retain the
original pitch and duration. Source XM files are kept separately for auditing.
"""
from __future__ import annotations

import argparse
from array import array
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'private/source/sounds'
MUSIC=ROOT/'private/music'
OUTPUT=ROOT/'private/resources/sounds'


def sha(data):return hashlib.sha256(data).hexdigest()


def read_xm(data):
    if not data.startswith(b'Extended Module: '):raise ValueError('UNMO3 output is not XM')
    header=struct.unpack_from('<I8H',data,60)
    header_size,orders,restart,channels,patterns,instruments,flags,speed,bpm=header
    offset=60+header_size
    pattern_info=[]
    for index in range(patterns):
        size,packing,rows,packed=struct.unpack_from('<IBHH',data,offset)
        pattern_info.append(dict(index=index,rows=rows,packed_bytes=packed))
        offset+=size+packed
    prefix=data[:offset]
    result=[]
    for index in range(instruments):
        size=struct.unpack_from('<I',data,offset)[0]
        count=struct.unpack_from('<H',data,offset+27)[0]
        sh_size=struct.unpack_from('<I',data,offset+29)[0] if count else 0
        if count and sh_size<40:raise ValueError('Short sample header')
        instrument=data[offset:offset+size]
        offset+=size
        headers=[data[offset+i*sh_size:offset+(i+1)*sh_size] for i in range(count)]
        offset+=count*sh_size
        samples=[]
        for sample_index,head in enumerate(headers):
            length,loop_start,loop_length=struct.unpack_from('<III',head)
            kind=head[14]
            if kind&32:raise ValueError('Unexpected stereo XM sample')
            payload=data[offset:offset+length]
            if len(payload)!=length:raise ValueError('Truncated XM sample')
            offset+=length
            samples.append(dict(instrument=index,sample=sample_index,header=head,payload=payload,
                                length=length,loop_start=loop_start,loop_length=loop_length,
                                kind=kind,relative_note=struct.unpack_from('b',head,16)[0]))
        result.append((instrument,samples))
    if offset!=len(data):raise ValueError('Unexpected data after XM instruments')
    metadata=dict(channels=channels,order_count=orders,orders=list(data[80:80+orders]),
                  pattern_count=patterns,instrument_count=instruments,restart=restart,
                  flags=flags,initial_speed=speed,initial_bpm=bpm,patterns=pattern_info,
                  pattern_and_order_prefix_sha256=sha(prefix))
    return prefix,result,metadata


def delta_to_pcm16(sample):
    payload=sample['payload']
    if sample['kind']&16:
        values=array('h');values.frombytes(payload)
        previous=0
        for i,value in enumerate(values):
            previous=(previous+value)&65535
            values[i]=previous if previous<32768 else previous-65536
    else:
        values=array('h');previous=0
        for value in payload:
            previous=(previous+value)&255
            values.append((previous if previous<128 else previous-256)<<8)
    return values


def convert_sample(sample,ffmpeg,folder):
    head=bytearray(sample['header'])
    pcm=delta_to_pcm16(sample)
    original_points=len(pcm)
    source=folder/f"i{sample['instrument']:03}-s{sample['sample']:03}.s16"
    target=source.with_suffix('.s8')
    if original_points>=32:
        source.write_bytes(pcm.tobytes())
        subprocess.run([ffmpeg,'-v','error','-nostdin','-y','-f','s16le','-ar','32000',
                        '-ac','1','-i',str(source),'-af','aresample=16000:dither_method=triangular',
                        '-ar','16000','-acodec','pcm_s8','-f','s8',str(target)],check=True)
        raw=target.read_bytes()
        source.unlink();target.unlink()
    else:
        # SWResample's filter delay can consume extremely short empty/silent
        # samples. A two-point average retains their length and polarity.
        raw=bytes((max(-128,min(127,round((pcm[i]+pcm[min(i+1,original_points-1)])/512)))&255)
                  for i in range(0,original_points,2))
    output=bytearray(len(raw));previous=0
    for i,value in enumerate(raw):
        output[i]=(value-previous)&255;previous=value
    original_stride=2 if sample['kind']&16 else 1
    start=sample['loop_start']//original_stride
    end=(sample['loop_start']+sample['loop_length'])//original_stride
    new_start=min(len(raw),(start+1)//2)
    new_end=min(len(raw),(end+1)//2)
    if original_points and sample['kind']&3 and end>start and new_end<=new_start:
        raise ValueError(f"Loop collapsed: instrument {sample['instrument']} sample {sample['sample']}: original {original_points} points loop {start}:{end}; new {len(raw)} points loop {new_start}:{new_end}")
    struct.pack_into('<III',head,0,len(output),new_start,new_end-new_start)
    head[14]=sample['kind']&~16
    new_note=sample['relative_note']-12
    if not -128<=new_note<=127:raise ValueError('XM relativeNote overflow')
    struct.pack_into('b',head,16,new_note)
    # Everything beyond lengths / bitdepth / tuning must stay exactly intact.
    invariant_before=bytearray(sample['header']);invariant_after=bytearray(head)
    for part in [slice(0,12),slice(14,15),slice(16,17)]:
        invariant_before[part]=bytes(len(invariant_before[part]))
        invariant_after[part]=bytes(len(invariant_after[part]))
    if invariant_before!=invariant_after:raise ValueError('Unintended sample-header change')
    meta=dict(instrument=sample['instrument'],sample=sample['sample'],
              original_points=original_points,new_points=len(raw),
              original_sample_bytes=sample['length'],new_sample_bytes=len(output),
              original_bit_depth=16 if original_stride==2 else 8,new_bit_depth=8,
              original_relative_note=sample['relative_note'],new_relative_note=new_note,
              original_loop_points=[start,end],new_loop_points=[new_start,new_end],
              loop_mode=sample['kind']&3,
              loop_state='empty-sample' if not original_points else 'active' if sample['kind']&3 else 'not-looped',
              loop_rounding_error_in_original_points=[new_start*2-start,new_end*2-end] if original_points and sample['kind']&3 else None,
              original_encoded_sample_sha256=sha(sample['payload']),
              new_encoded_sample_sha256=sha(output))
    return bytes(head),bytes(output),meta


def pcm_preview(path,dest):
    subprocess.run([shutil.which('ffmpeg'),'-v','error','-nostdin','-y','-i',str(path),
                    '-t','10','-ac','1','-ar','16000','-acodec','pcm_s16le','-f','s16le',str(dest)],check=True)
    values=array('h');values.frombytes(dest.read_bytes());return values


def correlation(a,b):
    n=min(len(a),len(b))
    if not n:return 0.
    aa=sum(float(v)*v for v in a[:n]);bb=sum(float(v)*v for v in b[:n])
    ab=sum(float(a[i])*b[i] for i in range(n))
    return ab/math.sqrt(aa*bb) if aa and bb else 0.


def fft_magnitudes(samples,start,n=2048):
    values=[complex(samples[start+i]*(.5-.5*math.cos(2*math.pi*i/(n-1))),0) for i in range(n)]
    j=0
    for i in range(1,n):
        bit=n>>1
        while j&bit:j^=bit;bit>>=1
        j^=bit
        if i<j:values[i],values[j]=values[j],values[i]
    size=2
    while size<=n:
        half=size//2;step=complex(math.cos(-2*math.pi/size),math.sin(-2*math.pi/size))
        for start_block in range(0,n,size):
            phase=1+0j
            for k in range(half):
                even=values[start_block+k];odd=values[start_block+k+half]*phase
                values[start_block+k]=even+odd;values[start_block+k+half]=even-odd;phase*=step
        size*=2
    return [abs(v) for v in values[:n//2+1]]


def compare_previews(original,compact,name):
    old=pcm_preview(original,MUSIC/(name+'-original-10s.s16'))
    new=pcm_preview(compact,MUSIC/(name+'-h1-10s.s16'))
    count=min(len(old),len(new))
    rms_old=math.sqrt(sum(float(v)*v for v in old)/len(old))
    rms_new=math.sqrt(sum(float(v)*v for v in new)/len(new))
    error=math.sqrt(sum((old[i]-new[i])**2 for i in range(count))/count)
    envelope=lambda data:[math.sqrt(sum(float(v)*v for v in data[i:i+1600])/len(data[i:i+1600])) for i in range(0,len(data),1600)]
    spectral=[]
    for second in [1,3,5,7,9]:
        a=fft_magnitudes(old,second*16000);b=fft_magnitudes(new,second*16000)
        search=range(7,129) # Dominant 55-1000Hz bin, not a monophonic pitch claim.
        pa=max(search,key=lambda i:a[i]);pb=max(search,key=lambda i:b[i])
        bands={}
        for low,high in [(0,250),(250,1000),(1000,4000),(4000,8000)]:
            lo=round(low/16000*2048);hi=min(len(a),round(high/16000*2048))
            ea=sum(v*v for v in a[lo:hi]);eb=sum(v*v for v in b[lo:hi])
            bands[f'{low}-{high}Hz']=10*math.log10(max(eb,1e-10)/max(ea,1e-10))
        spectral.append(dict(second=second,dominant_original_hz=pa*16000/2048,
                             dominant_h1_hz=pb*16000/2048,band_energy_change_db=bands))
    return dict(duration_original_seconds=len(old)/16000,duration_h1_seconds=len(new)/16000,
                waveform_correlation=correlation(old,new),rms_original=rms_old,rms_h1=rms_new,
                rms_error=error,signal_to_error_db=20*math.log10(rms_old/max(error,1e-12)),
                rms_envelope_correlation=correlation(envelope(old),envelope(new)),spectral_checks=spectral)


def all_subsong_duration(path):
    ffprobe=shutil.which('ffprobe')
    if not ffprobe:raise FileNotFoundError('FFprobe is required for full sequencing duration checks')
    value=subprocess.check_output([ffprobe,'-v','error','-subsong','-1','-show_entries','format=duration',
                                   '-of','default=noprint_wrappers=1:nokey=1',str(path)],text=True).strip()
    return float(value)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unmo3',type=Path,default=ROOT/'private/mo3-tools/un4seen/unmo3.exe')
    parser.add_argument('--workers',type=int,default=4)
    args=parser.parse_args()
    ffmpeg=shutil.which('ffmpeg')
    if not ffmpeg or not args.unmo3.is_file():raise FileNotFoundError('UNMO3 and FFmpeg are required')
    MUSIC.mkdir(parents=True,exist_ok=True);OUTPUT.mkdir(parents=True,exist_ok=True)
    report=dict(official_tool_url='https://www.un4seen.com/mo3.html',
                official_archive_url='https://www.un4seen.com/files/mo324.zip',
                unmo3_sha256=sha(args.unmo3.read_bytes()),
                unmo3_licence='MO3.TXT: free for non-commercial use; tool stays in private host build cache',
                ffmpeg=subprocess.check_output([ffmpeg,'-version'],text=True).splitlines()[0],
                adaptation='8-bit mono XM samples at half native rate; relativeNote -12; loop points divided by 2 with nearest-point rounding; all original patterns and instrument maps retained',
                modules={})
    archive=args.unmo3.parent.parent/'mo324.zip'
    if archive.is_file():report['unmo3_archive_sha256']=sha(archive.read_bytes())
    for name in ['mainmusic','mainmusic_hihats']:
        source=SOURCE/(name+'.mo3');original=MUSIC/(name+'.original.xm')
        subprocess.run([str(args.unmo3),'-s',str(source),str(original)],check=True)
        data=original.read_bytes();prefix,instruments,metadata=read_xm(data)
        folder=MUSIC/(name+'-sample-temp');folder.mkdir(exist_ok=True)
        samples=[s for header,entries in instruments for s in entries]
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            converted=list(pool.map(lambda s:convert_sample(s,ffmpeg,folder),samples))
        output=bytearray(prefix);sample_index=0;sample_report=[]
        for header,entries in instruments:
            output+=header
            block=converted[sample_index:sample_index+len(entries)]
            for head,payload,meta in block:output+=head
            for head,payload,meta in block:output+=payload;sample_report.append(meta)
            sample_index+=len(entries)
        compact=OUTPUT/(name+'.xm');compact.write_bytes(output)
        new_prefix,new_instruments,new_metadata=read_xm(output)
        if prefix!=new_prefix or metadata!=new_metadata:raise ValueError('Original XM sequencing data changed')
        comparisons=compare_previews(original,compact,name)
        if comparisons['duration_original_seconds']!=comparisons['duration_h1_seconds']:
            raise ValueError('Preview duration changed')
        original_duration=all_subsong_duration(original);compact_duration=all_subsong_duration(compact)
        if original_duration!=compact_duration:raise ValueError('Full XM sequencing duration changed')
        report['modules'][name]=dict(source_mo3_sha256=sha(source.read_bytes()),
                                    original_xm_path=original.relative_to(ROOT).as_posix(),
                                    original_xm_bytes=len(data),original_xm_sha256=sha(data),
                                    h1_xm_path=compact.relative_to(ROOT/'private/resources').as_posix(),
                                    h1_xm_bytes=len(output),h1_xm_sha256=sha(output),
                                    sequencing=metadata,samples=sample_report,preview_comparison=comparisons,
                                    original_sample_bytes=sum(x['length'] for x in samples),
                                    new_sample_bytes=sum(x['new_sample_bytes'] for x in sample_report),
                                    all_subsong_duration_original_xm_seconds=original_duration,
                                    all_subsong_duration_h1_xm_seconds=compact_duration)
        print(json.dumps(dict(module=name,original_bytes=len(data),h1_bytes=len(output),
                              sample_count=len(samples),correlation=comparisons['waveform_correlation'],
                              envelope_correlation=comparisons['rms_envelope_correlation'])),flush=True)
    (ROOT/'private/resources/module-index.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    (ROOT/'assets/module-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')


if __name__=='__main__':main()
