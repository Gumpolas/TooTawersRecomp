"""Build PlayStation versions of the loading-screen control diagrams.

For each of loadl2 / loadl3 / loadl4 (and their "pressed" overlays) and each
PlayStation model, the game's own picture provides the gold leader lines and
rings (lifted off it by colour); the pad under them is drawn by padart.py with
its face buttons on the same texels as the Xbox pad's, so every line and
overlay still lands on the right button. Where the PlayStation pad has a
control somewhere else (left stick) the line is re-routed; where it has no
counterpart in place (Start, the black button) the callout becomes an inset
showing the PlayStation control (OPTIONS / START, L1), the way the triggers
were already shown.
"""
import sys, os, struct, re
# usage: python3 compose.py <game UI folder> <output folder>  (writes prompts\*.bmp)
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy import ndimage as nd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dxt import decode
import padart
from padart import Canvas, SS, render, sym_mask, SYM_COL, SYM_COL5, FONT

UI = sys.argv[1] if len(sys.argv) > 1 else 'game_files/UI'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'prompts'
os.makedirs(OUT, exist_ok=True)
LINE = (240, 210, 58)


def load_bff(name):
    d = open(os.path.join(UI, name + '.BFF'), 'rb').read()
    res = []
    for m in re.finditer(b'xb\x05\x00', d):
        o = m.start()
        w, h, _ = struct.unpack_from('<III', d, o + 4)
        fmt = (struct.unpack_from('<I', d, o + 0x38)[0] >> 8) & 0xff
        res.append(np.asarray(Image.fromarray(decode(d[o + 0x80:], w, h, fmt), 'RGBA')).copy())
    return res


def line_layer(img, erase_discs=(), erase_rects=()):
    """the gold lines/rings of a game diagram, as RGBA"""
    a = img.astype(np.int32)
    r, g, b, al = a[..., 0], a[..., 1], a[..., 2], a[..., 3]
    hard = (r > 170) & (g > 120) & (b < 110) & (r - b > 100) & (al > 150)
    lab, n = nd.label(hard, np.ones((3, 3)))
    sizes = nd.sum(hard, lab, range(1, n + 1))
    keep = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s >= 15])
    yy, xx = np.mgrid[:img.shape[0], :img.shape[1]]
    for (cx, cy, rr) in erase_discs:
        keep &= (xx - cx) ** 2 + (yy - cy) ** 2 > rr * rr
    for (x0, y0, x1, y1) in erase_rects:
        keep &= ~((xx >= x0) & (xx <= x1) & (yy >= y0) & (yy <= y1))
    lab, n = nd.label(keep, np.ones((3, 3)))
    if n:
        sizes = nd.sum(keep, lab, range(1, n + 1))
        keep = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s >= 12])
    region = nd.binary_dilation(keep, iterations=1)
    for (cx, cy, rr) in erase_discs:
        region &= (xx - cx) ** 2 + (yy - cy) ** 2 > rr * rr
    for (x0, y0, x1, y1) in erase_rects:
        region &= ~((xx >= x0) & (xx <= x1) & (yy >= y0) & (yy <= y1))
    soft = np.clip((r - b - 40) / 110.0, 0, 1) * np.clip((g - 60) / 90.0, 0, 1) * (al / 255.0)
    alpha = np.where(keep, 1.0, np.where(region, soft, 0.0))
    out = np.zeros_like(img)
    out[..., :3] = np.where(alpha[..., None] > 0, img[..., :3], 0)
    # line pixels keep the game's colour; antialiased edges take the line colour
    out[..., :3] = np.where((alpha > 0)[..., None] & ~keep[..., None], np.array(LINE), out[..., :3])
    out[..., 3] = (alpha * 255).astype(np.uint8)
    return out


def draw_lines(size, polylines=(), rings=(), arrows=(), width=2.3):
    """new leader lines in the game's style, RGBA"""
    w, h = size
    im = Image.new('L', (w * SS, h * SS), 0)
    d = ImageDraw.Draw(im)
    for pl in polylines:
        d.line([(x * SS, y * SS) for (x, y) in pl], fill=255, width=int(width * SS), joint='curve')
    for (cx, cy, rr) in rings:
        d.ellipse([(cx - rr) * SS, (cy - rr) * SS, (cx + rr) * SS, (cy + rr) * SS], outline=255,
                  width=int(width * 1.15 * SS))
    for (x, y) in arrows:            # arrowhead pointing right, tip at (x, y)
        d.polygon([(x * SS, y * SS), ((x - 12) * SS, (y - 4.5) * SS), ((x - 12) * SS, (y + 4.5) * SS)], fill=255)
    a = np.asarray(im.resize((w, h), Image.LANCZOS), np.float32) / 255.0
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = LINE
    out[..., 3] = (a * 255).astype(np.uint8)
    return out


def over(dst, src):
    """src over dst, straight alpha float arrays 0..1"""
    sa, da = src[..., 3:4], dst[..., 3:4]
    oa = sa + da * (1 - sa)
    rgb = (src[..., :3] * sa + dst[..., :3] * da * (1 - sa)) / np.maximum(oa, 1e-6)
    return np.dstack([rgb, oa])


def pfx(model):
    return 'x1' if model == 1 else 'ps%d' % model


def f32(a):
    return np.asarray(a, np.float32) / 255.0


def u8(a):
    return (np.clip(a, 0, 1) * 255 + 0.5).astype(np.uint8)


# ---------------------------------------------------------------- insets

def inset_disc(cv, cx, cy, r_px):
    """a dark window like the game's trigger insets (texel coordinates)"""
    s = cv.s / SS
    x, y = (cx - cv.ox / SS) / s, (cy - cv.oy / SS) / s
    cv.paint(cv.mask_ellipse(x, y, r_px / s), (30, 26, 18), 1.0)
    return x, y, s


def inset_trigger(cv, cx, cy, r_px, label, pressed=False, right=False):
    x, y, s = inset_disc(cv, cx, cy, r_px)
    R = r_px / s
    # the trigger seen from above: a broad rounded key, curving toward the player
    k = -1 if right else 1
    pts = padart.catmull([(x - 0.55 * R, y - 0.55 * R), (x + 0.45 * R, y - 0.62 * R), (x + 0.6 * R, y - 0.1 * R),
                          (x + 0.45 * R, y + 0.55 * R), (x - 0.5 * R, y + 0.62 * R), (x - 0.62 * R, y)])
    if right:
        pts = [(2 * x - px, py) for (px, py) in pts]
    m = cv.mask_poly(pts)
    col = (150, 40, 32) if pressed else (70, 72, 78)
    col2 = (96, 22, 18) if pressed else (40, 40, 46)
    cv.shaded(m, col, col2, bevel_mm=0.25 * R, gloss=0.8, dome=True, spec=0.5)
    cv.paint(cv.mask_text(label, x, y + 0.02 * R, 0.62 * R), (235, 235, 240) if not pressed else (255, 220, 210), 0.95)


def inset_l1(cv, cx, cy, r_px, model):
    x, y, s = inset_disc(cv, cx, cy, r_px)
    R = r_px / s
    m = cv.mask_rrect(x - 0.72 * R, y - 0.38 * R, x + 0.72 * R, y + 0.38 * R, 0.3 * R)
    cv.shaded(m, (74, 76, 82), (44, 44, 50), bevel_mm=0.15 * R, gloss=0.8, dome=True, spec=0.5)
    cv.paint(cv.mask_text('L1', x, y + 0.02 * R, 0.62 * R), (235, 235, 240), 0.95)


def inset_options(cv, cx, cy, r_px, model):
    x, y, s = inset_disc(cv, cx, cy, r_px)
    R = r_px / s
    if model == 3:
        # START: the DualShock 3's arrow-shaped key
        m = cv.mask_poly([(x - 0.45 * R, y - 0.55 * R), (x + 0.65 * R, y), (x - 0.45 * R, y + 0.55 * R)])
        cv.shaded(m, (190, 190, 198), (140, 140, 150), bevel_mm=0.08 * R, gloss=0.8, spec=0.4)
    else:
        # OPTIONS: a small upright pill with three bars, as on the pad
        m = cv.mask_rrect(x - 0.42 * R, y - 0.72 * R, x + 0.42 * R, y + 0.72 * R, 0.4 * R)
        cv.shaded(m, (196, 196, 204), (150, 150, 160), bevel_mm=0.08 * R, gloss=0.8, spec=0.4)
        for i in (-1, 0, 1):
            cv.paint(cv.mask_rrect(x - 0.24 * R, y + i * 0.26 * R - 0.06 * R, x + 0.24 * R,
                                   y + i * 0.26 * R + 0.06 * R, 0.05 * R), (30, 30, 34), 0.95)


def inset_lb(cv, cx, cy, r_px):
    x, y, s = inset_disc(cv, cx, cy, r_px)
    R = r_px / s
    m = cv.mask_rrect(x - 0.78 * R, y - 0.38 * R, x + 0.78 * R, y + 0.38 * R, 0.3 * R)
    cv.shaded(m, (70, 70, 76), (40, 40, 46), bevel_mm=0.15 * R, gloss=0.8, dome=True, spec=0.5)
    cv.paint(cv.mask_text('LB', x, y + 0.02 * R, 0.58 * R), (235, 235, 240), 0.95)


def inset_menu(cv, cx, cy, r_px):
    x, y, s = inset_disc(cv, cx, cy, r_px)
    R = r_px / s
    cv.shaded(cv.mask_ellipse(x, y, 0.62 * R), (60, 60, 66), (34, 34, 40), bevel_mm=0.1 * R, gloss=0.8, dome=True, spec=0.5)
    cv.paint(padart.xb1_icon(cv, 'menu', x, y, 0.42 * R), (225, 225, 232), 0.95)


# ---------------------------------------------------------------- the diagrams

def build(name, model):
    imgs = load_bff(name)
    base = imgs[0]
    h, w = base.shape[:2]
    if name == 'loadl3':
        face, spacing = (293, 130), 23.0
    else:
        face, spacing = (293, 80.5), 22.5
    cv, scale, origin = render(model, w, h, face, spacing)
    M = padart.MODELS[model]

    def T(xmm, ymm):
        return (origin[0] + xmm * scale, origin[1] + ymm * scale)

    centres = [(face[0], face[1] - spacing), (face[0] + spacing, face[1]),
               (face[0], face[1] + spacing), (face[0] - spacing, face[1])]
    new_lines = dict(polylines=[], rings=[], arrows=[])
    # The game's rings round the face buttons are partly hidden by its
    # buttons' rims; lift none of them and draw whole ones round those it had.
    a = base.astype(np.int32)
    hard = (a[..., 0] > 170) & (a[..., 1] > 120) & (a[..., 2] < 110) & (a[..., 0] - a[..., 2] > 100) & (a[..., 3] > 150)
    yy, xx = np.mgrid[:h, :w]
    face_discs = []
    for (cx, cy) in centres:
        d2 = (xx - cx) ** 2 + (yy - cy) ** 2
        if (hard & (d2 >= 11.5 ** 2) & (d2 <= 15 ** 2)).sum() > 25:
            new_lines['rings'].append((cx, cy, 13.0))
            face_discs.append((cx, cy, 18.8))
            # the leader lines leaving this ring: carry each back in to the new ring
            ann = hard & (d2 >= 19.5 ** 2) & (d2 <= 27 ** 2)
            lab, n = nd.label(ann, np.ones((3, 3)))
            for k in range(1, n + 1):
                ys, xs = np.nonzero(lab == k)
                if len(ys) < 6:
                    continue
                pts = np.stack([xs, ys], 1).astype(float)
                c = pts.mean(0)
                u, sv, vt = np.linalg.svd(pts - c)
                dvec = vt[0]
                if sv[0] < 2 * (sv[1] + 1e-6):
                    continue                     # not a straight run (another ring)
                # nearest end to the button, then walk inwards along the line
                proj = (pts - c) @ dvec
                e1, e2 = c + dvec * proj.min(), c + dvec * proj.max()
                near = e1 if np.hypot(*(e1 - (cx, cy))) < np.hypot(*(e2 - (cx, cy))) else e2
                step = -dvec if np.dot(near - c, dvec) < 0 else dvec
                q = near.copy()
                for _ in range(40):
                    if np.hypot(q[0] - cx, q[1] - cy) <= 13.0:
                        break
                    q = q + step * 0.5
                others = [(ox, oy) for (ox, oy) in centres if (ox, oy) != (cx, cy)]
                if any(np.hypot(c[0] - ox, c[1] - oy) < 21 for (ox, oy) in others):
                    continue                     # a neighbour's ring, not a line
                if np.hypot(q[0] - cx, q[1] - cy) <= 13.5:
                    new_lines['polylines'].append([tuple(near), tuple(q)])
        else:
            face_discs.append((cx, cy, 10.5))
    erase_d, erase_r = list(face_discs), []
    # stray gold specks on the Xbox pad's body (not part of any line)
    erase_r += [(138, 228, 150, 240)] if name == 'loadl3' else [(138, 176, 158, 190)]
    if name == 'loadl3' and model == 1:
        # Xbox One: the left stick and Start sit where the old pad's are, so
        # their lines stay; the triggers, the Black button and Start become
        # insets showing LT / RT, LB and the Menu button.
        for (cx, cy, rr) in ((90, 53.5, 22.7), (329, 53.5, 22.8), (319.5, 172, 14.2), (115, 181, 14.1)):
            erase_d.append((cx, cy, rr + 3.2))
            new_lines['rings'].append((cx, cy, rr))
        def bridge1(cx, cy, rr, tx, ty, extra=4.5):
            v = np.array([tx - cx, ty - cy], float)
            v /= np.linalg.norm(v)
            new_lines['polylines'].append([(cx + v[0] * (rr - 0.5), cy + v[1] * (rr - 0.5)),
                                           (cx + v[0] * (rr + extra), cy + v[1] * (rr + extra))])
        bridge1(90, 53.5, 22.7, 130, 55.5)
        bridge1(90, 53.5, 22.7, 118, 80)
        bridge1(329, 53.5, 22.8, 370, 55.5)
        bridge1(329, 53.5, 22.8, 290, 75)
        new_lines['polylines'].append([(325.5, 184.5), (334, 184.5)])
        bridge1(115, 181, 14.1, 115, 240, 6)
        erase_r.append((284, 186, 292, 206))
        inset_trigger(cv, 90, 53.5, 20.2, 'LT')
        inset_trigger(cv, 329, 53.5, 20.3, 'RT', right=True)
        inset_lb(cv, 319.5, 172, 11.8)
        inset_menu(cv, 115, 181, 11.7)
        # left stick: a whole ring round the new stick, its line left to the old vertical
        erase_d.append((127.5, 130.5, 26))
        erase_r.append((80, 120, 101, 166))
        sx, sy = T(*M['sticks'][0])
        rr = 12.9 * scale
        new_lines['rings'].append((sx, sy, rr))
        new_lines['polylines'].append([(sx - rr, sy), (85.5, sy), (85.5, 176)])
    elif name == 'loadl3':
        # Insets (rings measured off the game's picture): whole rings drawn
        # anew, the game's (partly hidden by its art) not lifted.
        for (cx, cy, rr) in ((90, 53.5, 22.7), (329, 53.5, 22.8), (319.5, 172, 14.2)):
            erase_d.append((cx, cy, rr + 3.2))
            new_lines['rings'].append((cx, cy, rr))
        # rejoin the lines the wider erase cut back from the rings
        def bridge(cx, cy, rr, tx, ty, extra=4.5):
            v = np.array([tx - cx, ty - cy], float)
            v /= np.linalg.norm(v)
            new_lines['polylines'].append([(cx + v[0] * (rr - 0.5), cy + v[1] * (rr - 0.5)),
                                           (cx + v[0] * (rr + extra), cy + v[1] * (rr + extra))])
        bridge(90, 53.5, 22.7, 130, 55.5)
        bridge(90, 53.5, 22.7, 118, 80)
        bridge(329, 53.5, 22.8, 370, 55.5)
        bridge(329, 53.5, 22.8, 290, 75)
        new_lines['polylines'].append([(325.5, 184.5), (334, 184.5)])
        erase_r.append((284, 186, 292, 206))      # bits of the Xbox A button's rim
        inset_trigger(cv, 90, 53.5, 20.2, 'L2')
        inset_trigger(cv, 329, 53.5, 20.3, 'R2', right=True)
        inset_l1(cv, 319.5, 172, 11.8, model)
        # Start -> OPTIONS/START inset, moved down so the left stick's line can pass
        erase_d.append((115, 181, 17.5))
        erase_r.append((98, 160, 132, 229))
        inset_options(cv, 115, 214, 11.7, model)
        new_lines['rings'].append((115, 214, 14.1))
        new_lines['polylines'].append([(114.5, 228), (114.5, 237)])
        # left stick: ring the PlayStation stick, line left to the old vertical
        erase_d.append((127.5, 130.5, 26))
        erase_r.append((80, 120, 112, 175))
        sx, sy = T(*M['sticks'][0])
        rr = 12.9 * scale
        new_lines['rings'].append((sx, sy, rr))
        new_lines['polylines'].append([(sx - rr, sy), (85.5, sy), (85.5, 176)])
    lines = line_layer(base, erase_d, erase_r)
    extra = draw_lines((w, h), **new_lines)
    pad = f32(np.asarray(cv.image()))
    gold = over(f32(lines), f32(extra))
    # a thin dark edge under the lines, so they read on a white DualSense too
    sh = np.zeros_like(gold)
    sh[..., 3] = np.clip(nd.grey_dilation(gold[..., 3], size=(3, 3)) * 0.6, 0, 1)
    outimg = over(over(pad, sh), gold)
    return u8(outimg), imgs


def lit_button(model, kind, size=32):
    cv = Canvas(size, size, (size / 2, size / 2), size / 2 / 6.2)
    if model == 1:   # Xbox One: the lit button is the black one, its letter bright on a tint of its colour
        k = {'square': 'X', 'cross': 'A', 'triangle': 'Y'}[kind]
        cv.shaded(cv.mask_ellipse(0, 0, 5.9), (40, 40, 44), (18, 18, 20), bevel_mm=1.0, gloss=0.9, dome=True, spec=0.7)
        c = padart.XB1_COL[k]
        cv.paint(cv.mask_ellipse(0, 0, 5.6), c, 0.42)
        cv.paint(cv.mask_text(k, 0, 0.1, 6.8), tuple(min(255, int(v * 1.2 + 40)) for v in c), 1.0)
        return np.asarray(cv.image())
    cv.shaded(cv.mask_ellipse(0, 0, 5.9), (70, 70, 78), (36, 36, 42), bevel_mm=1.0, gloss=0.9, dome=True, spec=0.7)
    cols = SYM_COL5 if model == 5 else SYM_COL
    c = cols[kind]
    cv.paint(cv.mask_ellipse(0, 0, 5.6), c, 0.45)
    hi = tuple(min(255, int(v * 1.2 + 40)) for v in c)
    cv.paint(sym_mask(cv, kind, 0, 0, 5.9, 1.3), hi, 1.0)
    return np.asarray(cv.image())


def trigger_pressed(ref, label, size=64):
    """the 64x64 pressed-trigger overlay: the game's ring, a red PS trigger"""
    rl = line_layer(ref)
    a = ref.astype(np.int32)
    # ring radius from the game's overlay
    ys, xs = np.nonzero(rl[..., 3] > 128)
    cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
    r = (xs.max() - xs.min()) / 2
    cv = Canvas(size, size, (0, 0), 1.0)
    inset_trigger(cv, cx, cy, r - 2.5, label, pressed=True)
    return u8(over(f32(np.asarray(cv.image())), f32(rl)))


if __name__ == '__main__':
    models = [int(a) for a in sys.argv[3:]] or [3, 4, 5, 1]
    for model in models:
        for name in ('loadl2', 'loadl3', 'loadl4'):
            img, imgs = build(name, model)
            Image.fromarray(img, 'RGBA').save('%s/%s_%s.png' % (OUT, pfx(model), name))
        # overlays
        Image.fromarray(lit_button(model, 'square'), 'RGBA').save('%s/%s_lit_square.png' % (OUT, pfx(model)))
        Image.fromarray(lit_button(model, 'cross'), 'RGBA').save('%s/%s_lit_cross.png' % (OUT, pfx(model)))
        Image.fromarray(lit_button(model, 'triangle'), 'RGBA').save('%s/%s_lit_triangle.png' % (OUT, pfx(model)))
        print('model', model, 'done')
    # BMPs for the game folder (32-bit, alpha), prompts\
    import glob
    os.makedirs(OUT + '/bmp', exist_ok=True)
    for f in glob.glob(OUT + '/ps*.png') + glob.glob(OUT + '/x1_*.png'):
        im = Image.open(f).convert('RGBA')
        a = np.asarray(im)
        h, w = a.shape[:2]
        bgra = a[..., [2, 1, 0, 3]][::-1].tobytes()
        hd = bytearray(54)
        hd[0:2] = b'BM'
        struct.pack_into('<IHHI', hd, 2, 54 + len(bgra), 0, 0, 54)
        struct.pack_into('<IiiHHIIiiII', hd, 14, 40, w, h, 1, 32, 0, len(bgra), 2835, 2835, 0, 0)
        open(OUT + '/bmp/' + os.path.basename(f)[:-4] + '.bmp', 'wb').write(bytes(hd) + bgra)
    l3 = load_bff('loadl3')
    Image.fromarray(trigger_pressed(l3[1], 'L2'), 'RGBA').save('%s/ps_l2_pressed.png' % OUT)
    Image.fromarray(trigger_pressed(l3[1], 'LT'), 'RGBA').save('%s/x1_l2_pressed.png' % OUT)
    for n in ('ps_l2_pressed', 'x1_l2_pressed'):   # (written after the loop above: BMPs of these two here)
        a = np.asarray(Image.open('%s/%s.png' % (OUT, n)).convert('RGBA'))
        h, w = a.shape[:2]
        bgra = a[..., [2, 1, 0, 3]][::-1].tobytes()
        hd = bytearray(54)
        hd[0:2] = b'BM'
        struct.pack_into('<IHHI', hd, 2, 54 + len(bgra), 0, 0, 54)
        struct.pack_into('<IiiHHIIiiII', hd, 14, 40, w, h, 1, 32, 0, len(bgra), 2835, 2835, 0, 0)
        open(OUT + '/bmp/' + n + '.bmp', 'wb').write(bytes(hd) + bgra)
