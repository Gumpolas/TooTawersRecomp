"""Bancos de voces por idioma para Las Dos Torres (Xbox).

Los bancos S_SB_*.xsb USA v5 traen, junto a cada
pista hablada neutra (AM_IGC1A), sus versiones dobladas ES_AM_IGC1A,
IT_AM_IGC1A, FR_AM_IGC1A y DE_AM_IGC1A. El XBE PAL elige el prefijo por el
idioma de la consola; el XBE USA siempre pide la neutra (ingles).

Este script copia cada S_SB_*.xsb y hace que la pista neutra use el sonido
de la doblada (campo u16 +2 de la entrada de cue, 16 bytes desde 0x30). No
cambia nombres ni la tabla de busqueda, ni toca los S_WB_*.xwb.
Los PAL son v6: no se deben usar con este generador para el XBE USA.

uso: python voces_pack.py <carpeta SOUND del juego> <salida: lang\\voz> [informe.json]
Crea <salida>\\<es|it|fr|de>\\S_SB_*.xsb (solo los bancos con cambios).
"""
import json
import os
import struct
import sys

PREF = {'es': 'ES_', 'it': 'IT_', 'fr': 'FR_', 'de': 'DE_'}
# erratas del disco: nombre doblado (sin prefijo) -> nombre de la neutra
ALIAS = {'AR_VO19B': 'AR_VO198'}


def cues(d):
    if d[:6] != b'SDBK\x05\x00':
        raise ValueError('se requiere SDBK v5 USA (los PAL v6 no son compatibles)')
    n = struct.unpack_from('<H', d, 0x1c)[0]   # cues (u16 0x1a: sonidos)
    out = []
    for i in range(n):
        o = 0x30 + 16 * i
        snd, no = struct.unpack_from('<HI', d, o + 2)
        nm = d[no:d.index(b'\0', no)].decode('latin-1')
        out.append((o, snd, nm))
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    rep = {}
    for fn in sorted(os.listdir(src)):
        if not (fn.upper().startswith('S_SB_') and fn.lower().endswith('.xsb')):
            continue
        d = open(os.path.join(src, fn), 'rb').read()
        cs = cues(d)
        by = {nm.upper(): (o, snd, nm) for o, snd, nm in cs}
        for code, p in PREF.items():
            b = bytearray(d)
            done, orphan = [], []
            for o, snd, nm in cs:
                if not nm.upper().startswith(p):
                    continue
                k = nm[len(p):].upper()
                base = by.get(k) or by.get(ALIAS.get(k, ''))
                if not base:
                    orphan.append(nm)
                    continue
                struct.pack_into('<H', b, base[0] + 2, snd)
                done.append(base[2])
            r = rep.setdefault(code, {'bancos': {}, 'pistas': 0, 'sin_neutra': []})
            r['pistas'] += len(done)
            r['sin_neutra'] += orphan
            if done:
                os.makedirs(os.path.join(dst, code), exist_ok=True)
                open(os.path.join(dst, code, fn), 'wb').write(bytes(b))
                r['bancos'][fn] = len(done)
    for code, r in rep.items():
        print('%s: %d pistas en %d bancos; dobladas sin neutra: %s' % (code, r['pistas'], len(r['bancos']), r['sin_neutra'] or '-'))
    if len(sys.argv) > 3:
        json.dump(rep, open(sys.argv[3], 'w'), indent=1)


if __name__ == '__main__':
    main()
