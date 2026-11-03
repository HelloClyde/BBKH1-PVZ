from pathlib import Path
import json,struct
from native_day_entities import read_day_entities,LAYOUT
o=json.loads(LAYOUT.read_text())['offsets']
memory={};reads=[]
def put(a,b):memory.update((a+i,v) for i,v in enumerate(b))
def u32(a,v):put(a,struct.pack('<I',v))
def read(a,n):reads.append((a,n));return bytes(memory.get(a+i,0) for i in range(n))
app=0x81c00100;board=0x81c01000;symbol=0x83f60000
u32(symbol,app);u32(app+o['app.board'],board)
u32(board+o['board.level'],1);u32(board+o['board.main_counter'],100);u32(board+o['board.update_counter'],104)
put(board+o['board.paused'],b'\1')
for array,(kind,name) in enumerate((('plant','plants'),('zombie','zombies'),('projectile','projectiles'),('coin','coins'))):
    address=board+o['board.'+name];table=0x81c08000+array*0x100;ids=0x81c09000+array*0x1000
    page=0x81c20000+array*0x10000
    for off,value in ((0,table),(4,table+128),(8,table+128),(12,1024),(20,ids),(24,34 if kind=='plant' else 1),(28,1024),(36,2 if kind=='plant' else 1)):
        u32(address+off,value)
    put(address+16,b'\1');u32(table,page)
    for index in ([0,33] if kind=='plant' else [0]):
        p=page+(index//32)*o[kind+'.stride']*32
        u32(table+(index//32)*4,p);u32(ids+index*4,0x10010000+index)
        entity=p+(index%32)*o[kind+'.stride']
        for field,value in (('type',4 if kind=='coin' else 0),('row',2),('col',index),('health',250 if kind=='zombie' else 300),('max_health',270 if kind=='zombie' else 300)):
            if kind+'.'+field in o:u32(entity+o[kind+'.'+field],value)
        for field,value in (('pos_x',400.5),('pos_y',280.25)):
            if kind+'.'+field in o:put(entity+o[kind+'.'+field],struct.pack('<f',value))
result=read_day_entities(read,{'gLawnApp':(symbol,4)},2,3)
assert result['consistent'] and result['paused'] and result['main_counter']==100
assert result['plants']['active_count']==2 and result['plants']['entities'][1]['slot']==33
assert result['plants']['allocated_pages']==2
assert result['zombies']['entities'][0]['health']==250 and result['zombies']['entities'][0]['max_health']==270
assert result['projectiles']['entities'][0]['type']==0 and result['coins']['entities'][0]['pos_x']==400.5
assert read_day_entities(read,{'gLawnApp':(symbol,4)},3,3)=={}
assert read_day_entities(read,{'gLawnApp':(symbol,4)},2,1)=={}
old=memory[board+o['board.plants']+o['array.max_size']]
memory[board+o['board.plants']+o['array.max_size']]=255
assert 'decode_error' in read_day_entities(read,{'gLawnApp':(symbol,4)},2,3)
memory[board+o['board.plants']+o['array.max_size']]=old
count=[0]
def changing(a,n):
    data=read(a,n)
    if a==board+o['board.main_counter']:
        count[0]+=1
        if count[0]>1:return struct.pack('<I',101)
    return data
assert not read_day_entities(changing,{'gLawnApp':(symbol,4)},2,3)['consistent']
LAYOUT.with_name('day-decoder-test.json').write_text(json.dumps(dict(test_only=True,guest_access=False,production_modified=False,passed=True,cases=['sparse_second_page_slot33','live_ID_filter','health_and_type_fields','float_fields','paused_and_counters','abort_guard','scene_guard','invalid_header','changed_counter_rejection'],example=result),indent=2)+'\n')
print('PASS: 9 offline entity decoder cases; guest access=0')
