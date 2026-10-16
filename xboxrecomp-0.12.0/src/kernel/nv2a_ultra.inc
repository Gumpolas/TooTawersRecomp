/*
 * nv2a_ultra.inc -- "Ultra textures": a high-resolution texture pack drawn
 * in place of the game's own textures (included by nv2a_sw3d.inc).
 *
 * The pack is the GameCube release's (Dolphin's tex1_<w>x<h>_<hash>_<fmt>
 * files), whose names hash the GameCube's texture data and so say nothing
 * about the Xbox's. Textures are paired by what they look like instead:
 * textures_ultra/index.bin holds, for every picture of the pack, a 16 x 16
 * average of it (its "signature"), and each texture the game decodes is
 * averaged the same way and given the pack's picture of the same shape that
 * is nearest -- when it is near enough, and clearly nearer than any other.
 *
 *   index.bin: "TTU1", u32 count, then count entries of
 *     char path[120]   the DDS, relative to the game's folder ('/')
 *     u16 w, h         its size
 *     u16 gw, gh       the GameCube texture's size (its shape)
 *     u8 sig[16][16][4] R G B A averages
 *
 * The pack's DDS files (BC7, full mip chains) are read on a thread of their
 * own when first matched, and made into GPU textures by the backend
 * (ultra_gpu_*), within a memory budget (RECOMP_ULTRA_MB, default 1536).
 * The option is GFX_ULTRA ([Display] ultra_textures); RECOMP_ULTRA_ROOT is
 * the game's folder; RECOMP_ULTRA_LOG logs each pairing.
 */

typedef struct {
    char path[120];
    uint16_t w, h, gw, gh;
    uint8_t sig[1024];
} UltraIdx;

typedef struct {
    volatile LONG state;     /* 0 none, 1 queued, 2 read (data), 3 on the GPU, -1 failed */
    uint8_t *data;           /* the levels, one after another */
    uint32_t size, w, h, levels, fmt;
    uint64_t used;
} UltraFile;

#define UPAIR_N 4096
static struct { uint32_t hash; int idx; } g_up[UPAIR_N];
static int g_upn;
static UltraIdx *g_ui;
static UltraFile *g_uf;
static int g_uin, g_ui_tried, g_uicap;
#define UMAP_N 16384
static struct { uint32_t hash; int idx; uint8_t used; } g_umap[UMAP_N];   /* (by the texture's content hash, so each is paired once) */
static int g_ultra_on = -1;
static long g_ultra_gfx_rev = -1;
static int g_ultra_log = -1;

/* (1.9.2) pictures the title makes while it runs (the game's fonts with a
 * language's letters added, upscaled: twotowers lang.c), paired with a
 * texture by its hash: xbox_UltraAddPair(hash, path relative to the game's
 * folder). Any thread; taken in by the renderer's next look. */
#define UADD_N 512
static struct { uint32_t hash; char path[120]; } g_uadd[UADD_N];
static volatile LONG g_uaddn;
static int g_uadd_done;   /* (taken in so far) */
void xbox_UltraAddPair(uint32_t hash, const char *path)
{
    LONG i = g_uaddn;
    if (i >= UADD_N || !path || strlen(path) >= 120) return;
    g_uadd[i].hash = hash;
    snprintf(g_uadd[i].path, sizeof g_uadd[i].path, "%s", path);
    InterlockedExchange(&g_uaddn, i + 1);
}

static void ultra_adds(int all);
static const char *ultra_root(void)
{
    const char *r = getenv("RECOMP_ULTRA_ROOT");
    return r && r[0] ? r : ".";
}

/* a pairing's picture: the same path, or for one of the pack's (GLOS69/...)
 * the same file name -- the pack can sit one folder further down */
static int ultra_same_pic(const char *idx_path, const char *pair_path)
{
    const char *a, *b;
    if (!strcmp(idx_path, pair_path)) return 1;
    if (strncmp(pair_path, "GLOS69/", 7) || !strncmp(idx_path, "textures_ultra/", 15) || !strstr(idx_path, "GLOS69/")) return 0;
    a = strrchr(idx_path, '/'); b = strrchr(pair_path, '/');
    return a && b && !strcmp(a, b);
}

static void ultra_pairs(void)
{   /* pairings checked by eye: textures_ultra/pairs.txt, "HASH path" or "HASH -" */
    char ln[400], p[600];
    FILE *f;
    g_upn = 0;
    snprintf(p, sizeof p, "%s/textures_ultra/pairs.txt", ultra_root());
    f = fopen(p, "rb");
    while (f && fgets(ln, sizeof ln, f) && g_upn < UPAIR_N) {
        char hx[16], path[300];
        int i;
        if (ln[0] == '#' || sscanf(ln, "%15s %299s", hx, path) != 2) continue;
        g_up[g_upn].hash = (uint32_t)strtoul(hx, NULL, 16);
        g_up[g_upn].idx = -1;
        for (i = 0; i < g_uin; i++) if (ultra_same_pic(g_ui[i].path, path)) { g_up[g_upn].idx = i; break; }
        if (g_up[g_upn].idx < 0 && strcmp(path, "-")) continue;   /* (a picture not in the index) */
        g_upn++;
    }
    if (f) fclose(f);
}

/* (1.9) The index is made with room to grow: the pack, built while the game
 * runs (ultra_build_poll), is added after the pictures already there. */
#define UI_CAP_MIN 8192
static int ultra_index(void)
{
    char p[600];
    FILE *f;
    uint8_t hd[8];
    uint32_t n;
    if (g_ui_tried) return g_uin > 0;
    g_ui_tried = 1;
    snprintf(p, sizeof p, "%s/textures_ultra/index.bin", ultra_root());
    f = fopen(p, "rb");
    if (!f) { fprintf(stderr, "  [ULTRA] no texture pack index (%s)\n", p); fflush(stderr); return 0; }
    if (fread(hd, 1, 8, f) != 8 || memcmp(hd, "TTU1", 4)) { fclose(f); fprintf(stderr, "  [ULTRA] %s: not an index\n", p); return 0; }
    memcpy(&n, hd + 4, 4);
    if (n > 65536) n = 65536;
    g_uicap = n > UI_CAP_MIN ? (int)n : UI_CAP_MIN;
    g_ui = (UltraIdx *)calloc((size_t)g_uicap, sizeof *g_ui);
    g_uf = (UltraFile *)calloc((size_t)g_uicap, sizeof *g_uf);
    if (!g_ui || !g_uf) { fclose(f); return 0; }
    g_uin = (int)fread(g_ui, sizeof *g_ui, n, f);
    fclose(f);
    ultra_pairs();
    fprintf(stderr, "  [ULTRA] texture pack index: %d pictures, %d pairings checked by eye\n", g_uin, g_upn);
    fflush(stderr);
    return g_uin > 0;
}

/* (1.9) The GLOS69 pack is downloaded apart from the game and made ready
 * once (the launcher: ultra_build.h). Ultra textures turned on in the game's
 * menus with the pack there but not ready: RECOMP_ULTRA_BUILDER (the
 * launcher, --build-ultra) is started, and its pictures are taken in when
 * textures_ultra/built.txt appears. Until then the textures that ship with
 * the game (menus, HUD) are used. */
static int ultra_file_exists(const char *rel)
{
    char p[600];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", ultra_root(), rel);
    return stat(p, &st) == 0;
}
static int ultra_pack_there(void)
{
    static const char *cand[] = { "GLOS69", "GLOS69/GLOS69", "textures_ultra/GLOS69", "Load/Textures/GLOS69", "User/Load/Textures/GLOS69" };
    int i;
    for (i = 0; i < 5; i++) if (ultra_file_exists(cand[i])) return 1;
    return 0;
}
static void ultra_reload(void)
{
    char p[600];
    FILE *f;
    uint8_t hd[8];
    uint32_t n, i;
    UltraIdx e;
    snprintf(p, sizeof p, "%s/textures_ultra/index.bin", ultra_root());
    f = fopen(p, "rb");
    if (!f || fread(hd, 1, 8, f) != 8 || memcmp(hd, "TTU1", 4)) { if (f) fclose(f); return; }
    memcpy(&n, hd + 4, 4);
    if (!g_ui || (int)n > g_uicap) {
        fclose(f);
        fprintf(stderr, "  [ULTRA] the texture pack is ready: it is used from the next start\n"); fflush(stderr);
        return;
    }
    for (i = 0; i < n; i++) {
        if (fread(&e, sizeof e, 1, f) != 1) break;
        if ((int)i < g_uin) {
            if (strcmp(e.path, g_ui[i].path)) {   /* (made otherwise: not while running) */
                fclose(f);
                fprintf(stderr, "  [ULTRA] the texture pack is ready: it is used from the next start\n"); fflush(stderr);
                return;
            }
            continue;
        }
        g_ui[i] = e;
    }
    fclose(f);
    if ((int)i > g_uin) g_uin = (int)i;
    ultra_pairs();
    ultra_adds(1);
    memset(g_umap, 0, sizeof g_umap);   /* every texture paired again */
    xbox_tex_rev++;
    fprintf(stderr, "  [ULTRA] the texture pack is ready: %d pictures, %d pairings checked by eye\n", g_uin, g_upn);
    fflush(stderr);
}
static void ultra_build_poll(void)
{
    static int state = -1;   /* -1 not looked, 0 nothing to do, 1 building */
    static DWORD last;
    DWORD now = GetTickCount();
    if (state == 0) return;
    if (state < 0) {
        state = 0;
        if (!ultra_pack_there() || ultra_file_exists("textures_ultra/built.txt")) return;
        {
            const char *cmd = getenv("RECOMP_ULTRA_BUILDER");
            if (!cmd || !cmd[0]) return;
            fprintf(stderr, "  [ULTRA] the GLOS69 pack is not made ready yet: %s\n", cmd); fflush(stderr);
#ifdef _WIN32
            {
                STARTUPINFOA si; PROCESS_INFORMATION pi; char buf[1200];
                memset(&si, 0, sizeof si); si.cb = sizeof si; memset(&pi, 0, sizeof pi);
                snprintf(buf, sizeof buf, "%s", cmd);
                if (!CreateProcessA(NULL, buf, NULL, NULL, FALSE, 0, NULL, ultra_root(), &si, &pi)) return;
                CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            }
#else
            {
                char buf[1300];
                snprintf(buf, sizeof buf, "%s >/dev/null 2>&1 &", cmd);
                if (system(buf) != 0) return;
            }
#endif
        }
        state = 1; last = now;
        return;
    }
    if (now - last < 2000) return;
    last = now;
    if (!ultra_file_exists("textures_ultra/built.txt")) return;
    state = 0;
    ultra_reload();
}

/* the title's pictures: in the index (paired by hash only) and the pairs */
static void ultra_adds(int all)
{
    int n = (int)g_uaddn, i, k;
    if (!g_ui || (!all && g_uadd_done >= n)) return;
    for (i = all ? 0 : g_uadd_done; i < n; i++) {
        int idx = -1;
        for (k = 0; k < g_uin; k++) if (!strcmp(g_ui[k].path, g_uadd[i].path)) { idx = k; break; }
        if (idx < 0 && g_uin < g_uicap) {
            idx = g_uin;
            memset(&g_ui[idx], 0, sizeof g_ui[idx]);
            snprintf(g_ui[idx].path, sizeof g_ui[idx].path, "%s", g_uadd[i].path);
            g_ui[idx].gw = 1; g_ui[idx].gh = 0;   /* (by hash only, never by look) */
            g_uin++;
        }
        if (idx < 0) continue;
        for (k = 0; k < g_upn && g_up[k].hash != g_uadd[i].hash; k++) ;
        if (k == g_upn && g_upn < UPAIR_N) g_upn++;
        if (k < UPAIR_N) { g_up[k].hash = g_uadd[i].hash; g_up[k].idx = idx; }
        if (g_ultra_log) fprintf(stderr, "  [ULTRA] %08X -> %s (the title's)\n", g_uadd[i].hash, g_uadd[i].path);
    }
    g_uadd_done = n;
    memset(g_umap, 0, sizeof g_umap);   /* (paired again: one looked at before may be one of these) */
    xbox_tex_rev++;
}

/* the option, looked at again when the settings change (on: every texture
 * decoded again, to be paired) */
static int ultra_on(void)
{
    if (g_ultra_gfx_rev != xbox_gfx_rev) {
        int on;
        g_ultra_gfx_rev = xbox_gfx_rev;
        on = xbox_GfxGet(GFX_ULTRA) == 1;
        if (g_ultra_on >= 0 && on != g_ultra_on) xbox_tex_rev++;
        if (on != g_ultra_on) { fprintf(stderr, "  [ULTRA] Ultra textures %s\n", on ? "on" : "off"); fflush(stderr); }
        g_ultra_on = on;
        if (g_ultra_log < 0) g_ultra_log = getenv("RECOMP_ULTRA_LOG") != NULL;
    }
    if (g_ultra_on == 1 && ultra_index()) { ultra_adds(0); ultra_build_poll(); return 1; }
    return 0;
}

/* the 16 x 16 average (R G B A), as the index's were made */
static void ultra_sig(const uint32_t *argb, int w, int h, float *s)
{
    int x, y, xx, yy;
    for (y = 0; y < 16; y++) {
        int y0 = y * h / 16, y1 = (y + 1) * h / 16;
        if (y1 <= y0) y1 = y0 + 1;
        for (x = 0; x < 16; x++) {
            int x0 = x * w / 16, x1 = (x + 1) * w / 16;
            float a[4] = { 0, 0, 0, 0 }, k;
            if (x1 <= x0) x1 = x0 + 1;
            for (yy = y0; yy < y1; yy++)
                for (xx = x0; xx < x1; xx++) {
                    uint32_t v = argb[yy * w + xx];
                    a[0] += (float)((v >> 16) & 255); a[1] += (float)((v >> 8) & 255);
                    a[2] += (float)(v & 255); a[3] += (float)(v >> 24);
                }
            k = 1.0f / (float)((y1 - y0) * (x1 - x0));
            s[(y * 16 + x) * 4 + 0] = a[0] * k; s[(y * 16 + x) * 4 + 1] = a[1] * k;
            s[(y * 16 + x) * 4 + 2] = a[2] * k; s[(y * 16 + x) * 4 + 3] = a[3] * k;
        }
    }
}

/* how far apart two signatures are: the mean difference, alpha at half */
#define ULTRA_ERR_K (4.0f / (256.0f * 3.5f * 3.5f))
static float ultra_err_f(const float *s, const uint8_t *g, float stop)
{
    float e = 0;
    int i;
    stop /= ULTRA_ERR_K;
    for (i = 0; i < 1024; i += 4) {
        e += fabsf(s[i] - g[i]) + fabsf(s[i + 1] - g[i + 1]) + fabsf(s[i + 2] - g[i + 2]) + 0.5f * fabsf(s[i + 3] - g[i + 3]);
        if (e > stop) return 1e9f;
    }
    return e * ULTRA_ERR_K;
}
static float ultra_err_g(const uint8_t *a, const uint8_t *b)
{
    float e = 0;
    int i;
    for (i = 0; i < 1024; i += 4)
        e += (float)(abs(a[i] - b[i]) + abs(a[i + 1] - b[i + 1]) + abs(a[i + 2] - b[i + 2])) + 0.5f * (float)abs(a[i + 3] - b[i + 3]);
    return e * ULTRA_ERR_K;
}

/* the pack's picture for this texture, or -1 */
static int ultra_pick(const uint32_t *argb, int w, int h)
{
    float s[1024], top_e[8], mean = 0, var = 0, e, sec = 1e9f, std;
    int top_i[8], nt = 0, i, k, best;
    if (w < 4 || h < 4 || w * h < 64) return -1;
    ultra_sig(argb, w, h, s);
    for (i = 0; i < 1024; i++) if ((i & 3) != 3) mean += s[i];
    mean /= 768.0f;
    for (i = 0; i < 1024; i++) if ((i & 3) != 3) var += (s[i] - mean) * (s[i] - mean);
    std = sqrtf(var / 768.0f);
    for (i = 0; i < g_uin; i++) {
        const UltraIdx *u = &g_ui[i];
        if ((uint32_t)u->gw * (uint32_t)h != (uint32_t)u->gh * (uint32_t)w) continue;   /* (the same shape) */
        e = ultra_err_f(s, u->sig, nt < 8 ? 1e9f : top_e[7]);
        if (e >= 1e9f) continue;
        for (k = nt < 8 ? nt : 7; k > 0 && top_e[k - 1] > e; k--) { top_e[k] = top_e[k - 1]; top_i[k] = top_i[k - 1]; }
        top_e[k] = e; top_i[k] = i;
        if (nt < 8) nt++;
    }
    if (!nt) return -1;
    best = top_i[0]; e = top_e[0];
    for (k = 1; k < nt; k++)   /* the nearest that is another picture (not a copy of the best) */
        if (ultra_err_g(g_ui[best].sig, g_ui[top_i[k]].sig) > 4.0f) { sec = top_e[k]; break; }
    if (std < 6.0f) { if (!(e < 3.0f)) return -1; }   /* (a plain colour: only a very close one) */
    else if (!(e < 6.0f || (e < 10.0f && sec >= 2.0f * e) || (e < 12.0f && sec >= 3.0f * e))) return -1;
    return best;
}

/* by the texture's content hash, so each is paired once */
static int ultra_lookup(uint32_t hash, const uint32_t *argb, int w, int h)
{
    uint32_t i = (hash * 2654435761u) & (UMAP_N - 1), n;
    int r;
    for (n = 0; n < UMAP_N; n++, i = (i + 1) & (UMAP_N - 1)) {
        if (!g_umap[i].used) break;
        if (g_umap[i].hash == hash) return g_umap[i].idx;
    }
    {   /* textures the title keeps as they are (RECOMP_ULTRA_SKIP: hex hashes, as
         * RECOMP_TEX_DUMP names them -- e.g. the health pickup, which a mod recolours) */
        static uint32_t skip[64]; static int nskip = -1;
        int k;
        if (nskip < 0) {
            const char *e = getenv("RECOMP_ULTRA_SKIP");
            nskip = 0;
            while (e && *e && nskip < 64) {
                char *end;
                unsigned long v = strtoul(e, &end, 16);
                if (end == e) { e++; continue; }
                skip[nskip++] = (uint32_t)v;
                e = end;
            }
        }
        for (k = 0; k < nskip; k++) if (skip[k] == hash) { r = -1; goto store; }
        for (k = 0; k < g_upn; k++) if (g_up[k].hash == hash) { r = g_up[k].idx; goto store; }
    }
    r = ultra_pick(argb, w, h);
store:
    /* (1.9.2) the title's own pictures (lang_cache: the language's fonts) are
     * paired by hash and carry no signature: not checked */
    if (r >= 0 && strncmp(g_ui[r].path, "textures_ultra/", 15) && strncmp(g_ui[r].path, "lang_cache/", 11)) {
        /* (1.8.2) a pack picture opaque where the game's texture is see-through
         * (GameCube pictures kept their transparency apart): a black box round
         * the picture -- the game's own texture is kept instead (alphafix.py
         * makes such pictures with the game's transparency) */
        float gs[1024];
        int c, n = 0;
        ultra_sig(argb, w, h, gs);
        for (c = 0; c < 256; c++) {
            float ga = gs[c * 4 + 3], pa = (float)g_ui[r].sig[c * 4 + 3];
            if (ga < 100.0f && pa > ga + 80.0f) n++;   /* (opaque where the texture is see-through: a black box) */
            if (ga > 200.0f && pa < ga - 80.0f) n++;   /* (see-through where it is solid: panes of glass in the sky) */
        }
        if (n) {
            if (g_ultra_log) fprintf(stderr, "  [ULTRA] %08X: %s kept out (its transparency is not the texture's)\n", hash, g_ui[r].path);
            r = -1;
        }
    }
    if (n < UMAP_N) { g_umap[i].used = 1; g_umap[i].hash = hash; g_umap[i].idx = r; }
    if (g_ultra_log) {
        fprintf(stderr, "  [ULTRA] %08X %dx%d -> %s\n", hash, w, h, r >= 0 ? g_ui[r].path : "(none)");
        fflush(stderr);
    }
    return r;
}

/* ---- the files, read on a thread of their own ---- */
#define UQ_N 4096
static int g_uq[UQ_N];
static volatile LONG g_uq_head, g_uq_tail;
static HANDLE g_uq_ev;
static CRITICAL_SECTION g_uq_lock;
static volatile LONG g_uq_started;

static int ultra_read(int idx)
{
    char p[700];
    FILE *f;
    uint8_t hd[148];
    uint32_t w, h, mc, fmt, i, W, H, total = 0;
    long sz;
    UltraFile *u = &g_uf[idx];
    {   /* (1.8.2) a cut-out picture remade with its colour under the
         * transparent texels (textures_ultra/bleed, bleed.py): no dark rims */
        const char *bn = strrchr(g_ui[idx].path, '/');
        snprintf(p, sizeof p, "%s/textures_ultra/bleed/%s", ultra_root(), bn ? bn + 1 : g_ui[idx].path);
        f = fopen(p, "rb");
    }
    if (!f) { snprintf(p, sizeof p, "%s/%s", ultra_root(), g_ui[idx].path); f = fopen(p, "rb"); }
    if (!f) { fprintf(stderr, "  [ULTRA] cannot open %s\n", p); return 0; }
    if (fread(hd, 1, 148, f) != 148 || memcmp(hd, "DDS ", 4) || memcmp(hd + 84, "DX10", 4)) { fclose(f); return 0; }
    memcpy(&h, hd + 12, 4); memcpy(&w, hd + 16, 4); memcpy(&mc, hd + 28, 4); memcpy(&fmt, hd + 128, 4);
    if (!mc) mc = 1;
    if (fmt != 98 || !w || !h || w > 16384 || h > 16384 || (w & 3) || (h & 3)) { fclose(f); return 0; }
    for (i = 0, W = w, H = h; i < mc; i++) {
        total += ((W + 3) / 4) * ((H + 3) / 4) * 16;
        if (W == 1 && H == 1) { mc = i + 1; break; }
        W = W > 1 ? W / 2 : 1; H = H > 1 ? H / 2 : 1;
    }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 148, SEEK_SET);
    if (sz < 148 + (long)total) { fclose(f); return 0; }
    u->data = (uint8_t *)malloc(total);
    if (!u->data) { fclose(f); return 0; }
    if (fread(u->data, 1, total, f) != total) { fclose(f); free(u->data); u->data = NULL; return 0; }
    fclose(f);
    u->size = total; u->w = w; u->h = h; u->levels = mc; u->fmt = fmt;
    return 1;
}
static DWORD WINAPI ultra_thread(LPVOID arg)
{
    (void)arg;
    for (;;) {
        int idx = -1;
        EnterCriticalSection(&g_uq_lock);
        if (g_uq_tail != g_uq_head) { idx = g_uq[g_uq_tail]; g_uq_tail = (g_uq_tail + 1) % UQ_N; }
        LeaveCriticalSection(&g_uq_lock);
        if (idx < 0) { WaitForSingleObject(g_uq_ev, 200); continue; }
        InterlockedExchange(&g_uf[idx].state, ultra_read(idx) ? 2 : -1);
    }
    return 0;
}
static void ultra_request(int idx)
{
    if (g_uf[idx].state != 0) return;
    if (!g_uq_started) {
        InitializeCriticalSection(&g_uq_lock);
        g_uq_ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        g_uq_started = 1;
        CreateThread(NULL, 0, ultra_thread, NULL, 0, NULL);
    }
    EnterCriticalSection(&g_uq_lock);
    if ((g_uq_head + 1) % UQ_N != g_uq_tail) {
        g_uq[g_uq_head] = idx; g_uq_head = (g_uq_head + 1) % UQ_N;
        g_uf[idx].state = 1;
    }
    LeaveCriticalSection(&g_uq_lock);
    SetEvent(g_uq_ev);
}

/* the GPU's share: the backend says how much it holds, and lets the least
 * used go when it is over the budget */
static uint64_t g_ultra_bytes, g_ultra_clock;
static uint64_t ultra_budget(void)
{
    static uint64_t b;
    if (!b) { const char *e = getenv("RECOMP_ULTRA_MB"); b = (uint64_t)(e && atoi(e) > 64 ? atoi(e) : 1536) << 20; }
    return b;
}

/* (1.8.2) the sky: the pack's sky pictures are not laid out as the Xbox's
 * box (its faces meet at other places, with other light): the box showed
 * its corners, the faces of two skies side by side. A pack picture drawn
 * as the sky is -- clamped both ways, depth tested but not written, 256 or
 * wider -- is kept out from then on, and the game's own sky drawn. */
static uint8_t *g_uban;
static int ultra_sky_check(int idx, const Texture *t, int ztest, int zwrite)
{
    if (!g_uban) { g_uban = (uint8_t *)calloc((size_t)g_uin, 1); if (!g_uban) return 1; }
    if (g_uban[idx]) return 0;
    if (strncmp(g_ui[idx].path, "textures_ultra/", 15) && t->addr_u == 3 && t->addr_v == 3 && ztest && !zwrite && t->width >= 256 && !getenv("RECOMP_ULTRA_SKY")) {
        g_uban[idx] = 1;
        if (g_ultra_log) { fprintf(stderr, "  [ULTRA] %s: drawn as the sky -- kept out\n", g_ui[idx].path); fflush(stderr); }
        return 0;
    }
    return 1;
}
