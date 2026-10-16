/* e3hud_tex.inc -- texture work for the ECTS HUD and orange-pickup mods, done as the game's own
 * textures are decoded (xbox_tex_observer, renderer thread). Textures are
 * recognised by the FNV hash of their texels, the same on every copy of the
 * USA game.
 *  - character face textures: the portrait is cut from them;
 *  - the HUD's health sword: the cross-guard is redrawn more curved;
 *  - the health power-up lantern: green glass turned gold. */

/* ---- portraits from the character models' face textures ----
 * The model textures (256x256) hold half a face against the left edge --
 * the model mirrors it -- so the portrait mirrors it back. */
static const struct { uint32_t hash; int hero; float cy, r, halfw; int right; } s_atlas[] = {
    { 0x7016C467u, 1, 36.0f, 38.0f, 46.0f, 0 },     /* Aragorn */
    { 0x309C0622u, 2, 46.0f, 40.0f, 52.0f, 0 },     /* Legolas */
    { 0xC29442F6u, 3, 197.0f, 35.0f, 35.0f, 0 },    /* Gimli */
    /* the Extra heroes mod's guests (portrait slots 5-8); Frodo's half face
     * is against the right edge */
    { 0x7AF3913Bu, 5, 38.0f, 36.0f, 46.0f, 0 },     /* Boromir */
    { 0x235FD238u, 6, 58.0f, 42.0f, 52.0f, 0 },     /* Gandalf */
    { 0x1FE8F9FBu, 7, 52.0f, 38.0f, 48.0f, 1 },     /* Frodo */
    { 0x09D8665Au, 8, 50.0f, 40.0f, 50.0f, 0 },     /* Lurtz */
};
#define N_ATLAS ((int)(sizeof s_atlas / sizeof s_atlas[0]))

static uint32_t tex_bilerp(const uint32_t *p, int w, int h, float x, float y)
{
    int x0 = (int)floorf(x), y0 = (int)floorf(y), k;
    float fx = x - x0, fy = y - y0, acc[3] = { 0, 0, 0 };
    for (k = 0; k < 4; k++) {
        int xx = x0 + (k & 1), yy = y0 + (k >> 1);
        float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
        uint32_t c;
        if (xx < 0) xx = 0;
        if (yy < 0) yy = 0;
        if (xx >= w) xx = w - 1;
        if (yy >= h) yy = h - 1;
        c = p[yy * w + xx];
        acc[0] += wgt * ((c >> 16) & 255); acc[1] += wgt * ((c >> 8) & 255); acc[2] += wgt * (c & 255);
    }
    return ((uint32_t)acc[0] << 16) | ((uint32_t)acc[1] << 8) | (uint32_t)acc[2];
}

/* round medallion: picture inside, a silver rim */
static void medallion(uint32_t *out, uint32_t (*sample)(const void *ctx, float u, float v), const void *ctx)
{
    const float R = FACE_PX * 0.5f, rim = FACE_PX / 22.0f;
    int x, y;
    for (y = 0; y < FACE_PX; y++)
        for (x = 0; x < FACE_PX; x++) {
            float dx = x + 0.5f - R, dy = y + 0.5f - R, d = sqrtf(dx * dx + dy * dy);
            float a = R - d;
            uint32_t c;
            out[y * FACE_PX + x] = 0;
            if (a <= 0) continue;
            if (a > 1) a = 1;
            if (d < R - rim) {
                float k = 1.0f - 0.25f * (d / (R - rim)) * (d / (R - rim));   /* soft vignette */
                c = shade(sample(ctx, dx / (R - rim), dy / (R - rim)), k);
            } else {
                float t = 0.65f - 0.25f * dy / R;
                uint32_t v = (uint32_t)(t * 255);
                c = (v << 16) | (v << 8) | (v + 8 > 255 ? 255 : v + 8);
            }
            out[y * FACE_PX + x] = ((uint32_t)(a * 255) << 24) | c;
        }
}

typedef struct { const uint32_t *px; int w, h, i; } AtlasCtx;
static uint32_t atlas_sample(const void *ctx, float u, float v)
{
    const AtlasCtx *c = (const AtlasCtx *)ctx;
    float x = fabsf(u) * s_atlas[c->i].r, y = s_atlas[c->i].cy + v * s_atlas[c->i].r;
    if (x > s_atlas[c->i].halfw - 1) x = s_atlas[c->i].halfw - 1;
    if (s_atlas[c->i].right) x = (float)(c->w - 1) - x;
    return tex_bilerp(c->px, c->w, c->h, x, y);
}

static void atlas_seen(int i, const uint32_t *argb, int w, int h)
{
    int hero = s_atlas[i].hero;
    uint32_t *f;
    AtlasCtx c;
    if (s_face_ver[hero] >= 1000) return;       /* already from the model */
    f = (uint32_t *)malloc(FACE_PX * FACE_PX * 4);
    if (!f) return;
    c.px = argb; c.w = w; c.h = h; c.i = i;
    medallion(f, atlas_sample, &c);
    {
        uint32_t *old = s_face[hero];
        s_face[hero] = f;
        s_face_ver[hero] = 1000;
        s_face_tried[hero] = 1;
        (void)old;                               /* may still be uploading: kept */
    }
    tt_dbg("[E3HUD] portrait for hero %d from the model's face texture\n", hero);
}

/* ---- the sword's cross-guard, curved as on the demo's HUD ----
 * In the 256x64 sword texture the guard is a straight bar at x 56-67,
 * y 2-61, crossing the grip at y 23-41. Its two arms are erased and drawn
 * again as arcs bending forward toward the blade, with round ends. */
static const uint32_t s_sword_tex[] = { 0x9E0A4B1Au, 0xC3FD4D11u };

static void guard_redraw(uint32_t *a, int w, int h)
{
    double sr = 0, sg = 0, sb = 0, sn = 0;
    int x, y, k;
    if (w != 256 || h != 64) return;
    for (y = 4; y <= 20; y++)                     /* the metal's colour */
        for (x = 57; x <= 65; x++) {
            uint32_t c = a[y * w + x];
            double al = (c >> 24) / 255.0;
            sr += al * ((c >> 16) & 255); sg += al * ((c >> 8) & 255); sb += al * (c & 255); sn += al;
        }
    if (sn < 1) return;
    sr /= sn; sg /= sn; sb /= sn;
    /* the demo's guard is bright steel: lift the retail texture's dark
     * gunmetal to it, keeping its tint */
    {
        double lum = (sr + sg + sb) / 3.0, want = 175.0;
        if (lum < want && lum > 1) { sr *= want / lum; sg *= want / lum; sb *= want / lum; }
    }
    for (y = 0; y < h; y++)
        for (x = 52; x <= 70; x++)
            if (y < 23 || y > 41) a[y * w + x] = 0;
    for (y = 0; y < h; y++)
        for (x = 52; x <= 76; x++) {
            /* nearest point on either arm */
            float best = 1e9f, bs = 0, side = 0;
            int arm;
            for (arm = 0; arm < 2; arm++)
                for (k = 0; k <= 48; k++) {
                    float s = k / 48.0f;
                    float cx = 61.5f + 7.0f * s * s, cy = arm ? 40.0f + 20.0f * s : 24.0f - 21.0f * s;
                    float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
                    float hw = 2.6f - 0.9f * s + (s > 0.92f ? 0.9f : 0.0f);   /* taper, round knob */
                    float e = d - hw;
                    if (e < best) { best = e; bs = hw; side = dx; }
                }
            if (best < 0.7f) {
                float al = best < -0.3f ? 1.0f : (0.7f - best);
                float t = 1.0f - (best + bs) / (bs + 0.01f);           /* 1 on the centre line */
                float kk = 0.55f + 0.6f * t + (side < 0 ? 0.12f : -0.08f);
                float r = (float)sr * kk, g = (float)sg * kk, b = (float)sb * kk;
                uint32_t d0 = a[y * w + x], da = d0 >> 24;
                uint32_t na;
                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;
                if (al > 1) al = 1;
                na = (uint32_t)(al * 255);
                if (na > da || (y < 23 || y > 41))
                    a[y * w + x] = ((na > da ? na : da) << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
}

/* ---- the health vial: green glass to the pre-release orange ---- */
/* Hashes of the power-up's green textures. Not known yet for every level:
 * [Mods] e3_pickup_textures=HASH,HASH,... (hex, as RECOMP_TEX_DUMP names
 * the files) adds to them. */
#define N_PICKUP_TEX 16
static uint32_t s_pickup_tex[N_PICKUP_TEX];
static void pickup_list_init(void)
{
    static int done;
    const char *e;
    int n = 0;
    if (done) return;
    done = 1;
    /* the health vial's texture (64x64: green glass panes in a metal frame) */
    s_pickup_tex[n++] = 0x71C950C7u;
    e = getenv("TT_E3PICKUP_TEX");
    while (e && *e && n < N_PICKUP_TEX) {
        char *end;
        unsigned long v = strtoul(e, &end, 16);
        if (end == e) break;
        if (v) s_pickup_tex[n++] = (uint32_t)v;
        e = *end ? end + 1 : end;
    }
}

static uint32_t gold(uint32_t c)
{
    float a = (float)(c >> 24), r = (float)((c >> 16) & 255) / 255.0f, g = (float)((c >> 8) & 255) / 255.0f,
          b = (float)(c & 255) / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn, hh, sat, v = mx;
    if (d < 0.02f || mx <= 0.0f) return c;
    if (mx == r) hh = 60.0f * ((g - b) / d); else if (mx == g) hh = 60.0f * ((b - r) / d) + 120.0f;
    else hh = 60.0f * ((r - g) / d) + 240.0f;
    if (hh < 0) hh += 360.0f;
    if (hh < 60.0f || hh > 185.0f) return c;      /* only the greens */
    sat = d / mx * 1.15f;
    if (sat > 1.0f) sat = 1.0f;
    hh = 34.0f + (hh - 120.0f) * 0.12f;           /* the pre-release footage's orange */
    {
        float h6 = hh / 60.0f, f, p, q, t, R, G, B;
        int ii = (int)h6;
        f = h6 - (float)ii; p = v * (1 - sat); q = v * (1 - sat * f); t = v * (1 - sat * (1 - f));
        switch (ii) { case 0: R = v; G = t; B = p; break; case 1: R = q; G = v; B = p; break;
                      default: R = v; G = p; B = q; break; }
        return ((uint32_t)a << 24) | ((uint32_t)(R * 255 + 0.5f) << 16) | ((uint32_t)(G * 255 + 0.5f) << 8)
               | (uint32_t)(B * 255 + 0.5f);
    }
}

/* Level 10's progress bar ends in a hooded portrait in a spiked medallion
 * (0xBB82F059). The demo had a framed picture of the figure instead; with
 * e3hud\level10.bmp installed it replaces the texture, fitted to its height
 * and placed against the texture's left edge, where the bar ends, so bar and
 * picture meet with no gap (the retail medallion filled the whole width; a
 * centred picture left the bar's square end hanging in the air). The rest is
 * transparent. */
#define TEX_L10_PORTRAIT 0xBB82F059u
static void l10_icon(uint32_t *a, int w, int h)
{
    static int tried;
    static uint32_t *ic;
    static int iw, ih;
    int x, y, dw;
    float sc;
    if (!tried) { tried = 1; ic = icon_load_named("level10", &iw, &ih); }
    if (!ic || iw <= 0 || ih <= 0) return;
    s_l10_icon_on = 1;                          /* tt_e3hud_predraw lowers it onto the bar */
    sc = (float)ih / (float)h;                  /* texture px -> icon px */
    dw = (int)(iw / sc + 0.5f);
    if (dw > w) { dw = w; sc = (float)iw / (float)w; }
    memset(a, 0, (size_t)w * h * 4);
    for (y = 0; y < h; y++)
        for (x = 0; x < dw; x++) {
            /* bilinear from the icon */
            float fx = (x + 0.5f) * sc - 0.5f, fy = (y + 0.5f) * sc - 0.5f;
            int x0 = (int)floorf(fx), y0 = (int)floorf(fy), c, k;
            float tx = fx - x0, ty = fy - y0;
            uint32_t out = 0;
            for (c = 0; c < 4; c++) {
                float v = 0;
                for (k = 0; k < 4; k++) {
                    int sx = x0 + (k & 1), sy = y0 + (k >> 1);
                    float wt = ((k & 1) ? tx : 1 - tx) * ((k >> 1) ? ty : 1 - ty);
                    if (sx < 0) sx = 0;
                    if (sy < 0) sy = 0;
                    if (sx >= iw) sx = iw - 1;
                    if (sy >= ih) sy = ih - 1;
                    v += wt * (float)((ic[sy * iw + sx] >> (c * 8)) & 255);
                }
                out |= (uint32_t)(v + 0.5f > 255 ? 255 : v + 0.5f) << (c * 8);
            }
            a[y * w + x] = out;
        }
}

/* The cave troll's health bar (Balin's Tomb): the demo's instead -- a long
 * blade with a green fill ending in the troll's head -- from
 * e3hud\boss_troll_bar.bmp (the bar, 256x64, laid out as the retail one:
 * its fill shows through the same channel, so the game's green fill drains
 * as before) and e3hud\boss_troll_face.bmp (the head, 64x64). The bright
 * copies the game flashes as the troll is struck are lightened ones. */
#define TEX_TROLL_BAR    0x794B3A29u
#define TEX_TROLL_BAR_HI 0xA3A96BBBu
#define TEX_TROLL_FACE   0xF52A1E34u
#define TEX_TROLL_FACE_HI 0x0B4B0DE4u
static void boss_tex(const char *name, int bright, uint32_t *a, int w, int h)
{
    static struct { const char *n; uint32_t *px; int w, h, tried; } c[2] = { { "boss_troll_bar" }, { "boss_troll_face" } };
    int k = strcmp(name, c[0].n) ? 1 : 0, i;
    if (!c[k].tried) { c[k].tried = 1; c[k].px = icon_load_named(c[k].n, &c[k].w, &c[k].h); }
    if (!c[k].px || c[k].w != w || c[k].h != h) return;
    for (i = 0; i < w * h; i++) {
        uint32_t p = c[k].px[i];
        if (bright) {
            uint32_t r = ((p >> 16) & 255) * 55 / 100 + 115, g = ((p >> 8) & 255) * 55 / 100 + 115, b = (p & 255) * 55 / 100 + 115;
            p = (p & 0xFF000000u) | r << 16 | g << 8 | b;
        }
        a[i] = p;
    }
}

static void e3_tex(uint32_t hash, uint32_t *argb, int w, int h)
{
    int i;
    pickup_list_init();
    prompts_tex(hash, argb, w, h);
    for (i = 0; i < N_ATLAS; i++)
        if (hash == s_atlas[i].hash && w == 256 && h == 256) atlas_seen(i, argb, w, h);
    if (xbox_GfxGet(GFX_E3HUD) && hash == TEX_LOAD_TIP && w == 256 && h == 256) load_tip_edit(argb, w, h);
    if (xbox_GfxGet(GFX_E3HUD) && hash == TEX_SKILL_TIP && w == 256 && h == 256) skill_tip_edit(argb, w, h);
    if (xbox_GfxGet(GFX_E3HUD) && hash == TEX_L10_PORTRAIT) l10_icon(argb, w, h);
    if (xbox_GfxGet(GFX_E3HUD) && (hash == TEX_TROLL_BAR || hash == TEX_TROLL_BAR_HI)) boss_tex("boss_troll_bar", hash == TEX_TROLL_BAR_HI, argb, w, h);
    if (xbox_GfxGet(GFX_E3HUD) && (hash == TEX_TROLL_FACE || hash == TEX_TROLL_FACE_HI)) boss_tex("boss_troll_face", hash == TEX_TROLL_FACE_HI, argb, w, h);
    /* the retail gauge's ornate top, frame + silver medallion, and colour
     * ring: hidden in play by switching their HUD elements off, but the
     * mission results screen draws them itself, so they are cleared here
     * and the demo's gauge is drawn over that screen (e3_overlay) */
    if (xbox_GfxGet(GFX_E3HUD) && (hash == TEX_THERMO || hash == TEX_GAUGE_TOP || hash == TEX_GAUGE_RING))
        memset(argb, 0, (size_t)w * h * 4);
    if (xbox_GfxGet(GFX_E3HUD))
        for (i = 0; i < (int)(sizeof s_sword_tex / sizeof s_sword_tex[0]); i++)
            if (hash == s_sword_tex[i]) guard_redraw(argb, w, h);
    if (xbox_GfxGet(GFX_E3PICKUPS))
        for (i = 0; i < N_PICKUP_TEX; i++)
            if (s_pickup_tex[i] && hash == s_pickup_tex[i]) {
                int n = w * h, j;
                /* the vial texture's right strip (x >= 46) is an olive coin,
                 * not glass: left as it is */
                int xmax = (hash == 0x71C950C7u && w == 64) ? 46 : w;
                for (j = 0; j < n; j++)
                    if (j % w < xmax) argb[j] = gold(argb[j]);
            }
}
