# Ultra textures: cut-out pack pictures (grass, leaves, fences) remade with
# their colour carried under the transparent texels, so filtering and the
# smaller levels show no dark rims -> textures_ultra/bleed/<name>.png
import os, struct, sys, numpy as np
from PIL import Image

os.makedirs('textures_ultra/bleed', exist_ok=True)
for r in [l.strip() for l in open(sys.argv[1]) if l.strip()]:
    n = os.path.basename(r)[:-4]; o = 'textures_ultra/bleed/%s.png' % n
    if os.path.exists(o) or os.path.exists(o[:-4] + '.dds'): continue
    b = open(r, 'rb').read(); h, w = struct.unpack_from('<II', b, 12)
    a = np.asarray(Image.frombytes('RGBA', (w, h), b[148:148 + ((w + 3) // 4) * ((h + 3) // 4) * 16], 'bcn', (7,))).copy()
    solid = a[:, :, 3] >= 128
    if solid.any() and not solid.all():
        idx = ndimage.distance_transform_edt(~solid, return_distances=False, return_indices=True)
        a[:, :, :3] = a[idx[0], idx[1], :3]
    Image.fromarray(a, 'RGBA').save(o); print(o, flush=True)
