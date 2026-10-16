# Real-ESRGAN (RRDBNet) inference in numpy, weights read from the .pth without torch
import zipfile, pickle, numpy as np, sys, io
def load_pth(path):
    z = zipfile.ZipFile(path); names = z.namelist(); root = names[0].split('/')[0]
    class Storage:
        def __init__(s, key, dtype): s.key = key; s.dtype = dtype
    def rebuild(storage, offset, size, stride, *a):
        raw = np.frombuffer(z.read('%s/data/%s' % (root, storage.key)), dtype=storage.dtype)
        n = int(np.prod(size)) if size else 1
        arr = raw[offset:offset + (max((s - 1) * st for s, st in zip(size, stride)) + 1 if size else 1)]
        return np.lib.stride_tricks.as_strided(arr, shape=size, strides=[x * arr.itemsize for x in stride]).copy()
    class U(pickle.Unpickler):
        def find_class(s, mod, name):
            if name == '_rebuild_tensor_v2': return rebuild
            if name == 'FloatStorage': return np.float32
            if name == 'HalfStorage': return np.float16
            if mod == 'collections' and name == 'OrderedDict':
                import collections; return collections.OrderedDict
            if name == '_rebuild_parameter': return lambda d, *a: d
            return super().find_class(mod, name)
        def persistent_load(s, pid):
            typ, cls, key, loc, n = pid
            return Storage(key, cls)
    d = U(io.BytesIO(z.read('%s/data.pkl' % root))).load()
    return d.get('params_ema', d.get('params', d))
W = None
def conv(x, name, act=False):
    w = W[name + '.weight']; b = W[name + '.bias']
    co, ci, kh, kw = w.shape; H, Wd = x.shape[1:]
    xp = np.pad(x, ((0, 0), (1, 1), (1, 1)))
    cols = np.empty((ci * 9, H * Wd), np.float32); k = 0
    for c0 in range(ci):
        pass
    i = 0
    for dy in range(3):
        for dx in range(3):
            cols[i::9] = xp[:, dy:dy + H, dx:dx + Wd].reshape(ci, -1); i += 1
    wm = w.reshape(co, ci, 9).reshape(co, ci * 9)  # (ci,ky,kx) order: index c*9+ky*3+kx
    y = (wm @ cols).reshape(co, H, Wd) + b[:, None, None]
    if act: np.maximum(y, 0.2 * y, out=y)
    return y
def rdb(x, p):
    x1 = conv(x, p + '.conv1', True); x2 = conv(np.concatenate([x, x1]), p + '.conv2', True)
    x3 = conv(np.concatenate([x, x1, x2]), p + '.conv3', True)
    x4 = conv(np.concatenate([x, x1, x2, x3]), p + '.conv4', True)
    x5 = conv(np.concatenate([x, x1, x2, x3, x4]), p + '.conv5')
    return x5 * 0.2 + x
def up2(x): return x.repeat(2, 1).repeat(2, 2)
def run(img, nb):
    x = img.transpose(2, 0, 1).astype(np.float32) / 255.0
    f = conv(x, 'conv_first'); b = f
    for i in range(nb):
        o = rdb(rdb(rdb(b, 'body.%d.rdb1' % i), 'body.%d.rdb2' % i), 'body.%d.rdb3' % i); b = o * 0.2 + b
    f = f + conv(b, 'conv_body')
    f = conv(up2(f), 'conv_up1', True); f = conv(up2(f), 'conv_up2', True)
    o = conv(conv(f, 'conv_hr', True), 'conv_last')
    return (np.clip(o, 0, 1).transpose(1, 2, 0) * 255 + 0.5).astype(np.uint8)
def upscale(img, wpath='/tmp/claude-0/anime6b.pth', tile=96):
    """img HxWx3 uint8 -> 4H x 4W x 3, in tiles with a margin"""
    global W
    if W is None: W = {k: v.astype(np.float32) for k, v in load_pth(wpath).items()}
    nb = 1 + max(int(k.split('.')[1]) for k in W if k.startswith('body.'))
    H, Wd = img.shape[:2]; out = np.zeros((H * 4, Wd * 4, 3), np.uint8); m = 8
    for y0 in range(0, H, tile):
        for x0 in range(0, Wd, tile):
            ya, yb = max(0, y0 - m), min(H, y0 + tile + m); xa, xb = max(0, x0 - m), min(Wd, x0 + tile + m)
            t = run(img[ya:yb, xa:xb], nb)
            y1, x1 = min(H, y0 + tile), min(Wd, x0 + tile)
            out[y0 * 4:y1 * 4, x0 * 4:x1 * 4] = t[(y0 - ya) * 4:(y0 - ya + y1 - y0) * 4, (x0 - xa) * 4:(x0 - xa + x1 - x0) * 4]
    return out
if __name__ == '__main__':
    import time
    from PIL import Image
    im = np.asarray(Image.open(sys.argv[1]).convert('RGB'))
    t = time.time(); o = upscale(im); print(im.shape, '->', o.shape, '%.1fs' % (time.time() - t))
    Image.fromarray(o).save(sys.argv[2])
