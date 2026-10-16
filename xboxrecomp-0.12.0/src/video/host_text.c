/* host_text.c -- see host_text.h. stb_truetype does the rasterising. */
#if !defined(_WIN32)
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <pthread.h>
#include "host_text.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"

typedef struct {
    int tried, ok;
    unsigned char *data;
    stbtt_fontinfo info;
} HFont;

static HFont s_fonts[XFONT_N];
static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *const s_files[XFONT_N][6] = {
    /* the bundled file first, then look-alikes a Linux system often has */
    { "Carlito-Bold.ttf", "/usr/share/fonts/truetype/crosextra/Carlito-Bold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf" },
    { "texgyrepagella-regular.otf", "/usr/share/texmf/fonts/opentype/public/tex-gyre/texgyrepagella-regular.otf",
      "/usr/share/fonts/opentype/urw-base35/P052-Roman.otf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
      "/usr/share/fonts/TTF/DejaVuSerif.ttf", "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf" },
    { "texgyrepagella-bold.otf", "/usr/share/texmf/fonts/opentype/public/tex-gyre/texgyrepagella-bold.otf",
      "/usr/share/fonts/opentype/urw-base35/P052-Bold.otf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
      "/usr/share/fonts/TTF/DejaVuSerif-Bold.ttf", "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf" },
};

/* (other scripts: a glyph the style's font lacks comes from the first of
 * these that has it -- fonts/fallback*.ttf beside the program first) */
#define FB_N 16
static const char *const s_fb_files[FB_N] = {
    "fallback.ttf", "fallback.otf", "fallback.ttc",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/noto/NotoSansThai-Regular.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf", "/usr/share/fonts/truetype/freefont/FreeSerif.ttf",
    "/usr/share/fonts/gnu-free/FreeSerif.otf", "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "/Library/Fonts/Arial Unicode.ttf", "/System/Library/Fonts/Hiragino Sans GB.ttc", "/System/Library/Fonts/AppleSDGothicNeo.ttc",
};
static HFont s_fb[FB_N];

static unsigned char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    unsigned char *b;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (64l << 20)) { fclose(f); return NULL; }
    b = (unsigned char *)malloc((size_t)n);
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    return b;
}

static HFont *font_get(int font)
{
    HFont *h;
    if (font < 0 || font >= XFONT_N) return NULL;
    h = &s_fonts[font];
    if (!h->tried) {
        char exe[PATH_MAX], path[PATH_MAX + 64];
#if defined(__APPLE__)
        extern int _NSGetExecutablePath(char *buf, uint32_t *bufsize);
        uint32_t es = sizeof exe;
        ssize_t n = _NSGetExecutablePath(exe, &es) == 0 ? (ssize_t)strlen(exe) : -1;
#else
        ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
#endif
        int i;
        h->tried = 1;
        if (n > 0) {
            char *sl;
            exe[n] = 0;
            sl = strrchr(exe, '/');
            if (sl) *sl = 0;
        } else exe[0] = 0;
        for (i = 0; i < 6 && !h->ok; i++) {
            const char *f = s_files[font][i];
            if (!f) continue;
            if (f[0] == '/') snprintf(path, sizeof path, "%s", f);
            else snprintf(path, sizeof path, "%s/fonts/%s", exe, f);
            h->data = read_file(path);
            if (!h->data) continue;
            if (stbtt_InitFont(&h->info, h->data, stbtt_GetFontOffsetForIndex(h->data, 0))) {
                h->ok = 1;
                fprintf(stderr, "  [TEXT] font %d: %s\n", font, path);
            } else {
                free(h->data);
                h->data = NULL;
            }
        }
        if (!h->ok) fprintf(stderr, "  [TEXT] no font found for style %d (text will be missing)\n", font);
    }
    return h->ok ? h : NULL;
}

static void exe_dir(char *exe, size_t n0)
{
#if defined(__APPLE__)
    extern int _NSGetExecutablePath(char *buf, uint32_t *bufsize);
    uint32_t es = (uint32_t)n0;
    ssize_t n = _NSGetExecutablePath(exe, &es) == 0 ? (ssize_t)strlen(exe) : -1;
#else
    ssize_t n = readlink("/proc/self/exe", exe, n0 - 1);
#endif
    if (n > 0) { char *sl; exe[n] = 0; sl = strrchr(exe, '/'); if (sl) *sl = 0; }
    else exe[0] = 0;
}
/* the fallback font that has code point cp, its glyph in *g */
static HFont *fb_for(int cp, int *g)
{
    int i;
    for (i = 0; i < FB_N; i++) {
        HFont *h = &s_fb[i];
        if (!h->tried) {
            char exe[PATH_MAX], path[PATH_MAX + 64];
            const char *f = s_fb_files[i];
            h->tried = 1;
            if (f[0] == '/') snprintf(path, sizeof path, "%s", f);
            else { exe_dir(exe, sizeof exe); snprintf(path, sizeof path, "%s/fonts/%s", exe, f); }
            h->data = read_file(path);
            if (h->data && stbtt_InitFont(&h->info, h->data, stbtt_GetFontOffsetForIndex(h->data, 0))) h->ok = 1;
            else { free(h->data); h->data = NULL; }
        }
        if (h->ok && (*g = stbtt_FindGlyphIndex(&h->info, cp)) != 0) return h;
    }
    return NULL;
}

/* next code point of UTF-8 text (Latin-1 bytes pass through) */
static int next_cp(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    int c = *s++;
    if (c >= 0xC0 && c < 0xE0 && (*s & 0xC0) == 0x80) { c = ((c & 0x1F) << 6) | (*s++ & 0x3F); }
    else if (c >= 0xE0 && c < 0xF0 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((s[0] & 0x3F) << 6) | (s[1] & 0x3F); s += 2;
    } else if (c >= 0xF0 && c < 0xF8 && (s[0] & 0xC0) == 0x80 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        c = ((c & 0x07) << 18) | ((s[0] & 0x3F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); s += 3;
    }
    *ps = (const char *)s;
    return c;
}

static int text_run(uint8_t *cov, int W, int H, int x, int y, int font, int px, const char *s)
{
    HFont *h;
    float sc, pen;
    int asc, desc, gap, prev = 0, baseline;
    pthread_mutex_lock(&s_lock);
    h = font_get(font);
    if (!h || !s || px <= 0) { pthread_mutex_unlock(&s_lock); return 0; }
    sc = stbtt_ScaleForMappingEmToPixels(&h->info, (float)px);
    stbtt_GetFontVMetrics(&h->info, &asc, &desc, &gap);
    baseline = y + (int)(asc * sc + 0.5f);
    pen = (float)x;
    while (*s) {
        int cp = next_cp(&s), g = stbtt_FindGlyphIndex(&h->info, cp), adv, lsb;
        HFont *hm = h;
        float scm = sc;
        if (!g && cp > 0x7F) {   /* another font's glyph, on the same baseline */
            int g2;
            HFont *f2 = fb_for(cp, &g2);
            if (f2) { hm = f2; g = g2; scm = stbtt_ScaleForMappingEmToPixels(&f2->info, (float)px); }
        }
        if (prev && hm == h) pen += sc * stbtt_GetGlyphKernAdvance(&h->info, prev, g);
        stbtt_GetGlyphHMetrics(&hm->info, g, &adv, &lsb);
        if (cov) {
            int x0, y0, x1, y1, gw, gh, i, j;
            float sub = pen - (float)(int)pen;
            unsigned char *bm;
            stbtt_GetGlyphBitmapBoxSubpixel(&hm->info, g, scm, scm, sub, 0, &x0, &y0, &x1, &y1);
            gw = x1 - x0; gh = y1 - y0;
            if (gw > 0 && gh > 0 && gw < 4096 && gh < 4096) {
                bm = (unsigned char *)malloc((size_t)gw * gh);
                if (bm) {
                    stbtt_MakeGlyphBitmapSubpixel(&hm->info, bm, gw, gh, gw, scm, scm, sub, 0, g);
                    for (j = 0; j < gh; j++) {
                        int yy = baseline + y0 + j;
                        if (yy < 0 || yy >= H) continue;
                        for (i = 0; i < gw; i++) {
                            int xx = (int)pen + x0 + i;
                            uint8_t v = bm[j * gw + i];
                            if (xx < 0 || xx >= W || !v) continue;
                            if (v > cov[(size_t)yy * W + xx]) cov[(size_t)yy * W + xx] = v;
                        }
                    }
                    free(bm);
                }
            }
        }
        pen += adv * scm;
        prev = hm == h ? g : 0;
    }
    pthread_mutex_unlock(&s_lock);
    return (int)(pen - (float)x + 0.5f);
}

int xbox_TextWidth(int font, int px, const char *s) { return text_run(NULL, 0, 0, 0, 0, font, px, s); }

int xbox_TextLineHeight(int font, int px)
{
    HFont *h;
    int asc, desc, gap, r = 0;
    pthread_mutex_lock(&s_lock);
    h = font_get(font);
    if (h) {
        float sc = stbtt_ScaleForMappingEmToPixels(&h->info, (float)px);
        stbtt_GetFontVMetrics(&h->info, &asc, &desc, &gap);
        r = (int)((asc - desc) * sc + 0.5f);
    }
    pthread_mutex_unlock(&s_lock);
    return r;
}

int xbox_TextDraw(uint8_t *cov, int W, int H, int x, int y, int font, int px, const char *s)
{
    return text_run(cov, W, H, x, y, font, px, s);
}
#endif
