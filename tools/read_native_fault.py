"""Decode the bounded H1FAULT.BIN written only after an abnormal BDA exit."""
import argparse,json,struct
from pathlib import Path

def decode(data):
    if len(data)!=1100:raise ValueError('Unexpected native failure record size')
    words=struct.unpack_from('<83I',data)
    if words[:3]!=(0x58463148,1,len(data)):raise ValueError('Unknown native failure record')
    return dict(version=1,status=words[3],raw_tick=words[4],runtime=list(words[5:21]),
                termination=list(words[21:25]),native_charge=list(words[25:27]),
                native_charge_fault=list(words[27:31]),heap_failure=list(words[31:51]),
                game=list(words[51:83]),
                message=data[332:588].split(b'\0',1)[0].decode('utf8',errors='replace'),
                last_path=data[588:1100].split(b'\0',1)[0].decode('utf8',errors='replace'))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('record',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();text=json.dumps(decode(a.record.read_bytes()),ensure_ascii=False,indent=2)+'\n'
    if a.output:a.output.write_text(text,encoding='utf8')
    print(text,end='')
if __name__=='__main__':main()
