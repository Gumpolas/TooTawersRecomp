#!/usr/bin/env python3
"""build_lang.py -- makes a language pack for The Two Towers Recompiled (1.9.2).

    python3 build_lang.py <code> [<code> ...]      (or: all)

Reads twotowers/tools/lang/strings/<code>.tsv -- "key <TAB> translation",
plain Unicode:
    G<8 hex>   a game string: the FNV-1a hash of its English text (UTF-16
               units), so the game's own text is never stored
    P:<English>  the port's own words drawn with the game's fonts
    L:<English>  the port's words drawn with its own fonts (launcher, F10)
and writes twotowers/lang/<code>/ (src/lang.c reads it):
    info.txt     name=<the language's own name>, rtl=0|1
    text.txt     every key, Unicode
    game.txt     the G and P keys written in the game's character codes
    glyphs.bin   the letters the game's fonts lack, for its four fonts

The game's fonts have the characters 0x20-0xFF (Windows-1252, with a few of
their own: # $ % * @ ^ | ~ draw other things). Any other letter gets a code
from 0xE000 and a picture in each font:
  * a Latin letter with marks (Polish, Czech, Turkish, Vietnamese...): the
    game's own letter, and the marks as a reference font draws them
    (the reference letter with and without them) -- matched in the game
  * anything else: drawn with an open font made to the game font's measures
    (its line height, baseline, capital height)
Chinese, Japanese and Korean are written without spaces: a zero-width space
(code 0x20, made zero wide) goes between their characters so lines can wrap,
and real spaces become 0xA0. Thai the same, between its words (U+200B in the
translation). Arabic: each letter in its joined form (the presentation
forms), the lines turned right to left as they are drawn (lang.c).
Devanagari: each syllable one picture.
"""
import json, os, re, struct, sys, unicodedata, zlib
from PIL import Image, ImageDraw, ImageFont, features

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(ROOT, 'lang')
LANGS = json.load(open(os.path.join(HERE, 'languages.json'), encoding='utf-8'))

# ---- the game's fonts: line height -> measures (rows from the cell's top) ----
# (measured on the four fonts in UI\startup.BFF; 26 is also every level's)
SLOTS = [
    # lh, baseline, cap top, x-height top, descender bottom, uncial
    dict(lh=22, bl=17, cap=5, xt=9, desc=20, unc=False),
    dict(lh=26, bl=20, cap=6, xt=10, desc=23, unc=False),
    dict(lh=40, bl=31, cap=9, xt=16, desc=38, unc=True),
    dict(lh=30, bl=23, cap=7, xt=11, desc=28, unc=True),
]

# the game's codes for Unicode characters
GAME = {}
for c in range(0x20, 0x7F):
    if chr(c) not in '#$%*@^|~':
        GAME[chr(c)] = c
for c, u in {0x80: '€', 0x82: '‚', 0x83: 'ƒ', 0x84: '„', 0x85: '…', 0x86: '†', 0x87: '‡', 0x88: 'ˆ', 0x89: '‰',
             0x8A: 'Š', 0x8B: '‹', 0x8C: 'Œ', 0x8E: 'Ž', 0x91: '‘', 0x92: '’', 0x93: '“', 0x94: '”', 0x95: '•',
             0x96: '–', 0x97: '—', 0x98: '˜', 0x99: '™', 0x9A: 'š', 0x9B: '›', 0x9C: 'œ', 0x9E: 'ž', 0x9F: 'Ÿ'}.items():
    GAME[u] = c
for c in range(0xA0, 0x100):
    if c not in (0xA4, 0xA8, 0xB0, 0xB1, 0xB2, 0xB3):
        GAME[chr(c)] = c
GAME['Đ'] = 0xD0  # (drawn as Ð)
GAME['\u00ad'] = 0x2D

FONTS = {
    'lora': ('/usr/share/fonts/truetype/google-fonts/Lora-Variable.ttf', 0, 700),
    'freeserif': ('/usr/share/fonts/truetype/freefont/FreeSerifBold.ttf', 0, None),
    'dejavu': ('/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf', 0, None),
}
CJK_TTC = '/usr/share/fonts/opentype/noto/NotoSerifCJK-SemiBold.ttc'
CJK_INDEX = {'ja': 0, 'ko': 1, 'zh-Hans': 2, 'zh-Hant': 3}   # (the collection's order: JP, KR, SC, TC, HK)


def font(name, size):
    if name.startswith('cjk:'):
        return ImageFont.truetype(CJK_TTC, size, index=CJK_INDEX[name[4:]], layout_engine=ImageFont.Layout.RAQM)
    path, idx, wght = FONTS[name]
    f = ImageFont.truetype(path, size, index=idx, layout_engine=ImageFont.Layout.RAQM)
    if wght:
        try:
            f.set_variation_by_axes([wght])
        except Exception:
            pass
    return f


def ink(img, thr=60):
    w, h = img.size
    px = img.load()
    xs = [x for x in range(w) for y in range(h) if px[x, y] > thr]
    ys = [y for y in range(h) for x in range(w) if px[x, y] > thr]
    if not xs:
        return None
    return min(xs), min(ys), max(xs), max(ys)


def render(fname, size, text, W=None, H=None, x0=8, base=None):
    f = font(fname, size)
    W = W or (int(f.getlength(text)) + 32)
    H = H or size * 3
    im = Image.new('L', (W, H), 0)
    d = ImageDraw.Draw(im)
    d.text((x0, base if base is not None else H * 2 // 3), text, font=f, fill=255, anchor='ls')
    return im, f


_size_cache = {}
def size_for(fname, sample, target):
    """the font size at which sample's ink is target rows tall"""
    k = (fname, sample, target)
    if k in _size_cache:
        return _size_cache[k]
    best = None
    for s in range(6, 120):
        im, _ = render(fname, s, sample)
        b = ink(im)
        if not b:
            continue
        hgt = b[3] - b[1] + 1
        if best is None or abs(hgt - target) < abs(best[1] - target):
            best = (s, hgt)
        if hgt > target + 2:
            break
    _size_cache[k] = best[0]
    return best[0]


# ---- what a language's text is made of ----
def is_cjk(ch):
    o = ord(ch)
    return (0x3000 <= o <= 0x30FF or 0x3400 <= o <= 0x4DBF or 0x4E00 <= o <= 0x9FFF or 0xAC00 <= o <= 0xD7AF
            or 0x1100 <= o <= 0x11FF or 0x3130 <= o <= 0x318F or 0xF900 <= o <= 0xFAFF or 0xFF00 <= o <= 0xFFEF)
NO_BREAK_BEFORE = set('、。，．・：；？！ー」』）】〕〉》”’ゃゅょっャュョッぁぃぅぇぉァィゥェォ…,.!?:;)]}%')
NO_BREAK_AFTER = set('「『（【〔〈《“‘([{')


def clusters(s):
    """a grapheme cluster each: a letter and the marks after it (and, in
    Devanagari, consonants joined by a virama)"""
    out = []
    for ch in s:
        cat = unicodedata.category(ch)
        if out and (cat in ('Mn', 'Mc', 'Me') or ch in '\u200d' or (out[-1] and out[-1][-1] in '\u094d\u09cd' and unicodedata.category(ch) == 'Lo')):
            out[-1] += ch
        else:
            out.append(ch)
    return out


class Pack:
    def __init__(self, code):
        self.code = code
        self.cfg = LANGS[code]
        self.script = self.cfg.get('script', 'latin')
        self.glyphs = {}      # text -> code (from 0xE000)
        self.order = []

    def pua(self, t):
        if t not in self.glyphs:
            self.glyphs[t] = 0xE000 + len(self.order)
            self.order.append(t)
        return self.glyphs[t]

    def char(self, ch):
        if ch in GAME:
            return [GAME[ch]]
        n = unicodedata.normalize('NFC', ch)
        if n in GAME:
            return [GAME[n]]
        return [self.pua(n)]

    def encode(self, s):
        s = unicodedata.normalize('NFC', s)
        out = []
        sc = self.script
        if sc in ('cjk',):
            prev = None
            for ch in s:
                if ch == ' ':
                    out.append(0xA0); prev = ch; continue
                if ch == '\n':
                    out.append(0x0A); prev = None; continue
                if prev is not None and (is_cjk(ch) or is_cjk(prev)) and ch not in NO_BREAK_BEFORE and prev not in NO_BREAK_AFTER and prev != ' ':
                    out.append(0x20)   # a place to break, zero wide
                out += self.char(ch)
                prev = ch
            return out
        if sc == 'thai':
            for cl in clusters(s):
                if cl == '\u200b':
                    out.append(0x20)
                elif cl == ' ':
                    out.append(0xA0)
                elif cl in GAME:
                    out.append(GAME[cl])
                else:
                    out.append(self.pua(cl))
            return out
        if sc == 'indic':
            for cl in clusters(s):
                if cl in GAME:
                    out.append(GAME[cl])
                else:
                    out.append(self.pua(cl))
            return out
        if sc == 'arabic':
            # Arabic letters joined: each in its form for its place in the
            # word (the presentation forms), one picture per form -- the
            # game reverses each line for display (lang.c)
            for tok in re.findall(r'[\u0600-\u06FF\u0750-\u077F\uFB50-\uFDFF\uFE70-\uFEFF]+|.', s, re.S):
                if re.match(r'[\u0600-\u06FF]', tok):
                    for pf in arabic_shape(tok):
                        out.append(self.pua('\u0001' + pf))
                elif tok in GAME:
                    out.append(GAME[tok])
                elif tok == '\n':
                    out.append(0x0A)
                else:
                    out += self.char(tok)
            return out
        for ch in s:
            if ch == '\n':
                out.append(0x0A)
            elif ch == '\u200b':
                continue
            else:
                out += self.char(ch)
        return out


# ---- Arabic: the joined forms ----
def _ar_form(ch, form):
    try:
        return unicodedata.lookup(unicodedata.name(ch) + ' ' + form + ' FORM')
    except (KeyError, ValueError):
        return None


def _ar_join(ch):
    """D: joins both sides, R: joins the letter before only, U: none"""
    if _ar_form(ch, 'INITIAL') or _ar_form(ch, 'MEDIAL'):
        return 'D'
    if _ar_form(ch, 'FINAL'):
        return 'R'
    return 'U'


_LAM_ALEF = {'\u0627': ('\uFEFB', '\uFEFC'), '\u0623': ('\uFEF7', '\uFEF8'),
             '\u0625': ('\uFEF9', '\uFEFA'), '\u0622': ('\uFEF5', '\uFEF6')}


def arabic_shape(word):
    """a run of Arabic letters (logical order) in presentation forms; marks dropped"""
    ls = [c for c in word if unicodedata.category(c) != 'Mn' and c != '\u0640']
    out = []
    i = 0
    while i < len(ls):
        c = ls[i]
        prev_joins = i > 0 and _ar_join(ls[i - 1]) == 'D' and (out and not out[-1][1])
        if c == '\u0644' and i + 1 < len(ls) and ls[i + 1] in _LAM_ALEF:
            iso, fin = _LAM_ALEF[ls[i + 1]]
            out.append((fin if prev_joins else iso, True))   # (ends the joining)
            i += 2
            continue
        j = _ar_join(c)
        nxt = ls[i + 1] if i + 1 < len(ls) else None
        next_joins = j == 'D' and nxt is not None and _ar_join(nxt) in ('D', 'R')
        pj = prev_joins and j in ('D', 'R')
        form = 'MEDIAL' if pj and next_joins else 'FINAL' if pj else 'INITIAL' if next_joins else 'ISOLATED'
        f = _ar_form(c, form) or _ar_form(c, 'ISOLATED') or c
        out.append((f, not next_joins))
        i += 1
    return [f for f, _ in out]


# ---- a game letter with marks: the marks, as the reference font draws them ----
MARK_BASE = {'ł': 'l', 'Ł': 'L', 'đ': 'd', 'ħ': 'h', 'Ħ': 'H', 'ŧ': 't', 'Ŧ': 'T', 'ŀ': 'l', 'Ŀ': 'L', 'ŉ': 'n',
             'İ': 'I', 'ı': 'i', 'ȷ': 'j'}
ASC = set('bdfhklt')


def base_of(ch):
    if ch in MARK_BASE:
        return MARK_BASE[ch]
    d = unicodedata.normalize('NFD', ch)
    if len(d) > 1 and d[0] in GAME and d[0].isalpha() and all(unicodedata.category(c) == 'Mn' for c in d[1:]):
        return d[0]
    return None


def components(px, w, h, thr=70):
    seen = set(); comps = []
    for y in range(h):
        for x in range(w):
            if px[x, y] > thr and (x, y) not in seen:
                st = [(x, y)]; seen.add((x, y)); pts = []
                while st:
                    a, b = st.pop(); pts.append((a, b))
                    for dx in (-1, 0, 1):
                        for dy in (-1, 0, 1):
                            q = (a + dx, b + dy)
                            if 0 <= q[0] < w and 0 <= q[1] < h and q not in seen and px[q[0], q[1]] > thr:
                                seen.add(q); st.append(q)
                comps.append(pts)
    return comps


def marks_for(ch, base, slot, k=1):
    """[(place, dx, dy, w, h, bytes)] for the slot (k: at k times its size, for
    the Ultra textures' upscaled fonts)"""
    if ch == 'ı':
        return [(5, 0, slot['xt'] * k, 0, 0, b'')]
    target = (slot['bl'] - slot['cap'] + 1) if (base.isupper() or base in ASC) else (slot['bl'] - slot['xt'] + 1)
    s = size_for('lora', base, target) * k
    H = slot['lh'] * 3 * k; B = H * 2 // 3
    a, f = render('lora', s, ch, W=s * 3, H=H, base=B)
    b, _ = render('lora', s, base, W=s * 3, H=H, base=B)
    bi = ink(b)
    if not bi:
        return []
    pa, pb = a.load(), b.load()
    W = a.size[0]
    diff = Image.new('L', a.size, 0); pd = diff.load()
    for y in range(H):
        for x in range(W):
            v = pa[x, y] - pb[x, y]
            pd[x, y] = v if v > 0 else 0
    comps = [c for c in components(pd, W, H) if len(c) >= 2]
    bl, bt, br, bb = bi
    bcx = (bl + br + 1) / 2
    groups = {}
    for c in comps:
        xs = [p[0] for p in c]; ys = [p[1] for p in c]
        l, t, r, bo = min(xs), min(ys), max(xs), max(ys)
        if bo < bt + 1:
            kind = 0
        elif t > bb - 1:
            kind = 2 if (l + r) / 2 > bcx + (br - bl) / 4 else 1
        elif l > br - 1:
            kind = 4
        else:
            kind = 3
        groups.setdefault(kind, []).append((l, t, r, bo))
    out = []
    for kind, boxes in sorted(groups.items()):
        l = min(x[0] for x in boxes); t = min(x[1] for x in boxes); r = max(x[2] for x in boxes); bo = max(x[3] for x in boxes)
        mw, mh = r - l + 1, bo - t + 1
        bits = bytes(min(255, int(pd[x, y] * 1.15)) for y in range(t, bo + 1) for x in range(l, r + 1))
        mcx = (l + r + 1) / 2
        if kind == 0:
            dx, dy = round(mcx - bcx), -(bt - bo - 1)
        elif kind == 1:
            dx, dy = round(mcx - bcx), t - (bb + 1)
        elif kind == 2:
            dx, dy = r - br, t - (bb + 1)
        elif kind == 3:
            dx, dy = l - bl, t - bt
        else:
            dx, dy = l - (br + 1), t - bt
        lim = 127 if k == 1 else 32767
        out.append((kind, max(-lim - 1, min(lim, dx)), max(-lim - 1, min(lim, dy)), mw, mh, bits))
    return out


# ---- a drawn letter (word, syllable) for a slot ----
def script_font(pack, text):
    sc = pack.script
    if text.startswith('\u0001'):
        return 'freeserif'
    ch = text[0]
    if sc == 'cjk' and (is_cjk(ch) or ord(ch) > 0x2000):
        return 'cjk:' + pack.cfg['cjk']
    o = ord(ch)
    if 0x0590 <= o <= 0x05FF or 0x0600 <= o <= 0x06FF or 0x0900 <= o <= 0x097F or 0x0E00 <= o <= 0x0E7F:
        return 'freeserif'
    return 'lora'


def sample_for(fname, text):
    o = ord(text.lstrip('\u0001')[0])
    if fname.startswith('cjk:'):
        return '国', 1.18
    if 0x0590 <= o <= 0x05FF:
        return 'ה', 0.92
    if 0x0600 <= o <= 0x06FF or 0xFB50 <= o <= 0xFEFF:
        return 'ط', 1.0
    if 0x0900 <= o <= 0x097F:
        return 'क', 1.0
    if 0x0E00 <= o <= 0x0E7F:
        return 'ก', 0.0
    return 'H', 1.0


def drawn(pack, text, slot, scale=1):
    fname = script_font(pack, text)
    t = text.lstrip('\u0001')
    sample, k = sample_for(fname, t)
    cap = slot['bl'] - slot['cap'] + 1
    if sample == 'ก':
        target = round((slot['bl'] - slot['xt'] + 1) * 1.12)
    else:
        target = round(cap * k)
    s = size_for(fname, sample, target)
    lh = slot['lh']
    f = font(fname, s)
    adv = f.getlength(t)
    W = int(adv) + s * 2
    im = Image.new('L', (W, lh), 0)
    d = ImageDraw.Draw(im)
    base = slot['bl'] + 1
    if fname.startswith('cjk:'):
        base = slot['bl'] + 1 + max(0, round(s * 0.08))
    d.text((s, base), t, font=f, fill=255, anchor='ls')
    b = ink(im, 20)
    adv = max(1, round(adv))
    if fname.startswith('cjk:'):
        adv += 1
    l0 = s
    if b:
        l0 = min(l0, b[0])
        r = max(s + adv, b[2] + 1)
    else:
        r = s + adv
    cell = im.crop((l0, 0, r, lh))
    if scale > 1:   # the same, scale times the size (the Ultra textures' fonts): the cell that many times as wide
        q = scale
        f4 = font(fname, s * q)
        im4 = Image.new('L', (W * q, lh * q), 0)
        ImageDraw.Draw(im4).text((s * q, base * q), t, font=f4, fill=255, anchor='ls')
        return adv + (s - l0), im4.crop((l0 * q, 0, r * q, lh * q))
    return adv + (s - l0), cell


def build(code):
    pack = Pack(code)
    src = os.path.join(HERE, 'strings', code + '.tsv')
    rows = []
    for ln in open(src, encoding='utf-8'):
        ln = ln.rstrip('\n').rstrip('\r')
        if not ln or ln.startswith('#') or '\t' not in ln:
            continue
        k, v = ln.split('\t', 1)
        v = v.replace('\\n', '\n').replace('\\t', '\t').replace('\\\\', '\\')
        rows.append((k, v))
    od = os.path.join(OUT, code)
    os.makedirs(od, exist_ok=True)
    esc = lambda s: s.replace('\\', '\\\\').replace('\n', '\\n').replace('\t', '\\t').replace('\r', '\\r')
    with open(os.path.join(od, 'text.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# %s -- made by tools/lang/build_lang.py\n' % pack.cfg['name'])
        for k, v in rows:
            f.write('%s\t%s\n' % (k, esc(v)))
    with open(os.path.join(od, 'game.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# %s, in the game\'s characters -- made by tools/lang/build_lang.py\n' % pack.cfg['name'])
        for k, v in rows:
            if k.startswith('L:'):
                continue
            units = pack.encode(v)
            f.write('%s\t%s\n' % (k, esc(''.join(chr(u) for u in units))))
    with open(os.path.join(od, 'info.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('name=%s\nrtl=%d\n' % (pack.cfg['name'], 1 if pack.cfg.get('rtl') else 0))
    # the glyphs
    blob = bytearray(b'TTG1' + struct.pack('<I', len(SLOTS)))
    nd = nc = 0
    for slot in SLOTS:
        ents = []
        if pack.script in ('cjk', 'thai'):
            ents.append(struct.pack('<HBBH', 0x20, 2, 0, 0))
        for t in pack.order:
            code_ = pack.glyphs[t]
            base = base_of(t) if len(t) == 1 else None
            if base and pack.script not in ('arabic',):
                ms = marks_for(t, base, slot)
                if ms:
                    e = struct.pack('<HBBHB', code_, 1, 0, GAME[base], len(ms))
                    for (pl, dx, dy, mw, mh, bits) in ms:
                        e += struct.pack('<BbbBB', pl, dx, dy, mw, mh) + bits
                    ents.append(e); nc += 1
                    continue
            adv, cell = drawn(pack, t, slot)
            w = cell.size[0]
            ents.append(struct.pack('<HBBHH', code_, 0, 0, adv, w) + cell.tobytes())
            nd += 1
        blob += struct.pack('<II', slot['lh'], len(ents))
        for e in ents:
            blob += e
    open(os.path.join(od, 'glyphs.bin'), 'wb').write(blob)
    # the same letters K times the size, for the Ultra textures' upscaled
    # fonts (textures_ultra/ui): lang.c draws the font's picture again from them
    K = 4
    b4 = bytearray(struct.pack('<I', len(SLOTS)))
    for slot in SLOTS:
        ents = []
        for t in pack.order:
            code_ = pack.glyphs[t]
            base = base_of(t) if len(t) == 1 else None
            if base and pack.script not in ('arabic',):
                ms = marks_for(t, base, slot)
                if ms:
                    ms4 = marks_for(t, base, slot, K)
                    e = struct.pack('<HBB', code_, 1, len(ms4))
                    for (pl, dx, dy, mw, mh, bits) in ms4:
                        e += struct.pack('<BhhHH', pl, dx, dy, mw, mh) + bits
                    ents.append(e)
                    continue
            _, cell4 = drawn(pack, t, slot, K)
            ents.append(struct.pack('<HBBH', code_, 0, 0, cell4.size[0]) + cell4.tobytes())
        b4 += struct.pack('<II', slot['lh'] * K, len(ents))
        for e in ents:
            b4 += e
    z4 = zlib.compress(bytes(b4), 9)
    open(os.path.join(od, 'glyphs4.bin'), 'wb').write(b'TTG4' + struct.pack('<II', K, len(b4)) + z4)
    print('%s (%s): %d strings, %d new letters (%d drawn, %d made from the game\'s), glyphs.bin %d KB, glyphs4.bin %d KB'
          % (code, pack.cfg['name'], len(rows), len(pack.order), nd // len(SLOTS), nc // len(SLOTS), len(blob) // 1024, len(z4) // 1024))


if __name__ == '__main__':
    assert features.check('raqm'), 'Pillow needs libraqm (complex scripts)'
    codes = sys.argv[1:]
    if codes == ['all']:
        codes = [c for c in LANGS if c != 'en' and os.path.exists(os.path.join(HERE, 'strings', c + '.tsv'))]
    for c in codes:
        build(c)
