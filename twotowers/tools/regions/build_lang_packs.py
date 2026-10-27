#!/usr/bin/env python3
"""build_lang_packs.py -- builds the region language packs from the user's own discs.

    python build_lang_packs.py --game <USA game_files | port folder | USA Xbox ISO>
                               --out <new empty folder>
                               [--pal <PAL Xbox ISO | extracted PAL folder>]
                               [--ps2 es=<ISO> it=<ISO> fr=<ISO> de=<ISO>]
                               [--voices es it fr de] [--texts all | pal-es pal-de ...]
                               [--fmv] [--fmv-languages es de fr it uk]
                               [--community <lang folder with the community packs>]

Writes <out>/lang/, ready to be copied over the port's own lang/ folder:
    lang/voz/<game>+<cine>/   voice banks for every gameplay/cutscene voice pair
                              (TT_VOICE / TT_VOICE_CINE in src/lang.c)
    lang/pal-<code>/          official PAL text (es de fr it sv nl en), PAL credits
    lang/fmv/<code>/          localized PAL films (TT_FMV), only with --fmv
and next to it the receipts VOCES_ADICIONALES_PAL.json, VOCES_ADICIONALES_PS2.json,
idiomas_instalados.json and build_lang_packs.json.

Sources:
  * USA Xbox (NTSC) -- required. The only base: its default.xbe must be the USA retail
    one. Its SOUND banks already hold the ES/IT/FR/DE voices (voces_combo.py) and some
    lines placed in other banks (voces_usa_huecos.py).
  * PAL Xbox -- optional. Official PAL text (pal_lang_pack.py), localized films and a
    few voice lines the USA disc lacks (voces_pal_extra.py). The PAL XBE is never run.
  * PS2 PAL -- optional, one ISO per language (es it fr de). Fills the remaining missing
    voice lines by exact name, PS-ADPCM converted to Xbox ADPCM (voces_ps2_extra.py).
    Only the editions in ps2_disc_catalog.json are accepted.

The steps and their order are the same as the R8 installer (engine.py). Discs and
folders are only read: their size and modification time are checked before and after,
and the output folder must be new or empty. Nothing from the discs is ever stored
outside <out>. Pure Python standard library (3.11+).
"""
from pathlib import Path
import argparse, hashlib, importlib.util, json, os, shutil, sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
USA = 'ae2d1b5304b3e9572339b693a6a45ad0869bdaeec89aae374b5c14feebbb8d30'
PAL = '6bcf4cd9f3fa8983b5beea426cd225e0c7e55df5d316ca0a071a3fae3a1003f4'
VOICES = ('es', 'it', 'fr', 'de')
FMV_CODES = ('es', 'de', 'fr', 'it', 'uk')


class BuildError(Exception):
    pass


def module(name):
    sp = importlib.util.spec_from_file_location(name, HERE / (name + '.py'))
    m = importlib.util.module_from_spec(sp)
    sp.loader.exec_module(m)
    return m


def digest(p):
    with Path(p).open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def savejson(p, data):
    Path(p).write_bytes((json.dumps(data, ensure_ascii=False, indent=2) + '\n').encode())


def safe(root, rel):
    """root/rel, refusing absolute paths and anything that leaves root."""
    rel = rel.replace('\\', '/')
    if not rel or rel.startswith('/') or ':' in rel or any(p in ('', '.', '..') for p in rel.split('/')):
        raise BuildError('UNSAFE_PATH: ' + rel)
    return Path(root) / rel


def find(folder, name):
    """folder/name, matching the case of whatever is on disk."""
    return next((p for p in Path(folder).iterdir() if p.name.lower() == name.lower()), None)


def region(source):
    """('USA'|'PAL'|'INCOMPATIBLE', ISO inventory or None) from the default.xbe digest."""
    source = Path(source)
    if source.is_file():
        with source.open('rb') as f:
            rows = module('inspect_pal').inventory(f)
            x = next((r for r in rows if r['path'].lower() == 'default.xbe'), None)
            if x is None:
                raise BuildError('NO_DEFAULT_XBE: ' + str(source))
            f.seek(x['offset'])
            sha = hashlib.sha256(f.read(x['size'])).hexdigest()
    else:
        x = find(source, 'default.xbe') if source.is_dir() else None
        if x is None:
            raise BuildError('NO_DEFAULT_XBE: ' + str(source))
        sha, rows = digest(x), None
    return ('USA' if sha == USA else 'PAL' if sha == PAL else 'INCOMPATIBLE'), rows, sha


def extract(iso, out, rows, select):
    """Copies the selected ISO entries to out (a folder this program created)."""
    selected = [r for r in rows if select(r)]
    for r in selected:
        safe(out, r['path'])
    with Path(iso).open('rb') as f:
        for r in selected:
            p = safe(out, r['path'])
            p.parent.mkdir(parents=True, exist_ok=True)
            f.seek(r['offset'])
            left = r['size']
            with p.open('xb') as target:
                while left:
                    b = f.read(min(left, 4 << 20))
                    if not b:
                        raise BuildError('ISO_TRUNCATED: ' + str(iso))
                    target.write(b)
                    left -= len(b)
    print('extracted %d entries from %s' % (len(selected), Path(iso).name), flush=True)
    return Path(out)


def game_folder(p):
    """The USA data folder: the folder itself, or <port>/game_files (or its ini game_dir)."""
    p = Path(p)
    if p.is_file() or find(p, 'default.xbe'):
        return p
    ini = p / 'twotowers_recomp.ini'
    if ini.is_file():
        import configparser
        c = configparser.ConfigParser(interpolation=None)
        c.read(ini, encoding='utf-8-sig')
        d = Path(c.get('Paths', 'game_dir', fallback='game_files').strip() or 'game_files')
        return d if d.is_absolute() else p / d
    return p / 'game_files'


def snapshot(paths):
    """(size, mtime) of every input file read, to prove nothing was modified."""
    seen = {}
    for p in paths:
        p = Path(p)
        if p.is_file():
            seen[str(p)] = (p.stat().st_size, p.stat().st_mtime_ns)
        elif p.is_dir():
            for q in sorted(p.rglob('*')):
                if q.is_file():
                    seen[str(q)] = (q.stat().st_size, q.stat().st_mtime_ns)
    return seen


def inside(a, b):
    a, b = Path(a).resolve(), Path(b).resolve()
    return a == b or b in a.parents


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument('--game', required=True, help='USA game_files, the port folder, or the USA Xbox ISO')
    ap.add_argument('--out', required=True, help='new or empty output folder')
    ap.add_argument('--pal', help='PAL Xbox ISO or extracted PAL folder')
    ap.add_argument('--ps2', nargs='+', default=[], metavar='CODE=ISO', help='PS2 PAL ISO per language (es it fr de)')
    ap.add_argument('--voices', nargs='+', default=[], choices=VOICES + ('en',))
    ap.add_argument('--texts', nargs='+', default=[], help='all, or pal-es pal-de pal-fr pal-it pal-sv pal-nl pal-en')
    ap.add_argument('--fmv', action='store_true', help='also copy the localized PAL films')
    ap.add_argument('--fmv-languages', nargs='+', choices=FMV_CODES, help='default: the chosen voices')
    ap.add_argument('--community', help="community packs; default: the port's own lang folder")
    a = ap.parse_args(argv)
    pl = module('pal_lang_pack')
    pal_codes = [c for c, *_ in pl.LANGS]
    texts = pal_codes if a.texts == ['all'] else a.texts
    if any(c not in pal_codes for c in texts):
        raise BuildError('TEXT_LANGUAGE: choose from all ' + ' '.join(pal_codes))
    voices = [c for c in dict.fromkeys(a.voices) if c != 'en']
    ps2 = {}
    for item in a.ps2:
        code, _, path = item.partition('=')
        if code not in VOICES or not path or code in ps2:
            raise BuildError('PS2_SOURCES_BY_LANGUAGE_REQUIRED: es=<ISO> it=<ISO> ...')
        if code not in voices:
            raise BuildError('PS2_LANGUAGE_NOT_SELECTED: add %s to --voices' % code)
        ps2[code] = str(Path(path).resolve())
    fmv_codes = [c for c in (a.fmv_languages or voices) if c != 'en'] if a.fmv else []
    if not (voices or texts or a.fmv):
        raise BuildError('NOTHING_TO_DO: choose --voices, --texts and/or --fmv')
    if (texts or a.fmv) and not a.pal:
        raise BuildError('PAL_CONTENT_SOURCE_REQUIRED: --texts and --fmv need --pal')

    given = Path(a.game).resolve()
    game = game_folder(given).resolve()
    out = Path(a.out).resolve()
    pal = Path(a.pal).resolve() if a.pal else None
    community = Path(a.community).resolve() if a.community else None
    if community is None and texts:
        port = given if given.is_dir() and not find(given, 'default.xbe') else given.parent
        community = port / 'lang'
    if texts and any(comm for c, f, v, comm, n in pl.LANGS if c in texts) and not (community and community.is_dir()):
        raise BuildError('COMMUNITY_PACKS_REQUIRED: --community <the port lang folder>')
    inputs = [p for p in (game, pal, community, *map(Path, ps2.values())) if p]
    for p in inputs:
        if inside(out, p) or inside(p, out):
            raise BuildError('OVERLAPPING_PATHS: ' + str(p))
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        raise BuildError('OUTPUT_NOT_EMPTY: ' + str(out))

    reg, inv, usa_sha = region(game)
    if reg == 'PAL':
        raise BuildError('PAL_IS_ONLY_A_SUPPLEMENT: --game must be the USA game data, the PAL disc goes in --pal')
    if reg != 'USA':
        raise BuildError('INCOMPATIBLE_BASE: default.xbe is not the USA retail one')
    pal_sha = None
    if pal:
        r, pal_rows, pal_sha = region(pal)
        if r != 'PAL':
            raise BuildError('INCOMPATIBLE_PAL: default.xbe is not the PAL retail one')
    ps2_mod = module('voces_ps2_extra') if ps2 else None
    ps2_proof = {}
    for code, path in ps2.items():
        disc = ps2_mod.Disc(path, code)
        disc.unchanged()
        ps2_proof[code] = {'iso': Path(path).name, 'header_sha256': disc.header_sha}
    reads = [game / n for n in ('SOUND', 'UI', 'Worlds', 'default.xbe')] if not inv else [game]
    reads += [p for p in (pal, community, *map(Path, ps2.values())) if p]
    before = snapshot(reads)

    out.mkdir(parents=True, exist_ok=True)
    lang = out / 'lang'
    lang.mkdir()
    temps = []
    try:
        if inv:  # USA ISO: only the folders the tools read
            temps.append(out / '_usa_source')
            game = extract(game, temps[-1], inv,
                           lambda r: r['path'].replace('\\', '/').split('/')[0].lower() in ('sound', 'ui', 'worlds'))
        sound = find(game, 'SOUND')
        if voices:
            if sound is None:
                raise BuildError('NO_SOUND_FOLDER: ' + str(game))
            print('voices: ' + ' '.join(voices), flush=True)
            module('voces_combo').generate(sound, lang / 'voz', languages=voices)
            module('voces_usa_huecos').generate(sound, lang / 'voz', languages=voices)
            if pal:
                print('voices: PAL supplements', flush=True)
                savejson(out / 'VOCES_ADICIONALES_PAL.json',
                         module('voces_pal_extra').generate(sound, pal, lang / 'voz', voices))
            if ps2:
                print('voices: PS2 supplements', flush=True)
                savejson(out / 'VOCES_ADICIONALES_PS2.json', ps2_mod.generate(sound, ps2, lang / 'voz', voices))
        report = {}
        if texts or a.fmv:
            src = pal
            if pal.is_file():
                folders = {folder for code, folder, *_ in pl.LANGS if code in texts}
                videos = set(fmv_codes)

                def needed(row):
                    path = row['path'].replace('\\', '/').lower()
                    parts = path.split('/')
                    if parts[0] in folders and len(parts) > 1 and parts[1] in ('ui', 'worlds'):
                        return True
                    return parts[0] == 'fmv' and any(path.endswith('_' + c + '.bik') for c in folders | videos)
                temps.append(out / '_pal_source')
                src = extract(pal, temps[-1], pal_rows, needed)
            for code, folder, var, comm, name in pl.LANGS:
                if code not in texts:
                    continue
                print('text: ' + code, flush=True)
                pl.build(str(game), str(src), str(community), str(lang), code, folder, var, comm, name, report)
                if not a.fmv and (lang / code / 'fmv').exists():
                    shutil.rmtree(lang / code / 'fmv')
            fmv_dir = find(src, 'FMV') if a.fmv else None
            for code in fmv_codes:
                for base in pl.FMV_LOCAL:
                    p = fmv_dir and find(fmv_dir, base + '_' + code + '.bik')
                    if p:
                        q = lang / 'fmv' / code / (base + '.bik')
                        q.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(p, q)
            savejson(out / 'idiomas_instalados.json', report)
    finally:
        for t in temps:
            if t.exists():
                shutil.rmtree(t)
    after = snapshot(reads)
    if after != before:
        changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
        raise BuildError('INPUT_CHANGED_DURING_BUILD: ' + ', '.join(changed[:5]))
    files = sorted(p for p in lang.rglob('*') if p.is_file())
    receipt = {
        'tool': 'build_lang_packs.py', 'schema': 1,
        'usa_xbe_sha256': usa_sha, 'pal_xbe_sha256': pal_sha, 'ps2': ps2_proof,
        'voices': voices, 'texts': texts, 'fmv_languages': fmv_codes,
        'community': str(community) if community else None,
        'inputs_unchanged': {'files_checked': len(before), 'result': 'size and mtime identical before and after'},
        'output': {'files': len(files), 'bytes': sum(p.stat().st_size for p in files)},
        'notice': 'Built from the user\'s own discs. Do not redistribute the output: it contains game data.',
    }
    savejson(out / 'build_lang_packs.json', receipt)
    print('done: %d files in %s' % (len(files), lang), flush=True)
    return receipt


if __name__ == '__main__':
    try:
        main()
    except (BuildError, ValueError, FileExistsError) as e:
        print('error: ' + str(e), file=sys.stderr)
        sys.exit(1)
