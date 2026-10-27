/* lang.c -- the game in other languages (1.9.2).
 *
 * The launcher's Language setting ([Language] language in
 * twotowers_recomp.ini, a folder name under lang\ beside the program) picks a
 * language pack: lang\<code>\
 *   info.txt    name=, rtl=0|1
 *   game.txt    key <TAB> text, the text written with the game's own character
 *               codes (made by tools/lang/build_lang.py): what the game's fonts draw
 *   text.txt    key <TAB> text, plain Unicode: what the port draws with its own
 *               fonts (the F10 settings, the launcher)
 *   glyphs.bin  the letters the game's fonts do not have (below)
 * Keys: "G" + eight hex digits, the FNV-1a hash of a game string's English
 * text (its UTF-16 units), so no game text is stored; "P:" + English, the
 * port's own words.
 *
 * The game's text lives in its files: string tables (OBSS, resource types
 * 900-902: startup.BFF -- menus, hints, upgrades; credits.BFF), timed subtitle
 * tables (OBST, 905-907: every level's lines, the main menu's film clips) and
 * fonts (XF, type 800 -- four: 22, 26, 40 and 30 pixels a line). As the game
 * opens such a file, a copy is made for the language (lang_cache\<code>\, made
 * once): every table rebuilt with the language's text (any length: the
 * strings' offsets are the table's own), and the fonts given the letters the
 * language needs.
 *
 * A font (XF): +8 u16 ranges, +0xA u16 line height, +0xC the ranges'
 * offset, +0x10 the texture's (both from the font's start); a range is
 * {u32 count, u32 first character, u32 offset of its records}; a record
 * {u32 x0, y0, x1, y1, u16 advance, u16 height}: a cell of the texture, a
 * line high (the game looks a character up in the ranges, 0x0003F510). The
 * texture: the game's 0x80-byte "xb" header (+4 width, +8 height, +0x30 the
 * pixels' offset from it, +0x38 the D3D format word, +0x40 the size), then
 * A8 pixels, swizzled. Letters added: one range from 0xE000 (the pack's
 * codes for them), cells packed below the font's own in a taller texture.
 *
 * glyphs.bin: "TTG1", u32 slots; per slot u32 line height, u32 count, then
 * per glyph u16 code, u8 kind, u8 0:
 *   kind 0 (a drawn letter): u16 advance, u16 width, width x height bytes
 *   kind 1 (a game letter with marks): u16 base code, u8 marks; per mark
 *     u8 place, s8 dx, s8 dy, u8 w, u8 h, w x h bytes
 *   kind 2 (an advance changed): u16 advance
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../../xboxrecomp-0.12.0/src/kernel/kernel.h"

extern ptrdiff_t xbox_GetMemoryOffset(void);
static char *dupstr(const char *t) { size_t n = strlen(t) + 1; char *d = (char *)malloc(n); if (d) memcpy(d, t, n); return d; }
#define G16(a) (*(volatile uint16_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
extern void tt_dbg(const char *fmt, ...);
extern const char *(*xbox_path_redirect)(const char *xbox_path);

static char s_dir[600], s_code[32] = "en", s_pack[700];
static int s_on = -1, s_rtl;
static char s_subs[32], s_cache[70];
static int s_subrtl, s_sub_fallback, s_build_bad;
static unsigned long s_substamp;
static uint8_t s_direction[65536]; /* 1 RTL; 2 LTR, por código renumerado */
static unsigned long s_stamp;      /* the pack's files, as a number: a new pack makes new copies */

/* ===================================================================== */
/* the pack                                                               */
/* ===================================================================== */
typedef struct { char *key; uint16_t *v; int n; } Str16;
typedef struct { char *key; char *v; } Str8;
static Str16 *s_g; static int s_ng;
static Str16 *s_subg; static int s_nsubg;
static Str8 *s_t; static int s_nt;

static unsigned char *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long l;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
    if (l < 0 || !(b = (unsigned char *)malloc((size_t)l + 1))) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)l, f) != (size_t)l) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[l] = 0;
    *n = (size_t)l;
    return b;
}
/* "\n", "\t", "\\" in the files */
static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '\\' && s[1]) { s++; *o++ = *s == 'n' ? '\n' : *s == 't' ? '\t' : *s == 'r' ? '\r' : *s; }
        else *o++ = *s;
    }
    *o = 0;
}
static int utf8_to16(const char *s, uint16_t *o, int max)
{
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < max) {
        uint32_t c = *p++;
        if (c >= 0xF0 && p[0] && p[1] && p[2]) { c = ((c & 7) << 18) | ((p[0] & 63u) << 12) | ((p[1] & 63u) << 6) | (p[2] & 63u); p += 3; }
        else if (c >= 0xE0 && p[0] && p[1]) { c = ((c & 15) << 12) | ((p[0] & 63u) << 6) | (p[1] & 63u); p += 2; }
        else if (c >= 0xC0 && p[0]) { c = ((c & 31) << 6) | (p[0] & 63u); p += 1; }
        o[n++] = c > 0xFFFF ? '?' : (uint16_t)c;
    }
    return n;
}
static int cmp_g(const void *a, const void *b) { return strcmp(((const Str16 *)a)->key, ((const Str16 *)b)->key); }
static int cmp_t(const void *a, const void *b) { return strcmp(((const Str8 *)a)->key, ((const Str8 *)b)->key); }
/* key <TAB> value lines */
static void load_lines(const char *path, int game)
{
    size_t n;
    unsigned char *b = read_all(path, &n);
    char *p, *e;
    int cap = 0;
    if (!b) return;
    for (p = (char *)b; p && *p; p = e) {
        char *tab, *ln = p;
        e = strchr(p, '\n');
        if (e) *e++ = 0;
        if (ln[0] && ln[strlen(ln) - 1] == '\r') ln[strlen(ln) - 1] = 0;
        if (!ln[0] || ln[0] == '#' || !(tab = strchr(ln, '\t'))) continue;
        *tab++ = 0;
        unescape(tab);
        if (game) {
            uint16_t tmp[4096];
            int m = utf8_to16(tab, tmp, 4095);
            if (s_ng >= cap) { cap = cap ? cap * 2 : 2048; s_g = (Str16 *)realloc(s_g, sizeof *s_g * (size_t)cap); }
            s_g[s_ng].key = dupstr(ln);
            s_g[s_ng].v = (uint16_t *)malloc(2 * (size_t)(m + 1));
            memcpy(s_g[s_ng].v, tmp, 2 * (size_t)m); s_g[s_ng].v[m] = 0;
            s_g[s_ng].n = m;
            s_ng++;
        } else {
            if (s_nt >= cap) { cap = cap ? cap * 2 : 2048; s_t = (Str8 *)realloc(s_t, sizeof *s_t * (size_t)cap); }
            s_t[s_nt].key = dupstr(ln); s_t[s_nt].v = dupstr(tab); s_nt++;
        }
    }
    free(b);
    if (game) qsort(s_g, (size_t)s_ng, sizeof *s_g, cmp_g);
    else qsort(s_t, (size_t)s_nt, sizeof *s_t, cmp_t);
}
static const Str16 *find_g(const char *key)
{
    Str16 k; k.key = (char *)key;
    return s_ng ? (const Str16 *)bsearch(&k, s_g, (size_t)s_ng, sizeof *s_g, cmp_g) : NULL;
}
static unsigned long file_stamp(const char *p)
{
    struct stat st;
    if (stat(p, &st)) return 0;
    return (unsigned long)st.st_size * 2654435761u + (unsigned long)st.st_mtime;
}

/* the glyphs: by slot (line height) */
typedef struct { uint16_t code; uint8_t kind; uint16_t adv, w, base; int nm; const uint8_t *data; } Glyph;
typedef struct { int lh, n; Glyph *g; } Slot;
static Slot s_slot[8];
static int s_nslot;
static unsigned char *s_gbin;

static void load_glyphs(const char *path)
{
    size_t n, o = 8;
    unsigned char *b = read_all(path, &n);
    uint32_t ns, i, k;
    if (!b || n < 8 || memcmp(b, "TTG1", 4)) { free(b); return; }
    s_gbin = b;
    memcpy(&ns, b + 4, 4);
    for (i = 0; i < ns && i < 8 && o + 8 <= n; i++) {
        Slot *s = &s_slot[s_nslot++];
        uint32_t lh, cnt;
        memcpy(&lh, b + o, 4); memcpy(&cnt, b + o + 4, 4); o += 8;
        s->lh = (int)lh; s->n = 0;
        s->g = (Glyph *)calloc(cnt ? cnt : 1, sizeof *s->g);
        for (k = 0; k < cnt && o + 4 <= n; k++) {
            Glyph *g = &s->g[s->n];
            uint16_t c; memcpy(&c, b + o, 2);
            g->code = c; g->kind = b[o + 2]; o += 4;
            if (g->kind == 0) {
                memcpy(&g->adv, b + o, 2); memcpy(&g->w, b + o + 2, 2); o += 4;
                g->data = b + o; o += (size_t)g->w * lh;
            } else if (g->kind == 1) {
                int m;
                memcpy(&g->base, b + o, 2); g->nm = b[o + 2]; o += 3;
                g->data = b + o;
                for (m = 0; m < g->nm && o + 5 <= n; m++) o += 5 + (size_t)b[o + 3] * b[o + 4];
            } else if (g->kind == 2) {
                memcpy(&g->adv, b + o, 2); o += 2;
            }
            if (o > n) break;
            s->n++;
        }
    }
}

/* glyphs4.bin: "TTG4", u32 K, u32 size, then zlib of: u32 slots; per slot
 * u32 line height (x K), u32 count; per letter u16 code, u8 kind, and
 *   kind 0: u8 0, u16 width, width x height alpha (drawn K times the size)
 *   kind 1: u8 marks; per mark u8 place, i16 dx, i16 dy, u16 w, u16 h, w x h
 * -- the letters K times the size, for the Ultra textures' fonts */
typedef struct { uint16_t code; uint8_t kind; int w, nm; const uint8_t *data; } Glyph4;
typedef struct { int lh, n; Glyph4 *g; } Slot4;
static Slot4 s_slot4[8];
static int s_nslot4, s_k4;
extern int tt_ul_inflate(const unsigned char *in, size_t n, unsigned char **out, size_t *outn);
extern int tt_ul_read_dds(const char *path, unsigned char **rgba, int *w, int *h);
extern int tt_ul_write_dds(const char *path, const unsigned char *rgba, int w, int h);
extern void xbox_UltraAddPair(uint32_t hash, const char *path);
static void load_glyphs4_path(const char *pack)
{
    char p[800];
    size_t n, un = 0, o = 4;
    unsigned char *b, *u = NULL;
    uint32_t ns, i, k, K;

    snprintf(p, sizeof p, "%s/glyphs4.bin", pack);
    b = read_all(p, &n);
    if (!b || n < 12 || memcmp(b, "TTG4", 4)) { free(b); return; }
    memcpy(&K, b + 4, 4);
    if (!tt_ul_inflate(b + 12, n - 12, &u, &un) || un < 4) { free(b); free(u); return; }
    free(b);
    memcpy(&ns, u, 4);
    for (i = 0; i < ns && i < 8 && o + 8 <= un; i++) {
        Slot4 *sl = &s_slot4[s_nslot4++];
        uint32_t lh, cnt;
        memcpy(&lh, u + o, 4); memcpy(&cnt, u + o + 4, 4); o += 8;
        sl->lh = (int)lh; sl->n = 0;
        sl->g = (Glyph4 *)calloc(cnt ? cnt : 1, sizeof *sl->g);
        for (k = 0; k < cnt && o + 4 <= un; k++) {
            Glyph4 *g = &sl->g[sl->n];
            memcpy(&g->code, u + o, 2); g->kind = u[o + 2];
            if (g->kind == 0) {
                uint16_t w; memcpy(&w, u + o + 4, 2); o += 6;
                g->w = w; g->data = u + o; o += (size_t)w * lh;
            } else {
                int m;
                g->nm = u[o + 3]; o += 4; g->data = u + o;
                for (m = 0; m < g->nm && o + 9 <= un; m++) { uint16_t mw, mh; memcpy(&mw, u + o + 5, 2); memcpy(&mh, u + o + 7, 2); o += 9 + (size_t)mw * mh; }
            }
            if (o > un) break;
            sl->n++;
        }
    }
    s_k4 = (int)K;
}
/* the Xbox A8 picture's hash as the renderer makes it (FNV-1a over its
 * texels as ARGB -- A8 is white with that alpha -- then the size): the
 * name of its upscaled picture in textures_ultra/ui */
static uint32_t a8_hash(const unsigned char *pix, uint32_t w, uint32_t h)
{
    uint32_t hh = 2166136261u, i, n = w * h;
    for (i = 0; i < n; i++) hh = (hh ^ (((uint32_t)pix[i] << 24) | 0x00FFFFFFu)) * 16777619u;
    return hh ^ ((w << 16) | h);
}
typedef struct { int k, x, y, cw; } Placed;
/* (the upscaled fonts a copy pairs: written beside it as <copy>.ultra, and
 * paired again from there when the copy is used another time) */
static char s_ulist[8192];
static size_t s_ulist_n;
static void ul_note(uint32_t nh, const char *rel)
{
    int w = snprintf(s_ulist + s_ulist_n, sizeof s_ulist - s_ulist_n, "%08X %s\n", nh, rel);
    if (w > 0 && s_ulist_n + (size_t)w < sizeof s_ulist) s_ulist_n += (size_t)w;
    xbox_UltraAddPair(nh, rel);
}
static uint32_t rd32(const unsigned char *b, size_t o);
/* (1.9.2) Ultra textures: the font with the language's letters, upscaled as
 * textures_ultra/ui upscales the game's own -- that picture, and the
 * letters drawn K times the size (glyphs4.bin) where the font's are put --
 * made once into lang_cache\<code>\ultra and paired with the texture */
static void ultra_font(const unsigned char *s, size_t a, uint32_t (*rng)[3], int nr, int slot,
                       const unsigned char *pix0, uint32_t w0, uint32_t h0, const unsigned char *pix, uint32_t W, uint32_t H,
                       const Placed *pl, int npl)
{
    char rel[200], full[900], ui[900];
    uint32_t oh = a8_hash(pix0, w0, h0), nh = a8_hash(pix, W, H);
    unsigned char *base = NULL, *cv = NULL;
    int bw = 0, bh = 0, q, lh = s_slot[slot].lh, i, s4 = -1;
    FILE *f;
    if (getenv("TT_LANG_NOULTRA")) return;
    snprintf(rel, sizeof rel, "lang_cache/%s/ultra/%08X.dds", s_cache, nh);
    snprintf(full, sizeof full, "%s/%s", s_dir, rel);
    if ((f = fopen(full, "rb"))) { fclose(f); ul_note(nh, rel); return; }
    snprintf(ui, sizeof ui, "%s/textures_ultra/ui/%08X.dds", s_dir, oh);
    if ((f = fopen(ui, "rb")) == NULL) { tt_dbg("[LANG] font %08X (%ux%u, %d high): no upscaled picture\n", oh, w0, h0, lh); return; }
    fclose(f);
    /* Los dos paquetes y sus códigos se fusionan durante la inicialización. */
    for (i = 0; i < s_nslot4; i++) if (s_slot4[i].lh == lh * s_k4) s4 = i;
    if (s4 < 0 || !tt_ul_read_dds(ui, &base, &bw, &bh)) { tt_dbg("[LANG] font %08X (%d high): no upscaled letters (%d)\n", oh, lh, s4); return; }
    q = bw / (int)w0;
    if (q != s_k4 || bw != q * (int)w0 || bh != q * (int)h0) { tt_dbg("[LANG] font %08X: upscaled %dx%d, not %d times %ux%u\n", oh, bw, bh, s_k4, w0, h0); free(base); return; }
    cv = (unsigned char *)malloc((size_t)W * q * H * q * 4);
    if (!cv) { free(base); return; }
    {
        size_t t, nn = (size_t)W * q * H * q;
        for (t = 0; t < nn; t++) { cv[t * 4] = cv[t * 4 + 1] = cv[t * 4 + 2] = 255; cv[t * 4 + 3] = 0; }
    }
    {
        int yy;
        for (yy = 0; yy < bh; yy++) memcpy(cv + ((size_t)yy * W * q) * 4, base + (size_t)yy * bw * 4, (size_t)bw * 4);
    }
    for (i = 0; i < npl; i++) {
        const Glyph *g = &s_slot[slot].g[pl[i].k];
        const Glyph4 *g4 = NULL;
        int j, L = lh * q, CW = pl[i].cw * q, row, col;
        unsigned char *cell;
        for (j = 0; j < s_slot4[s4].n; j++) if (s_slot4[s4].g[j].code == g->code) { g4 = &s_slot4[s4].g[j]; break; }
        if (!g4 || CW <= 0) continue;
        cell = (unsigned char *)calloc((size_t)CW * L, 1);
        if (!cell) continue;
        if (g4->kind == 0) {
            for (row = 0; row < L; row++) for (col = 0; col < CW && col < g4->w; col++) cell[row * CW + col] = g4->data[(size_t)row * g4->w + col];
        } else {
            /* the game's letter from the upscaled picture, and the marks K times the size */
            int bx0 = 0, by0 = 0, bx1 = -1, r, ink_t = L, ink_b = -1, ink_l = CW, ink_r = -1, top_now, m;
            const unsigned char *mp = g4->data;
            for (r = 0; r < nr; r++)
                if (g->base >= rng[r][1] && g->base < rng[r][1] + rng[r][0]) {
                    size_t ra = a + rng[r][2] + 20 * (g->base - rng[r][1]);
                    bx0 = (int)rd32(s, ra); by0 = (int)rd32(s, ra + 4); bx1 = (int)rd32(s, ra + 8);
                }
            {
                int bcw = (bx1 - bx0 + 1) * q;
                for (row = 0; row < L; row++)
                    for (col = 0; col < bcw && col < CW; col++) {
                        int sx = bx0 * q + col, sy = by0 * q + row;
                        unsigned char v = (sx < bw && sy < bh) ? base[((size_t)sy * bw + sx) * 4 + 3] : 0;
                        cell[row * CW + col] = v;
                        if (v > 96) { if (row < ink_t) ink_t = row; if (row > ink_b) ink_b = row; if (col < ink_l) ink_l = col; if (col > ink_r) ink_r = col; }
                    }
                if (ink_r < 0) { ink_l = 0; ink_r = bcw - 1; ink_t = L / 3; ink_b = L * 3 / 4; }
            }
            top_now = ink_t;
            for (m = 0; m < g4->nm; m++) {
                int place = mp[0], mx, my, xx, yy, cx = (ink_l + ink_r + 1) / 2;
                int16_t dx, dy; uint16_t mw, mh;
                const unsigned char *mb;
                memcpy(&dx, mp + 1, 2); memcpy(&dy, mp + 3, 2); memcpy(&mw, mp + 5, 2); memcpy(&mh, mp + 7, 2);
                mb = mp + 9; mp += 9 + (size_t)mw * mh;
                if (place == 5) { for (yy = 0; yy < dy && yy < L; yy++) for (xx = 0; xx < CW; xx++) cell[yy * CW + xx] = 0; continue; }
                if (place == 0) { mx = cx - mw / 2 + dx; my = top_now - mh + dy; if (my < 0) my = 0; top_now = my; }
                else if (place == 1) { mx = cx - mw / 2 + dx; my = ink_b + 1 + dy; }
                else if (place == 2) { mx = ink_r + 1 - mw + dx; my = ink_b + 1 + dy; }
                else if (place == 3) { mx = ink_l + dx; my = ink_t + dy; }
                else { mx = ink_r + 1 + dx; my = ink_t + dy; }
                for (yy = 0; yy < mh; yy++)
                    for (xx = 0; xx < mw; xx++) {
                        int px = mx + xx, py = my + yy;
                        if (px < 0 || py < 0 || px >= CW || py >= L) continue;
                        if (mb[yy * mw + xx] > cell[py * CW + px]) cell[py * CW + px] = mb[yy * mw + xx];
                    }
            }
        }
        for (row = 0; row < L; row++) {
            int Y = pl[i].y * q + row;
            if (Y >= (int)(H * q)) break;
            for (col = 0; col < CW; col++) {
                int X = pl[i].x * q + col;
                if (X < (int)(W * q)) cv[((size_t)Y * W * q + X) * 4 + 3] = cell[row * CW + col];
            }
        }
        free(cell);
    }
    if (tt_ul_write_dds(full, cv, (int)(W * q), (int)(H * q))) {
        tt_dbg("[LANG] the upscaled font: %s (%ux%u)\n", rel, W * q, H * q);
        ul_note(nh, rel);
    }
    free(cv); free(base);
}

static void paths(void)
{
    char *sl;
    if (s_dir[0]) return;
    GetModuleFileNameA(NULL, s_dir, sizeof s_dir);
    sl = strrchr(s_dir, '\\'); if (!sl) sl = strrchr(s_dir, '/');
    if (sl) *sl = 0;
}
/* Cada eje conserva su diccionario; solo se comparten las fuentes fusionadas. */
static void read_axis(const char *ini, const char *key, const char *env, const char *def, char out[32])
{
    const char *e = getenv(env);
    size_t i;
    GetPrivateProfileStringA("Language", key, "", out, 32, ini);
    if (e && *e) snprintf(out, 32, "%s", e);
    if (!out[0]) snprintf(out, 32, "%s", def);
    for (i = 0; out[i]; i++) if (!((out[i] >= 'a' && out[i] <= 'z') ||
        (out[i] >= 'A' && out[i] <= 'Z') || (out[i] >= '0' && out[i] <= '9') || out[i] == '-')) {
        tt_dbg("[LANG] código inválido en %s; se usa %s\n", key, def);
        snprintf(out, 32, "%s", def); break;
    }
}
static int pack_rtl(const char *pack)
{
    char p[800]; size_t n; unsigned char *b; int rtl = 0;
    snprintf(p, sizeof p, "%s/info.txt", pack);
    b = read_all(p, &n);
    if (b) { rtl = strstr((char *)b, "rtl=1") != NULL; free(b); }
    return rtl;
}
static unsigned long load_pack(const char *pack, int port)
{
    char p[800]; unsigned long stamp;
    snprintf(p, sizeof p, "%s/game.txt", pack); load_lines(p, 1); stamp = file_stamp(p);
    if (port) { snprintf(p, sizeof p, "%s/text.txt", pack); load_lines(p, 0); stamp = stamp * 31u + file_stamp(p); }
    snprintf(p, sizeof p, "%s/glyphs.bin", pack); load_glyphs(p); stamp = stamp * 31u + file_stamp(p);
    snprintf(p, sizeof p, "%s/glyphs4.bin", pack); stamp = stamp * 31u + file_stamp(p);
    load_glyphs4_path(pack);
    return stamp;
}
int tt_lang_on(void)
{
    if (s_on < 0) {
        char ini[700], subpack[700];
        int i, j, k, next = 0xE000, good = 1;
        Slot textslots[8]; Slot4 text4[8];
        int nt, nt4, textk;
        uint16_t *map = (uint16_t *)calloc(65536, sizeof *map);
        paths(); s_on = 0;
        snprintf(ini, sizeof ini, "%s/twotowers_recomp.ini", s_dir);
        read_axis(ini, "language", "TT_LANG", "en", s_code);
        read_axis(ini, "subtitles", "TT_SUBS", s_code, s_subs);
        snprintf(s_pack, sizeof s_pack, "%s/lang/%s", s_dir, s_code);
        s_rtl = pack_rtl(s_pack);
        if (_stricmp(s_code, "en")) s_stamp = load_pack(s_pack, 1);
        if (_stricmp(s_code, "en") && !s_ng) {
            tt_dbg("[LANG] texto %s no instalado; inglés\n", s_code);
            snprintf(s_code, sizeof s_code, "en"); s_pack[0] = 0; s_rtl = 0;
        }
        if (!_stricmp(s_code, "en")) s_pack[0] = 0; /* en siempre conserva el disco. */
        nt = s_nslot; nt4 = s_nslot4; textk = s_k4;
        memcpy(textslots, s_slot, sizeof textslots); memcpy(text4, s_slot4, sizeof text4);
        for (i = 0; i < nt; i++) for (j = 0; j < textslots[i].n; j++) {
            int c = textslots[i].g[j].code;
            if (c >= 0xE000 && c < 0xF900) { if (c >= next) next = c + 1; s_direction[c] = s_rtl ? 1 : 2; }
        }
        if (_stricmp(s_subs, s_code)) {
            Str16 *textg = s_g; int textng = s_ng;
            s_g = NULL; s_ng = 0; s_nslot = s_nslot4 = 0;
            memset(s_slot, 0, sizeof s_slot); memset(s_slot4, 0, sizeof s_slot4);
            snprintf(subpack, sizeof subpack, "%s/lang/%s", s_dir, s_subs);
            s_subrtl = pack_rtl(subpack);
            if (_stricmp(s_subs, "en")) s_substamp = load_pack(subpack, 0);
            s_subg = s_g; s_nsubg = s_ng; s_g = textg; s_ng = textng;
            if (_stricmp(s_subs, "en") && !s_nsubg) good = 0;
            /* Mapa único para todos los tamaños, diccionario y Ultra. */
            for (i = 0; i < s_nslot; i++) for (j = 0; j < s_slot[i].n; j++) {
                int c = s_slot[i].g[j].code;
                if (c >= 0xE000 && c < 0xF900 && !map[c]) {
                    if (next >= 0xF900) good = 0;
                    else { map[c] = (uint16_t)next++; s_direction[map[c]] = s_subrtl ? 1 : 2; }
                }
            }
            for (i = 0; i < s_nsubg; i++) for (j = 0; j < s_subg[i].n; j++) {
                int c = s_subg[i].v[j];
                if (c >= 0xE000 && c < 0xF900) { if (!map[c]) good = 0; else s_subg[i].v[j] = map[c]; }
            }
            if (s_nslot4 && nt4 && textk != s_k4) good = 0;
            for (i = 0; i < s_nslot; i++) {
                Slot *ss = &s_slot[i]; int at = -1;
                for (j = 0; j < nt; j++) if (textslots[j].lh == ss->lh) at = j;
                if (at < 0 && nt < 8) { at = nt++; textslots[at] = (Slot){ss->lh, 0, NULL}; }
                if (at < 0) { good = 0; continue; }
                for (j = 0; j < ss->n; j++) {
                    Glyph g = ss->g[j];
                    /* Los cambios de avance globales no se pueden aplicar por eje. */
                    if (g.kind == 2) continue;
                    if (g.code >= 0xE000 && g.code < 0xF900) g.code = map[g.code];
                    if (g.base >= 0xE000 && g.base < 0xF900) g.base = map[g.base];
                    Slot *d = &textslots[at];
                    d->g = (Glyph *)realloc(d->g, (size_t)(d->n + 1) * sizeof *d->g); d->g[d->n++] = g;
                }
                free(ss->g);
            }
            for (i = 0; i < s_nslot4; i++) {
                Slot4 *ss = &s_slot4[i]; int at = -1;
                for (j = 0; j < nt4; j++) if (text4[j].lh == ss->lh) at = j;
                if (at < 0 && nt4 < 8) { at = nt4++; text4[at] = (Slot4){ss->lh, 0, NULL}; }
                if (at < 0) { good = 0; continue; }
                for (k = 0; k < ss->n; k++) {
                    Glyph4 g = ss->g[k]; if (g.code >= 0xE000 && g.code < 0xF900) g.code = map[g.code];
                    Slot4 *d = &text4[at]; d->g = (Glyph4 *)realloc(d->g, (size_t)(d->n + 1) * sizeof *d->g); d->g[d->n++] = g;
                }
                free(ss->g);
            }
            if (!good) { tt_dbg("[LANG] AVISO: subtítulos %s incompatibles o ausentes; se usa %s\n", s_subs, s_code); s_sub_fallback = 1; }
        } else { s_subg = s_g; s_nsubg = s_ng; s_subrtl = s_rtl; s_substamp = s_stamp; }
        memcpy(s_slot, textslots, sizeof textslots); s_nslot = nt;
        memcpy(s_slot4, text4, sizeof text4); s_nslot4 = nt4;
        if (textk) s_k4 = textk;
        free(map);
        snprintf(s_cache, sizeof s_cache, "%s+%s", s_code, s_subs);
        /* Revisión del algoritmo en la clave: no reutiliza cachés anteriores. */
        s_stamp = (s_stamp * 31u + s_substamp) ^ 0x05030002u;
        s_on = s_ng > 0 || s_nsubg > 0;
        tt_dbg("[LANG] diccionarios: texto %d, subtítulos %d, tamaños %d\n", s_ng, s_nsubg, s_nslot);
    }
    return s_on;
}
const char *tt_lang_code(void) { tt_lang_on(); return s_code; }
int tt_lang_rtl(void) { return tt_lang_on() && s_rtl; }

/* the port's own words, plain Unicode (UTF-8): for its own fonts */
const char *tt_tr(const char *en)
{
    char k[600];
    Str8 q, *r;
    if (!en || !tt_lang_on() || !s_nt) return en;
    snprintf(k, sizeof k, "P:%s", en);
    q.key = k;
    r = (Str8 *)bsearch(&q, s_t, (size_t)s_nt, sizeof *s_t, cmp_t);
    return r ? r->v : en;
}
/* ...and in the game's own characters (for the game's fonts): the units, 0-ended,
 * in out (max units); English as it is (Latin-1) without a translation */
int tt_tr16(const char *en, uint16_t *out, int max)
{
    char k[600];
    const Str16 *r = NULL;
    int n = 0;
    if (max <= 0) return 0;
    if (en && tt_lang_on()) { snprintf(k, sizeof k, "P:%s", en); r = find_g(k); }
    if (r) { n = r->n < max - 1 ? r->n : max - 1; memcpy(out, r->v, 2 * (size_t)n); }
    else if (en) for (; en[n] && n < max - 1; n++) out[n] = (uint8_t)en[n];
    out[n] = 0;
    return n;
}
/* (1.9.8) a text with numbers in it (the co-op and Versus words: "PLAYER 2 WINS ROUND 3"): its numbers taken out as {1},
 * {2}... ("PLAYER {1} WINS ROUND {2}"), that looked up, and the numbers put back in the language's places; no such key:
 * tt_tr16 of the whole */
static int put_tmpl(const uint16_t *v, int vn, const char args[][48], int na, uint16_t *out, int max)
{
    int i, n = 0;
    for (i = 0; i < vn && n < max - 1; i++) {
        if (v[i] == '{' && i + 2 < vn && v[i + 2] == '}' && v[i + 1] >= '1' && v[i + 1] <= '9' && v[i + 1] - '1' < na) {
            const char *a = args[v[i + 1] - '1']; int k;
            for (k = 0; a[k] && n < max - 1; k++) out[n++] = (uint8_t)a[k];
            i += 2; continue;
        }
        out[n++] = v[i];
    }
    out[n] = 0;
    return n;
}
int tt_tr16n(const char *en, uint16_t *out, int max)
{
    char k[600], args[6][48];
    int na = 0, o = 2, i;
    const Str16 *r;
    if (max <= 0) return 0;
    if (!en || !tt_lang_on()) return tt_tr16(en, out, max);
    k[0] = 'P'; k[1] = ':';
    for (i = 0; en[i] && o < (int)sizeof k - 4; ) {
        if (en[i] >= '0' && en[i] <= '9' && na < 6) {
            int j = 0;
            while (en[i] >= '0' && en[i] <= '9' && j < 47) args[na][j++] = en[i++];
            args[na][j] = 0;
            k[o++] = '{'; k[o++] = (char)('1' + na); k[o++] = '}'; na++;
            continue;
        }
        k[o++] = en[i++];
    }
    k[o] = 0;
    if (!na || !(r = find_g(k))) return tt_tr16(en, out, max);
    return put_tmpl(r->v, r->n, (const char (*)[48])args, na, out, max);
}
/* ...and a text with a name in it: tmpl's {1} the name (a device's: Latin-1 as it is) */
int tt_tr16s(const char *tmpl, const char *a1, uint16_t *out, int max)
{
    char k[600], args[1][48];
    const Str16 *r = NULL;
    uint16_t u[300];
    int n;
    if (max <= 0) return 0;
    snprintf(args[0], sizeof args[0], "%s", a1 ? a1 : "");
    if (tmpl && tt_lang_on()) { snprintf(k, sizeof k, "P:%s", tmpl); r = find_g(k); }
    if (r) return put_tmpl(r->v, r->n, (const char (*)[48])args, 1, out, max);
    for (n = 0; tmpl && tmpl[n] && n < 299; n++) u[n] = (uint8_t)tmpl[n];
    return put_tmpl(u, n, (const char (*)[48])args, 1, out, max);
}
/* a game string by its English (UTF-16 units): the language's, or NULL */
static uint32_t fnv16(const uint16_t *s, int n)
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < n; i++) { h = (h ^ (s[i] & 0xFF)) * 16777619u; h = (h ^ (s[i] >> 8)) * 16777619u; }
    return h;
}
static const Str16 *game_tr_axis(const uint16_t *en, int n, int subs)
{
    char k[16];
    if (n <= 0) return NULL;
    snprintf(k, sizeof k, "G%08X", fnv16(en, n));
    if (subs && !s_sub_fallback) {
        Str16 q; q.key = k;
        return s_nsubg ? (const Str16 *)bsearch(&q, s_subg, (size_t)s_nsubg, sizeof *s_subg, cmp_g) : NULL;
    }
    return find_g(k);
}
static const Str16 *game_tr(const uint16_t *en, int n) { return game_tr_axis(en, n, 0); }
/* the game's English (UTF-16 in guest memory at va, n units) in the language:
 * used by the code that looks for the game's own words (prompts, heroes) */
int tt_lang_game16(const uint16_t *en, int n, const uint16_t **out)
{
    const Str16 *r = tt_lang_on() ? game_tr(en, n) : NULL;
    if (!r) return -1;
    *out = r->v;
    return r->n;
}

/* a game string by its key (the hash of its English): the language's units */
int tt_lang_key16(uint32_t hash, const uint16_t **out)
{
    char k[16];
    const Str16 *r;
    if (!tt_lang_on()) return -1;
    snprintf(k, sizeof k, "G%08X", hash);
    if (!(r = find_g(k))) return -1;
    *out = r->v;
    return r->n;
}

/* ===================================================================== */
/* the files: a copy in the language                                       */
/* ===================================================================== */
static uint32_t rd32(const unsigned char *b, size_t o) { uint32_t v; memcpy(&v, b + o, 4); return v; }
static uint16_t rd16(const unsigned char *b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static void wr32(unsigned char *b, size_t o, uint32_t v) { memcpy(b + o, &v, 4); }
static void wr16(unsigned char *b, size_t o, uint16_t v) { memcpy(b + o, &v, 2); }

typedef struct { unsigned char *p; size_t n, cap; } Buf;
static int buf_room(Buf *b, size_t more)
{
    if (b->n + more <= b->cap) return 1;
    size_t c = b->cap ? b->cap : 65536;
    while (c < b->n + more) c *= 2;
    unsigned char *q = (unsigned char *)realloc(b->p, c);
    if (!q) return 0;
    memset(q + b->cap, 0, c - b->cap);
    b->p = q; b->cap = c;
    return 1;
}
static void buf_put(Buf *b, const void *d, size_t n) { if (buf_room(b, n)) { memcpy(b->p + b->n, d, n); b->n += n; } }
static void buf_align(Buf *b, size_t a) { size_t m = (a - b->n % a) % a; if (buf_room(b, m)) b->n += m; }

/* a string at b+o (0-ended UTF-16, before lim): its length in units */
static int u16len(const unsigned char *b, size_t o, size_t lim)
{
    int n = 0;
    while (o + 2 * (size_t)n + 1 < lim && rd16(b, o + 2 * (size_t)n)) n++;
    return n;
}
/* (room after every string: the hints that name the keys are rewritten in
 * place, sometimes longer -- prompts_kb.inc) */
#define STR_ROOM 48
static void put_str(Buf *o, const unsigned char *src, size_t so, size_t lim, int *ntr, int subs)
{
    int n = u16len(src, so, lim), i;
    const Str16 *t = game_tr_axis((const uint16_t *)(src + so), n, subs);   /* (aligned: the tables are) */
    uint16_t z = 0;
    if (t) { buf_put(o, t->v, 2 * (size_t)t->n); (*ntr)++; }
    else buf_put(o, src + so, 2 * (size_t)n);
    {   /* (testing: TT_LANG_ROOM units of room) */
        static int room = -1;
        if (room < 0) room = getenv("TT_LANG_ROOM") ? atoi(getenv("TT_LANG_ROOM")) : STR_ROOM;
        for (i = 0; i < 1 + room; i++) buf_put(o, &z, 2);
    }
}
/* OBSS: "OBSS", 1, ?, count; count x {id, offset}; the strings */
static int obss(Buf *o, const unsigned char *s, size_t a, size_t e, int *ntr)
{
    uint32_t cnt = rd32(s, a + 12), i;
    size_t base = o->n, hdr = 16 + 8 * (size_t)cnt;
    if (16 + 8 * (size_t)cnt > e - a) return 0;
    buf_put(o, s + a, hdr);
    for (i = 0; i < cnt; i++) {
        uint32_t so = rd32(s, a + 16 + 8 * i + 4);
        wr32(o->p, base + 16 + 8 * i + 4, (uint32_t)(o->n - base));
        if (so < hdr || a + so >= e) { uint16_t z = 0; buf_put(o, &z, 2); continue; }
        put_str(o, s, a + so, e, ntr, 0);
    }
    return 1;
}
/* OBST: "OBST", 1, count, 0; count x {hash, offset, start, end}; the strings */
static int obst(Buf *o, const unsigned char *s, size_t a, size_t e, int *ntr)
{
    uint32_t cnt = rd32(s, a + 8), i;
    size_t base = o->n, hdr = 16 + 16 * (size_t)cnt;
    if (hdr > e - a) return 0;
    buf_put(o, s + a, hdr);
    for (i = 0; i < cnt; i++) {
        uint32_t so = rd32(s, a + 16 + 16 * i + 4);
        wr32(o->p, base + 16 + 16 * i + 4, (uint32_t)(o->n - base));
        if (so < hdr || a + so >= e) { uint16_t z = 0; buf_put(o, &z, 2); continue; }
        put_str(o, s, a + so, e, ntr, 1);
    }
    return 1;
}

/* swizzled A8 (Xbox): the x and y bits interleaved, x first */
static uint32_t swz(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t o = 0, bit = 0;
    while (w > 1 || h > 1) {
        if (w > 1) { o |= (x & 1u) << bit; bit++; x >>= 1; w >>= 1; }
        if (h > 1) { o |= (y & 1u) << bit; bit++; y >>= 1; h >>= 1; }
    }
    return o;
}
static int ilog2(uint32_t v) { int n = 0; while ((1u << n) < v) n++; return n; }

typedef struct { int x0, y0, x1, y1, adv, h; } Rec;
static int u16len(const unsigned char *b, size_t o, size_t lim);
/* the letters a file's text uses (codes 0xE000 on): its fonts get only those --
 * startup.BFF's fonts draw the menus, the credits and the port's words: all of them */
static uint8_t s_used[0x2000];
static void use_all(void)
{
    int i, k;
    /* No borrar lo marcado por las tablas OBST. */
    for (i = 0; i < s_ng; i++) for (k = 0; k < s_g[i].n; k++) s_used[s_g[i].v[k] >> 3] |= (uint8_t)(1u << (s_g[i].v[k] & 7));
    if (!s_sub_fallback) for (i = 0; i < s_nsubg; i++) for (k = 0; k < s_subg[i].n; k++) s_used[s_subg[i].v[k] >> 3] |= (uint8_t)(1u << (s_subg[i].v[k] & 7));
}
static void use_str(const unsigned char *src, size_t so, size_t lim, int subs)
{
    int n = u16len(src, so, lim), k;
    const Str16 *t = game_tr_axis((const uint16_t *)(src + so), n, subs);
    if (t) for (k = 0; k < t->n; k++) s_used[t->v[k] >> 3] |= (uint8_t)(1u << (t->v[k] & 7));
}
static int used(int c) { return (s_used[c >> 3] >> (c & 7)) & 1; }
/* XF: a font, with the slot's letters added */
static int font(Buf *o, const unsigned char *s, size_t a, size_t e, int *nadded)
{
    int nr = rd16(s, a + 8), lh = rd16(s, a + 10), i, k, slot = -1;
    uint32_t rt = rd32(s, a + 12), to = rd32(s, a + 16), w0, h0, doff, W, H, pushed = 0;
    size_t th, base, recs_at, tex_at;
    unsigned char *pix0, *pix;
    Rec *nrec = NULL;
    Placed *placed = NULL;
    int ncode = 0, first = 0xE000, maxc = 0, nplaced = 0;
    uint32_t (*rng)[3];
    for (k = 0; k < s_nslot; k++) if (s_slot[k].lh == lh) slot = k;
    /* Nunca escribir una traducción que la fuente de este archivo no sabe
     * dibujar. build() vuelve a intentarlo con subtítulos como el texto. */
    for (i = 0xE000; i < 0xF900; i++) if (used(i)) {
        int found = 0;
        if (slot >= 0) for (k = 0; k < s_slot[slot].n; k++)
            if (s_slot[slot].g[k].code == i && s_slot[slot].g[k].kind != 2) { found = 1; break; }
        if (!found) {
            tt_dbg("[LANG] AVISO: glifo %04X ausente en fuente de %d px (%s)\n", i, lh, s_cache);
            s_build_bad = 1; return 0;
        }
    }
    if (slot < 0 || !s_slot[slot].n || nr <= 0 || nr > 64 || a + to + 0x80 > e) return 0;
    th = a + to;
    if (rd16(s, th) != 0x6278) return 0;   /* "xb" */
    w0 = rd32(s, th + 4); h0 = rd32(s, th + 8); doff = rd32(s, th + 0x30);
    if (w0 < 8 || h0 < 8 || w0 > 4096 || h0 > 4096 || (rd32(s, th + 0x38) >> 8 & 0xFF) != 0x19 || th + doff + (size_t)w0 * h0 > e) return 0;
    rng = (uint32_t (*)[3])calloc((size_t)nr + 1, sizeof *rng);
    for (i = 0; i < nr; i++) { rng[i][0] = rd32(s, a + rt + 12 * i); rng[i][1] = rd32(s, a + rt + 12 * i + 4); rng[i][2] = rd32(s, a + rt + 12 * i + 8); }
    /* the font's own picture, unswizzled */
    pix0 = (unsigned char *)malloc((size_t)w0 * h0);
    for (k = 0; k < (int)h0; k++) for (i = 0; i < (int)w0; i++) pix0[(size_t)k * w0 + i] = s[th + doff + swz((uint32_t)i, (uint32_t)k, w0, h0)];
    /* the new letters: codes from 0xE000 */
    for (k = 0; k < s_slot[slot].n; k++) {
        int c = s_slot[slot].g[k].code;
        if (s_slot[slot].g[k].kind == 2 || !used(c)) continue;
        if (c >= first && c - first + 1 > ncode) ncode = c - first + 1;
        if (c > maxc) maxc = c;
    }
    /* the picture's size: the font's, and rows of cells below it */
    {
        int need = 0, x = 0, rows = 1;
        for (W = w0 < 256 ? 256 : w0; ; W *= 2) {
            x = 0; rows = 1;
            for (k = 0; k < s_slot[slot].n; k++) {
                const Glyph *g = &s_slot[slot].g[k];
                int cw = g->kind == 0 ? g->w : g->kind == 1 ? 0 : -1;
                if (cw < 0 || !used(g->code)) continue;
                if (g->kind == 1) {   /* (a game letter's cell: its width) */
                    int r, c = g->base;
                    cw = 0;
                    for (r = 0; r < nr; r++) if (c >= (int)rng[r][1] && c < (int)(rng[r][1] + rng[r][0])) cw = (int)rd32(s, a + rng[r][2] + 20 * (c - rng[r][1]) + 8) - (int)rd32(s, a + rng[r][2] + 20 * (c - rng[r][1])) + 1;
                    if (cw <= 0) cw = lh;
                }
                if (x + cw + 1 > (int)W) { x = 0; rows++; }
                x += cw + 1;
            }
            need = (int)h0 + rows * (lh + 1);
            for (H = 8; H < (uint32_t)need; H *= 2) ;
            /* (wider rather than taller: the renderer takes textures up to
             * 2048 a side -- a 256 x 4096 font drew as a black box) */
            if (H <= 2048 || W >= 2048) break;
        }
        if (H > 2048 || W > 2048) {
            tt_dbg("[LANG] AVISO: fuente de %d px: no cabe la combinación %s\n", lh, s_cache);
            s_build_bad = 1; free(pix0); free(rng); return 0;
        }
    }
    pix = (unsigned char *)calloc((size_t)W * H, 1);
    for (k = 0; k < (int)h0; k++) memcpy(pix + (size_t)k * W, pix0 + (size_t)k * w0, w0);
    nrec = (Rec *)calloc((size_t)(ncode ? ncode : 1), sizeof *nrec);
    placed = (Placed *)calloc((size_t)s_slot[slot].n + 1, sizeof *placed);
    {
        int x = 0, y = (int)h0;
        for (k = 0; k < s_slot[slot].n; k++) {
            const Glyph *g = &s_slot[slot].g[k];
            int c = g->code, cw, r, row, col;
            unsigned char cell[512 * 64];
            if (g->kind == 2 || !used(c)) continue;
            if (c < first || c - first >= ncode) continue;
            memset(cell, 0, sizeof cell);
            if (g->kind == 0) {
                cw = g->w > 512 ? 512 : g->w;
                for (row = 0; row < lh && row < 64; row++) for (col = 0; col < cw; col++) cell[row * 512 + col] = g->data[(size_t)row * g->w + col];
            } else {
                /* a game letter (its cell), and marks over or under it */
                int bx0 = 0, by0 = 0, bx1 = -1, ink_t = lh, ink_b = -1, ink_l = 512, ink_r = -1, m, top_now;
                const unsigned char *mp = g->data;
                for (r = 0; r < nr; r++)
                    if (g->base >= rng[r][1] && g->base < rng[r][1] + rng[r][0]) {
                        size_t ra = a + rng[r][2] + 20 * (g->base - rng[r][1]);
                        bx0 = (int)rd32(s, ra); by0 = (int)rd32(s, ra + 4); bx1 = (int)rd32(s, ra + 8);
                    }
                cw = bx1 - bx0 + 1;
                if (cw <= 0 || cw > 512) { s_build_bad = 1; continue; }
                for (row = 0; row < lh && row < 64; row++)
                    for (col = 0; col < cw; col++) {
                        int sx = bx0 + col, sy = by0 + row;
                        unsigned char v = (sx < (int)w0 && sy < (int)h0) ? pix0[(size_t)sy * w0 + sx] : 0;
                        cell[row * 512 + col] = v;
                        if (v > 96) { if (row < ink_t) ink_t = row; if (row > ink_b) ink_b = row; if (col < ink_l) ink_l = col; if (col > ink_r) ink_r = col; }
                    }
                if (ink_r < 0) { ink_l = 0; ink_r = cw - 1; ink_t = lh / 3; ink_b = lh * 3 / 4; }
                top_now = ink_t;
                for (m = 0; m < g->nm; m++) {
                    int place = mp[0], dx = (int8_t)mp[1], dy = (int8_t)mp[2], mw = mp[3], mh = mp[4], mx, my, xx, yy;
                    const unsigned char *mb = mp + 5;
                    int cx = (ink_l + ink_r + 1) / 2;
                    mp += 5 + mw * mh;
                    if (place == 5) {   /* the dot taken off (an i made dotless): ink above row dy */
                        for (yy = 0; yy < dy && yy < lh; yy++) for (xx = 0; xx < cw; xx++) cell[yy * 512 + xx] = 0;
                        continue;
                    }
                    /* (as the reference font placed them, from the letter's ink: above
                     * its top, below its foot, over it, or to its right) */
                    if (place == 0) { mx = cx - mw / 2 + dx; my = top_now - mh + dy; if (my < 0) my = 0; top_now = my; }
                    else if (place == 1) { mx = cx - mw / 2 + dx; my = ink_b + 1 + dy; }
                    else if (place == 2) { mx = ink_r + 1 - mw + dx; my = ink_b + 1 + dy; }
                    else if (place == 3) { mx = ink_l + dx; my = ink_t + dy; }
                    else { mx = ink_r + 1 + dx; my = ink_t + dy; }
                    for (yy = 0; yy < mh; yy++)
                        for (xx = 0; xx < mw; xx++) {
                            int px = mx + xx, py = my + yy;
                            if (px < 0 || py < 0 || px >= 512 || py >= lh || py >= 64) continue;
                            if (mb[yy * mw + xx] > cell[py * 512 + px]) cell[py * 512 + px] = mb[yy * mw + xx];
                        }
                    if (place == 4 && mx + mw > cw) cw = mx + mw;   /* (a mark to the right widens it) */
                }
            }
            if (x + cw + 1 > (int)W) { x = 0; y += lh + 1; }
            if (y + lh > (int)H) { s_build_bad = 1; break; }
            for (row = 0; row < lh && row < 64; row++) memcpy(pix + (size_t)(y + row) * W + x, cell + row * 512, (size_t)cw);
            nrec[c - first].x0 = x; nrec[c - first].y0 = y; nrec[c - first].x1 = x + cw - 1; nrec[c - first].y1 = y + lh - 1;
            nrec[c - first].adv = g->kind == 0 ? g->adv : cw; nrec[c - first].h = lh;
            if (placed) { placed[nplaced].k = k; placed[nplaced].x = x; placed[nplaced].y = y; placed[nplaced].cw = cw; nplaced++; }
            x += cw + 1;
            pushed++;
        }
    }
    /* (no letters put in: the picture as it was, so that the Ultra textures'
     * upscale of it is still found -- it was made bigger for nothing, and its
     * menus' words were not upscaled) */
    if (!pushed) { W = w0; H = h0; memcpy(pix, pix0, (size_t)w0 * h0); }
    /* the font written: header, ranges (its own, then ours), records, texture */
    base = o->n;
    {
        unsigned char hd[0x14];
        memcpy(hd, s + a, 0x14);
        wr16(hd, 8, (uint16_t)(nr + (ncode ? 1 : 0)));
        wr32(hd, 12, 0x14);
        buf_put(o, hd, 0x14);
    }
    {
        size_t rt_at = o->n;
        uint32_t z[3] = { 0, 0, 0 };
        for (i = 0; i < nr + (ncode ? 1 : 0); i++) buf_put(o, z, 12);
        recs_at = o->n;
        for (i = 0; i < nr; i++) {
            size_t ra = a + rng[i][2];
            wr32(o->p, rt_at + 12 * i, rng[i][0]); wr32(o->p, rt_at + 12 * i + 4, rng[i][1]);
            wr32(o->p, rt_at + 12 * i + 8, (uint32_t)(o->n - base));
            buf_put(o, s + ra, 20 * (size_t)rng[i][0]);
        }
        if (ncode) {
            wr32(o->p, rt_at + 12 * nr, (uint32_t)ncode); wr32(o->p, rt_at + 12 * nr + 4, (uint32_t)first);
            wr32(o->p, rt_at + 12 * nr + 8, (uint32_t)(o->n - base));
            for (i = 0; i < ncode; i++) {
                unsigned char r[20];
                wr32(r, 0, (uint32_t)nrec[i].x0); wr32(r, 4, (uint32_t)nrec[i].y0); wr32(r, 8, (uint32_t)nrec[i].x1); wr32(r, 12, (uint32_t)nrec[i].y1);
                wr16(r, 16, (uint16_t)nrec[i].adv); wr16(r, 18, (uint16_t)(nrec[i].h ? nrec[i].h : lh));
                buf_put(o, r, 20);
            }
        }
        /* advances changed (the space, for languages written without spaces) */
        for (k = 0; k < s_slot[slot].n; k++) {
            const Glyph *g = &s_slot[slot].g[k];
            if (g->kind != 2) continue;
            for (i = 0; i < nr; i++)
                if (g->code >= rng[i][1] && g->code < rng[i][1] + rng[i][0]) {
                    uint32_t at = rd32(o->p, rt_at + 12 * i + 8);
                    wr16(o->p, base + at + 20 * (g->code - rng[i][1]) + 16, g->adv);
                }
        }
    }
    (void)recs_at;
    buf_align(o, 16);
    tex_at = o->n;
    wr32(o->p, base + 16, (uint32_t)(tex_at - base));
    {
        unsigned char th2[0x80];
        uint32_t fmt = rd32(s, th + 0x38), d;
        size_t data_at;
        memcpy(th2, s + th, 0x80);
        buf_put(o, th2, 0x80);
        /* the pixels on a page of their own, as the game's are (from the file's start: the copy's resources are page-aligned) */
        buf_align(o, 0x1000);
        data_at = o->n;
        d = (uint32_t)(data_at - tex_at);
        wr32(o->p, tex_at + 4, W); wr32(o->p, tex_at + 8, H); wr32(o->p, tex_at + 0x30, d);
        fmt = (fmt & 0x0000FFFFu) | ((uint32_t)1 << 16) | ((uint32_t)ilog2(W) << 20) | ((uint32_t)ilog2(H) << 24) | (fmt & 0xF0000000u);
        wr32(o->p, tex_at + 0x38, fmt);
        wr32(o->p, tex_at + 0x40, W * H);
        if (buf_room(o, (size_t)W * H)) {
            uint32_t yy, xx;
            for (yy = 0; yy < H; yy++) for (xx = 0; xx < W; xx++) o->p[data_at + swz(xx, yy, W, H)] = pix[(size_t)yy * W + xx];
            o->n += (size_t)W * H;
        }
    }
    wr32(o->p, base + 4, (uint32_t)(o->n - base));
    if (pushed) ultra_font(s, a, rng, nr, slot, pix0, w0, h0, pix, W, H, placed, nplaced);
    free(pix0); free(pix); free(nrec); free(rng); free(placed);
    *nadded += (int)pushed;
    return 1;
}

/* the copy of a file: the original's resources before its table, the
 * rebuilt ones after them (each on a page), then the table */
static int build(const char *src, const char *out, int *ntr, int *nglyph)
{
    size_t n, i;
    unsigned char *s = read_all(src, &n);
    uint32_t tab, cnt, *offs = NULL;
    Buf o = { 0 };
    int changed = 0, ok = 0;
    FILE *f;
    char tmp[1000];
    int oldfallback = s_sub_fallback;
retry:
    s_build_bad = 0;
    if (!s || n < 16 || s[0] != 9) goto done;
    tab = rd32(s, 4);
    if ((size_t)tab + 4 > n) goto done;
    cnt = rd32(s, tab);
    if ((size_t)tab + 4 + 12 * (size_t)cnt > n) goto done;
    offs = (uint32_t *)malloc(sizeof *offs * (cnt + 1));
    for (i = 0; i < cnt; i++) offs[i] = rd32(s, tab + 4 + 12 * i + 4);
    {   /* the letters this file's text uses (startup.BFF: every one) */
        int startup = 0;
        memset(s_used, 0, sizeof s_used);
        for (i = 0; i < cnt; i++) {
            uint32_t type = rd32(s, tab + 4 + 12 * i), a = offs[i], e = tab, j, c2, q;
            for (j = 0; j < cnt; j++) if (offs[j] > a && offs[j] < e) e = offs[j];
            if (a + 16 > e) continue;
            if (type == 900 && rd32(s, tab + 4 + 12 * i + 8) == 0 && !memcmp(s + a, "OBSS", 4) && rd32(s, a + 12) > 400) startup = 1;
            if (type >= 900 && type <= 902 && !memcmp(s + a, "OBSS", 4)) {
                c2 = rd32(s, a + 12);
                for (q = 0; q < c2 && 16 + 8 * (size_t)q + 8 <= e - a; q++) { uint32_t so = rd32(s, a + 16 + 8 * q + 4); if (a + so < e) use_str(s, a + so, e, 0); }
            } else if (type >= 905 && type <= 907 && !memcmp(s + a, "OBST", 4)) {
                c2 = rd32(s, a + 8);
                for (q = 0; q < c2 && 16 + 16 * (size_t)q + 16 <= e - a; q++) { uint32_t so = rd32(s, a + 16 + 16 * q + 4); if (a + so < e) use_str(s, a + so, e, 1); }
            }
        }
        if (startup) use_all();
    }
    buf_put(&o, s, tab);
    for (i = 0; i < cnt; i++) {
        uint32_t type = rd32(s, tab + 4 + 12 * i), a = offs[i], e = tab, j;
        size_t at;
        int r = 0;
        if (!(type >= 900 && type <= 907) && type != 800) continue;
        for (j = 0; j < cnt; j++) if (offs[j] > a && offs[j] < e) e = offs[j];
        if (a + 16 > e) continue;
        buf_align(&o, 0x1000);
        at = o.n;
        if (getenv("TT_LANG_NOTEXT") && type != 800) r = 0;
        else if ((type == 900 || type == 901 || type == 902) && !memcmp(s + a, "OBSS", 4)) r = obss(&o, s, a, e, ntr);
        else if (type >= 905 && type <= 907 && !memcmp(s + a, "OBST", 4)) r = obst(&o, s, a, e, ntr);
        else if (type == 800 && s[a] == 'X' && s[a + 1] == 'F' && !getenv("TT_LANG_NOFONTS")) r = font(&o, s, a, e, nglyph);
        if (r) { offs[i] = (uint32_t)at; changed = 1; }
        else o.n = at;
    }
    if (s_build_bad) {
        if (!s_sub_fallback && _stricmp(s_subs, s_code)) {
            tt_dbg("[LANG] AVISO: %s: subtítulos %s no caben; se usa el texto %s\n", src, s_subs, s_code);
            s_sub_fallback = 1; free(offs); offs = NULL; free(o.p); o = (Buf){0};
            *ntr = *nglyph = 0; changed = 0; goto retry;
        }
        goto done;
    }
    if (!changed) goto done;
    buf_align(&o, 0x1000);
    {
        size_t tat = o.n;
        buf_put(&o, s + tab, 4 + 12 * (size_t)cnt);
        for (i = 0; i < cnt; i++) wr32(o.p, tat + 4 + 12 * i + 4, offs[i]);
        wr32(o.p, 4, (uint32_t)tat);
    }
    snprintf(tmp, sizeof tmp, "%s.tmp", out);
    if (!(f = fopen(tmp, "wb"))) goto done;
    ok = fwrite(o.p, 1, o.n, f) == o.n;
    if (fclose(f)) ok = 0;
    if (ok) { remove(out); ok = rename(tmp, out) == 0; }
    if (!ok) remove(tmp);
done:
    s_sub_fallback = oldfallback;
    free(s); free(o.p); free(offs);
    return ok;
}

static void mkdirs(const char *p)
{
    char t[800];
    size_t i;
    snprintf(t, sizeof t, "%s", p);
    for (i = 3; t[i]; i++)
        if (t[i] == '\\' || t[i] == '/') { char c = t[i]; t[i] = 0; CreateDirectoryA(t, NULL); t[i] = c; }
}
/* the host path of an Xbox path, as the game opens it (no redirection) */
static int host_path(const char *xpath, char *out, size_t n)
{
    xbox_host_char h[600];
    const char *(*save)(const char *) = xbox_path_redirect;
    BOOL ok;
    xbox_path_redirect = NULL;
    ok = xbox_translate_path(xpath, h, 600);
    xbox_path_redirect = save;
    if (!ok) return 0;
#ifdef _WIN32
    WideCharToMultiByte(CP_UTF8, 0, h, -1, out, (int)n, NULL, NULL);
#else
    snprintf(out, n, "%s", h);
#endif
    return 1;
}
/* files with text or the fonts: the front end's and the levels' */
static int texty(const char *xp)
{
    const char *b = strrchr(xp, '\\');
    size_t l;
    b = b ? b + 1 : xp;
    l = strlen(b);
    if (!_stricmp(b, "startup.BFF") || !_stricmp(b, "credits.BFF") || !_stricmp(b, "mainmenu.BFF")) return 1;
    if (l > 4 && (strstr(xp, "orlds\\") || strstr(xp, "ORLDS\\")) && !_strnicmp(b + l - 4, ".bf", 3)) return 1;
    return 0;
}
/* the copy for the file the game would open (src: its host path) */
static const char *lang_copy(const char *xp, const char *src)
{
    static char out[900];
    char name[300], stp[900];
    const char *b = strrchr(src, '\\'), *b2 = strrchr(src, '/');
    unsigned long st = file_stamp(src);
    FILE *f;
    int ntr = 0, ng = 0;
    if (b2 > b) b = b2;
    b = b ? b + 1 : src;
    if (!st) return NULL;
    snprintf(name, sizeof name, "%s", b);
    snprintf(out, sizeof out, "%s/lang_cache/%s/%08lX_%08lX_%s", s_dir, s_cache, st, s_stamp & 0xFFFFFFFFul, name);
    snprintf(stp, sizeof stp, "%s.ultra", out);
    if ((f = fopen(out, "rb"))) {
        /* made before: its upscaled fonts paired again (all there, or it is made again) */
        FILE *u;
        int ok = 1;
        char ln[400], full[900];
        fclose(f);
        if ((u = fopen(stp, "rb")) == NULL) ok = 0;
        else {
            while (ok && fgets(ln, sizeof ln, u)) {
                unsigned nh;
                char rel[300];
                if (sscanf(ln, "%8X %299s", &nh, rel) != 2) continue;
                snprintf(full, sizeof full, "%s/%s", s_dir, rel);
                if ((f = fopen(full, "rb"))) { fclose(f); xbox_UltraAddPair(nh, rel); } else ok = 0;
            }
            fclose(u);
        }
        if (ok) { tt_dbg("[LANG] %s: made before (its upscaled fonts paired again)\n", xp); return out; }
        remove(out);
    }
    mkdirs(out);
    s_ulist_n = 0; s_ulist[0] = 0;
    if (!build(src, out, &ntr, &ng)) { tt_dbg("[LANG] %s: kept as it is\n", xp); return NULL; }
    if ((f = fopen(stp, "wb"))) { fwrite(s_ulist, 1, s_ulist_n, f); fclose(f); }
    tt_dbg("[LANG] %s: %d strings in %s, %d letters added\n", xp, ntr, s_code, ng);
    return out;
}

/* ===================================================================== */
/* the files the game opens: the Extra heroes' copies (heroes.c), then the  */
/* language's                                                              */
/* ===================================================================== */
const char *(*tt_heroes_redirect)(const char *xp);
/* (Versus) the arena's streaming sound bank (its music) from another level: tt_vs_music_donor() names the level
 * (NULL: the arena's own) */
extern const char *tt_vs_music_donor(const char *level_of_file);
static const char *vs_music_redirect(const char *xp)
{
    static char out[900]; char x[300]; const char *b, *d; size_t L = strlen(xp);
    if (L < 20 || L > 250) return NULL;
    b = strrchr(xp, '\\'); if (!b) return NULL; b++;
    if (_strnicmp(b, "S_WB_", 5) && _strnicmp(b, "S_SB_", 5)) return NULL;
    {   char lv[16]; size_t i; for (i = 0; i < 15 && b[5 + i] && b[5 + i] != '.'; i++) lv[i] = b[5 + i]; lv[i] = 0;
        d = tt_vs_music_donor(lv); if (!d) return NULL;
        snprintf(x, sizeof x, "%.*s%.5s%s%s", (int)(b - xp), xp, b, d, strchr(b, '.') ? strchr(b, '.') : "");
    }
    if (!host_path(x, out, sizeof out)) return NULL;
    { extern void tt_dbg(const char *, ...); tt_dbg("[VS] music: %s -> %s\n", xp, out); }
    return out;
}
/* (PAL) a pack's own copy of a whole file: lang\<code>\fmv\<name> for a film
 * ("\Device\CdRom0\FMV\mont.bik": the European disc's films with the
 * language's voice -- mont, yar1_2, logos) and lang\<code>\ui\credits.BFF
 * (its credits, longer than the USA ones) -- made by pal_lang_pack.py */
/* (PAL) the films' and the voices' languages, apart from the text's:
 * [Language] fmv= and voice= (TT_FMV, TT_VOICE; unset: the text's language,
 * "pal-" taken off; "en": the disc's own).
 *   films:  lang\fmv\<fmv>\<name>, else lang\<fmv>\fmv\, else lang\pal-<fmv>\fmv\
 *   voices: lang\voz\<voice>\S_SB_<level>.xsb -- the level's sound bank with
 *           every spoken line pointing at its dubbed twin (ES_, IT_, FR_,
 *           DE_: on both discs, picked by the PAL program only), made by
 *           voces_pack.py */
static char s_fmv[32], s_voice[32], s_cine[32];
#ifndef _WIN32
#include <dirent.h>
#include <fnmatch.h>
#ifndef FNM_CASEFOLD
#define FNM_CASEFOLD 0
#endif
#endif
static int media_present(const char *dir, const char *pattern)
{
#ifdef _WIN32
    char p[900]; WIN32_FIND_DATAA fd; HANDLE h;
    snprintf(p,sizeof p,"%s/%s",dir,pattern);
    h=FindFirstFileA(p,&fd);
    if(h==INVALID_HANDLE_VALUE)return 0;
    do {if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && (fd.nFileSizeLow||fd.nFileSizeHigh)) {FindClose(h);return 1;}} while(FindNextFileA(h,&fd));
    FindClose(h);return 0;
#else   /* (1.9.9) Linux and macOS: the same, by directory listing */
    DIR *d = opendir(dir); struct dirent *e; int found = 0;
    if (!d) return 0;
    while (!found && (e = readdir(d))) {
        char p[900]; struct stat st;
        if (fnmatch(pattern, e->d_name, FNM_CASEFOLD)) continue;
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (!stat(p, &st) && S_ISREG(st.st_mode) && st.st_size > 0) found = 1;
    }
    closedir(d); return found;
#endif
}
static int voice_present(const char *code,int cine)
{
    char dir[900],name[300],*plus;
#ifdef _WIN32
    char p[900];WIN32_FIND_DATAA fd;HANDLE h;
    snprintf(p,sizeof p,"%s/lang/voz/*",s_dir);h=FindFirstFileA(p,&fd);
    if(h==INVALID_HANDLE_VALUE)return 0;
    do {
        if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))continue;
        snprintf(name,sizeof name,"%s",fd.cFileName);
#else
    DIR *dd; struct dirent *e; char vz[900];
    snprintf(vz,sizeof vz,"%s/lang/voz",s_dir); dd=opendir(vz);
    if(!dd)return 0;
    while ((e = readdir(dd))) {
        struct stat st; char q[1200]; snprintf(q,sizeof q,"%s/%s",vz,e->d_name);
        if(e->d_name[0]=='.'||stat(q,&st)||!S_ISDIR(st.st_mode))continue;
        snprintf(name,sizeof name,"%s",e->d_name);
#endif
        plus=strchr(name,'+');
        if(plus){*plus++=0;if(_stricmp(cine?plus:name,code))continue;}
        else if(_stricmp(name,code))continue;
#ifdef _WIN32
        snprintf(dir,sizeof dir,"%s/lang/voz/%s",s_dir,fd.cFileName);
        if(media_present(dir,"S_SB_*.xsb")){FindClose(h);return 1;}
    } while(FindNextFileA(h,&fd));
    FindClose(h);return 0;
#else
        snprintf(dir,sizeof dir,"%s/lang/voz/%s",s_dir,e->d_name);
        if(media_present(dir,"S_SB_*.xsb")){closedir(dd);return 1;}
    }
    closedir(dd);return 0;
#endif
}
static int film_present(const char *code)
{
    char p[900];
    snprintf(p,sizeof p,"%s/lang/pal-%s/fmv",s_dir,code);if(media_present(p,"*.bik"))return 1;
    snprintf(p,sizeof p,"%s/lang/fmv/%s",s_dir,code);if(media_present(p,"*.bik"))return 1;
    snprintf(p,sizeof p,"%s/lang/%s/fmv",s_dir,code);return media_present(p,"*.bik");
}
static void media_codes(void)
{
    static int done;
    char ini[700], def[32];
    if (done) return;
    done = 1;
    tt_lang_on();
    snprintf(ini, sizeof ini, "%s/twotowers_recomp.ini", s_dir);
    snprintf(def, sizeof def, "%s", _strnicmp(s_code, "pal-", 4) ? s_code : s_code + 4);
    read_axis(ini, "fmv", "TT_FMV", def, s_fmv);
    read_axis(ini, "voice", "TT_VOICE", def, s_voice);
    read_axis(ini, "voice_cine", "TT_VOICE_CINE", def, s_cine);
    if(_stricmp(s_voice,"en") && !voice_present(s_voice,0)) {tt_dbg("[LANG] AVISO: voces de juego %s no instaladas; se usa en\n",s_voice);snprintf(s_voice,sizeof s_voice,"en");}
    if(_stricmp(s_cine,"en") && !voice_present(s_cine,1)) {tt_dbg("[LANG] AVISO: voces de cinemáticas %s no instaladas; se usa en\n",s_cine);snprintf(s_cine,sizeof s_cine,"en");}
    if(_stricmp(s_fmv,"en") && !film_present(s_fmv)) {tt_dbg("[LANG] AVISO: audio de películas %s no instalado; se usa en\n",s_fmv);snprintf(s_fmv,sizeof s_fmv,"en");}
    tt_dbg("[LANG] text %s, subs %s, voices %s, cine %s, films %s\n", s_code, s_subs, s_voice, s_cine, s_fmv);
}
static int have(const char *p)
{
    struct stat st;
    return !stat(p, &st) && st.st_size > 0;
}
static void lang_fmv_open(const char *xp, const char *name);
static const char *pack_file(const char *xp)
{
    static char out[900];
    char low[300];
    const char *b = strrchr(xp, '\\');
    size_t i;
    b = b ? b + 1 : xp;
    if (strlen(xp) >= sizeof low || strlen(b) > 64) return NULL;
    media_codes();
    for (i = 0; xp[i]; i++) low[i] = (char)((xp[i] >= 'A' && xp[i] <= 'Z') ? xp[i] + 32 : xp[i]);
    low[i] = 0;
    if (strstr(low, "\\fmv\\")) {
        lang_fmv_open(xp, b);
        if (!s_fmv[0] || !_stricmp(s_fmv, "en")) return NULL;
        /* El PAL de Xbox tiene prioridad sobre cualquier conversión PS2. */
        snprintf(out, sizeof out, "%s/lang/pal-%s/fmv/%s", s_dir, s_fmv, b);
        if (have(out)) return out;
        snprintf(out, sizeof out, "%s/lang/fmv/%s/%s", s_dir, s_fmv, b);
        if (have(out)) return out;
        snprintf(out, sizeof out, "%s/lang/%s/fmv/%s", s_dir, s_fmv, b);
        if (have(out)) return out;
        snprintf(out, sizeof out, "%s/lang/pal-%s/fmv/%s", s_dir, s_fmv, b);
        return have(out) ? out : NULL;
    }
    if (strstr(low, "\\sound\\") && (!_strnicmp(b, "s_sb_", 5) || !_strnicmp(b, "s_wb_", 5))) {
        if (!_stricmp(s_voice, "en") && !_stricmp(s_cine, "en")) return NULL;
        snprintf(out, sizeof out, "%s/lang/voz/%s+%s/%s", s_dir, s_voice, s_cine, b);
        if (have(out)) return out;
        if (_stricmp(s_voice, s_cine)) return NULL;
        snprintf(out, sizeof out, "%s/lang/voz/%s/%s", s_dir, s_voice, b);
        return have(out) ? out : NULL;
    }
    if (!_stricmp(b, "credits.BFF") && s_pack[0]) {
        snprintf(out, sizeof out, "%s/ui/credits.BFF", s_pack);
        return have(out) ? out : NULL;
    }
    return NULL;
}
static const char *redirect_all(const char *xp)
{
    const char *r = tt_heroes_redirect ? tt_heroes_redirect(xp) : NULL;
    { const char *v = vs_music_redirect(xp); if (v) return v; }
    char src[900];
    if (!r) {
        const char *p = pack_file(xp);
        if (p) { tt_dbg("[LANG] %s: the pack's %s\n", xp, p); r = p; }
    }
    if (!tt_lang_on() || !texty(xp)) return r;
    if (r) snprintf(src, sizeof src, "%s", r);
    else if (!host_path(xp, src, sizeof src)) return r;
    {
        const char *c = lang_copy(xp, src);
        return c ? c : r;
    }
}
/* startup.BFF's copy, made now (its area is made before the file is read:
 * twotowers_manual.c): its size, with room */
uint32_t tt_lang_startup_size(void)
{
    static uint32_t sz = 0xFFFFFFFFu;
    if (sz == 0xFFFFFFFFu) {
        const char *r;
        struct stat st;
        sz = 0;
        if (tt_lang_on() && (r = redirect_all("\\Device\\CdRom0\\ui\\startup.bff")) && !stat(r, &st))
            sz = ((uint32_t)st.st_size + 0x10000u + 0xFFFu) & ~0xFFFu;
    }
    return sz;
}

/* ---- right to left ----
 * A line of Arabic or Hebrew (the pack's letters, 0xE000 on) is drawn as it
 * is seen: the line turned around, and the runs of Latin letters and digits
 * in it turned back (a light form of the Unicode bidirectional rules: the
 * line's direction right to left, a space or a sign between two Latin runs
 * Latin, any other right to left; brackets mirrored). */
extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
static int rtl_class(uint16_t c)
{
    if (c >= 0xE000 && c < 0xF900) return s_direction[c] ? s_direction[c] : 2;
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7)) return 2;
    return 0;
}
static uint16_t s_back[2048];
static int s_back_n;
static uint32_t s_back_at;
int tt_lang_line(uint32_t first, uint32_t end, uint32_t *nfirst, uint32_t *nend)
{
    uint16_t u[2048], v[2048];
    uint8_t cl[2048];
    int n, i, j;
    tt_lang_on();
    if (end <= first) return 0;
    n = (int)((end - first) / 2);
    if (n <= 0 || n > 2048) return 0;
    for (i = 0; i < n; i++) { u[i] = G16(first + 2 * (uint32_t)i); cl[i] = (uint8_t)rtl_class(u[i]); }
    for (i = 0; i < n && cl[i] != 1; i++) { /* Buscar una letra RTL. */ }
    if (i == n) return 0; /* La dirección pertenece al eje de esta línea. */
    for (i = 0; i < n; i++) if (!cl[i]) {   /* (between two Latin: Latin; else right to left) */
        int a = i - 1, b = i + 1;
        while (a >= 0 && !cl[a]) a--;
        while (b < n && !cl[b]) b++;
        cl[i] = (a >= 0 && b < n && cl[a] == 2 && cl[b] == 2) ? 2 : 3;
    }
    {
        uint8_t c2[2048];
        for (i = 0; i < n; i++) { v[i] = u[n - 1 - i]; c2[i] = cl[n - 1 - i]; }
        memcpy(cl, c2, (size_t)n);
    }
    for (i = 0; i < n; ) {   /* the Latin runs back as they were */
        if (cl[i] != 2) {
            uint16_t c = v[i];
            if (c == '(') v[i] = ')'; else if (c == ')') v[i] = '('; else if (c == '[') v[i] = ']'; else if (c == ']') v[i] = '[';
            else if (c == '<') v[i] = '>'; else if (c == '>') v[i] = '<'; else if (c == 0xAB) v[i] = 0xBB; else if (c == 0xBB) v[i] = 0xAB;
            i++; continue;
        }
        for (j = i; j < n && cl[j] == 2; j++) { /* Tramo latino. */ }
        { int a = i, b = j - 1; while (a < b) { uint16_t t = v[a]; v[a] = v[b]; v[b] = t; a++; b--; } }
        i = j;
    }
    /* (turned in place, the same length: put back after drawing --
     * tt_lang_line_back. A buffer of our own in the contiguous memory was
     * the heap's first page, and the game's heap broke on it.) */
    for (i = 0; i < n; i++) { s_back[i] = u[i]; G16(first + 2 * (uint32_t)i) = v[i]; }
    s_back_n = n; s_back_at = first;
    *nfirst = first; *nend = end;
    return 1;
}
void tt_lang_line_back(void)
{
    int i;
    for (i = 0; i < s_back_n; i++) G16(s_back_at + 2 * (uint32_t)i) = s_back[i];
    s_back_n = 0;
}

/* the port's own words drawn with the computer's fonts (the F10 panel, the
 * ECTS tips): "L:" and then "P:" keys, plain Unicode (UTF-8) */
const char *tt_trL(const char *en)
{
    char k[600];
    Str8 q, *r;
    if (!en || !tt_lang_on() || !s_nt) return en;
    snprintf(k, sizeof k, "L:%s", en);
    q.key = k;
    r = (Str8 *)bsearch(&q, s_t, (size_t)s_nt, sizeof *s_t, cmp_t);
    return r ? r->v : tt_tr(en);
}
/* a Windows font with the language's letters, for that text (NULL: the usual) */
const char *tt_lang_face(void)
{
    static const struct { const char *code, *face; } F[] = {
        { "ja", "Meiryo UI" }, { "ko", "Malgun Gothic" }, { "zh-Hans", "Microsoft YaHei UI" },
        { "zh-Hant", "Microsoft JhengHei UI" }, { "th", "Leelawadee UI" }, { "hi", "Nirmala UI" },
        { "he", "Segoe UI" }, { "ar", "Segoe UI" } };
    size_t i;
    if (!tt_lang_on()) return NULL;
    for (i = 0; i < sizeof F / sizeof F[0]; i++) if (!strcmp(F[i].code, s_code)) return F[i].face;
    return NULL;
}

#include "lang_fmv.inc"

void tt_lang_init(void)
{
    extern const char *(*xbox_gfxmenu_tr)(const char *en);
    extern const char *xbox_gfxmenu_face;
    extern int xbox_gfxmenu_rtl;
    tt_lang_on();
    xbox_path_redirect = redirect_all;
    if (s_on) {   /* the F10 panel in the language */
        xbox_gfxmenu_tr = tt_trL;
        xbox_gfxmenu_face = tt_lang_face();
        xbox_gfxmenu_rtl = s_rtl;
    }
}
