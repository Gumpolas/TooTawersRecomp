"""Optional, disc-derived PS2 voice supplements; no audio is distributed.

Only exact cue names, localized FSTR entries, a unique MIH format, and an
exclusive mono Xbox wave are eligible. This is not a complete-dub claim.
The FSTR hash follows getHeaderData__9ClMStreamFPc in the user's PS2 ELF.
"""
from pathlib import Path
from array import array
import collections,hashlib,itertools,json,os,struct,sys,uuid
sys.path.insert(0,str(Path(__file__).parent))
import voces_combo as vc
import voces_usa_huecos as usa

LANGUAGES=('es','it','fr','de')
COEFFICIENTS=((0,0),(60,0),(115,-52),(98,-55),(122,-60))
STEPS=(7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767)
INDEX=(-1,-1,-1,-1,2,4,6,8)

def header_key(name):
 h=0
 for c in name.replace('\\','/').rsplit('/',1)[-1].lower().encode('ascii'):
  h=(h+c+(h<<(c&7)))&0xffffffff
 return h

class Disc:
 def __init__(self,path,language):
  if language not in LANGUAGES:raise ValueError('PS2_LANGUAGE')
  self.path=Path(path);self.language=language;self.initial=self.path.stat()
  if not self.path.is_file():raise ValueError('PS2_ISO_FILE_REQUIRED')
  self.rows={};self._walk_iso()
  if 'system.cnf' not in self.rows or 'sound/strmhdrs.hdl' not in self.rows:raise ValueError('PS2_DISC_REQUIRED')
  config=self.read('system.cnf').upper()
  if b'BOOT2' not in config or b'SLES_' not in config:raise ValueError('PS2_PAL_GAME_DISC_REQUIRED')
  if not any(p.startswith('sound/'+language+'/') and p.endswith('.mib') for p in self.rows):raise ValueError('PS2_WRONG_LANGUAGE: '+language)
  d=self.read('sound/strmhdrs.hdl')
  if len(d)<16 or d[:8]!=b'FSTR\x01\0\0\0':raise ValueError('PS2_FSTR_FORMAT')
  count=struct.unpack_from('<I',d,8)[0]
  if not 0<count<=20000 or 16+20*count>len(d):raise ValueError('PS2_FSTR_BOUNDS')
  self.headers=collections.defaultdict(list)
  for i in range(count):
   key,offset,a,b,localized=struct.unpack_from('<5I',d,16+20*i)
   if offset<16+20*count or offset+64>len(d):raise ValueError('PS2_MIH_BOUNDS')
   size,last,channels,rate,frame,frames=struct.unpack_from('<6I',d,offset)
   self.headers[key].append({'index':i,'offset':offset,'localized':localized,'size':size,'last':last>>8,'channels':channels,'rate':rate,'frame':frame,'frames':frames})
  self.header_sha=hashlib.sha256(d).hexdigest()
  catalog=json.loads(Path(__file__).with_name('ps2_disc_catalog.json').read_text(encoding='utf-8'))['languages'][language]
  if self.header_sha!=catalog['header_sha256'] or hashlib.sha256(self.read('system.cnf')).hexdigest()!=catalog['system_cnf_sha256']:raise ValueError('PS2_UNVERIFIED_DISC_EDITION: '+language)
 def _walk_iso(self):
  with self.path.open('rb') as f:
   f.seek(16*2048);pvd=f.read(2048)
   if len(pvd)!=2048 or pvd[:7]!=b'\x01CD001\x01':raise ValueError('PS2_ISO9660_REQUIRED')
   record=pvd[156:190];start,size=struct.unpack_from('<I',record,2)[0],struct.unpack_from('<I',record,10)[0]
   visited=set()
   def walk(lba,size,prefix,depth):
    if depth>12 or size>32*1024*1024 or (lba,size) in visited:raise ValueError('PS2_ISO_DIRECTORY_BOUNDS')
    visited.add((lba,size));offset=lba*2048
    if offset+size>self.initial.st_size:raise ValueError('PS2_ISO_TRUNCATED')
    f.seek(offset);data=f.read(size);pos=0
    while pos<len(data):
     length=data[pos]
     if not length:pos=(pos//2048+1)*2048;continue
     if length<34 or pos+length>len(data):raise ValueError('PS2_ISO_RECORD')
     r=data[pos:pos+length];pos+=length;nl=r[32]
     if 33+nl>len(r):raise ValueError('PS2_ISO_NAME')
     raw=r[33:33+nl]
     if raw in (b'\0',b'\1'):continue
     name=raw.decode('ascii').split(';')[0].lower()
     if not name or any(c in name for c in '/\\:') or name in ('.','..'):raise ValueError('PS2_ISO_UNSAFE_NAME')
     path=prefix+name;lba=struct.unpack_from('<I',r,2)[0];n=struct.unpack_from('<I',r,10)[0]
     if lba*2048+n>self.initial.st_size:raise ValueError('PS2_ISO_TRUNCATED')
     if r[25]&2:walk(lba,n,path+'/',depth+1)
     else:
      if path in self.rows:raise ValueError('PS2_DUPLICATE_PATH')
      self.rows[path]=(lba*2048,n)
   walk(start,size,'',0)
 def read(self,name):
  off,n=self.rows[name.lower()]
  if n>32*1024*1024:raise ValueError('PS2_SOURCE_ENTRY_TOO_LARGE')
  with self.path.open('rb') as f:f.seek(off);d=f.read(n)
  if len(d)!=n:raise ValueError('PS2_ISO_TRUNCATED')
  return d
 def unchanged(self):
  now=self.path.stat()
  if (now.st_size,now.st_mtime_ns)!=(self.initial.st_size,self.initial.st_mtime_ns):raise ValueError('PS2_ISO_CHANGED')
 def mono(self,cue):
  name='sound/'+self.language+'/'+cue.lower()+'.mib'
  if name not in self.rows:return None
  entries=self.headers.get(header_key(cue+'.mih'),[])
  valid=[h for h in entries if h['size']==64 and h['localized']==1 and h['channels']==1 and h['rate'] in (22050,44100,48000) and h['frame']>0 and h['frame']%16==0 and h['frames']>0 and h['last']%16==0 and h['last']<=h['frame']]
  formats={(h['rate'],h['frame'],h['frames'],h['last']) for h in valid}
  if len(formats)!=1:return None
  rate,frame,frames,last=next(iter(formats));expected=frame*(frames-1)+(last or frame)
  if expected>16*1024*1024 or self.rows[name][1]!=expected:return None
  raw=self.read(name)
  return raw,rate,{'source_path':name,'header_key':hex(header_key(cue+'.mih')),'header_indices':[h['index'] for h in valid],'header_sha256':self.header_sha,'source_sha256':hashlib.sha256(raw).hexdigest(),'rate':rate,'channels':1,'frame_bytes':frame,'frame_count':frames}

def decode_ps_adpcm(raw):
 if not raw or len(raw)%16:raise ValueError('PS2_ADPCM_BLOCK_SIZE')
 out=array('h');h1=h2=0
 for off in range(0,len(raw),16):
  header,flag=raw[off:off+2];coef=header>>4;shift=header&15
  if coef>4 or shift>12 or flag>7:raise ValueError('PS2_ADPCM_BLOCK_HEADER')
  c1,c2=COEFFICIENTS[coef]
  for b in raw[off+2:off+16]:
   for n in (b&15,b>>4):
    n=n-16 if n&8 else n
    prediction=h1*c1+h2*c2
    # Integer predictor division truncates toward zero. History keeps the
    # unclipped value; only the emitted PCM sample is saturated.
    prediction=prediction//64 if prediction>=0 else -((-prediction)//64)
    sample=0 if flag==7 else ((n<<12)>>shift)+prediction
    out.append(max(-32768,min(32767,sample)));h2,h1=h1,sample
 return out

def encode_xbox_adpcm(samples):
 if not samples:raise ValueError('EMPTY_PCM')
 data=bytearray();index=0
 for start in range(0,len(samples),64):
  values=samples[start:start+64];predictor=values[0];block=bytearray(36);struct.pack_into('<hBB',block,0,predictor,index,0)
  for j in range(1,64):
   target=values[j] if j<len(values) else values[-1];step=STEPS[index];diff=target-predictor;code=8 if diff<0 else 0;diff=abs(diff);delta=step>>3
   for bit,threshold in ((4,step),(2,step>>1),(1,step>>2)):
    if diff>=threshold:code|=bit;diff-=threshold;delta+=threshold
   predictor=max(-32768,min(32767,predictor+(-delta if code&8 else delta)));index=max(0,min(88,index+INDEX[code&7]))
   pos=j-1;block[4+pos//2]|=code<<(4*(pos&1))
  data+=block
 return bytes(data)

def plan(sound,sources,languages):
 if not isinstance(sources,dict) or any(c not in LANGUAGES for c in sources):raise ValueError('PS2_SOURCES_BY_LANGUAGE_REQUIRED')
 if any(c not in languages for c in sources):raise ValueError('PS2_LANGUAGE_NOT_SELECTED')
 discs={c:Disc(p,c) for c,p in sources.items() if p};banks,_=usa.find(sound);rows=[];blocked=[]
 for name,(d,cs,wb) in banks.items():
  refs=collections.defaultdict(list)
  for _,snd,cue in cs:refs[usa.sound_wave(d,cs,snd)].append(cue)
  for code,disc in discs.items():
   missing=vc.combo(d,code,code)[1]['sin_gemelo']
   for row in missing:
    cue=row['cue'];snd=row['sonido_original'];wi=usa.sound_wave(d,cs,snd)
    candidate=disc.mono(cue)
    if candidate is None:continue
    raw,rate,proof=candidate
    if wi>=wb.count or refs[wi]!=[cue]:blocked.append({'bank':name,'cue':cue,'language':code,'reason':'NONEXCLUSIVE_OR_INVALID_TARGET'});continue
    fmt=wb.meta[wi][1]
    if fmt&3!=1 or (fmt>>2)&7!=1 or fmt>>5!=rate:blocked.append({'bank':name,'cue':cue,'language':code,'reason':'TARGET_FORMAT_OR_RATE'});continue
    pcm=decode_ps_adpcm(raw)
    if not any(pcm):blocked.append({'bank':name,'cue':cue,'language':code,'reason':'SILENT_SOURCE'});continue
    audio=encode_xbox_adpcm(pcm)
    rows.append({'banco':name,'cue':cue,'clase':row['clase'],'idioma':code,'onda':wi,**proof,'converted_sha256':hashlib.sha256(audio).hexdigest(),'source_samples':len(pcm),'seconds_source':len(pcm)/rate,'seconds_original':wb.meta[wi][3]/36*64/rate,'method':'Exact localized cue; PS-ADPCM decoded and encoded as mono Xbox ADPCM; no resampling or retiming','validation':'Automatic format and bank checks; listening/gameplay still pending','_audio':audio})
 for disc in discs.values():disc.unchanged()
 return banks,rows,blocked

def atomic(path,data):
 path=Path(path);tmp=path.with_name(path.name+'.ps2-'+uuid.uuid4().hex)
 try:tmp.write_bytes(data);os.replace(tmp,path)
 finally:
  if tmp.exists():tmp.unlink()

def generate(sound,sources,out,languages):
 chosen=tuple(dict.fromkeys(('en',*languages)));out=Path(out);banks,rows,blocked=plan(sound,sources,chosen);applied=[]
 for game,cine in itertools.product(chosen,repeat=2):
  if game==cine=='en':continue
  for name in sorted({r['banco'] for r in rows}):
   d,cs,wb=banks[name];report=out/(game+'+'+cine)/(Path(name).stem+'.json')
   value=json.loads(report.read_text(encoding='utf-8'));missing={r['cue'] for r in value['sin_gemelo']}
   patches=[r for r in rows if r['banco']==name and r['cue'] in missing and r['idioma']==(cine if r['clase']=='cine' else game)]
   if not patches:continue
   target=report.with_name(wb.path.name);image=bytearray(target.read_bytes() if target.exists() else wb.path.read_bytes())
   for row in patches:
    meta=wb.meta[row['onda']];image+=bytes((-len(image))%2048);offset=len(image)-wb.seg[3][0];image+=row['_audio']
    struct.pack_into('<6I',image,wb.seg[1][0]+24*row['onda'],meta[0],meta[1],offset,len(row['_audio']),0,0)
    public={k:v for k,v in row.items() if not k.startswith('_')};applied.append(dict(public,pair=game+'+'+cine))
    value.setdefault('rescatados_PS2',[]).append(public);cls=row['clase'];value['conteos'][cls+'_doblados']=value['conteos'].get(cls+'_doblados',0)+1;value['conteos'][cls+'_ingles']-=1
   struct.pack_into('<I',image,0x24,len(image)-wb.seg[3][0]);atomic(target,image)
   value['sin_gemelo']=[r for r in value['sin_gemelo'] if r['cue'] not in {p['cue'] for p in patches}];atomic(report,(json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode())
 return {'applied':applied,'blocked':blocked,'complete_dub_certified':False,'notice':'Only checked exact-name mono voices. Remaining English events and PS2 videos are not replaced by this component.'}
