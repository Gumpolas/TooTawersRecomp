# realesr-general-x4v3 (SRVGGNetCompact) in numpy
import numpy as np
from esr import load_pth
import esr
Wg = None
def conv(x, w, b):
    co, ci = w.shape[:2]; H, Wd = x.shape[1:]
    xp = np.pad(x, ((0, 0), (1, 1), (1, 1))); cols = np.empty((ci * 9, H * Wd), np.float32); i = 0
    for dy in range(3):
        for dx in range(3):
            cols[i::9] = xp[:, dy:dy + H, dx:dx + Wd].reshape(ci, -1); i += 1
    return (w.reshape(co, ci * 9) @ cols).reshape(co, H, Wd) + b[:, None, None]
def run(img):
    x = img.transpose(2, 0, 1).astype(np.float32) / 255.0; f = x; k = 0
    while 'body.%d.weight' % k in Wg:
        w = Wg['body.%d.weight' % k]
        if w.ndim == 4:
            f = conv(f, w, Wg['body.%d.bias' % k])
        else:
            f = np.maximum(f, 0) + w[:, None, None] * np.minimum(f, 0)
        k += 1
    C = 3; H, Wd = img.shape[:2]
    o = f.reshape(C, 4, 4, H, Wd).transpose(0, 3, 1, 4, 2).reshape(C, H * 4, Wd * 4)
    o = o + x.repeat(4, 1).repeat(4, 2)
    return (np.clip(o, 0, 1).transpose(1, 2, 0) * 255 + 0.5).astype(np.uint8)
def upscale(img, tile=128):
    global Wg
    if Wg is None: Wg = {k: v.astype(np.float32) for k, v in load_pth('/tmp/claude-0/gx4v3.pth').items()}
    H, Wd = img.shape[:2]; out = np.zeros((H * 4, Wd * 4, 3), np.uint8); m = 8
    for y0 in range(0, H, tile):
        for x0 in range(0, Wd, tile):
            ya, yb = max(0, y0 - m), min(H, y0 + tile + m); xa, xb = max(0, x0 - m), min(Wd, x0 + tile + m)
            t = run(img[ya:yb, xa:xb]); y1, x1 = min(H, y0 + tile), min(Wd, x0 + tile)
            out[y0 * 4:y1 * 4, x0 * 4:x1 * 4] = t[(y0 - ya) * 4:(y0 - ya + y1 - y0) * 4, (x0 - xa) * 4:(x0 - xa + x1 - x0) * 4]
    return out
