#!/usr/bin/env python3
"""make_index.py -- builds textures_ultra/index.bin for the Ultra textures option.

Run from the game's folder (needs Python 3 with Pillow and numpy):
    python3 textures_ultra/tools/make_index.py [GLOS69]

The pack's PNG pictures must first be made into BC7 DDS files in
textures_ultra/png (the png2dds tool beside this script does it). The game
pairs its own textures with the pack's pictures by what they look like: this
records a 16 x 16 average of every picture, and its shape.

  index.bin: "TTU1", u32 count, then per picture
    char path[120] (relative to the game's folder), u16 w, h (the DDS's size),
    u16 gw, gh (the GameCube texture's size), u8 sig[16][16][4] (R G B A)
"""
import os, re, struct, sys
import numpy as np
from PIL import Image

PACK = sys.argv[1] if len(sys.argv) > 1 else 'GLOS69'
# pictures never used: the green health pickup (the Orange health pickups mod recolours the game's own)
EXCLUDE = {'tex1_64x64_86315efe98503949_14.dds'}
S = 16

def box(a):
    H, W = a.shape[:2]
    out = np.zeros((S, S, 4))
    for y in range(S):
        y0 = y * H // S; y1 = max(y0 + 1, (y + 1) * H // S)
        for x in range(S):
            x0 = x * W // S; x1 = max(x0 + 1, (x + 1) * W // S)
            out[y, x] = a[y0:y1, x0:x1].reshape(-1, 4).mean(0)
    return out

def dds_sig(path):
    b = open(path, 'rb').read()
    h, w = struct.unpack_from('<II', b, 12)
    mc = max(1, struct.unpack_from('<I', b, 28)[0])
    if b[84:88] != b'DX10' or struct.unpack_from('<I', b, 128)[0] != 98:
        return None
    off = 148; W, H = w, h; lv = []
    for i in range(mc):
        sz = max(1, (W + 3) // 4) * max(1, (H + 3) // 4) * 16
        lv.append((W, H, off, sz)); off += sz
        W = max(1, W // 2); H = max(1, H // 2)
    W, H, o, sz = ([l for l in lv if l[0] >= 64 and l[1] >= 64] or [lv[0]])[-1]
    im = Image.frombytes('RGBA', (max(4, (W + 3) // 4 * 4), max(4, (H + 3) // 4 * 4)), b[o:o + sz], 'bcn', (7,)).crop((0, 0, W, H))
    return w, h, box(np.asarray(im, dtype=np.float64))

ents = []
for n in sorted(os.listdir(PACK)):
    m = re.match(r'tex1_(\d+)x(\d+)_(m_)?([0-9a-f]{16})_(\d+)(.*)\.(dds|png)$', n)
    if not m or m.group(6) or n in EXCLUDE:
        continue
    rel = PACK + '/' + n if n.endswith('.dds') else 'textures_ultra/png/' + n[:-4] + '.dds'
    if not os.path.exists(rel):
        print('missing (convert the PNG first):', rel); continue
    r = dds_sig(rel)
    if r is None or r[0] % 4 or r[1] % 4:
        print('skipped (not BC7, or not a multiple of 4):', rel); continue
    ents.append((rel, r[0], r[1], int(m.group(1)), int(m.group(2)), np.clip(np.rint(r[2]), 0, 255).astype(np.uint8)))

# the game's own textures, upscaled 4x (textures_ultra/ui/<hash>.dds): paired by hash only (pairs.txt),
# never by look (shape 1 x 0 matches nothing)
for UI in ('textures_ultra/ui', 'textures_ultra/alphafix'):
  if os.path.isdir(UI):
    for n in sorted(os.listdir(UI)):
        if not n.endswith('.dds'): continue
        b = open(UI + '/' + n, 'rb').read(148); h, w = struct.unpack_from('<II', b, 12)
        ents.append((UI + '/' + n, w, h, 1, 0, np.zeros((16, 16, 4), np.uint8)))
os.makedirs('textures_ultra', exist_ok=True)
with open('textures_ultra/index.bin', 'wb') as f:
    f.write(b'TTU1' + struct.pack('<I', len(ents)))
    for rel, w, h, gw, gh, s in ents:
        f.write(rel.encode()[:119].ljust(120, b'\0') + struct.pack('<HHHH', w, h, gw, gh) + s.tobytes())
print(len(ents), 'pictures in textures_ultra/index.bin')
