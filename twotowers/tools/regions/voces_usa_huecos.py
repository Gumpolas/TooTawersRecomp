"""Reutiliza gemelos USA colocados en otro banco antes de recurrir a PS2.

No recodifica: añade sus bytes ADPCM a una copia del XWB y reasigna SOLO la
entrada de onda neutra exclusiva. Los XSB conservan su tamaño y sus tablas.
"""
from pathlib import Path
import hashlib,json,os,shutil,struct
import voces_combo as vc

class Bank:
    def __init__(self,p):
        self.path=Path(p)
        with self.path.open('rb') as f:
            h=f.read(40)
            if h[:8]!=b'WBND\2\0\0\0':raise ValueError('Se requiere WBND v2')
            self.seg=[struct.unpack_from('<II',h,8+8*i) for i in range(4)]
            f.seek(self.seg[0][0]);bd=f.read(self.seg[0][1])
            self.count=struct.unpack_from('<I',bd,4)[0]
            assert struct.unpack_from('<II',bd,24)==(24,64)
            f.seek(self.seg[1][0]);meta=f.read(self.seg[1][1])
            self.meta=[struct.unpack_from('<6I',meta,i*24) for i in range(self.count)]
    def data(self,i):
        _,fmt,off,n,_,_=self.meta[i]
        with self.path.open('rb') as f:f.seek(self.seg[3][0]+off);d=f.read(n)
        assert len(d)==n
        return d

def sound_wave(data,cs,snd):
    w=struct.unpack_from('<I',data,0x30+len(cs)*16+snd*20)[0]
    return w

def find(sound):
    banks={};localized={}
    for p in sorted(Path(sound).glob('S_SB_*.xsb')):
        d=p.read_bytes();cs=vc.read_cues(d);wb=Bank(p.with_name(p.name.replace('S_SB_','S_WB_')).with_suffix('.xwb'))
        banks[p.name]=(d,cs,wb)
        for off,snd,nm in cs:
            if vc.neutral(nm):continue
            c=nm[:2].lower(); key=vc.ALIAS.get(nm[3:].upper(),nm[3:].upper())
            w=sound_wave(d,cs,snd)
            if w<wb.count:localized.setdefault((c,key),[]).append((p.name,nm,wb,w))
    changes=[]
    for name,(d,cs,wb) in banks.items():
        refs={}
        for off,snd,nm in cs:
            w=sound_wave(d,cs,snd); refs.setdefault(w,[]).append(nm)
        for off,snd,nm in cs:
            cl=vc.clase(nm)
            if not vc.neutral(nm) or cl not in ('juego','cine'):continue
            w=sound_wave(d,cs,snd)
            if w>=wb.count or refs[w]!=[nm]:continue
            for code in vc.LANGS[1:]:
                if nm.upper() in vc.twins(cs,code):continue
                candidates=localized.get((code,nm.upper()),[])
                if not candidates:continue
                # Evitar escoger entre sonidos distintos con un mismo nombre.
                hashes={hashlib.sha256(src.data(i)).hexdigest() for b,n,src,i in candidates}
                if len(hashes)!=1:continue
                sourcebank,twin,src,sw=candidates[0]
                fmt=src.meta[sw][1]; targetfmt=wb.meta[w][1]
                if fmt!=targetfmt or fmt&3!=1 or (fmt>>2)&7!=1:continue
                changes.append({'banco':name,'cue':nm,'clase':cl,'idioma':code,'onda':w,
                                'fuente_banco':sourcebank,'gemelo':twin,'fuente_onda':sw,
                                'sha256_audio':next(iter(hashes)),'metodo':'bytes Xbox ADPCM USA copiados, sin recodificar',
                                'segundos_original':wb.meta[w][3]/36*64/(fmt>>5),
                                'segundos_doblado':src.meta[sw][3]/36*64/(fmt>>5)})
    return banks,changes

def generate(sound,out,force=False,languages=None):
    chosen = vc.LANGS if languages is None else tuple(dict.fromkeys(('en', *languages)))
    if any(c not in vc.LANGS for c in chosen): raise ValueError('Idioma no soportado')
    out=Path(out);banks,changes=find(sound)
    changes=[c for c in changes if c['idioma'] in chosen]
    for code in chosen:
        if code == 'en': continue
        for name in sorted({c['banco'] for c in changes if c['idioma']==code}):
            selected=[c for c in changes if c['idioma']==code and c['banco']==name]
            d,cs,wb=banks[name];data=bytearray(wb.path.read_bytes())
            for c in selected:
                src=banks[c['fuente_banco']][2];wave=src.meta[c['fuente_onda']];audio=src.data(c['fuente_onda'])
                data+=bytes((-len(data))%2048)
                offset=len(data)-wb.seg[3][0];data+=audio
                meta=(wave[0],wave[1],offset,len(audio),wave[4],wave[5])
                struct.pack_into('<6I',data,wb.seg[1][0]+c['onda']*24,*meta)
            struct.pack_into('<I',data,0x24,len(data)-wb.seg[3][0])
            shared=out/'_compartidas'/code/wb.path.name;shared.parent.mkdir(parents=True,exist_ok=True)
            expected=hashlib.sha256(data).hexdigest()
            if shared.exists() and hashlib.sha256(shared.read_bytes()).hexdigest()!=expected and not force:
                raise FileExistsError(str(shared)+' distinto: use --forzar')
            if not shared.exists() or force:
                tmp=shared.with_suffix('.tmp');tmp.write_bytes(data);os.replace(tmp,shared)
            for game,cine in __import__('itertools').product(chosen,repeat=2):
                keep=[c for c in selected if c['idioma']==(cine if c['clase']=='cine' else game)]
                if not keep:continue
                # En los discos actuales solo hay rescates de juego; no
                # mezclar cambios de ambos ejes en una misma copia compartida.
                if len(keep)!=len(selected):raise ValueError('Se requiere copia combinada adicional')
                dst=out/(game+'+'+cine)/wb.path.name
                if not dst.exists():
                    try:os.link(shared,dst)
                    except OSError:shutil.copyfile(shared,dst)
                elif force:
                    dst.unlink()
                    try:os.link(shared,dst)
                    except OSError:shutil.copyfile(shared,dst)
                elif hashlib.sha256(dst.read_bytes()).hexdigest()!=expected:raise FileExistsError(str(dst))
                rp=dst.with_name(name).with_suffix('.json')
                report=vc.combo(banks[name][0],game,cine)[1]
                report['rescatados_otro_banco_USA']=keep
                report['sin_gemelo']=[r for r in report['sin_gemelo'] if r['cue'] not in {x['cue'] for x in keep}]
                for c in keep:
                    cl=c['clase'];counts=report['conteos']
                    counts[cl+'_doblados']=counts.get(cl+'_doblados',0)+1
                    counts[cl+'_ingles']-=1
                serialized=json.dumps(report,ensure_ascii=False,indent=2)+'\n'
                if not rp.exists() or rp.read_text(encoding='utf-8')!=serialized:
                    if rp.exists() and not force and 'rescatados_otro_banco_USA' in json.loads(rp.read_text(encoding='utf-8')):
                        raise FileExistsError(str(rp)+' distinto; use --forzar')
                    rp.write_text(serialized,encoding='utf-8')
    return changes

if __name__=='__main__':
    import sys
    _,rows=find(sys.argv[1]);print(json.dumps(rows,ensure_ascii=False,indent=2))
