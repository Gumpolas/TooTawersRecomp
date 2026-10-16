# Ultra textures: a pack picture that lost the game texture's transparency
# (GameCube pictures with black where the Xbox's has none) gets the game
# texture's alpha, upscaled to its size -> textures_ultra/alphafix/<HASH>.png
import json, struct, os, sys
import numpy as np
from PIL import Image
lst = json.load(open(sys.argv[1]))
os.makedirs('textures_ultra/alphafix', exist_ok=True)
for f, src, r in lst:
    o = 'textures_ultra/alphafix/%s.png' % f[:8]
    if os.path.exists(o): continue
    b = open(r, 'rb').read(); h, w = struct.unpack_from('<II', b, 12)
    pk = Image.frombytes('RGBA', (w, h), b[148:148 + ((w + 3) // 4) * ((h + 3) // 4) * 16], 'bcn', (7,))
    g = open(src, 'rb').read(); gw, gh = struct.unpack_from('<ii', g, 18); gh = abs(gh)
    ga = np.frombuffer(g[54:54 + gw * gh * 4], np.uint8).reshape(gh, gw, 4)[:, :, 3]
    al = Image.fromarray(ga.copy(), 'L').resize((w, h), Image.BICUBIC)
    pa = np.asarray(pk).copy(); pa[:, :, 3] = np.minimum(pa[:, :, 3], np.asarray(al))
    Image.fromarray(pa, 'RGBA').save(o)
    print(o, flush=True)
