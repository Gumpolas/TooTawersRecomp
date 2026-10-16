/* e3hud_face.inc -- the portrait medallion, cut at run time from the
 * character's picture on the upgrade screen (UI\upgbig?.BFF in the user's
 * game files: a 512x256 DXT1 image at offset 0x1000). */

static void dxt1_decode(const uint8_t *src, int w, int h, uint32_t *out)
{
    int bx, by, i;
    for (by = 0; by < h / 4; by++)
        for (bx = 0; bx < w / 4; bx++, src += 8) {
            uint32_t c0 = src[0] | (src[1] << 8), c1 = src[2] | (src[3] << 8), pal[4];
            uint32_t bits = src[4] | (src[5] << 8) | (src[6] << 16) | ((uint32_t)src[7] << 24);
            int r[4], g[4], b[4];
            r[0] = ((c0 >> 11) & 31) * 255 / 31; g[0] = ((c0 >> 5) & 63) * 255 / 63; b[0] = (c0 & 31) * 255 / 31;
            r[1] = ((c1 >> 11) & 31) * 255 / 31; g[1] = ((c1 >> 5) & 63) * 255 / 63; b[1] = (c1 & 31) * 255 / 31;
            if (c0 > c1) {
                r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
                r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
            } else {
                r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; b[2] = (b[0] + b[1]) / 2;
                r[3] = g[3] = b[3] = 0;
            }
            for (i = 0; i < 4; i++) pal[i] = 0xFF000000u | (r[i] << 16) | (g[i] << 8) | b[i];
            for (i = 0; i < 16; i++)
                out[(by * 4 + i / 4) * w + bx * 4 + i % 4] = pal[(bits >> (2 * i)) & 3];
        }
}

static uint32_t bilerp(const uint32_t *p, int w, int h, float x, float y)
{
    int x0 = (int)floorf(x), y0 = (int)floorf(y), k;
    float fx = x - x0, fy = y - y0, acc[3] = { 0, 0, 0 };
    for (k = 0; k < 4; k++) {
        int xx = x0 + (k & 1), yy = y0 + (k >> 1);
        float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
        uint32_t c;
        if (xx < 0) xx = 0; if (yy < 0) yy = 0; if (xx >= w) xx = w - 1; if (yy >= h) yy = h - 1;
        c = p[yy * w + xx];
        acc[0] += wgt * ((c >> 16) & 255); acc[1] += wgt * ((c >> 8) & 255); acc[2] += wgt * (c & 255);
    }
    return ((uint32_t)acc[0] << 16) | ((uint32_t)acc[1] << 8) | (uint32_t)acc[2];
}

static uint32_t *face_build(int hero)
{
    /* file letter and the face's circle in the 512x256 picture */
    static const struct { char c; float x, y, r; } F[5] = {
        { 0 }, { 'a', 76, 112, 60 }, { 'l', 66, 110, 56 }, { 'g', 68, 106, 52 }, { 'i', 93, 94, 42 } };
    char rel[64];
    FILE *fh;
    size_t got = 0;
    uint8_t *raw;
    uint32_t *pic, *out;
    int x, y;
    if (hero < 1 || hero > 4) return NULL;
    snprintf(rel, sizeof rel, "UI/upgbig%c.BFF", F[hero].c);
    fh = tt_game_fopen(rel);
    if (!fh) { tt_dbg("[E3HUD] no portrait file %s\n", rel); return NULL; }
    raw = (uint8_t *)malloc(65536);
    if (raw && fseek(fh, 0x1000, SEEK_SET) == 0) got = fread(raw, 1, 65536, fh);
    fclose(fh);
    if (!raw || got != 65536) { free(raw); return NULL; }
    pic = (uint32_t *)malloc(512 * 256 * 4);
    out = (uint32_t *)calloc(FACE_PX * FACE_PX, 4);
    if (!pic || !out) { free(raw); free(pic); free(out); return NULL; }
    dxt1_decode(raw, 512, 256, pic);
    free(raw);
    {
        const float R = FACE_PX * 0.5f, rim = FACE_PX / 22.0f;     /* rim ~ 2 px on screen */
        for (y = 0; y < FACE_PX; y++)
            for (x = 0; x < FACE_PX; x++) {
                float dx = x + 0.5f - R, dy = y + 0.5f - R, d = sqrtf(dx * dx + dy * dy);
                float a = R - d;                                   /* outer edge, anti-aliased */
                uint32_t c;
                if (a <= 0) continue;
                if (a > 1) a = 1;
                if (d < R - rim) {
                    c = bilerp(pic, 512, 256, F[hero].x + dx / (R - rim) * F[hero].r,
                               F[hero].y + dy / (R - rim) * F[hero].r);
                } else {
                    /* silver rim, lighter at the top */
                    float t = 0.65f - 0.25f * dy / R;
                    uint32_t v = (uint32_t)(t * 255);
                    c = (v << 16) | (v << 8) | (v + 8 > 255 ? 255 : v + 8);
                }
                out[y * FACE_PX + x] = ((uint32_t)(a * 255) << 24) | c;
            }
    }
    free(pic);
    tt_dbg("[E3HUD] portrait for hero %d from %s\n", hero, rel);
    return out;
}

/* The demo's portrait icons: e3hud\aragorn.bmp, legolas.bmp, gimli.bmp,
 * isildur.bmp beside the exe, 32-bit BMPs with alpha. Optional: without
 * them the portrait is made from the character model's face texture. */
extern const char *tt_exe_dir(void);
extern FILE *tt_exe_fopen(const char *rel);
/* e3hud\<name>.bmp beside the exe (32-bit BMP), or NULL */
static uint32_t *icon_load_named(const char *name, int *ow, int *oh)
{
    char rel[MAX_PATH];
    FILE *f;
    unsigned char hd[54];
    int w, h, x, y, bottom_up;
    uint32_t off, *out;
    if (!name) return NULL;
    snprintf(rel, sizeof rel, "e3hud/%s.bmp", name);
    if (!(f = tt_exe_fopen(rel))) return NULL;
    if (fread(hd, 1, 54, f) != 54 || hd[0] != 'B' || hd[1] != 'M' || hd[28] != 32) { fclose(f); return NULL; }
    off = hd[10] | (hd[11] << 8) | (hd[12] << 16) | ((uint32_t)hd[13] << 24);
    w = (int)(hd[18] | (hd[19] << 8) | (hd[20] << 16) | ((uint32_t)hd[21] << 24));
    h = (int)(hd[22] | (hd[23] << 8) | (hd[24] << 16) | ((uint32_t)hd[25] << 24));
    bottom_up = h > 0;
    if (h < 0) h = -h;
    if (w <= 0 || w > 1024 || h <= 0 || h > 1024) { fclose(f); return NULL; }
    out = (uint32_t *)malloc((size_t)w * h * 4);
    if (!out) { fclose(f); return NULL; }
    fseek(f, (long)off, SEEK_SET);
    for (y = 0; y < h; y++) {
        uint32_t *row = out + (size_t)(bottom_up ? h - 1 - y : y) * w;
        for (x = 0; x < w; x++) {
            unsigned char px[4];
            if (fread(px, 1, 4, f) != 4) { free(out); fclose(f); return NULL; }
            row[x] = ((uint32_t)px[3] << 24) | ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | px[0];
        }
    }
    fclose(f);
    *ow = w; *oh = h;
    tt_dbg("[E3HUD] icon %s (%dx%d)\n", rel, w, h);
    return out;
}

static uint32_t *icon_load(int hero, int *ow, int *oh)
{
    static const char *nm[5] = { "", "aragorn", "legolas", "gimli", "isildur" };
    if (hero < 1 || hero > 4) return NULL;
    return icon_load_named(nm[hero], ow, oh);
}
