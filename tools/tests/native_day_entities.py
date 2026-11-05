"""Pure decoder for an existing read-only guest transport. No connections.

The caller alone owns GDB/input. read(address, count) returns bytes. Offsets are
emitted by the current MIPS compiler in day_entities.json, not host layouts.
"""
from pathlib import Path
import json,struct,math

LAYOUT=Path(__file__).resolve().parents[2]/'build/input-offsets/day_entities.json'

def read_day_entities(read,symbols,runtime_stage,scene,layout_path=LAYOUT):
    if runtime_stage!=2 or scene not in (2,3) or 'gLawnApp' not in symbols:return {}
    layout=json.loads(Path(layout_path).read_text());o=layout['offsets']
    def u32(address):return int.from_bytes(read(address,4),'little')
    def heap(address,count):return address%4==0 and 0x81c00000<=address and address+count<=0x83c00000
    def word(data,offset,signed=False):return int.from_bytes(data[offset:offset+4],'little',signed=signed)
    def entity_array(board,kind,plural):
        address=board+o['board.'+plural];header=read(address,o['data_array_bytes'])
        maximum=word(header,o['array.max_size']);used=word(header,o['array.max_used'])
        count=word(header,o['array.size']);capacity=word(header,o['array.page_capacity'])
        if maximum!=1024 or capacity!=maximum or used>maximum or count>used or header[o['array.initialized']]!=1:
            raise ValueError('invalid original entity-array header: '+plural)
        begin=word(header,o['array.page_vector']);end=word(header,o['array.page_vector']+4)
        finish=word(header,o['array.page_vector']+8);ids_address=word(header,o['array.item_ids'])
        pages=(maximum+o['page_size']-1)//o['page_size']
        if end-begin!=pages*4 or not begin<=end<=finish or not heap(begin,finish-begin) or not heap(ids_address,maximum*4):
            raise ValueError('invalid original page-vector/ID pointers: '+plural)
        table=read(begin,pages*4);ids=read(ids_address,used*4) if used else b'';entities=[]
        stride=o[kind+'.stride']
        for index in range(used):
            entity_id=word(ids,index*4)
            if not entity_id&0xffff0000:continue
            if entity_id&0xffff!=index:raise ValueError('entity ID/index differs: '+plural)
            base=word(table,(index//o['page_size'])*4)
            if not heap(base,stride*o['page_size']):raise ValueError('unallocated live page: '+plural)
            pointer=base+(index%o['page_size'])*stride;data=read(pointer,o[kind+'.bytes'])
            values=dict(slot=index,id=entity_id,address=hex(pointer),dead=bool(data[o[kind+'.dead']]))
            for field in ('type','row','col','health','max_health','x','y','age','width','height'):
                key=kind+'.'+field
                if key in o:values[field]=word(data,o[key],True)
            for field in ('pos_x','pos_y'):
                key=kind+'.'+field
                if key in o:
                    value=struct.unpack_from('<f',data,o[key])[0]
                    values[field]=value if math.isfinite(value) else str(value)
            if kind=='coin':values['collecting']=bool(data[o['coin.collecting']])
            entities.append(values)
        unchanged=read(address,o['data_array_bytes'])==header and (read(ids_address,used*4) if used else b'')==ids and read(begin,pages*4)==table
        return dict(slot_count=count,high_water=used,max_size=maximum,page_table=hex(begin),
                    allocated_pages=sum(bool(word(table,i*4)) for i in range(pages)),
                    header_ids_and_pages_unchanged=unchanged,
                    active_count=sum(not item['dead'] for item in entities),entities=entities)
    try:
        app=u32(symbols['gLawnApp'][0])
        if not heap(app,o['app.board']+4):return {}
        board=u32(app+o['app.board'])
        if not heap(board,o['board.bytes']):return {}
        before_main=u32(board+o['board.main_counter']);before_update=u32(board+o['board.update_counter'])
        values=dict(app_pointer=hex(app),board_pointer=hex(board),
                    level=u32(board+o['board.level']),wave=u32(board+o['board.current_wave']),
                    sun=u32(board+o['board.sun']),paused=bool(read(board+o['board.paused'],1)[0]),
                    main_counter=before_main,board_update_counter=before_update,
                    board_x=int.from_bytes(read(board+o['board.x'],4),'little',signed=True),
                    board_y=int.from_bytes(read(board+o['board.y'],4),'little',signed=True))
        for kind,plural in (('plant','plants'),('zombie','zombies'),('projectile','projectiles'),('coin','coins')):
            values[plural]=entity_array(board,kind,plural)
        if 'board.seed_bank' in o:
            values['sod_position']=u32(board+o['board.sod_position']);values['total_waves']=u32(board+o['board.total_waves'])
            cursor=u32(board+o['board.cursor'])
            if heap(cursor,o['cursor.type']+4):values['cursor_type']=u32(cursor+o['cursor.type'])
            cutscene=u32(board+o['board.cutscene'])
            if heap(cutscene,o['cutscene.seed_choosing']+1):values['cutscene']=dict(time=u32(cutscene+o['cutscene.time']),sod_time=u32(cutscene+o['cutscene.sod_time']),seed_choosing=bool(read(cutscene+o['cutscene.seed_choosing'],1)[0]))
            bank=u32(board+o['board.seed_bank'])
            if heap(bank,o['seed_bank.bytes']):
                data=read(bank,o['seed_bank.bytes']);n=word(data,o['seed_bank.num_packets']);packets=[]
                if not 0<=n<=10:raise ValueError('invalid seed packet count')
                for i in range(n):
                    at=o['seed_bank.packets']+i*o['seed_packet.stride'];item=dict(index=i)
                    for field in ('type','x','y','width','height'):item[field]=word(data,at+o['seed_packet.'+field],True)
                    for field in ('active','refreshing'):item[field]=bool(data[at+o['seed_packet.'+field]])
                    packets.append(item)
                values['seed_bank']=dict(x=word(data,o['seed_bank.x'],True),y=word(data,o['seed_bank.y'],True),packets=packets)
        after_main=u32(board+o['board.main_counter']);after_update=u32(board+o['board.update_counter'])
        values['app_and_board_pointers_unchanged']=u32(symbols['gLawnApp'][0])==app and u32(app+o['app.board'])==board
        values['counters_unchanged_during_read']=before_main==after_main and before_update==after_update
        values['counters_after_read']=[after_main,after_update]
        values['consistent']=values['app_and_board_pointers_unchanged'] and values['counters_unchanged_during_read'] and all(values[name]['header_ids_and_pages_unchanged'] for name in ('plants','zombies','projectiles','coins'))
        values['offset_source']='MIPS build/input-offsets/day_entities.o'
        values['layout_elf_sha256']=layout['elf_sha256']
        values['game_RAM_written']=False
        return values
    except (ValueError,IndexError,struct.error) as error:return dict(decode_error=str(error))
