"""Compile Simplified Chinese text, UI pixels and shared bitmap glyph atlases offline."""
from pathlib import Path
from collections import Counter
import argparse,hashlib,json,math,re,shutil,struct,sys
from xml.sax.saxutils import escape
from PIL import Image,ImageDraw,ImageFont
from prepare_h1_assets import metadata_index,read_image
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'private/zh-cn/source-upstream'
def read(path):return json.loads(path.read_text(encoding='utf8'))
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def parse(text):return {k.strip():v.strip() for k,v in re.findall(r'^\[([^]\r\n]+)\][ \t]*\r?\n(.*?)(?=^\[[^]\r\n]+\]|\Z)',text,re.M|re.S)}
def tokens(text):return Counter(re.findall(r'\{[^{}]+\}|%(?:\d+\$)?[-+0-9.]*[sdfiu]',text))
def translated_strings():
 en=parse((ROOT/'localization/en_US.lawnstrings.txt').read_text(encoding='utf-8-sig'))
 primary=parse((SOURCE/'bamcane-lawnstrings.txt').read_text(encoding='utf-8-sig'))
 secondary=parse((SOURCE/'gonewx-lawnstrings.txt').read_text(encoding='utf-8-sig'))
 overrides=read(ROOT/'localization/overrides.zh_CN.json')
 out={k:overrides.get(k,primary.get(k,secondary.get(k,''))) for k in en}
 assert all(v or not en[k] for k,v in out.items()),[k for k,v in out.items() if not v and en[k]]
 fixes={
  'PEASHOOTER_DESCRIPTION':(('说：{STAT}','说：'),),
  'TANGLE_KELP_DESCRIPTION':(('必须种植在水中','{AQUATIC}必须种植在水中'),),
  'JALAPENO_DESCRIPTION':(('\n用法：','\n{KEYWORD}用法：'),),
  'CABBAGE_PULT_DESCRIPTION':(('\n伤害：','\n{KEYWORD}伤害：'),('\n范围：','\n{KEYWORD}范围：')),
  'WINTER_MELON_DESCRIPTION':(('\n特点：','\n{KEYWORD}特点：'),),
  'SCREEN_DOOR_ZOMBIE_DESCRIPTION':(('磁力菇\n','磁力菇{KEYWORD}\n'),),
  'SNORKEL_ZOMBIE_DESCRIPTION':(('呢？\n','呢？{KEYWORD}\n'),('答案：{STAT}','答案：')),
  'BOSS_DESCRIPTION':(('\n{KEYWORD}\n{FLAVOR}','\n{SHORTLINE}\n{FLAVOR}'),)
 }
 for k,changes in fixes.items():
  for old,new in changes:
   assert old in out[k],(k,old);out[k]=out[k].replace(old,new,1)
 for k in ('BUCKETHEAD_ZOMBIE_DESCRIPTION','FOOTBALL_ZOMBIE_DESCRIPTION','JACK_IN_THE_BOX_ZOMBIE_DESCRIPTION','POGO_ZOMBIE_DESCRIPTION'):
  out[k]=out[k].replace('{KEYWORD}弱点：{STAT}','{KEYMETAL}弱点：{METAL}')
 out['ZOMBONI_DESCRIPTION']=out['ZOMBONI_DESCRIPTION'].removeprefix('{SHORTLINE}\n')
 out['ZOMBONI_DESCRIPTION']=out['ZOMBONI_DESCRIPTION'].replace('www，zamboni，com！僵尸。','www.zamboni.com！')
 # These directives control Dave's mouth, effects and item grants. They must
 # remain present even where an old translation dropped them.
 for k in ('CRAZY_DAVE_303','CRAZY_DAVE_304','CRAZY_DAVE_2000','CRAZY_DAVE_2414','CRAZY_DAVE_2502','CRAZY_DAVE_2601'):
  missing=tokens(en[k])-tokens(out[k])
  out[k]=''.join(t*n for t,n in missing.items())+out[k]
 bad={k:{'en':dict(tokens(en[k])),'zh':dict(tokens(out[k]))} for k in en if tokens(en[k])!=tokens(out[k])}
 assert not bad,json.dumps(bad,ensure_ascii=False)
 extra={k:v for k,v in overrides.items() if k not in en}
 allowed=('CREDITS_NAMES','CREDITS_MAC_NAMES','LOCALE','CURRENCY_STRING')
 untranslated={k:v for k,v in out.items() if not any(k.startswith(x) for x in allowed) and re.search('[a-zA-Z]{3}',v) and not re.search('[\u3400-\u9fff]',v)}
 assert not untranslated,untranslated
 return en,out,extra

def write_image(path,logical,im,channels,flags=0):
 w,h=logical;sw,sh=im.size;data=im.convert({1:'L',3:'RGB',4:'RGBA'}[channels]).tobytes()
 path.parent.mkdir(parents=True,exist_ok=True)
 path.write_bytes(struct.pack('<4s7I',b'H1I2',w,h,channels,flags,len(data),sw,sh)+data)

def localize_images(base,out):
 records=read(SOURCE/'ui-assets-provenance.json')['files'];changed=[]
 primary={Path(x['path'].removeprefix('pak/')).with_suffix('').as_posix().casefold():SOURCE/x['local'] for x in records if x['repository']=='Bamcane/re-plants-vs-zombies'}
 alternatives={Path(x['path']).stem.lower():SOURCE/x['local'] for x in records if x['repository']=='gonewx/pvz'}
 text_only=re.compile(r'^(?:start(?:ready|set|plant)|zombieswon|store_(?:comingsoonlabel|soldoutlabel)|selectorscreen_(?:options[12]|help[12]|quit[12]))_?$')
 for item in records:
  if item['repository']!='Bamcane/re-plants-vs-zombies':continue
  name=Path(item['path'].removeprefix('pak/')).with_suffix('').as_posix().lower()
  src=SOURCE/item['local'];stem=Path(name).name
  chosen=alternatives.get(stem) if text_only.match(stem) or stem.startswith('pvz_logo') else None
  if chosen:src=chosen
  mask_source=primary.get(name+'_',SOURCE/'missing-mask')
  # .h1a can hold a precomposed RGBA variant and takes priority at runtime.
  # Updating only .h1i would leave the old English preferred variant alive.
  for extension in ('.h1i','.h1a'):
   target=base/(name+extension)
   if not target.exists():continue
   info,pixels=read_image(target);w,h=info['width'],info['height'];sw,sh=info['storage_width'],info['storage_height'];channels=info['channels']
   original=Image.frombytes({1:'L',3:'RGB',4:'RGBA'}[channels],(sw,sh),pixels)
   with Image.open(src) as loaded:
    if channels==1:
     im=loaded.convert('L').resize((sw,sh),Image.Resampling.LANCZOS)
    elif channels==4:
     if loaded.mode=='RGBA' and chosen:
      im=Image.new('RGBA',(sw,sh));scaled=loaded.copy();scaled.thumbnail((sw,sh),Image.Resampling.LANCZOS)
      im.alpha_composite(scaled,((sw-scaled.width)//2,(sh-scaled.height)//2))
     elif text_only.match(stem):
      rgb=loaded.convert('RGB').resize((sw,sh),Image.Resampling.LANCZOS)
      alpha=Image.new('L',(sw,sh));alpha.putdata([min(255,max(0,(max(p)-3)*255//13)) for p in rgb.getdata()])
      im=rgb.convert('RGBA');im.putalpha(alpha)
     else:
      im=loaded.convert('RGB').resize((sw,sh),Image.Resampling.LANCZOS).convert('RGBA')
      if mask_source.exists():
       with Image.open(mask_source) as mask:im.putalpha(mask.convert('L').resize((sw,sh),Image.Resampling.LANCZOS))
      else:im.putalpha(original.getchannel('A'))
    else:im=loaded.convert('RGB').resize((sw,sh),Image.Resampling.LANCZOS)
   destination=out/target.relative_to(base);write_image(destination,(w,h),im,channels,info['flags'])
   changed.append({'path':destination.relative_to(out).as_posix(),'source':src.relative_to(ROOT).as_posix(),'bytes':destination.stat().st_size,'sha256':sha(destination),'logical':[w,h],'storage':[sw,sh]})
 return changed

def font_atlas(chars,size,style,out):
 font=ImageFont.truetype(str(ROOT/'assets/fonts/NotoSansCJKsc-Regular.otf'),size)
 boxes=[font.getbbox(c,anchor='ls') for c in chars]
 ascent=max(-b[1] for b in boxes);descent=max(b[3] for b in boxes)
 ink_width=max(b[2]-b[0] for b in boxes);pad=2
 cw=ink_width+pad*2;ch=ascent+descent+pad*2;cols=48;rows=math.ceil(len(chars)/cols)
 atlas=Image.new('L',(cols*cw,rows*ch));draw=ImageDraw.Draw(atlas)
 missing=[];tofu=font.getmask('\uffff').tobytes() if hasattr(font.getmask('\uffff'),'tobytes') else bytes(font.getmask('\uffff'))
 for i,c in enumerate(chars):
  if bytes(font.getmask(c))==tofu and c not in ('\u3000',):missing.append(c)
  draw.text((i%cols*cw+pad,i//cols*ch+pad+ascent),c,font=font,anchor='ls',fill=0 if style=='outline' else 255,stroke_width=1 if style=='outline' else 0,stroke_fill=255)
 assert not missing,('Noto missing codepoints',missing)
 lcw=round(cw/.45);lch=round(ch/.45);ratio=cw/lcw
 name=f'h1zh{size:02d}'+('o' if style=='outline' else '')
 path=out/'data'/(name+'.h1a');write_image(path,(cols*lcw,rows*lch),atlas,1,1)
 rects=[(i%cols*lcw,i//cols*lch,lcw,lch) for i in range(len(chars))]
 widths=[max(1,round(font.getlength(c)/ratio)) for c in chars]
 baseline=round((pad+ascent)/(ch/lch))
 metrics=out/'data'/(name+'.h1f')
 metrics.write_bytes(struct.pack('<4sI',b'H1F1',len(chars))+b''.join(struct.pack('<6I',ord(c),width,*rect) for c,width,rect in zip(chars,widths,rects)))
 return {'name':name,'rects':rects,'widths':widths,'baseline':baseline,'logical_cell':[lcw,lch],'size':size,'style':style,'bytes':path.stat().st_size,'metrics_file':metrics.name,'metrics_bytes':metrics.stat().st_size}

def localize_fonts(strings,base,out):
 chars=sorted({c for text in strings.values() for c in text if ord(c)>=256 and not c.isspace()})
 atlas_cache={};reports=[]
 def values(seq):return '('+', '.join(str(v) if not isinstance(v,tuple) else '('+', '.join(map(str,v))+')' for v in seq)+')'
 for source in sorted((ROOT/'localization/base-fonts').glob('*.txt')):
  original=source.read_bytes().decode('cp1252',errors='replace')
  point=int(re.findall(r'SetDefaultPointSize\s+(\d+)',original)[0])
  # Same readable glyph cell family shared by all color/face variants.
  pixel_size=max(9,min(20,round(point*.45+1)))
  # Direct bounded commands avoid retaining huge Define trees/text snapshots
  # and prevent a single command from creating thousands of parser objects.
  additions=['\n']
  layers=re.findall(r'CreateLayer\s+(\w+)\s*;',original)
  for layer in layers:
   ascent_match=re.search(r'LayerSetAscent\s+'+layer+r'\s+(-?\d+)',original);ascent=int(ascent_match[1])
   style='outline' if 'outline' in layer.lower() or 'outline' in source.stem or 'outback' in source.stem else 'normal'
   key=(pixel_size,style)
   if key not in atlas_cache:atlas_cache[key]=font_atlas(chars,*key,out)
   atlas=atlas_cache[key];layer_name='H1Zh'+layer
   color=(1.,1.,1.,1.)
   if style=='outline' or 'black' in source.stem:color=(0.,0.,0.,1.)
   elif 'green' in source.stem:color=(.45 if 'bright' in source.stem else .25,1.,.15,1.)
   elif 'yellow' in source.stem:color=(1.,.83,.15,1.)
   offsets=[(0,ascent-atlas['baseline'])]*len(chars)
   original_image=re.search(r'LayerSetImage\s+'+layer+r"\s+['\"]([^'\"]+)['\"]",original)[1].lower()
   old_image=base/'data'/(original_image+'.h1i')
   if not old_image.exists():old_image=base/'data'/(original_image+'.h1a')
   original_height=read_image(old_image)[0]['height']
   explicit_height=re.search(r'LayerSetHeight\s+'+layer+r'\s+(\d+)',original)
   if explicit_height:original_height=int(explicit_height[1])
   # A 9 px Chinese glyph needs a full baseline interval, even when the
   # original Latin face was only 17 logical pixels tall (7.7 px on H1).
   cjk_height=max(original_height,math.ceil((pixel_size+1)/.45))
   additions.extend((f'CreateLayer {layer_name};\nLayerSetImage {layer_name} \'{atlas["name"]}\';\nLayerSetAscent {layer_name} {ascent};\n',
    f'LayerSetPointSize {layer_name} {point};\nLayerSetHeight {layer_name} {cjk_height};\nLayerSetColorMult {layer_name} '+values(color)+';\n'))
   additions.append(f'LayerSetH1CJK {layer_name} \'data/{atlas["metrics_file"]}\' {ascent-atlas["baseline"]};\n')
   for command in ('LayerSetAscentPadding','LayerSetLineSpacingOffset','LayerRequireTags','LayerExcludeTags'):
    m=re.search(command+r'\s+'+layer+r'\s+([^;]+);',original)
    if m:additions.append(f'{command} {layer_name} {m[1]};\n')
  destination=out/'data'/source.name;destination.write_text(original+''.join(additions),encoding='utf8',newline='\n')
  reports.append({'descriptor':source.name,'glyphs':len(chars),'physical_point_size':pixel_size,'original_Latin_glyph_metrics_preserved':True,'CJK_line_height_minimum_pixels':pixel_size+1,'shared_compact_metrics':True,'glyph_objects_created_only_on_use':True})
 return {'glyphs':len(chars),'characters':''.join(chars),'descriptors':reports,'atlases':[{k:v for k,v in a.items() if k not in ('rects','widths')} for a in atlas_cache.values()]}

def refresh_inventory(out,report):
 index=read(out/'resource-index.json')
 for path in sorted(out.rglob('*')):
  if not path.is_file():continue
  name=path.relative_to(out).as_posix()
  if path.suffix in ('.h1i','.h1a'):
   info,_=read_image(path)
   index['images'][name]=dict(index['images'].get(name,{}),**info,bytes=path.stat().st_size,sha256=sha(path))
  elif name in index['copied'] or name=='properties/pvz-portable.xml' or path.suffix=='.h1f':
   index['copied'][name]=dict(index['copied'].get(name,{}),bytes=path.stat().st_size,sha256=sha(path))
 index.update(locale='zh_CN',image_count=len(index['images']),image_bytes=sum(x['bytes'] for x in index['images'].values()),
              copied_files=len(index['copied']),copied_bytes=sum(x['bytes'] for x in index['copied'].values()),image_metadata_index=report['metadata_index'])
 index['localization']={'manifest':'zh-cn-manifest.json','font_source':'Noto Sans CJK SC Regular / SIL OFL 1.1','UI_image_variants':len(report['localized_images'])}
 (out/'resource-index.json').write_text(json.dumps(index,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 wide=read(out/'widescreen-manifest.json');wide['locale']='zh_CN';wide['localization_manifest']='zh-cn-manifest.json'
 for name,entry in wide['changed'].items():
  path=out/name;entry.update(bytes=path.stat().st_size,sha256=sha(path))
 (out/'widescreen-manifest.json').write_text(json.dumps(wide,ensure_ascii=False,indent=2)+'\n',encoding='utf8')

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--base-resources',type=Path,default=ROOT/'private/resources-h1-wide');p.add_argument('--output',type=Path,default=ROOT/'build/zh-cn/resources');a=p.parse_args()
 base=a.base_resources.resolve();out=a.output.resolve();assert out!=base and out.is_relative_to(ROOT)
 owner=out/'zh-cn-owner.json'
 if out.exists() and any(out.iterdir()):assert owner.exists(),'Preserve an unowned output directory'
 out.mkdir(parents=True,exist_ok=True);owner.write_text('{"owner":"h1-pvz-zh-cn-1"}\n')
 shutil.copytree(base,out,dirs_exist_ok=True)
 en,translated,extra=translated_strings();all_strings=dict(translated,**extra)
 legacy={k:v for k,v in en.items() if k.startswith('ADVICE_') or 'WAVE' in k}
 header=ROOT/'platform/localization/LegacyMessageMap.h';header.parent.mkdir(exist_ok=True)
 header.write_text('// Generated from localization/en_US.lawnstrings.txt for one-time save migration.\n#pragma once\nnamespace {\nstruct H1LegacyMessageEntry { const char* english; const char* key; };\nconstexpr H1LegacyMessageEntry H1LegacyMessages[]={\n'+''.join('  {'+json.dumps(v,ensure_ascii=False)+','+json.dumps(k)+'},\n' for k,v in legacy.items() if v)+'};\n}\n',encoding='utf8',newline='\n')
 (ROOT/'localization/zh_CN.json').write_text(json.dumps({'locale':'zh_CN','strings':all_strings},ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 (out/'properties/lawnstrings.txt').write_text(''.join('['+k+']\n'+v+'\n\n' for k,v in translated.items()),encoding='utf8',newline='\n')
 # Loaded last by the pinned engine, after default.xml and Layout.xml.
 xml='<?xml version="1.0" encoding="UTF-8"?>\n<Properties language="zh" country="CN">\n'
 xml+=''.join('<String id="'+k+'">'+escape(v)+'</String>\n' for k,v in dict(extra,**read(ROOT/'localization/overrides.zh_CN.json')).items())+'</Properties>\n'
 (out/'properties/pvz-portable.xml').write_text(xml,encoding='utf8',newline='\n')
 images=localize_images(base,out);fonts=localize_fonts(all_strings,base,out)
 index,_=metadata_index(out)
 report={'schema':'h1-pvz-zh-cn-1','original_keys':len(en),'translated_keys':len(translated),'extra_UI_keys':len(extra),'placeholder_and_animation_directives_preserved':True,'localized_images':images,'fonts':fonts,'metadata_index':index}
 refresh_inventory(out,report)
 (out/'zh-cn-manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 (ROOT/'build/zh-cn').mkdir(parents=True,exist_ok=True)
 (ROOT/'build/zh-cn/localization-verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps({'keys':len(translated),'extra':len(extra),'images':len(images),'glyphs':fonts['glyphs'],'font_atlases':len(fonts['atlases']),'output':str(out)},ensure_ascii=False,indent=2))
if __name__=='__main__':main()
