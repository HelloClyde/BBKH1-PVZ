"""Decode the bounded H1FAULT.BIN written only after an abnormal BDA exit."""
import argparse,json,struct
from pathlib import Path

def native_path(data):
    raw=data.split(b'\0',1)[0]
    try:return raw.decode('utf8')
    except UnicodeDecodeError:return raw.decode('gbk',errors='replace')

def decode(data):
    if len(data) not in (1100,1484):raise ValueError('Unexpected native failure record size')
    words=struct.unpack_from('<83I',data)
    if words[0]!=0x58463148 or (words[1],words[2]) not in ((1,1100),(2,1484)) or words[2]!=len(data):raise ValueError('Unknown native failure record')
    result=dict(version=words[1],status=words[3],raw_tick=words[4],runtime=list(words[5:21]),
                termination=list(words[21:25]),native_charge=list(words[25:27]),
                native_charge_fault=list(words[27:31]),heap_failure=list(words[31:51]),
                game=list(words[51:83]),
                message=data[332:588].split(b'\0',1)[0].decode('utf8',errors='replace'),
                last_path=native_path(data[588:1100]))
    if words[1]==2:
        v=struct.unpack_from('<72I',data,1100)
        result.update(actor_page_failure=list(v[:16]),actor_page_heap=list(v[16:36]),actor_residency=list(v[36:52]),image_lifetime=list(v[52:68]),actor_frame_lifetime=list(v[68:72]),actor_path=data[1388:1484].split(b'\0',1)[0].decode('utf8',errors='replace'))
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('record',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();text=json.dumps(decode(a.record.read_bytes()),ensure_ascii=False,indent=2)+'\n'
    if a.output:a.output.write_text(text,encoding='utf8')
    print(text,end='')
if __name__=='__main__':main()
