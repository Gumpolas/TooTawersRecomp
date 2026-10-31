#!/usr/bin/env python3
"""runes.py -- the Anglo-Saxon runes (futhorc) for The Two Towers Recompiled (1.9.9).

    python3 runes.py            writes strings/ang-Runr.tsv from strings/ang.tsv

Old English written in runes, letter by letter (the futhorc's sound values:
æ -> ᚫ, þ/ð -> ᚦ, ng -> ᛝ, ea -> ᛠ, eo -> ᛇ, c -> ᚳ, k -> ᛣ, x -> ᛉ ...).
Kept in Latin letters: {0}-style placeholders, numbers and anything with a
digit, file names and paths, acronyms and button letters (A, LB, HUD, ISO...),
and the L: keys (the launcher's words, drawn with the computer's fonts, which
may have no runes).

Also the runes' pictures: build_lang.py draws each rune from the strokes in
STROKES (no font needed), to the game font's measures.
"""
import os, re

# each rune: (width, [polyline, ...]) in a box 1 high (y 0 the top, 1 the baseline)
STROKES = {
    'ᚠ': (.5, [[(0, 0), (0, 1)], [(0, .48), (.5, .2)], [(0, .2), (.45, -.02)]]),
    'ᚢ': (.5, [[(0, 1), (0, 0), (.5, .32), (.5, 1)]]),
    'ᚦ': (.45, [[(0, 0), (0, 1)], [(0, .25), (.45, .5), (0, .75)]]),
    'ᚩ': (.5, [[(0, 0), (0, 1)], [(0, .02), (.5, .26)], [(0, .32), (.5, .56)]]),
    'ᚱ': (.48, [[(0, 1), (0, 0), (.44, .22), (0, .46), (.48, 1)]]),
    'ᚳ': (.42, [[(0, 0), (0, 1)], [(0, .02), (.42, .3)]]),
    'ᚷ': (.6, [[(0, 0), (.6, 1)], [(.6, 0), (0, 1)]]),
    'ᚹ': (.44, [[(0, 1), (0, 0), (.44, .22), (0, .46)]]),
    'ᚻ': (.55, [[(0, 0), (0, 1)], [(.55, 0), (.55, 1)], [(0, .3), (.55, .48)], [(0, .5), (.55, .68)]]),
    'ᚾ': (.5, [[(.25, 0), (.25, 1)], [(0, .35), (.5, .62)]]),
    'ᛁ': (0, [[(0, 0), (0, 1)]]),
    'ᛄ': (.56, [[(.28, 0), (.28, 1)], [(.28, .3), (.56, .5), (.28, .7), (0, .5), (.28, .3)]]),
    'ᛇ': (.5, [[(.25, 0), (.25, 1)], [(.25, 0), (.5, .22)], [(.25, 1), (0, .78)]]),
    'ᛈ': (.42, [[(.42, .28), (0, 0), (0, 1), (.42, .72)]]),
    'ᛉ': (.6, [[(.3, 0), (.3, 1)], [(.3, .45), (0, .08)], [(.3, .45), (.6, .08)]]),
    'ᛋ': (.48, [[(.42, 0), (0, .36), (.46, .62), (.04, 1)]]),
    'ᛏ': (.6, [[(.3, 0), (.3, 1)], [(0, .32), (.3, 0), (.6, .32)]]),
    'ᛒ': (.46, [[(0, 0), (0, 1)], [(0, 0), (.46, .25), (0, .5), (.46, .75), (0, 1)]]),
    'ᛖ': (.6, [[(0, 1), (0, 0), (.3, .34), (.6, 0), (.6, 1)]]),
    'ᛗ': (.6, [[(0, 0), (0, 1)], [(.6, 0), (.6, 1)], [(0, 0), (.6, .46)], [(.6, 0), (0, .46)]]),
    'ᛚ': (.44, [[(0, 1), (0, 0), (.44, .3)]]),
    'ᛝ': (.6, [[(.3, .08), (.6, .5), (.3, .92), (0, .5), (.3, .08)], [(.3, 0), (.3, .08)], [(.3, .92), (.3, 1)]]),
    'ᛟ': (.6, [[(.3, 0), (.6, .36), (0, 1)], [(.3, 0), (0, .36), (.6, 1)]]),
    'ᛞ': (.7, [[(0, 0), (0, 1)], [(.7, 0), (.7, 1)], [(0, 0), (.7, 1)], [(.7, 0), (0, 1)]]),
    'ᚪ': (.46, [[(0, 0), (0, 1)], [(0, .12), (.4, .34), (.4, .5)], [(0, .44), (.4, .66), (.4, .82)]]),
    'ᚫ': (.46, [[(0, 0), (0, 1)], [(0, .14), (.46, .4)], [(0, .42), (.46, .68)]]),
    'ᚣ': (.56, [[(0, 1), (0, 0), (.56, .32), (.56, 1)], [(.28, .16), (.28, 1)]]),
    'ᛠ': (.6, [[(.3, 0), (.3, 1)], [(0, .28), (.3, 0), (.6, .28)], [(0, .56), (.3, .28), (.6, .56)]]),
    'ᛣ': (.6, [[(.3, 0), (.3, 1)], [(.3, .55), (0, .92)], [(.3, .55), (.6, .92)]]),
}

# Old English letters -> runes (two-letter sounds first)
DIGRAPHS = [('ng', 'ᛝ'), ('ea', 'ᛠ'), ('eo', 'ᛇ'), ('th', 'ᚦ'), ('cw', 'ᚳᚹ'), ('qu', 'ᚳᚹ')]
SINGLE = {'a': 'ᚪ', 'æ': 'ᚫ', 'b': 'ᛒ', 'c': 'ᚳ', 'd': 'ᛞ', 'ð': 'ᚦ', 'þ': 'ᚦ', 'e': 'ᛖ', 'f': 'ᚠ', 'g': 'ᚷ',
          'h': 'ᚻ', 'i': 'ᛁ', 'j': 'ᛄ', 'k': 'ᛣ', 'l': 'ᛚ', 'm': 'ᛗ', 'n': 'ᚾ', 'o': 'ᚩ', 'œ': 'ᛟ', 'p': 'ᛈ',
          'q': 'ᚳ', 'r': 'ᚱ', 's': 'ᛋ', 't': 'ᛏ', 'u': 'ᚢ', 'v': 'ᚠ', 'w': 'ᚹ', 'ƿ': 'ᚹ', 'x': 'ᛉ', 'y': 'ᚣ',
          'z': 'ᛋ', 'ȝ': 'ᚷ', 'é': 'ᛖ', 'á': 'ᚪ', 'í': 'ᛁ', 'ó': 'ᚩ', 'ú': 'ᚢ', 'ý': 'ᚣ', 'ö': 'ᛟ', 'ë': 'ᛖ',
          'â': 'ᚪ', 'ê': 'ᛖ', 'î': 'ᛁ', 'ô': 'ᚩ', 'û': 'ᚢ', 'ā': 'ᚪ', 'ē': 'ᛖ', 'ī': 'ᛁ', 'ō': 'ᚩ', 'ū': 'ᚢ',
          'ȳ': 'ᚣ', 'ǣ': 'ᚫ'}


def rune_word(w):
    out = ''
    lw = w.lower()
    i = 0
    while i < len(lw):
        for dg, r in DIGRAPHS:
            if lw.startswith(dg, i):
                out += r; i += len(dg); break
        else:
            ch = lw[i]
            out += SINGLE.get(ch, w[i]); i += 1
    return out


NAMES = ('Xbox', 'PlayStation', 'DualShock', 'DualSense', 'Windows', 'Linux', 'macOS', 'Steam', 'Vulkan', 'OpenGL', 'Direct3D',
         'AppImage', 'Esc', 'Enter', 'Shift', 'Ctrl', 'Alt', 'Tab', 'Space', 'Backspace', 'Delete', 'Insert', 'Home', 'End',
         'PgUp', 'PgDn', 'Page Up', 'Page Down', 'Caps Lock', 'Num', 'Pause')
KEEP = re.compile(r'(\{[^}]*\}|\\n|https?://\S+|\b(?:' + '|'.join(re.escape(n) for n in sorted(NAMES, key=len, reverse=True)) + r')\b'
                  r'|[\w\-]*(?:\d|_|[./]\w)[\w.\-/]*|\b[A-Z]{1,5}\b(?![a-zæþð]))')


def to_runes(s):
    parts = KEEP.split(s)
    out = []
    for k, p in enumerate(parts):
        if k % 2 == 1:
            out.append(p)        # kept as it is
        else:
            out.append(re.sub(r"[^\W\d_]+", lambda m: rune_word(m.group(0)), p))
    return ''.join(out)


if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    src = os.path.join(here, 'strings', 'ang.tsv')
    dst = os.path.join(here, 'strings', 'ang-Runr.tsv')
    n = 0
    with open(src, encoding='utf-8') as fi, open(dst, 'w', encoding='utf-8', newline='\n') as fo:
        for ln in fi:
            ln = ln.rstrip('\n')
            if '\t' not in ln or ln.startswith('#'):
                fo.write(ln + '\n'); continue
            k, v = ln.split('\t', 1)
            fo.write('%s\t%s\n' % (k, v if k.startswith('L:') else to_runes(v)))
            n += 1
    print('ang-Runr: %d strings' % n)
