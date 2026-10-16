/* prompts_kb.inc -- the loading screens' controls pictures with keyboard and
 * mouse prompts (1.8).
 *
 * The first levels' loading screens explain the controls on a picture of the
 * Xbox pad (UI\loadl2..4), the game's own labels written to the right of
 * gold arrows. With keyboard / mouse prompts the picture is drawn here, as it
 * is decoded: a keyboard and a mouse, the keys and buttons in use lit in gold,
 * and at each of the game's arrows the key (and mouse button) bound to that
 * pad button now -- drawn again when one is rebound. The arrows end where the
 * game's did, so its labels still sit beside them. The small "pressed"
 * pictures the game flashes over the pad's buttons are left empty (there is
 * no pad under them). Nothing here is taken from a picture: all is drawn. */

#define KB_LINE 0xF0D23Au   /* the game's leader lines (240, 210, 58) */
#define KB_GLOW 0xBE8E2Eu   /* the glow around the game's pad (190, 142, 46) */

static float kb_c01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static uint32_t kb_mix(uint32_t a, uint32_t b, float t)
{
    int i;
    uint32_t r = 0;
    t = kb_c01(t);
    for (i = 0; i < 24; i += 8) {
        float ca = (float)((a >> i) & 255), cb = (float)((b >> i) & 255);
        r |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << i;
    }
    return r;
}
static float kb_sd_rrect(float px, float py, float x0, float y0, float x1, float y1, float r)
{
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, hx = (x1 - x0) * 0.5f - r, hy = (y1 - y0) * 0.5f - r;
    float qx = fabsf(px - cx) - hx, qy = fabsf(py - cy) - hy;
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
    if (in > 0) in = 0;
    return sqrtf(ox * ox + oy * oy) + in - r;
}
#define KB_CLIP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

/* a rounded rectangle, shaded top to bottom; glow > 0: a soft halo outside it instead */
static void kb_rrect(uint32_t *a, int w, int h, float x0, float y0, float x1, float y1, float r,
                     uint32_t c0, uint32_t c1, float alpha, float glow)
{
    int X0 = KB_CLIP((int)floorf(x0 - glow) - 1, 0, w), X1 = KB_CLIP((int)ceilf(x1 + glow) + 1, 0, w);
    int Y0 = KB_CLIP((int)floorf(y0 - glow) - 1, 0, h), Y1 = KB_CLIP((int)ceilf(y1 + glow) + 1, 0, h);
    int x, y;
    for (y = Y0; y < Y1; y++)
        for (x = X0; x < X1; x++) {
            float d = kb_sd_rrect(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r), cv;
            if (glow > 0) {
                if (d <= 0 || d >= glow) continue;
                cv = powf(1.0f - d / glow, 1.66f);
                pr_blend(&a[y * w + x], cv * alpha, c0);
                continue;
            }
            cv = kb_c01(0.5f - d);
            if (cv > 0) pr_blend(&a[y * w + x], cv * alpha, kb_mix(c0, c1, (y + 0.5f - y0) / (y1 - y0)));
        }
}
/* a line of width wd, round ends */
static void kb_line(uint32_t *a, int w, int h, float ax, float ay, float bx, float by, float wd, uint32_t c)
{
    int X0 = KB_CLIP((int)floorf((ax < bx ? ax : bx) - wd) - 1, 0, w), X1 = KB_CLIP((int)ceilf((ax > bx ? ax : bx) + wd) + 1, 0, w);
    int Y0 = KB_CLIP((int)floorf((ay < by ? ay : by) - wd) - 1, 0, h), Y1 = KB_CLIP((int)ceilf((ay > by ? ay : by) + wd) + 1, 0, h);
    int x, y;
    for (y = Y0; y < Y1; y++)
        for (x = X0; x < X1; x++) {
            float cv = kb_c01(0.5f - (pr_seg(x + 0.5f, y + 0.5f, ax, ay, bx, by) - wd * 0.5f));
            if (cv > 0) pr_blend(&a[y * w + x], cv, c);
        }
}
/* the game's arrow: a line ending in a head, its tip at (tx, y) */
static void kb_arrow(uint32_t *a, int w, int h, float sx, float tx, float y)
{
    int x, yy, i, j;
    if (tx - 17 > sx) kb_line(a, w, h, sx, y, tx - 15, y, 2.2f, KB_LINE);
    for (yy = (int)(y - 6); yy <= (int)(y + 6); yy++)
        for (x = (int)(tx - 19); x <= (int)tx + 1; x++) {
            int n = 0;
            if (x < 0 || x >= w || yy < 0 || yy >= h) continue;
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++) {
                    float px = x + (i + 0.5f) / 4, py = yy + (j + 0.5f) / 4, k = (px - (tx - 17)) / 17.0f;
                    n += k >= 0 && k <= 1 && fabsf(py - y) <= 4.3f * (1 - k);
                }
            if (n) pr_blend(&a[yy * w + x], n / 16.0f, KB_LINE);
        }
}
/* text in the stroke font, centred on (cx, cy); s: pixels per grid unit, th: stroke width */
static void kb_text(uint32_t *a, int w, int h, const char *str, float cx, float cy, float s, float th, uint32_t c)
{
    float seg[96][4], x0 = cx - pr_text_w(str) * s * 0.5f, y0 = cy - 3.0f * s, px, py, gx, gy;
    int n = 0, X0, X1, Y0, Y1, x, y, i;
    for (; *str; str++, x0 += 5.2f * s) {
        const char *g = pr_glyph((unsigned char)*str);
        int have = 0, used;
        if (*str == ' ') continue;
        px = py = 0;
        while (*g) {
            if (*g == '|') { have = 0; g++; continue; }
            if (*g == ' ') { g++; continue; }
            if (sscanf(g, "%f,%f%n", &gx, &gy, &used) != 2) break;
            g += used;
            if (have && n < 96) { seg[n][0] = x0 + px * s; seg[n][1] = y0 + py * s; seg[n][2] = x0 + gx * s; seg[n][3] = y0 + gy * s; n++; }
            px = gx; py = gy; have = 1;
        }
    }
    if (!n) return;
    X0 = KB_CLIP((int)floorf(cx - pr_text_w(str) * s) - 4, 0, w);
    {   /* the bounds of the strokes */
        float l = seg[0][0], r = seg[0][0], t = seg[0][1], b = seg[0][1];
        for (i = 0; i < n; i++) {
            if (seg[i][0] < l) l = seg[i][0]; if (seg[i][2] < l) l = seg[i][2];
            if (seg[i][0] > r) r = seg[i][0]; if (seg[i][2] > r) r = seg[i][2];
            if (seg[i][1] < t) t = seg[i][1]; if (seg[i][3] < t) t = seg[i][3];
            if (seg[i][1] > b) b = seg[i][1]; if (seg[i][3] > b) b = seg[i][3];
        }
        X0 = KB_CLIP((int)floorf(l - th) - 1, 0, w); X1 = KB_CLIP((int)ceilf(r + th) + 1, 0, w);
        Y0 = KB_CLIP((int)floorf(t - th) - 1, 0, h); Y1 = KB_CLIP((int)ceilf(b + th) + 1, 0, h);
    }
    for (y = Y0; y < Y1; y++)
        for (x = X0; x < X1; x++) {
            float d = 1e9f, cv;
            for (i = 0; i < n; i++) {
                float e = pr_seg(x + 0.5f, y + 0.5f, seg[i][0], seg[i][1], seg[i][2], seg[i][3]);
                if (e < d) d = e;
            }
            cv = kb_c01(0.5f - (d - th * 0.5f));
            if (cv > 0) pr_blend(&a[y * w + x], cv, c);
        }
}
/* a small picture (sz x sz, ARGB) laid over the big one at (x0, y0) */
static void kb_paste(uint32_t *a, int w, int h, const uint32_t *s, int sz, int x0, int y0)
{
    int x, y;
    for (y = 0; y < sz; y++)
        for (x = 0; x < sz; x++) {
            uint32_t p = s[y * sz + x];
            if (x0 + x < 0 || x0 + x >= w || y0 + y < 0 || y0 + y >= h || !(p >> 24)) continue;
            pr_blend(&a[(y0 + y) * w + x0 + x], (float)(p >> 24) / 255.0f, p & 0xFFFFFF);
        }
}

/* ---- the keyboard: the main block and the function row, in key units ---- */
typedef struct { float x, y, w, hgt; const char *lab; int vk, vk2, vk3; } KbKey;
static KbKey s_kb_keys[80];
static int s_kb_nkeys;
static void kb_layout(void)
{
    static const struct { float w; const char *lab; int vk, vk2, vk3; } rows[5][15] = {
        { { 1, "`", VK_OEM_3 }, { 1, "1", '1' }, { 1, "2", '2' }, { 1, "3", '3' }, { 1, "4", '4' }, { 1, "5", '5' },
          { 1, "6", '6' }, { 1, "7", '7' }, { 1, "8", '8' }, { 1, "9", '9' }, { 1, "0", '0' }, { 1, "-", VK_OEM_MINUS },
          { 1, "=", VK_OEM_PLUS }, { 2, "BACK", VK_BACK } },
        { { 1.5f, "TAB", VK_TAB }, { 1, "Q", 'Q' }, { 1, "W", 'W' }, { 1, "E", 'E' }, { 1, "R", 'R' }, { 1, "T", 'T' },
          { 1, "Y", 'Y' }, { 1, "U", 'U' }, { 1, "I", 'I' }, { 1, "O", 'O' }, { 1, "P", 'P' }, { 1, "[", VK_OEM_4 },
          { 1, "]", VK_OEM_6 }, { 1.5f, "\\", VK_OEM_5 } },
        { { 1.75f, "CAPS", VK_CAPITAL }, { 1, "A", 'A' }, { 1, "S", 'S' }, { 1, "D", 'D' }, { 1, "F", 'F' }, { 1, "G", 'G' },
          { 1, "H", 'H' }, { 1, "J", 'J' }, { 1, "K", 'K' }, { 1, "L", 'L' }, { 1, ";", VK_OEM_1 }, { 1, "'", VK_OEM_7 },
          { 2.25f, "ENTER", VK_RETURN } },
        { { 2.25f, "SHIFT", VK_SHIFT, VK_LSHIFT }, { 1, "Z", 'Z' }, { 1, "X", 'X' }, { 1, "C", 'C' }, { 1, "V", 'V' },
          { 1, "B", 'B' }, { 1, "N", 'N' }, { 1, "M", 'M' }, { 1, ",", VK_OEM_COMMA }, { 1, ".", VK_OEM_PERIOD },
          { 1, "/", VK_OEM_2 }, { 2.75f, "SHIFT", VK_SHIFT, VK_RSHIFT } },
        { { 1.5f, "CTRL", VK_CONTROL, VK_LCONTROL }, { 1.5f, "ALT", VK_MENU, VK_LMENU }, { 9, "", VK_SPACE },
          { 1.5f, "ALT", VK_MENU, VK_RMENU }, { 1.5f, "CTRL", VK_CONTROL, VK_RCONTROL } },
    };
    static const char *const fn[12] = { "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12" };
    int r, i;
    if (s_kb_nkeys) return;
    {   /* the function row: Esc, then F1-F12 in threes of four, three quarters high */
        KbKey *k = &s_kb_keys[s_kb_nkeys++];
        k->x = 0; k->y = 0; k->w = 1; k->hgt = 0.75f; k->lab = "ESC"; k->vk = VK_ESCAPE;
        for (i = 0; i < 12; i++) {
            k = &s_kb_keys[s_kb_nkeys++];
            k->x = 2.0f + (float)i + 0.5f * (float)(i / 4);
            if (i >= 8) k->x = 11.0f + (float)(i - 8);
            k->y = 0; k->w = 1; k->hgt = 0.75f; k->lab = fn[i]; k->vk = VK_F1 + i;
        }
    }
    for (r = 0; r < 5; r++) {
        float x = 0;
        for (i = 0; i < 15 && rows[r][i].w > 0; i++) {
            KbKey *k = &s_kb_keys[s_kb_nkeys++];
            k->x = x; k->y = 1.0f + (float)r; k->w = rows[r][i].w; k->hgt = 1; k->lab = rows[r][i].lab;
            k->vk = rows[r][i].vk; k->vk2 = rows[r][i].vk2; k->vk3 = rows[r][i].vk3;
            x += rows[r][i].w;
        }
    }
}
static int kb_key_is(const KbKey *k, int vk)
{
    return vk && (k->vk == vk || k->vk2 == vk || k->vk3 == vk);
}

/* the keyboard at (X, Y), u pixels a key; lit: the keys to light (vk list) */
static void kb_draw_keyboard(uint32_t *a, int w, int h, float X, float Y, float u, const int *lit, int nlit)
{
    float m = 0.42f * u, x1 = X + 15 * u, y1 = Y + 6 * u;
    int i, j;
    kb_layout();
    kb_rrect(a, w, h, X - m, Y - m, x1 + m, y1 + m, 0.55f * u, KB_GLOW, 0, 0.55f, 0.85f * u);   /* the glow */
    kb_rrect(a, w, h, X - m, Y - m, x1 + m, y1 + m, 0.55f * u, 0x575960, 0x2B2C31, 1, 0);         /* the case */
    kb_rrect(a, w, h, X - m + 1.2f, Y - m + 1.2f, x1 + m - 1.2f, y1 + m - 1.2f, 0.5f * u, 0x45474D, 0x2F3035, 1, 0);
    kb_rrect(a, w, h, X - 0.08f * u, Y - 0.08f * u, x1 + 0.08f * u, y1 + 0.08f * u, 0.2f * u, 0x16171A, 0x1C1D21, 1, 0);   /* the well */
    for (i = 0; i < s_kb_nkeys; i++) {
        const KbKey *k = &s_kb_keys[i];
        float kx0 = X + (k->x + 0.05f) * u, kx1 = X + (k->x + k->w - 0.05f) * u;
        float ky0 = Y + (k->y + 0.05f) * u, ky1 = Y + (k->y + k->hgt - 0.05f) * u;
        float ts, tw;
        int on = 0;
        for (j = 0; j < nlit && !on; j++) on = kb_key_is(k, lit[j]);
        if (on) kb_rrect(a, w, h, kx0, ky0, kx1, ky1, 0.16f * u, 0xF2B233, 0, 0.9f, 0.55f * u);
        kb_rrect(a, w, h, kx0, ky0, kx1, ky1, 0.16f * u, on ? 0xA8771C : 0x26272B, on ? 0x7A5310 : 0x1D1E22, 1, 0);
        kb_rrect(a, w, h, kx0 + 0.09f * u, ky0 + 0.05f * u, kx1 - 0.09f * u, ky1 - 0.15f * u, 0.12f * u,
                 on ? 0xFFD766 : 0x46484E, on ? 0xE9A92E : 0x35363C, 1, 0);
        if (!k->lab[0]) continue;
        /* the name: as big as fits the face */
        ts = (k->hgt < 1 ? 0.075f : 0.095f) * u;
        tw = pr_text_w(k->lab) * ts;
        if (tw > (k->w - 0.36f) * u) ts *= (k->w - 0.36f) * u / tw;
        kb_text(a, w, h, k->lab, (kx0 + kx1) * 0.5f, (ky0 + ky1) * 0.5f - 0.05f * u, ts,
                ts * 0.9f > 0.85f ? ts * 0.9f : 0.85f, on ? 0x2A1C04 : 0xB9BBC2);
    }
}

/* the mouse: its body in a box of side S centred on (cx, cy); lit: 1 << part */
static void kb_draw_mouse(uint32_t *a, int w, int h, float cx, float cy, float S, int lit)
{
    float hw = 0.62f * 0.86f * S * 0.5f, hh = 0.90f * 0.88f * S * 0.5f, oy = -0.06f * S * 0.5f;
    int X0 = KB_CLIP((int)(cx - S * 0.5f - 14), 0, w), X1 = KB_CLIP((int)(cx + S * 0.5f + 14), 0, w);
    int Y0 = KB_CLIP((int)(cy - S * 0.5f - 14), 0, h), Y1 = KB_CLIP((int)(cy + S * 0.5f + 14), 0, h);
    int x, y, i, j, part;
    for (y = Y0; y < Y1; y++)
        for (x = X0; x < X1; x++) {
            uint32_t *p = &a[y * w + x];
            float u = (x + 0.5f - cx) / (S * 0.5f), v = (y + 0.5f - cy) / (S * 0.5f);
            int nb = 0, np[5] = { 0, 0, 0, 0, 0 }, nl = 0;
            /* the glow: by an ellipse's distance */
            {
                float ex = (x + 0.5f - cx) / hw, ey = (y + 0.5f - cy - oy) / hh, e = sqrtf(ex * ex + ey * ey);
                float d = (e - 1.0f) * (hw < hh ? hw : hh), g = 0.85f * 14.0f * S / 100.0f;
                if (d > 0 && d < g) pr_blend(p, 0.55f * powf(1.0f - d / g, 1.66f), KB_GLOW);
            }
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++) {
                    float uu = (x + (i + 0.5f) / 4 - cx) / (S * 0.5f), vv = (y + (j + 0.5f) / 4 - cy) / (S * 0.5f);
                    if (!pr_mouse_body(uu / 0.86f, (vv + 0.06f) / 0.88f, NULL)) continue;
                    nb++;
                    for (part = 0; part < 5; part++)
                        if (part == 2 ? fabsf(uu) <= 0.055f && vv > -0.60f && vv < -0.27f   /* (a slimmer wheel) */
                                      : (part < 2 || ((lit >> part) & 1)) && pr_mouse_part(uu, vv, &part)) np[part]++;
                    nl += pr_mouse_lines(uu, vv, NULL);
                }
            if (!nb) continue;
            pr_blend(p, nb / 16.0f, kb_mix(0x585A61, 0x2A2B30, (v + 1) * 0.5f - u * 0.1f));
            for (part = 0; part < 5; part++)
                if (np[part]) {
                    int on = (lit >> part) & 1;
                    uint32_t c = on ? kb_mix(0xFFD766, 0xE9A92E, (v + 1) * 0.5f) : part == 2 ? 0x24252A : kb_mix(0x63656C, 0x46484E, (v + 1) * 0.6f);
                    pr_blend(p, np[part] / 16.0f, c);
                }
            if (nl) pr_blend(p, nl / 16.0f * 0.9f, 0x1A1B1F);
        }
}

/* the keys bound to action act (up to 3), and which mouse buttons (bits) */
static int kb_bound(int act, int *keys, int *mouse)
{
    int i, n = 0, vk;
    *mouse = 0;
    for (i = 0; i < 3; i++) {
        vk = xbox_BindKey(act, i);
        if (!vk) continue;
        if (vk == VK_LBUTTON) *mouse |= 1;
        else if (vk == VK_RBUTTON) *mouse |= 2;
        else if (vk == VK_MBUTTON) *mouse |= 4;
        else if (vk == VK_XBUTTON1) *mouse |= 8;
        else if (vk == VK_XBUTTON2) *mouse |= 16;
        else keys[n++] = vk;
    }
    return n;
}
/* the legend at an arrow: the bound key and (if any) mouse button, ending at x1, centred on y */
static float kb_legend(uint32_t *a, int w, int h, int act, float x1, float y, int sz)
{
    static uint32_t buf[40 * 40];
    int keys[3], mouse, n = kb_bound(act, keys, &mouse), shown = 0, part = -1, b;
    float x = x1;
    if (sz > 40) sz = 40;
    if (act == 8 && n > 1) {   /* pausing: Esc, if it is one of them, and only one key (a small one: ENTER is too long) */
        for (b = 0; b < n; b++) if (keys[b] == VK_ESCAPE) { keys[0] = VK_ESCAPE; break; }
        n = 1;
    }
    if (mouse) for (b = 0; b < 5; b++) if ((mouse >> b) & 1) { part = b; break; }
    /* right to left: the mouse button, then the key (a second key if no mouse) */
    if (part >= 0) {
        pr_mouse(buf, sz, sz, part);
        kb_paste(a, w, h, buf, sz, (int)(x - sz), (int)(y - sz * 0.5f));
        x -= sz + 4; shown++;
    } else if (n > 1) {
        pr_keycap(buf, sz, sz, keys[1]);
        kb_paste(a, w, h, buf, sz, (int)(x - sz), (int)(y - sz * 0.5f));
        x -= sz + 4; shown++;
    }
    if (n > 0) {
        pr_keycap(buf, sz, sz, keys[0]);
        kb_paste(a, w, h, buf, sz, (int)(x - sz), (int)(y - sz * 0.5f));
        x -= sz + 4; shown++;
    }
    return shown ? x + 4 : x1;
}

/* moving: the four move keys as an inverted T, its right edge x1, centred on y */
static void kb_move_keys(uint32_t *a, int w, int h, float x1, float y, int sz)
{
    static uint32_t buf[40 * 40];
    static const int act[4] = { 16, 18, 17, 19 };   /* move up, left, down, right */
    float px[4], py[4];
    int i, vk;
    px[0] = x1 - 2 * sz - 1; py[0] = y - sz - 0.5f;          /* up: over the middle */
    px[1] = x1 - 3 * sz - 2; py[1] = y + 0.5f;
    px[2] = x1 - 2 * sz - 1; py[2] = y + 0.5f;
    px[3] = x1 - sz; py[3] = y + 0.5f;
    for (i = 0; i < 4; i++) {
        vk = xbox_BindKey(act[i], 0);
        if (!vk) vk = xbox_BindKey(act[i], 1);
        if (!vk) continue;
        pr_keycap(buf, sz, sz, vk);
        kb_paste(a, w, h, buf, sz, (int)px[i], (int)py[i]);
    }
}

/* one callout: pad button act's keys at the arrow whose tip is (tx, y) */
typedef struct { int act; float tx, y; int sz; } KbCall;

static void kb_picture(uint32_t *a, int w, int h, int which)
{
    /* loadl2 (512x256): X; loadl3 (512x512): the full layout; loadl4 (512x256): Y and A */
    static const KbCall c2[] = { { 2, 389, 38.5f, 26 } };
    static const KbCall c3[] = { { 7, 389, 55.5f, 26 }, { 2, 389, 87.5f, 26 }, { 3, 389, 120.5f, 26 }, { 1, 389, 153.5f, 26 },
                                 { 4, 389, 183.5f, 26 }, { 0, 389, 216.5f, 26 }, { 6, 141, 56.0f, 26 }, { 8, 182, 243.5f, 22 } };
    static const KbCall c4[] = { { 3, 389, 58.5f, 26 }, { 0, 389, 104.5f, 26 } };
    const KbCall *c = which == 0 ? c2 : which == 1 ? c3 : c4;
    int n = which == 0 ? 1 : which == 1 ? 8 : 2, i, j, nlit = 0, lit[48], mlit = 0;
    for (i = 0; i < n; i++) {
        int keys[3], mouse, k = kb_bound(c[i].act, keys, &mouse);
        for (j = 0; j < k && nlit < 40; j++) lit[nlit++] = keys[j];
        mlit |= mouse;
    }
    if (which == 1)
        for (i = 16; i < 20; i++) { int vk = xbox_BindKey(i, 0); if (vk && nlit < 48) lit[nlit++] = vk; }
    memset(a, 0, (size_t)w * h * 4);
    if (which == 1) {
        kb_draw_keyboard(a, w, h, 24, 112, 14.0f, lit, nlit);
        kb_draw_mouse(a, w, h, 268, 154, 80, mlit);
    } else {
        kb_draw_keyboard(a, w, h, 36, 118, 15.0f, lit, nlit);
        kb_draw_mouse(a, w, h, 302, 163, 88, mlit);
    }
    for (i = 0; i < n; i++) {
        float tail = c[i].tx - (c[i].tx > 300 ? 28 : c[i].act == 6 ? 29 : 27);
        float x = kb_legend(a, w, h, c[i].act, tail - 3, c[i].y, c[i].sz);
        kb_arrow(a, w, h, x < tail - 3 ? tail : x, c[i].tx, c[i].y);
    }
    if (which == 1) {   /* moving / aiming: the move keys */
        kb_move_keys(a, w, h, 116, 262.0f, 15);
        kb_arrow(a, w, h, 120, 157, 265.5f);
    }
}


/* the game's controls pictures, as decoded, with keyboard / mouse prompts */
static void prompts_kb_pic(uint32_t *argb, int w, int h)
{
    int k;
    if (!((w == 512 && (h == 256 || h == 512)) || (w == 32 && h == 32) || (w == 64 && h == 64))) return;
        k = pr_which(argb, w, h);
    if (k < 0 && w == 64 && s_kb_load_seen && GetTickCount() - s_kb_load_seen < 120000u) {
        /* the trigger "pulled" (a red disc, transparent around) flashed on the
         * loading screen: not in prompts_sig.h -- known by its look */
        int x, y, in = 0, clear = 0;
        double sr = 0, sg = 0, sb = 0, sa = 0;
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++) {
                uint32_t p = argb[y * 64 + x];
                int dx = x - 32, dy = y - 32, d2 = dx * dx + dy * dy;
                if (d2 < 20 * 20) { in++; sa += p >> 24; sr += (p >> 16) & 255; sg += (p >> 8) & 255; sb += p & 255; }
                else if (d2 > 31 * 31) clear += (p >> 24) < 40;
            }
        sr /= in; sg /= in; sb /= in; sa /= in;
        if (sa > 60 && sr > 30 && sr > 2.2 * sg && sr > 2.2 * sb && clear > 400) {
            memset(argb, 0, (size_t)w * h * 4);
            tt_dbg("[PROMPTS] keyboard / mouse: the trigger's flash left out\n");
        }
        return;
    }
    if (k < 0) return;
    if (!strncmp(s_pr_sig[k].file, "loadl", 5)) s_kb_load_seen = GetTickCount();
    if (getenv("TT_KBPIC_DUMP")) {   /* testing: what was recognised */
        static int nd;
        char fn[64]; FILE *f;
        snprintf(fn, sizeof fn, "kbpic_%03d_%dx%d.raw", nd++, w, h);
        if ((f = fopen(fn, "wb"))) { fwrite(argb, 4, (size_t)w * h, f); fclose(f); }
    }
    if (!strncmp(s_pr_sig[k].file, "loadl", 5)) kb_txt_apply(1);
    if (!strcmp(s_pr_sig[k].file, "loadl2")) kb_picture(argb, w, h, 0);
    else if (!strcmp(s_pr_sig[k].file, "loadl3")) kb_picture(argb, w, h, 1);
    else if (!strcmp(s_pr_sig[k].file, "loadl4")) kb_picture(argb, w, h, 2);
    else memset(argb, 0, (size_t)w * h * 4);   /* a "pressed" button: nothing under it to flash */
    tt_dbg("[PROMPTS] keyboard / mouse for the game's %s picture\n", s_pr_sig[k].file);
}

/* ---- the words under the pictures ----
 * The loading screen's hint (and the upgrade screen's) name the pad's
 * buttons: "Pull and hold the left trigger ... Press the A button to fire
 * ... the left thumbstick". With keyboard / mouse prompts they are written
 * with the keys bound now, in place in the game's string table (in memory,
 * from UI\startup.BFF: zero-ended UTF-16, so a shorter text fits), and put
 * back as they were for the pad. */
typedef struct { const char *orig, *kb, *pad; } KbTxt;   /* pad: with the controller's bound buttons (1.7.8) */
static const KbTxt s_kb_txt[] = {
    { "Pull and hold the left trigger to switch to your Ranged Weapon.  Press the A button to fire.  Use the left thumbstick to aim.",
      "Hold {6} to switch to your Ranged Weapon.  Press {0} to fire.  Use {M} to aim.",
      "Pull and hold {6} to switch to your Ranged Weapon.  Press {0} to fire.  Use {M} to aim." },
    { "Pull and hold the left trigger to use.  Fire with the A button (hold down to increase damage).",
      "Hold {6} to use.  Fire with {0} (hold down to increase damage).",
      "Pull and hold {6} to use.  Fire with {0} (hold down to increase damage)." },
    { "Pull and hold the left trigger to use.  Throw with the A button (hold down to increase damage).",
      "Hold {6} to use.  Throw with {0} (hold down to increase damage).",
      "Pull and hold {6} to use.  Throw with {0} (hold down to increase damage)." },
    { "A devastating attack.  Press and hold the Y button to use.", "A devastating attack.  Press and hold {3} to use.",
      "A devastating attack.  Press and hold {3} to use." },
    /* (1.9) the menus' messages: saving, loading, the upgrades, the pause screen
     * (keys: Start's key -- Enter -- which closes them as A does, menus.c) */
    { "Game Saved.  Press the A button to continue.", "Game Saved.  Press {8} to continue.", "Game Saved.  Press {0} to continue." },
    { "Unable to save game. Press the A button to continue.", "Unable to save game. Press {8} to continue.",
      "Unable to save game. Press {0} to continue." },
    { "Unable to load game. Press the A button to continue.", "Unable to load game. Press {8} to continue.",
      "Unable to load game. Press {0} to continue." },
    { "No saved games. Press the A button to continue.", "No saved games. Press {8} to continue.",
      "No saved games. Press {0} to continue." },
    { "Not unlocked yet. Press the A button to continue.", "Not unlocked yet. Press {8} to continue.",
      "Not unlocked yet. Press {0} to continue." },
    { "You have not collected enough upgrade points to purchase this upgrade. Press the A button to continue.",
      "You have not collected enough upgrade points to purchase this upgrade. Press {8} to continue.",
      "You have not collected enough upgrade points to purchase this upgrade. Press {0} to continue." },
    { "Press START to resume", "Press {8} to resume", "Press START to resume" },
};
#define KB_TXT_N ((int)(sizeof s_kb_txt / sizeof s_kb_txt[0]))
#define KB_TXT_MAX 64
static struct { uint32_t at; int which; } s_kb_txt_at[KB_TXT_MAX];
static int s_kb_txt_n, s_kb_txt_state = -1;   /* -1 not looked for; 0 the game's words; 1 the keys'; 2 the controller's */

static const char *kb_key_word(int vk)
{
    switch (vk) {
    case VK_LBUTTON: return "the left mouse button";
    case VK_RBUTTON: return "the right mouse button";
    case VK_MBUTTON: return "the middle mouse button";
    case VK_XBUTTON1: return "mouse button 4";
    case VK_XBUTTON2: return "mouse button 5";
    default: return xbox_BindKeyName(vk);
    }
}
/* a controller button's name in the words of the prompts shown (0 Xbox, 1 PlayStation, 3 Xbox One) */
static const char *pr_pad_word(int src, int kind)
{
    static const char *const xb[17] = { "?", "the A button", "the B button", "the X button", "the Y button",
        "the Black button", "the White button", "the left trigger", "the right trigger", "Start", "Back",
        "up on the d-pad", "down on the d-pad", "left on the d-pad", "right on the d-pad",
        "the left thumbstick button", "the right thumbstick button" };
    static const char *const x1[17] = { "?", "the A button", "the B button", "the X button", "the Y button",
        "LB", "RB", "the left trigger", "the right trigger", "the Menu button", "the View button",
        "up on the d-pad", "down on the d-pad", "left on the d-pad", "right on the d-pad", "LS", "RS" };
    static const char *const ps[17] = { "?", "the cross button", "the circle button", "the square button", "the triangle button",
        "L1", "R1", "L2", "R2", "OPTIONS", "SHARE", "up on the d-pad", "down on the d-pad", "left on the d-pad",
        "right on the d-pad", "L3", "R3" };
    if (src < 0 || src > 16) src = 0;
    if (kind == 1) {
        int m = prompts_model();
        if (src == 9 && m == 3) return "START";
        if (src == 10) return m == 3 ? "SELECT" : m == 5 ? "CREATE" : "SHARE";
        return ps[src];
    }
    return kind == 3 ? x1[src] : xb[src];
}
/* the text with the keys: {n} action n's first key (and a mouse button, if one).
 * (1.9.2) Built in the game's own character units, in the chosen language:
 * the template, the button and key words all through tt_tr16 (lang.c), which
 * gives the English as it is when no language is chosen. */
extern int tt_tr16(const char *en, uint16_t *out, int max);
extern int tt_lang_game16(const uint16_t *en, int n, const uint16_t **out);
extern int tt_lang_on(void);
static int s_kb_txt_pad;   /* kb_txt_make for the controller: its bound buttons' names */
static int s_kb_short;     /* (too long for its place) 1: "Enter / Left Click", 2: the first key only */
#define KB_U 256
static int kb_cat(uint16_t *o, int n, int max, const char *en)
{
    if (n < max - 1) n += tt_tr16(en, o + n, max - n);
    return n;
}
static int kb_txt_make(const char *tmpl, uint16_t *out, int max)
{
    uint16_t t[KB_U];
    int tn = tt_tr16(tmpl, t, KB_U), i = 0, o = 0;
    while (i < tn && o < max - 1) {
        if (t[i] == '{' && i + 2 < tn && t[i + 2] == '}') {
            int c = t[i + 1];
            if (s_kb_txt_pad) {
                if (c == 'M') o = kb_cat(out, o, max, "the left thumbstick");
                else o = kb_cat(out, o, max, pr_pad_word(xbox_BindPad(c - '0'), prompts_kind_now()));
            } else if (c == 'M') {
                int j, vk;
                static const int mv[4] = { 16, 18, 17, 19 };   /* up, left, down, right: W A S D */
                for (j = 0; j < 4; j++) {
                    vk = xbox_BindKey(mv[j], 0);
                    if (j) o = kb_cat(out, o, max, " ");
                    o = kb_cat(out, o, max, vk ? xbox_BindKeyName(vk) : "?");
                }
            } else {
                int keys[3], mouse, k = kb_bound(c - '0', keys, &mouse), b, o0 = o;
                if (k) o = kb_cat(out, o, max, kb_key_word(keys[0]));
                for (b = 0; b < 5 && !(k && s_kb_short == 2); b++)
                    if ((mouse >> b) & 1) {
                        static const int mv[5] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
                        static const char *const sh[5] = { "Left Click", "Right Click", "Middle Click", "Mouse 4", "Mouse 5" };
                        if (k) o = kb_cat(out, o, max, s_kb_short ? " / " : " or ");
                        o = kb_cat(out, o, max, s_kb_short ? sh[b] : kb_key_word(mv[b]));
                        break;
                    }
                if (o == o0) o = kb_cat(out, o, max, "?");
            }
            i += 3;
            continue;
        }
        out[o++] = t[i++];
    }
    out[o] = 0;
    return o;
}
/* the game's words for a hint, in its units: the language's, or the English */
static int kb_orig(int which, uint16_t *o, int max)
{
    const char *en = s_kb_txt[which].orig;
    const uint16_t *tr;
    int n = 0, m;
    for (; en[n] && n < max - 1; n++) o[n] = (uint8_t)en[n];
    m = tt_lang_game16(o, n, &tr);
    if (m > 0) { n = m < max - 1 ? m : max - 1; memcpy(o, tr, 2 * (size_t)n); }
    o[n] = 0;
    return n;
}
static int kb_txt_is(uint32_t at, const uint16_t *s, int n)
{
    int i;
    for (i = 0; i < n; i++, at += 2) if (G16(at) != s[i]) return 0;
    return n > 0;
}
static void kb_txt_put(uint32_t at, const uint16_t *s, int n, int pad)
{
    int i;
    for (i = 0; i < n; i++) G16(at + 2 * (uint32_t)i) = s[i];
    for (; i < pad; i++) G16(at + 2 * (uint32_t)i) = ' ';   /* (as long as the original: the game lays its */
    G16(at + 2 * (uint32_t)i) = 0;                           /*  labels out by the strings' lengths) */
}
static struct { uint16_t u[KB_U]; int n; } s_kb_orig[KB_TXT_N];
static int s_kb_orig_ok;
static uint16_t s_kb_last[KB_TXT_MAX][KB_U];
static int s_kb_last_n[KB_TXT_MAX];
static void kb_txt_apply(int keys)   /* 0 the game's words, 1 the keys', 2 the controller's buttons as bound */
{
    static DWORD scanned;
    int i, ok = 0, extra = tt_lang_on() ? 47 : 0;   /* (a language's tables have room after each string: lang.c) */
    if (getenv("TT_KB_NOTEXT")) return;
    if (!s_kb_orig_ok) { for (i = 0; i < KB_TXT_N; i++) s_kb_orig[i].n = kb_orig(i, s_kb_orig[i].u, KB_U); s_kb_orig_ok = 1; }
    /* still where they were? */
    for (i = 0; i < s_kb_txt_n; i++) {
        uint32_t at = s_kb_txt_at[i].at;
        int w = s_kb_txt_at[i].which;
        if (kb_txt_is(at, s_kb_orig[w].u, s_kb_orig[w].n) || (s_kb_last_n[i] && kb_txt_is(at, s_kb_last[i], s_kb_last_n[i]))) ok++;
        else s_kb_txt_at[i].at = 0;   /* (not the game's words any more: never written to) */
    }
    if ((!s_kb_txt_n || ok < s_kb_txt_n) && keys && GetTickCount() - scanned > 1000u) {
        /* find them: the game's memory, for each one's words */
        const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
        uint32_t a;
        scanned = GetTickCount();
        s_kb_txt_n = 0;
        for (a = 0x80010000u; a < 0x83FFFF00u && s_kb_txt_n < KB_TXT_MAX; a += 2) {   /* (the heap: 0x80000000 on) */
            uint16_t c0 = (uint16_t)(m[a] | m[a + 1] << 8), c1 = (uint16_t)(m[a + 2] | m[a + 3] << 8);
            for (i = 0; i < KB_TXT_N; i++)
                if (c0 == s_kb_orig[i].u[0] && c1 == s_kb_orig[i].u[1] && kb_txt_is(a, s_kb_orig[i].u, s_kb_orig[i].n)) {
                    s_kb_txt_at[s_kb_txt_n].at = a; s_kb_txt_at[s_kb_txt_n].which = i; s_kb_last_n[s_kb_txt_n] = 0; s_kb_txt_n++;
                    break;
                }
        }
        tt_dbg("[PROMPTS] the hints naming the pad's buttons: %d found in memory\n", s_kb_txt_n);
    }
    for (i = 0; i < s_kb_txt_n; i++) {
        uint32_t at = s_kb_txt_at[i].at;
        int w = s_kb_txt_at[i].which, room = s_kb_orig[w].n, n;
        const KbTxt *t = &s_kb_txt[w];
        uint16_t buf[KB_U];
        if (!at) continue;
        s_kb_txt_pad = keys == 2;
        if (keys) n = kb_txt_make(keys == 2 ? t->pad : t->kb, buf, KB_U);
        else { n = room; memcpy(buf, s_kb_orig[w].u, 2 * (size_t)n); }
        for (s_kb_short = 1; keys == 1 && n > room + extra && s_kb_short <= 2; s_kb_short++)
            n = kb_txt_make(t->kb, buf, KB_U);   /* (shorter: the mouse button's short name, then the key alone) */
        s_kb_short = 0;
        s_kb_txt_pad = 0;
        if (n > room + extra) { n = room; memcpy(buf, s_kb_orig[w].u, 2 * (size_t)n); }   /* (too long for its place: as it was) */
        kb_txt_put(at, buf, n, room);
        memcpy(s_kb_last[i], buf, 2 * (size_t)n); s_kb_last_n[i] = n;
    }
    s_kb_txt_state = keys;
}
