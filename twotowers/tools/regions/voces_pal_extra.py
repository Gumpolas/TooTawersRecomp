"""Conservative PAL voice supplements, retaining the USA XSB v5 layout.

Only exact named missing cues with an exclusive compatible mono ADPCM wave
are filled. No PAL executable or v6 sound bank is installed into the USA game.
"""
from pathlib import Path
import sys,struct,hashlib,json,itertools,os,uuid
sys.path.insert(0,str(Path(__file__).parent))
import voces_combo as vc
import voces_usa_huecos as usa
import inspect_pal

class Reader:
    def __init__(self,source):
        self.source=Path(source)
        if self.source.is_file():
            with self.source.open('rb') as f:self.rows={r['path'].lower():r for r in inspect_pal.inventory(f)}
        else:self.rows={r.relative_to(self.source).as_posix().lower():r for r in self.source.rglob('*') if r.is_file()}
    def read(self,name,offset=0,size=None):
        row=self.rows[name.lower()]
        if isinstance(row,Path):
            with row.open('rb') as f:f.seek(offset);return f.read() if size is None else f.read(size)
        if offset<0 or offset>row['size'] or (size is not None and offset+size>row['size']):raise ValueError('PAL_WAVE_BOUNDS')
        with self.source.open('rb') as f:f.seek(row['offset']+offset);return f.read(row['size']-offset if size is None else size)

class WaveBank:
    def __init__(self,reader,name):
        self.reader,self.name=reader,name
        h=reader.read(name,0,40)
        if h[:8]!=b'WBND\2\0\0\0':raise ValueError('PAL_WAVE_FORMAT')
        self.seg=[struct.unpack_from('<II',h,8+8*i) for i in range(4)]
        bd=reader.read(name,*self.seg[0]);self.count=struct.unpack_from('<I',bd,4)[0]
        if struct.unpack_from('<II',bd,24)!=(24,64):raise ValueError('PAL_WAVE_METADATA')
        raw=reader.read(name,*self.seg[1]);self.meta=[struct.unpack_from('<6I',raw,24*i) for i in range(self.count)]
    def data(self,index):
        m=self.meta[index]
        if m[2]+m[3]>self.seg[3][1]:raise ValueError('PAL_WAVE_BOUNDS')
        data=self.reader.read(self.name,self.seg[3][0]+m[2],m[3])
        if len(data)!=m[3]:raise ValueError('PAL_WAVE_TRUNCATED')
        return data

def pal_cues(data):
    if data[:6]!=b'SDBK\x06\x00':raise ValueError('PAL_SOUND_BANK_V6_REQUIRED')
    count=struct.unpack_from('<H',data,0x1e)[0]
    result=[]
    for i in range(count):
        off=0x34+16*i;sound,name=struct.unpack_from('<HI',data,off+2)
        if name>=len(data):raise ValueError('PAL_CUE_NAME_BOUNDS')
        text=data[name:data.index(b'\0',name)].decode('ascii')
        result.append((off,sound,text))
    return result

def plan(usa_sound,pal_source,languages):
    banks,usa_rescues=usa.find(usa_sound)
    reader=Reader(pal_source);local={}
    for name in sorted(reader.rows):
        if not name.startswith('sound/s_sb_') or not name.endswith('.xsb'):continue
        d=reader.read(name);cs=pal_cues(d);wave=WaveBank(reader,name.replace('/s_sb_','/s_wb_')[:-4]+'.xwb')
        for off,sound,cue in cs:
            if cue[:3] not in ('ES_','FR_','DE_','IT_'):continue
            key=vc.ALIAS.get(cue[3:].upper(),cue[3:].upper())
            wi=struct.unpack_from('<I',d,0x34+len(cs)*16+sound*20)[0]
            if wi<wave.count:local.setdefault((cue[:2].lower(),key),[]).append((wave,wi,cue))
    previously={(r['banco'],r['cue'],r['idioma']) for r in usa_rescues}
    rows=[]
    for name,(d,cs,wb) in banks.items():
        refs={}
        for off,sound,cue in cs:refs.setdefault(usa.sound_wave(d,cs,sound),[]).append(cue)
        for off,sound,cue in cs:
            cls=vc.clase(cue);wi=usa.sound_wave(d,cs,sound)
            if not vc.neutral(cue) or cls not in ('juego','cine') or wi>=wb.count or refs[wi]!=[cue]:continue
            for code in languages:
                if code=='en' or cue.upper() in vc.twins(cs,code) or (name,cue,code) in previously:continue
                candidates=local.get((code,cue.upper()),[])
                valid=[(w,i,n) for w,i,n in candidates if w.meta[i][1]==wb.meta[wi][1] and (w.meta[i][1]&3)==1 and ((w.meta[i][1]>>2)&7)==1]
                if not valid:continue
                hashes={hashlib.sha256(w.data(i)).hexdigest() for w,i,n in valid}
                if len(hashes)!=1:continue
                w,i,twin=valid[0]
                rows.append({'banco':name,'cue':cue,'clase':cls,'idioma':code,'onda':wi,'gemelo':twin,'fuente_banco':w.name,'fuente_onda':i,'sha256_audio':next(iter(hashes)),'_wave':w})
    return banks,rows

def atomic(path,data):
    path=Path(path);temp=path.with_name(path.name+'.pal-'+uuid.uuid4().hex)
    try:temp.write_bytes(data);os.replace(temp,path)
    finally:
        if temp.exists():temp.unlink()

def generate(sound,pal_source,out,languages):
    chosen=tuple(dict.fromkeys(('en',*languages)));out=Path(out)
    banks,rows=plan(sound,pal_source,chosen)
    public=[{k:v for k,v in r.items() if not k.startswith('_')} for r in rows]
    for game,cine in itertools.product(chosen,repeat=2):
        if game==cine=='en':continue
        selected=[r for r in rows if r['idioma']==(cine if r['clase']=='cine' else game)]
        for name in sorted({r['banco'] for r in selected}):
            d,cs,wb=banks[name];target=out/(game+'+'+cine)/wb.path.name
            image=bytearray(target.read_bytes() if target.exists() else wb.path.read_bytes())
            patches=[r for r in selected if r['banco']==name]
            for row in patches:
                w=row['_wave'];meta=w.meta[row['fuente_onda']];audio=w.data(row['fuente_onda'])
                image+=bytes((-len(image))%2048);offset=len(image)-wb.seg[3][0];image+=audio
                struct.pack_into('<6I',image,wb.seg[1][0]+24*row['onda'],meta[0],meta[1],offset,len(audio),meta[4],meta[5])
            struct.pack_into('<I',image,0x24,len(image)-wb.seg[3][0])
            # Atomic replacement breaks any hardlink to other language pairs.
            atomic(target,bytes(image))
            report=target.with_name(name).with_suffix('.json')
            value=json.loads(report.read_text(encoding='utf-8'))
            value['rescatados_PAL']=[{k:v for k,v in r.items() if not k.startswith('_')} for r in patches]
            for r in patches:
                cls=r['clase'];value['conteos'][cls+'_doblados']=value['conteos'].get(cls+'_doblados',0)+1;value['conteos'][cls+'_ingles']-=1
            value['sin_gemelo']=[r for r in value['sin_gemelo'] if r['cue'] not in {p['cue'] for p in patches}]
            atomic(report,(json.dumps(value,ensure_ascii=False,indent=2)+'\n').encode())
    return public
