"""Bancos combinados USA v5. Solo cambia el u16 del sonido de cues hablados.

Uso: python voces_combo.py SOUND lang/voz [--forzar]
Las clases son explícitas; una voz sin gemelo no se considera doblada.
"""
import argparse
import hashlib
import itertools
import json
import re
import struct
from collections import Counter
from pathlib import Path
import importlib.util
_spec = importlib.util.spec_from_file_location('tt_voces_pack', Path(__file__).with_name('voces_pack.py'))
base = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(base)

LANGS = ('en', 'es', 'it', 'fr', 'de')
# Solo erratas con neutra existente, misma estructura de nombre y duración
# comprobada por auditar_audio.py. Las restantes se informan, no se adivinan.
ALIAS = dict(base.ALIAS, HOLXVO8='HOL_VO8', RO2VO18='RO2_V18',
             RO2VO24M='RO2_V24M', RO2VO27Y='RO2_V27Y', ROVO24O='RO2_V24O')
# Cues con gemelos en los bancos USA aunque no usen la abreviatura VO.
# No se usa esta lista para inferir habla en un cue sin gemelo.
SPOKEN = {'IS_CE1','IS_CE2','BAL_SE1','HOL_HRC1','RO2_SE1','RO2_SE3','TOP_HRC2','RO3_SE1','RO3_SE2C'}

def read_cues(data):
    if data[:6] != b'SDBK\x05\x00':
        raise ValueError('Se requieren bancos Xbox USA SDBK v5; no PAL v6')
    return base.cues(data)

def neutral(name):
    return not name.upper().startswith(tuple(base.PREF.values()))

def clase(name):
    n = name.upper()
    if '_AMBIENT' in n:
        return 'ambiente'
    if 'IGC' in n:
        return 'cine'
    if 'VO' in n or re.search(r'(?:_|X)V\d',n) or n in SPOKEN:
        return 'juego'
    return 'efecto'

def twins(cs, code):
    by = {n.upper(): (o, snd, n) for o, snd, n in cs}
    out = {}
    if code == 'en':
        return out
    for o, snd, name in cs:
        if not name.upper().startswith(base.PREF[code]):
            continue
        key = name[3:].upper()
        target = by.get(key)
        alias = False
        if target is None and key in ALIAS:
            target = by.get(ALIAS[key]); alias = target is not None
        if target and (not alias or target[2].upper() not in out):
            out[target[2].upper()] = (snd, name, alias)
    return out

def combo(data, game, cine):
    cs = read_cues(data)
    maps = {c: twins(cs, c) for c in {game, cine}}
    b = bytearray(data)
    report = {'cambios': [], 'sin_gemelo': [], 'alias': [], 'conteos': {}}
    count = Counter()
    for o, snd, name in cs:
        if not neutral(name):
            continue
        cl = clase(name)
        code = cine if cl == 'cine' else game
        row = {'cue': name, 'clase': cl, 'idioma': code, 'sonido_original': snd}
        if cl in ('ambiente', 'efecto'):
            count[cl] += 1
            continue
        target = maps[code].get(name.upper())
        count[cl + '_total'] += 1
        if target and code != 'en':
            ns, nm, alias = target
            struct.pack_into('<H', b, o+2, ns)
            row.update(sonido_nuevo=ns, gemelo=nm)
            report['cambios'].append(row)
            if alias:
                report['alias'].append(row)
            count[cl + '_doblados'] += 1
        else:
            count[cl + '_ingles'] += 1
            if code != 'en':
                report['sin_gemelo'].append(row)
    report['conteos'] = dict(count)
    # Verificación independiente de todos los bytes que pueden cambiar.
    allowed = {o+i for o, snd, nm in cs if neutral(nm) and clase(nm) in ('juego','cine') for i in (2,3)}
    assert all(a == z or i in allowed for i, (a,z) in enumerate(zip(data,b)))
    report['sha256_original'] = hashlib.sha256(data).hexdigest()
    report['sha256_generado'] = hashlib.sha256(b).hexdigest()
    return bytes(b), report

def generate(src, dst, force=False, languages=None):
    src, dst = Path(src), Path(dst)
    chosen = LANGS if languages is None else tuple(dict.fromkeys(('en', *languages)))
    if any(code not in LANGS for code in chosen):
        raise ValueError('Idioma de voz no soportado')
    banks = sorted(src.glob('S_SB_*.xsb'))
    if not banks:
        raise ValueError('No hay bancos S_SB_*.xsb USA v5')
    for bank in banks:
        read_cues(bank.read_bytes())
    summary = {}
    for game, cine in itertools.product(chosen, repeat=2):
        if game == cine == 'en':
            continue
        key = game+'+'+cine; totals = Counter()
        out = dst / key
        out.mkdir(parents=True, exist_ok=True)
        for p in banks:
            data, rep = combo(p.read_bytes(), game, cine)
            target = out / p.name
            if target.exists() and target.read_bytes() != data and not force:
                raise FileExistsError(str(target)+' distinto: use --forzar')
            if force or not target.exists():
                target.write_bytes(data)
            rp = out/(p.stem+'.json')
            serialized = json.dumps(rep, ensure_ascii=False, indent=2)+'\n'
            if not rp.exists() or force:
                rp.write_text(serialized, encoding='utf-8')
            totals.update(rep['conteos'])
        summary[key] = dict(totals)
        print(key, dict(totals), flush=True)
    return summary

if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('sound'); ap.add_argument('salida'); ap.add_argument('--forzar', action='store_true')
    ap.add_argument('--idiomas', nargs='+', choices=LANGS)
    a = ap.parse_args()
    generate(a.sound,a.salida,a.forzar,a.idiomas)
