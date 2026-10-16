"""PlayStation controller drawings for the loading-screen control diagrams.

Everything is drawn here from geometry: no picture of a real pad is used.
Coordinates are millimetres on the pad, origin at its centre, y down, as the
player looks at it from above. render() places the pad so that the centre of
the face-button diamond lands on a given texel with a given button spacing,
which is what lets the game's own leader lines and "pressed" overlays (drawn
at the Xbox pad's button positions) still point at the right buttons.
"""
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from scipy import ndimage as nd

SS = 4                      # supersampling
FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'

# ---------------------------------------------------------------- geometry

def catmull(pts, n=24, closed=True):
    P = list(pts)
    if closed:
        P = [P[-1]] + P + [P[0], P[1]]
    out = []
    for i in range(1, len(P) - 2):
        p0, p1, p2, p3 = map(np.array, P[i - 1:i + 3])
        for t in np.linspace(0, 1, n, endpoint=False):
            t2, t3 = t * t, t * t * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                              + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    return out


def mirror(half):
    """half: outline points for the right half, top centre to bottom centre"""
    right = list(half)
    left = [(-x, y) for (x, y) in reversed(half[1:-1])]
    return right + left


MODELS = {
    # DualShock 3: glossy black, rounder, SELECT/START, LEDs on top
    3: dict(
        outline=mirror([(0, -33), (30, -34), (52, -36), (66, -33), (76, -24), (80, -10),
                        (79, 6), (76, 24), (71, 40), (64, 50), (54, 54), (45, 49), (38, 36),
                        (30, 25), (18, 21), (0, 20)]),
        face=(44, -10), dpad=(-44, -10), sticks=((-20, 14), (20, 14)), stick_r=8.5,
        body=(52, 52, 58), body2=(26, 26, 30), gloss=1.0, two_tone=False,
        center='ds3'),
    # DualShock 4: matte black, touchpad, light bar, SHARE/OPTIONS
    4: dict(
        outline=mirror([(0, -37), (28, -38), (50, -38), (66, -35), (76, -26), (80, -12),
                        (79, 4), (75, 22), (70, 38), (63, 50), (53, 55), (44, 50), (37, 37),
                        (30, 26), (18, 22), (0, 21)]),
        face=(46, -12), dpad=(-46, -12), sticks=((-22, 14), (22, 14)), stick_r=8.5,
        body=(62, 63, 68), body2=(34, 34, 38), gloss=0.55, two_tone=False,
        center='ds4'),
    # DualSense: white wings, black centre plate, big touchpad, CREATE/OPTIONS
    5: dict(
        outline=mirror([(0, -40), (30, -41), (52, -41), (68, -37), (79, -27), (83, -12),
                        (82, 4), (78, 22), (73, 40), (66, 54), (56, 60), (46, 55), (39, 41),
                        (31, 29), (18, 25), (0, 24)]),
        face=(47, -13), dpad=(-47, -13), sticks=((-22, 15), (22, 15)), stick_r=8.5,
        body=(228, 228, 232), body2=(178, 178, 186), gloss=0.6, two_tone=True,
        center='ds5'),
    # Xbox One / Series: matte black, offset sticks (left stick high, d-pad low),
    # the face buttons black with their letters in colour, View / Menu, a plain
    # round guide button (no logo). Its face diamond and left stick sit where
    # the original Xbox pad's are in the game's pictures, so the leader lines fit.
    1: dict(
        outline=mirror([(0, -26), (20, -27), (38, -31), (54, -31), (65, -26), (72, -15),
                        (76, 0), (78, 16), (77, 36), (72, 54), (64, 67), (53, 71), (44, 63),
                        (37, 47), (27, 36), (13, 32), (0, 32)]),
        face=(43, -8), dpad=(-21, 16), sticks=((-43, -8), (21, 16)), stick_r=8.5,
        body=(58, 59, 64), body2=(28, 28, 32), gloss=0.45, two_tone=False,
        center='xb1'),
}

XB1_COL = {'A': (96, 196, 52), 'B': (232, 66, 60), 'X': (52, 140, 236), 'Y': (242, 194, 28)}
XB1_FACE = [('Y', 0, -1), ('B', 1, 0), ('A', 0, 1), ('X', -1, 0)]


def draw_xb1(cv, lit=()):
    M = MODELS[1]
    out = catmull(M['outline'])
    body = cv.mask_poly(out)
    cv.glow(body)
    cv.shaded(body, M['body'], M['body2'], bevel_mm=4.5, gloss=M['gloss'], spec=0.45)
    fx, fy = M['face']
    dx, dy = M['dpad']
    sp = 12.0
    # face buttons: black domes, coloured letters
    for (k, ux, uy) in XB1_FACE:
        cx, cy = fx + ux * sp, fy + uy * sp
        cv.shaded(cv.mask_ellipse(cx, cy, 5.5), (34, 34, 38), (16, 16, 18), bevel_mm=1.0, gloss=0.9, dome=True, spec=0.7)
        c = XB1_COL[k]
        if k in lit:
            cv.paint(cv.mask_ellipse(cx, cy, 5.3), c, 0.4)
            c = tuple(min(255, int(v * 1.2 + 40)) for v in c)
        cv.paint(cv.mask_text(k, cx, cy + 0.1, 6.4), c, 1.0)
    # d-pad: a cross in a shallow well
    cv.shaded(cv.mask_ellipse(dx, dy, 11.5), (20, 20, 22), None, bevel_mm=1.2, gloss=0.2, light=(0.45, 0.75, 0.5), spec=0.1)
    arm, hw = 9.5, 3.6
    cross = np.clip(cv.mask_rrect(dx - arm, dy - hw, dx + arm, dy + hw, 1.2) + cv.mask_rrect(dx - hw, dy - arm, dx + hw, dy + arm, 1.2), 0, 1)
    cv.shaded(cross, (52, 52, 58), (30, 30, 34), bevel_mm=0.9, gloss=0.6, spec=0.5)
    # sticks
    for (sx, sy) in M['sticks']:
        cv.shaded(cv.mask_ellipse(sx, sy, 12.0), (14, 14, 16), None, bevel_mm=1.5, gloss=0.2, light=(0.45, 0.75, 0.5), spec=0.1)
        cv.shaded(cv.mask_ellipse(sx, sy, M['stick_r']), (52, 52, 58), (28, 28, 32), bevel_mm=1.6, gloss=0.5, dome=True, spec=0.4)
        ring = np.clip(cv.mask_ellipse(sx, sy, 7.0) - cv.mask_ellipse(sx, sy, 6.0), 0, 1)
        cv.paint(ring, (74, 74, 80), 0.7)
        cv.shaded(cv.mask_ellipse(sx, sy, 5.8), (30, 30, 34), (42, 42, 48), bevel_mm=1.0, gloss=0.2, light=(0.45, 0.75, 0.5), spec=0.05)
    # guide button (plain), View and Menu
    cv.shaded(cv.mask_ellipse(0, -15, 6.2), (30, 30, 34), None, bevel_mm=1.0, gloss=0.9, dome=True, spec=0.6)
    cv.paint(np.clip(cv.mask_ellipse(0, -15, 4.6) - cv.mask_ellipse(0, -15, 3.7), 0, 1), (200, 200, 206), 0.85)
    for (bx, icon) in ((-12, 'view'), (12, 'menu')):
        cv.shaded(cv.mask_ellipse(bx, -4, 2.8), (40, 40, 46), None, bevel_mm=0.6, gloss=0.7, dome=True, spec=0.5)
        cv.paint(xb1_icon(cv, icon, bx, -4, 1.7), (190, 190, 198), 0.9)
    return body


def xb1_icon(cv, icon, cx, cy, r):
    """the View (two windows) and Menu (three lines) marks"""
    w = r * 0.22
    if icon == 'menu':
        return cv.mask_lines([[(cx - r * 0.6, cy + d * r), (cx + r * 0.6, cy + d * r)] for d in (-0.45, 0, 0.45)], w)
    return cv.mask_lines([[(cx - r * 0.7, cy - r * 0.1), (cx + r * 0.2, cy - r * 0.1), (cx + r * 0.2, cy + r * 0.6),
                           (cx - r * 0.7, cy + r * 0.6), (cx - r * 0.7, cy - r * 0.1)],
                          [(cx - r * 0.3, cy - r * 0.1), (cx - r * 0.3, cy - r * 0.6), (cx + r * 0.7, cy - r * 0.6),
                           (cx + r * 0.7, cy + r * 0.2), (cx + r * 0.2, cy + r * 0.2)]], w)

# ---------------------------------------------------------------- canvas

class Canvas:
    def __init__(self, w, h, origin, scale):
        """origin: texel where pad (0,0) lands; scale: texels per mm"""
        self.w, self.h = w * SS, h * SS
        self.ox, self.oy = origin[0] * SS, origin[1] * SS
        self.s = scale * SS
        self.rgb = np.zeros((self.h, self.w, 3), np.float32)
        self.a = np.zeros((self.h, self.w), np.float32)

    def P(self, x, y):
        return (self.ox + x * self.s, self.oy + y * self.s)

    def mm(self, v):
        return v * self.s

    def mask_poly(self, pts):
        im = Image.new('L', (self.w, self.h), 0)
        ImageDraw.Draw(im).polygon([self.P(*p) for p in pts], fill=255)
        return np.asarray(im, np.float32) / 255.0

    def mask_ellipse(self, cx, cy, rx, ry=None):
        ry = rx if ry is None else ry
        im = Image.new('L', (self.w, self.h), 0)
        x0, y0 = self.P(cx - rx, cy - ry)
        x1, y1 = self.P(cx + rx, cy + ry)
        ImageDraw.Draw(im).ellipse([x0, y0, x1, y1], fill=255)
        return np.asarray(im, np.float32) / 255.0

    def mask_rrect(self, x0, y0, x1, y1, r):
        im = Image.new('L', (self.w, self.h), 0)
        a, b = self.P(x0, y0)
        c, d = self.P(x1, y1)
        ImageDraw.Draw(im).rounded_rectangle([a, b, c, d], radius=self.mm(r), fill=255)
        return np.asarray(im, np.float32) / 255.0

    def mask_lines(self, segs, width):
        im = Image.new('L', (self.w, self.h), 0)
        d = ImageDraw.Draw(im)
        for seg in segs:
            d.line([self.P(*p) for p in seg], fill=255, width=max(1, int(self.mm(width))), joint='curve')
        return np.asarray(im, np.float32) / 255.0

    def mask_text(self, text, cx, cy, size_mm):
        im = Image.new('L', (self.w, self.h), 0)
        f = ImageFont.truetype(FONT, max(4, int(self.mm(size_mm))))
        d = ImageDraw.Draw(im)
        x, y = self.P(cx, cy)
        d.text((x, y), text, font=f, fill=255, anchor='mm')
        return np.asarray(im, np.float32) / 255.0

    # ---- painting

    def paint(self, mask, color, alpha=1.0):
        m = np.clip(mask, 0, 1)[..., None] * alpha
        col = np.asarray(color, np.float32)
        if col.ndim == 1:
            col = col[None, None, :]
        self.rgb = self.rgb * (1 - m) + col * m
        self.a = self.a + (1 - self.a) * m[..., 0]

    def shaded(self, mask, color, color2=None, bevel_mm=2.0, gloss=0.6, light=(-0.45, -0.75, 0.5),
               dome=False, alpha=1.0, spec=0.35):
        """paint `mask` as a bevelled, lit solid"""
        r = max(1.0, self.mm(bevel_mm))
        if dome:
            dist = nd.distance_transform_edt(mask > 0.5)
            hgt = np.sqrt(np.clip(dist / (dist.max() + 1e-6), 0, 1))
            hgt = nd.gaussian_filter(hgt, r * 0.3) * r * 3
        else:
            hgt = nd.gaussian_filter(mask, r) * r
        gy, gx = np.gradient(hgt)
        n = np.stack([-gx, -gy, np.ones_like(gx)], -1)
        n /= np.linalg.norm(n, axis=-1, keepdims=True)
        L = np.array(light, np.float32)
        L /= np.linalg.norm(L)
        lam = np.clip((n * L).sum(-1), 0, 1)
        flat = L[2]
        shade = 0.55 + 0.6 * (lam - flat)               # 0.55 on flat areas, darker/brighter on slopes
        # vertical gradient: top a little lighter
        ys = np.linspace(0, 1, self.h, dtype=np.float32)[:, None]
        c1 = np.asarray(color, np.float32)
        c2 = np.asarray(color2 if color2 is not None else color, np.float32)
        ext = np.nonzero(mask.max(1) > 0.5)[0]
        if len(ext):
            t = np.clip((ys - ext[0] / self.h) / max(1e-3, (ext[-1] - ext[0]) / self.h), 0, 1)
        else:
            t = ys
        base = c1[None, None, :] * (1 - t[..., None]) + c2[None, None, :] * t[..., None]
        H = L + np.array([0, 0, 1], np.float32)
        H /= np.linalg.norm(H)
        sp = np.clip((n * H).sum(-1), 0, 1) ** 40 * spec * gloss * 255
        col = base * (shade[..., None] / 0.55) + sp[..., None]
        self.paint(mask, np.clip(col, 0, 255), alpha)

    def glow(self, mask, color=(190, 142, 46), k_px=11.4, p=1.66, top=0.55):
        """the gold aura the game's pad pictures have: colour and alpha fall
        off together (measured from the game's own pad pictures)"""
        d = nd.distance_transform_edt(mask < 0.5) / SS
        f = np.exp(-(d / k_px) ** p) * (1 - mask)
        # weaker over the top edge than along the sides and bottom
        ys, xs = np.nonzero(mask > 0.5)
        if len(ys):
            y0, y1 = ys.min(), ys.max()
            yy = np.arange(self.h, dtype=np.float32)[:, None]
            w = np.clip((yy - y0) / max(1, (y1 - y0) * 0.5), 0, 1)
            f = f * (top + (1 - top) * w)
        a = np.clip(f ** 0.75 * 0.97, 0, 1)
        col = np.asarray(color, np.float32)[None, None, :] * (f / np.maximum(a, 1e-4))[..., None]
        self.paint(a, np.clip(col, 0, 255), 1.0)

    def image(self):
        rgba = np.dstack([np.clip(self.rgb, 0, 255), np.clip(self.a * 255, 0, 255)]).astype(np.uint8)
        im = Image.fromarray(rgba, 'RGBA')
        # downsample with premultiplied alpha
        a = np.asarray(im, np.float32) / 255.0
        pm = a.copy()
        pm[..., :3] *= pm[..., 3:4]
        small = Image.fromarray((pm * 255).astype(np.uint8), 'RGBA').resize(
            (self.w // SS, self.h // SS), Image.LANCZOS)
        s = np.asarray(small, np.float32) / 255.0
        out = s.copy()
        aa = np.maximum(s[..., 3:4], 1e-6)
        out[..., :3] = np.where(s[..., 3:4] > 0, s[..., :3] / aa, 0)
        return Image.fromarray((np.clip(out, 0, 1) * 255).astype(np.uint8), 'RGBA')

# ---------------------------------------------------------------- symbols

SYM_COL = {'cross': (124, 170, 236), 'circle': (255, 98, 110), 'square': (232, 140, 210),
           'triangle': (64, 216, 168)}
SYM_COL5 = {k: (200, 200, 206) for k in SYM_COL}       # DualSense: light grey symbols


def sym_mask(cv, kind, cx, cy, r, width):
    if kind == 'cross':
        d = r * 0.62
        return cv.mask_lines([[(cx - d, cy - d), (cx + d, cy + d)], [(cx - d, cy + d), (cx + d, cy - d)]], width)
    if kind == 'circle':
        o = cv.mask_ellipse(cx, cy, r * 0.62 + width / 2)
        i = cv.mask_ellipse(cx, cy, r * 0.62 - width / 2)
        return np.clip(o - i, 0, 1)
    if kind == 'square':
        d = r * 0.52
        o = cv.mask_poly([(cx - d - width / 2, cy - d - width / 2), (cx + d + width / 2, cy - d - width / 2),
                          (cx + d + width / 2, cy + d + width / 2), (cx - d - width / 2, cy + d + width / 2)])
        i = cv.mask_poly([(cx - d + width / 2, cy - d + width / 2), (cx + d - width / 2, cy - d + width / 2),
                          (cx + d - width / 2, cy + d - width / 2), (cx - d + width / 2, cy + d - width / 2)])
        return np.clip(o - i, 0, 1)
    # triangle
    d = r * 0.68
    p = [(cx, cy - d), (cx + d * 0.92, cy + d * 0.55), (cx - d * 0.92, cy + d * 0.55)]
    return cv.mask_lines([[p[0], p[1], p[2], p[0], p[1]]], width)


FACE = [('triangle', 0, -1), ('circle', 1, 0), ('cross', 0, 1), ('square', -1, 0)]

# ---------------------------------------------------------------- the pad

def draw_pad(cv, model, lit=()):
    if model == 1:
        return draw_xb1(cv, lit)
    M = MODELS[model]
    out = catmull(M['outline'])
    body = cv.mask_poly(out)
    cv.glow(body)
    cv.shaded(body, M['body'], M['body2'], bevel_mm=4.5, gloss=M['gloss'], spec=0.5)

    fx, fy = M['face']
    dx, dy = M['dpad']
    sp = 12.0                                      # face button offset (mm)
    dark = (40, 40, 46) if model != 5 else (26, 26, 30)

    if M['two_tone']:
        # DualSense black centre plate: from the touchpad's lower corners around the sticks
        plate = catmull([(-30, -6), (-14, 0), (0, 2), (14, 0), (30, -6), (36, 8), (35, 24), (28, 29),
                         (16, 26), (0, 24.5), (-16, 26), (-28, 29), (-35, 24), (-36, 8)])
        pm = cv.mask_poly(plate) * body
        cv.shaded(pm, (30, 30, 34), (18, 18, 20), bevel_mm=1.2, gloss=0.4, spec=0.3)

    # recesses for the d-pad and the face buttons
    for (cx, cy) in (((dx, dy), (fx, fy)) if model != 5 else ()):
        rec = cv.mask_ellipse(cx, cy, 20.5)
        col = (16, 16, 18) if model != 5 else (196, 196, 204)
        cv.shaded(rec, col, col, bevel_mm=1.5, gloss=0.2, light=(0.45, 0.75, 0.5), spec=0.1)

    # d-pad: four separate arrow keys
    for (ux, uy) in ((0, -1), (1, 0), (0, 1), (-1, 0)):
        px, py = -uy, ux
        tip, base, hw = 3.6, 14.0, 3.6
        pts = [(dx + ux * tip, dy + uy * tip),
               (dx + ux * (tip + 2.6) + px * hw, dy + uy * (tip + 2.6) + py * hw),
               (dx + ux * base + px * hw, dy + uy * base + py * hw),
               (dx + ux * base - px * hw, dy + uy * base - py * hw),
               (dx + ux * (tip + 2.6) - px * hw, dy + uy * (tip + 2.6) - py * hw)]
        k = cv.mask_poly(pts)
        cv.shaded(k, dark, (24, 24, 28), bevel_mm=0.9, gloss=0.8, spec=0.6)
        # embossed arrowhead on each key
        a0 = (dx + ux * (base - 2.2), dy + uy * (base - 2.2))
        tri = [(a0[0] + ux * 1.4, a0[1] + uy * 1.4), (a0[0] - ux * 0.9 + px * 1.5, a0[1] - uy * 0.9 + py * 1.5),
               (a0[0] - ux * 0.9 - px * 1.5, a0[1] - uy * 0.9 - py * 1.5)]
        cv.paint(cv.mask_poly(tri), (92, 92, 100), 0.8)

    # face buttons
    cols = SYM_COL5 if model == 5 else SYM_COL
    for (kind, ux, uy) in FACE:
        cx, cy = fx + ux * sp, fy + uy * sp
        cv.shaded(cv.mask_ellipse(cx, cy, 5.4), dark, (22, 22, 26), bevel_mm=1.0, gloss=0.9, dome=True, spec=0.6)
        on = kind in lit
        c = cols[kind]
        if on:
            cv.paint(cv.mask_ellipse(cx, cy, 5.2), c, 0.35)
            c = tuple(min(255, int(v * 1.25 + 30)) for v in c)
        cv.paint(sym_mask(cv, kind, cx, cy, 5.4, 1.15), c, 1.0)

    # sticks
    for (sx, sy) in M['sticks']:
        cv.shaded(cv.mask_ellipse(sx, sy, 12.5), (14, 14, 16) if model != 5 else (22, 22, 24),
                  None, bevel_mm=1.5, gloss=0.2, light=(0.45, 0.75, 0.5), spec=0.1)
        cv.shaded(cv.mask_ellipse(sx, sy, M['stick_r']), (52, 52, 58), (30, 30, 34), bevel_mm=1.6,
                  gloss=0.5, dome=True, spec=0.4)
        # concave top with grip ring
        ring = np.clip(cv.mask_ellipse(sx, sy, 6.6) - cv.mask_ellipse(sx, sy, 5.6), 0, 1)
        cv.paint(ring, (70, 70, 78), 0.7)
        cv.shaded(cv.mask_ellipse(sx, sy, 5.4), (30, 30, 34), (42, 42, 48), bevel_mm=1.0,
                  gloss=0.2, light=(0.45, 0.75, 0.5), spec=0.05)

    c = M['center']
    if c == 'ds3':
        # SELECT, START, PS button, LEDs
        cv.shaded(cv.mask_rrect(-12.5, -12.5, -6.5, -9.5, 0.6), (60, 60, 66), None, bevel_mm=0.5, gloss=0.5)
        st = cv.mask_poly([(6.5, -13), (12.5, -11), (6.5, -9)])
        cv.shaded(st, (60, 60, 66), None, bevel_mm=0.5, gloss=0.5)
        cv.shaded(cv.mask_ellipse(0, 2, 5.2), (20, 20, 24), None, bevel_mm=1.0, gloss=0.9, dome=True)
        cv.paint(ps_logo(cv, 0, 2, 3.0), (150, 150, 160), 0.9)
        for i in range(4):
            cv.paint(cv.mask_rrect(-9 + i * 5, -31.5, -6.6 + i * 5, -30.2, 0.4), (200, 40, 40) if i == 0 else (60, 30, 30), 0.9)
    else:
        big = model == 5
        x0, y0, x1, y1 = (-30, -40, 30, -6) if big else (-26, -37.5, 26, -9)
        tp = cv.mask_rrect(x0, y0, x1, y1, 5 if big else 4)
        if big:
            cv.shaded(tp, (236, 236, 240), (212, 212, 220), bevel_mm=1.0, gloss=0.5, spec=0.3)
            # light strips along the touchpad's sides
            for sx in (x0 - 1.2, x1 + 1.2):
                cv.paint(cv.mask_rrect(sx - 0.5, y0 + 6, sx + 0.5, y1 - 2, 0.5), (90, 150, 255), 0.85)
        else:
            cv.shaded(tp, (44, 44, 50), (30, 30, 34), bevel_mm=1.0, gloss=0.3, spec=0.2)
            # light bar glimpse along the top edge
            cv.paint(cv.mask_rrect(x0 + 6, y0 - 1.6, x1 - 6, y0 - 0.5, 0.5), (70, 120, 255), 0.75)
            # speaker grille
            for i in range(-3, 4):
                for j in range(2):
                    cv.paint(cv.mask_ellipse(i * 1.6, -3.0 + j * 1.5, 0.45), (8, 8, 10), 0.9)
        # SHARE/CREATE and OPTIONS
        ox = x1 + (6 if big else 6.5)
        for sx in (-ox, ox):
            cv.shaded(cv.mask_rrect(sx - 1.4, y0 + 3, sx + 1.4, y0 + 8.5, 1.3), (54, 54, 60), None,
                      bevel_mm=0.5, gloss=0.6)
        # PS button (and the DualSense mute button)
        py = 8 if not big else 9
        cv.shaded(cv.mask_ellipse(0, py, 4.2), (24, 24, 28), None, bevel_mm=0.8, gloss=0.9, dome=True)
        cv.paint(ps_logo(cv, 0, py, 2.6), (150, 150, 160), 0.9)
        if big:
            cv.shaded(cv.mask_rrect(-3, 15, 3, 17.2, 1.0), (40, 40, 44), None, bevel_mm=0.4, gloss=0.6)
            cv.paint(cv.mask_rrect(-1.6, 15.7, 1.6, 16.5, 0.4), (230, 120, 80), 0.6)
    return body


def ps_logo(cv, cx, cy, r):
    """a generic 'home' glyph for the PS button: a small house-shaped mark (no logo)"""
    return cv.mask_lines([[(cx - r * 0.55, cy + r * 0.45), (cx - r * 0.55, cy - r * 0.05), (cx, cy - r * 0.55),
                           (cx + r * 0.55, cy - r * 0.05), (cx + r * 0.55, cy + r * 0.45),
                           (cx - r * 0.55, cy + r * 0.45)]], r * 0.22)


def render(model, w, h, face_at, spacing_px, lit=()):
    scale = spacing_px / 12.0
    M = MODELS[model]
    origin = (face_at[0] - M['face'][0] * scale, face_at[1] - M['face'][1] * scale)
    cv = Canvas(w, h, origin, scale)
    draw_pad(cv, model, lit)
    return cv, scale, origin


if __name__ == '__main__':
    for m in (3, 4, 5):
        cv, s, o = render(m, 512, 300, (293, 130), 23)
        im = cv.image()
        bg = Image.new('RGBA', im.size, (0, 0, 0, 255))
        bg.alpha_composite(im)
        bg.convert('RGB').save('test_ps%d.png' % m)
