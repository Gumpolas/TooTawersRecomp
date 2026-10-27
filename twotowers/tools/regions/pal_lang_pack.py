"""Paquetes de idioma del port (lang/<codigo>/) con el TEXTO OFICIAL de la edicion PAL.

El port USA traduce cada cadena del juego por el hash FNV-1a de su ingles (lang.c,
claves "G%08X"). La PAL guarda las mismas tablas (OBSS 900-902, OBST 905-907) con el
mismo numero de entradas, los mismos identificadores y los mismos tiempos de
subtitulo; solo cambia el texto. Este programa empareja tabla a tabla y entrada a
entrada (comprobando id/hash/tiempos) el ingles USA con el texto PAL y escribe
game.txt. Las palabras propias del port (claves P:/L:, text.txt) y las letras
anadidas (glyphs*.bin) se copian del paquete comunitario del mismo idioma; las
cadenas que la PAL no cubre conservan la traduccion comunitaria y se cuentan.

Variantes PAL (carpeta, tabla): es/de/fr/it usan la 900/905; uk lleva ingles (900/905),
sueco (901/906) y neerlandes (902/907).

uso: python pal_lang_pack.py <game_files USA> <pal_files> <lang comunitario> <salida lang> [informe.json]
Solo lee las tres primeras carpetas; escribe en <salida lang>/pal-*/.
"""
from collections import Counter, defaultdict
import bisect
import json
import os
import struct
import sys
import zlib

LANGS = [  # codigo, carpeta PAL, variante, paquete comunitario, nombre
    ('pal-es', 'es', 0, 'es', 'Español (PAL original)'),
    ('pal-de', 'de', 0, 'de', 'Deutsch (PAL original)'),
    ('pal-fr', 'fr', 0, 'fr', 'Français (PAL original)'),
    ('pal-it', 'it', 0, 'it', 'Italiano (PAL original)'),
    ('pal-sv', 'uk', 1, 'sv', 'Svenska (PAL original)'),
    ('pal-nl', 'uk', 2, 'nl', 'Nederlands (PAL original)'),
    ('pal-en', 'uk', 0, None, 'English UK (PAL original)'),
]
# FMV con voz/rotulos propios por idioma en el disco PAL (FMV/<base>_<carpeta>.bik)
FMV_LOCAL = ('logos', 'mont', 'yar1_2')


def load(path):
    d = open(path, 'rb').read()
    if d[:4] == b'\x01\0\0\0':
        us, cs = struct.unpack_from('<II', d, 4)
        d = zlib.decompress(d[12:12 + cs])
        if len(d) != us:
            raise ValueError('%s: tamano descomprimido %d != %d' % (path, len(d), us))
    if d[0] != 9:
        raise ValueError('%s: no es BFF v9' % path)
    return d


def table(d):
    tab = struct.unpack_from('<I', d, 4)[0]
    cnt = struct.unpack_from('<I', d, tab)[0]
    ents = [struct.unpack_from('<3I', d, tab + 4 + 12 * i) for i in range(cnt)]
    offs = sorted(set(e[1] for e in ents) | {tab})
    return [(t, o, x, offs[bisect.bisect_right(offs, o)]) for t, o, x in ents]


def u16s(d, o, lim):
    n = 0
    while o + 2 * n + 1 < lim and struct.unpack_from('<H', d, o + 2 * n)[0]:
        n += 1
    return tuple(struct.unpack_from('<%dH' % n, d, o))


def strings(d, t, a, e):
    """('OBSS'|'OBST', [(clave de entrada, unidades)]) o None"""
    out = []
    if 900 <= t <= 902 and d[a:a + 4] == b'OBSS':
        for i in range(struct.unpack_from('<I', d, a + 12)[0]):
            sid, so = struct.unpack_from('<II', d, a + 16 + 8 * i)
            out.append((sid, u16s(d, a + so, e) if a + so < e else ()))
        return 'OBSS', out
    if 905 <= t <= 907 and d[a:a + 4] == b'OBST':
        for i in range(struct.unpack_from('<I', d, a + 8)[0]):
            h, so, st, en = struct.unpack_from('<4I', d, a + 16 + 16 * i)
            out.append(((h, st, en), u16s(d, a + so, e) if a + so < e else ()))
        return 'OBST', out
    return None


def fnv16(u):
    h = 2166136261
    for c in u:
        h = ((h ^ (c & 0xFF)) * 16777619) & 0xFFFFFFFF
        h = ((h ^ (c >> 8)) * 16777619) & 0xFFFFFFFF
    return h


def tables_of(d, kind_base):
    """tablas de texto del archivo en orden, solo las del tipo base+variante pedido"""
    return [(t, strings(d, t, a, e)) for t, a, x, e in table(d) if strings(d, t, a, e)]


def pairs_for(usa_path, pal_path, variant, stats):
    """[(ingles USA, texto PAL)] de un archivo"""
    du, dp = load(usa_path), load(pal_path)
    tu = [(t, s) for t, s in tables_of(du, 0) if t in (900, 905)]          # ingles USA
    tp = [(t, s) for t, s in tables_of(dp, 0) if t in (900 + variant, 905 + variant)]
    out = []
    if len(tu) != len(tp):
        stats['tablas_distintas'].append((usa_path, len(tu), len(tp)))
        return out
    for (t1, (k1, e1)), (t2, (k2, e2)) in zip(tu, tp):
        if k1 != k2 or len(e1) != len(e2) or t2 - t1 != variant:
            stats['tablas_distintas'].append((usa_path, t1, len(e1), t2, len(e2)))
            continue
        for (id1, s1), (id2, s2) in zip(e1, e2):
            if id1 != id2:
                stats['entradas_distintas'] += 1
                continue
            if not s1 or not s2 or all(c == 0x20 for c in s1):
                continue
            out.append((s1, s2))
    return out


def esc(u):
    s = ''.join(chr(c) for c in u)
    return s.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t').replace('\r', '\\r')


def file_pairs(usa, pal, folder):
    """(USA, PAL) de cada archivo con texto"""
    r = []
    for n in ('startup.BFF', 'credits.BFF', 'mainmenu.BFF'):
        r.append((os.path.join(usa, 'UI', n), os.path.join(pal, folder, 'UI', n)))
    pw = os.path.join(pal, folder, 'Worlds')
    pal_worlds = {f.lower(): f for f in os.listdir(pw)}
    for f in sorted(os.listdir(os.path.join(usa, 'Worlds'))):
        b, ext = os.path.splitext(f)
        if len(ext) == 4 and ext[:3].lower() == '.bf':
            p = pal_worlds.get((b + '.sf' + ext[3]).lower())
            r.append((os.path.join(usa, 'Worlds', f), os.path.join(pw, p) if p else None))
    return r


def read_pack_lines(path):
    if not path or not os.path.exists(path):
        return []
    return open(path, encoding='utf-8').read().splitlines()


def build(usa, pal, community, outdir, code, folder, variant, comm, name, report):
    stats = {'tablas_distintas': [], 'entradas_distintas': 0, 'archivos_sin_pal': []}
    by_en = defaultdict(Counter)
    for u, p in file_pairs(usa, pal, folder):
        if not p or not os.path.exists(p):
            stats['archivos_sin_pal'].append(u)
            continue
        for en, tr in pairs_for(u, p, variant, stats):
            by_en[en][tr] += 1
    official, conflicts = {}, 0
    for en, c in by_en.items():
        (tr, n), = c.most_common(1)
        if len(c) > 1:
            conflicts += 1
        official['G%08X' % fnv16(en)] = tr
    d = os.path.join(outdir, code)
    os.makedirs(d, exist_ok=True)
    cdir = os.path.join(community, comm) if comm else None
    comm_lines = read_pack_lines(os.path.join(cdir, 'game.txt')) if cdir else []
    kept, port_lines = 0, []
    for ln in comm_lines:
        if not ln or ln.startswith('#') or '\t' not in ln:
            continue
        k = ln.split('\t', 1)[0]
        if k.startswith('G') and k in official:
            continue
        if k.startswith('G'):
            kept += 1
        port_lines.append(ln)
    with open(os.path.join(d, 'game.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# %s -- texto oficial de la edicion PAL (pal_lang_pack.py); el resto, del paquete comunitario %s\n'
                % (name, comm or '-'))
        for k in sorted(official):
            f.write('%s\t%s\n' % (k, esc(official[k])))
        for ln in port_lines:
            f.write(ln + '\n')
    with open(os.path.join(d, 'info.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('name=%s\nrtl=0\n' % name)
    for n in ('text.txt', 'glyphs.bin', 'glyphs4.bin'):
        src = os.path.join(cdir, n) if cdir else None
        if src and os.path.exists(src):
            open(os.path.join(d, n), 'wb').write(open(src, 'rb').read())
    # creditos PAL enteros (tienen mas lineas que los USA): lang.c abre lang/<codigo>/ui/credits.BFF.
    # El juego USA lee la tabla 900; las tres entradas se apuntan a la de la variante.
    cr = bytearray(load(os.path.join(pal, folder, 'UI', 'credits.BFF')))
    tab = struct.unpack_from('<I', cr, 4)[0]
    ents = table(bytes(cr))
    src_off = [o for t, o, x, e in ents if t == 900 + variant]
    if len(src_off) == 1 and {t for t, o, x, e in ents} == {900, 901, 902}:
        for i in range(len(ents)):
            struct.pack_into('<I', cr, tab + 4 + 12 * i + 4, src_off[0])
        os.makedirs(os.path.join(d, 'ui'), exist_ok=True)
        open(os.path.join(d, 'ui', 'credits.BFF'), 'wb').write(bytes(cr))
        credits = True
    else:
        credits = False
    # FMV localizados: la redireccion de lang.c los busca en lang/<codigo>/fmv/<nombre USA>
    fmv = []
    fd = os.path.join(pal, 'FMV')
    for b in FMV_LOCAL:
        src = os.path.join(fd, '%s_%s.bik' % (b, folder))
        if os.path.exists(src):
            os.makedirs(os.path.join(d, 'fmv'), exist_ok=True)
            dst = os.path.join(d, 'fmv', b + '.bik')
            if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src):
                with open(src, 'rb') as i, open(dst + '.tmp', 'wb') as o:
                    while True:
                        blk = i.read(1 << 22)
                        if not blk:
                            break
                        o.write(blk)
                os.replace(dst + '.tmp', dst)
            fmv.append(b + '.bik')
    report[code] = {'carpeta_pal': folder, 'variante': variant, 'cadenas_oficiales': len(official),
                    'ingles_con_varias_traducciones': conflicts,
                    'cadenas_comunitarias_conservadas': kept, 'fmv': fmv, 'creditos_pal': credits,
                    'tablas_distintas': stats['tablas_distintas'], 'entradas_distintas': stats['entradas_distintas'],
                    'archivos_sin_pal': stats['archivos_sin_pal']}
    print('%-7s oficiales %5d  conflictos %3d  comunitarias %4d  creditos %d  fmv %s  tablas!=%d entradas!=%d sin_pal=%d'
          % (code, len(official), conflicts, kept, credits, fmv, len(stats['tablas_distintas']),
             stats['entradas_distintas'], len(stats['archivos_sin_pal'])))


def main():
    if len(sys.argv) not in (5, 6):
        sys.exit(__doc__)
    usa, pal, community, outdir = sys.argv[1:5]
    report = {}
    for code, folder, variant, comm, name in LANGS:
        build(usa, pal, community, outdir, code, folder, variant, comm, name, report)
    if len(sys.argv) == 6:
        json.dump(report, open(sys.argv[5], 'w', encoding='utf-8'), indent=1, ensure_ascii=False)


if __name__ == '__main__':
    main()
