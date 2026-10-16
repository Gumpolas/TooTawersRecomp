/* e3hud_load.inc -- the loading screen tip about experience, in the demo's
 * style. The retail tip is one 256x256 picture (thermometer on its silver
 * medallion, ringed with colour, an arrow to "Experience Points Earned")
 * with the game's text around it. With the ECTS HUD on, the picture's gauge is
 * redrawn as the demo's (plain rectangle, blue fill, rune orb) and a second
 * arrow from the orb points to a few lines on the combo runes, drawn over
 * the frame. Texture space -> screen: x * 0.95 + 192, y * 0.95 + 112. */
#define TEX_LOAD_TIP 0x509D0703u

static void tip_arrow(uint32_t *a, int w, int h, float x0, float x1, float y, uint32_t col)
{
    int x, yy;
    for (x = (int)x0; x <= (int)x1 - 9; x++)              /* shaft, 2 px */
        for (yy = (int)y - 1; yy <= (int)y; yy++) put(a, w, h, x, yy, col, 1.0f);
    for (x = 0; x < 10; x++) {                             /* head */
        float half = 4.0f * (1.0f - x / 10.0f);
        for (yy = (int)(y - half - 1); yy <= (int)(y + half); yy++) {
            float d = fabsf(yy + 0.5f - y) - half;
            if (d < 0.5f) put(a, w, h, (int)x1 - 10 + x, yy, col, d < -0.5f ? 1.0f : 0.5f - d);
        }
    }
}

static void load_tip_edit(uint32_t *a, int w, int h)
{
    static const int runes[4] = { 1, 2, 3, 0 };     /* speed, fierce, special */
    uint32_t arrow = 0;
    int x, y;
    /* the arrow's colour, from the retail arrow */
    for (x = 60; x < 140 && !arrow; x++)
        if ((a[39 * w + x] >> 24) > 200) arrow = a[39 * w + x] & 0xFFFFFF;
    if (!arrow) arrow = 0xF0C838;
    /* clear the retail gauge, medallion and colour ring (keep the arrow) */
    for (y = 0; y < 150; y++)
        for (x = 0; x < 72; x++)
            if (x < 44 || y < 32 || y > 46) a[y * w + x] = 0;
            else {   /* the "+" badge beside the arrow's start: keep only the arrow's gold */
                uint32_t c = a[y * w + x];
                int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
                if (!(r > 150 && g > 120 && b < 120)) a[y * w + x] = 0;
            }
    /* the demo's gauge: dark inside, blue fill, pale outline */
    for (y = 10; y < 104; y++)
        for (x = 28; x <= 40; x++) {
            uint32_t c = y >= 36 ? 0x2E50F0 : 0x101010;      /* filled about three quarters */
            if (y >= 36) c = shade(c, 0.8f + 0.4f * (1.0f - fabsf(x - 33.5f) / 7.0f));
            a[y * w + x] = 0xFF000000u | c;
        }
    draw_tube(a, w, h, 28.0f, 10.0f, 41.0f, 108.0f, 1.6f);
    draw_orb_ex(a, w, h, 34.5f, 112.0f, 18.0f, runes, 3, 0);
    /* second arrow: orb -> the rune lines */
    tip_arrow(a, w, h, 56.0f, 145.0f, 112.0f, arrow);
}

/* The tip about the Skill Meter (shown loading Fangorn) is the same picture
 * with the arrow lower down: it leaves the medallion at y 101-103 for the
 * game's "Skill Meter" text. Same demo gauge and orb; the arrow is kept and
 * run back to the orb's edge, and there is no rune arrow (it would cross it). */
#define TEX_SKILL_TIP 0x4F380BD7u

static void skill_tip_edit(uint32_t *a, int w, int h)
{
    static const int none[4] = { 0, 0, 0, 0 };
    uint32_t arrow = 0;
    int x, y;
    for (x = 90; x < 150 && !arrow; x++)
        if ((a[102 * w + x] >> 24) > 200) arrow = a[102 * w + x] & 0xFFFFFF;
    if (!arrow) arrow = 0xF0C838;
    /* clear the retail gauge, medallion and colour ring (the arrow starts at x 69) */
    for (y = 0; y < 150; y++)
        for (x = 0; x < 72; x++) {
            uint32_t c = a[y * w + x];
            int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            if (!(x >= 64 && y >= 98 && y <= 106 && r > 150 && g > 120 && b < 120)) a[y * w + x] = 0;
        }
    for (y = 10; y < 104; y++)
        for (x = 28; x <= 40; x++) {
            uint32_t c = y >= 36 ? 0x2E50F0 : 0x101010;
            if (y >= 36) c = shade(c, 0.8f + 0.4f * (1.0f - fabsf(x - 33.5f) / 7.0f));
            a[y * w + x] = 0xFF000000u | c;
        }
    draw_tube(a, w, h, 28.0f, 10.0f, 41.0f, 108.0f, 1.6f);
    draw_orb_ex(a, w, h, 34.5f, 112.0f, 18.0f, none, 0, 0);
    /* the arrow's shaft, from the orb's edge to where the retail one starts */
    for (x = 50; x < 72; x++)
        for (y = 101; y <= 103; y++)
            if ((a[y * w + x] >> 24) < 200) put(a, w, h, x, y, arrow, 1.0f);
}

/* the lines beside the second arrow, drawn over the frame */
static uint32_t *s_tip_px;
static int s_tip_w, s_tip_h;

#if defined(_WIN32)
static void tip_text(HDC dc, uint32_t *px, int W, int H, int x, int y, const char *t, uint32_t col, HFONT f)
{
    SIZE sz;
    BITMAPINFO bi;
    uint32_t *m = NULL;
    HBITMAP bm;
    int i, j;
    wchar_t w[256];
    int wn;
    extern const char *tt_trL(const char *en);
    t = tt_trL(t);   /* (in the chosen language: lang.c) */
    wn = MultiByteToWideChar(CP_UTF8, 0, t, -1, w, 256) - 1;
    if (wn <= 0) return;
    SelectObject(dc, f);
    GetTextExtentPoint32W(dc, w, wn, &sz);
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = sz.cx + 4; bi.bmiHeader.biHeight = -(sz.cy + 4);
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&m, NULL, 0);
    if (!bm) return;
    SelectObject(dc, bm);
    memset(m, 0, (size_t)(sz.cx + 4) * (sz.cy + 4) * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutW(dc, 2, 2, w, wn);
    GdiFlush();
    for (j = 0; j < sz.cy + 4; j++)
        for (i = 0; i < sz.cx + 4; i++) {
            float al = (float)((m[j * (sz.cx + 4) + i] >> 8) & 255) / 255.0f;
            if (al > 0) put(px, W, H, x + i, y + j, col, al);
        }
    DeleteObject(bm);
}

static int load_tip_overlay(XboxOverlayQuad *q, int max)
{
    /* 220 x 70 screen pixels at 4x, left edge at the retail text's (x 344),
     * centred on the second arrow (texture y 112 -> screen 218) */
    const int K = 4, W = 220 * K, H = 70 * K;
    if (max < 1) return 0;
    if (!s_tip_px) {
        HDC dc = CreateCompatibleDC(NULL);
        /* the game's tip lettering is a flared humanist sans: Candara is
         * the nearest Windows face (Trebuchet if it is missing) */
        extern const char *tt_lang_face(void);
        extern const char *tt_trL(const char *en);
        const char *face = tt_lang_face() ? tt_lang_face() : "Candara";
        HFONT big = CreateFontA(-17 * K, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, face);
        HFONT sml = CreateFontA(-13 * K, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                                CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, face);
        s_tip_px = (uint32_t *)calloc((size_t)W * H, 4);
        if (s_tip_px && dc) {
            int x = 0;
            SIZE sz;
            tip_text(dc, s_tip_px, W, H, 0, 0, "Combo Runes", 0xF0C838, big);
            /* one rune per attack, in its colour */
            SelectObject(dc, sml);
            tip_text(dc, s_tip_px, W, H, x, 24 * K, "Speed", 0x4878FF, sml);
            { wchar_t w[128]; int n = MultiByteToWideChar(CP_UTF8, 0, tt_trL("Speed"), -1, w, 120) - 1;
              if (n < 0) n = 0; w[n] = w[n + 1] = L' '; GetTextExtentPoint32W(dc, w, n + 2, &sz); x += sz.cx; }
            tip_text(dc, s_tip_px, W, H, x, 24 * K, "Fierce", 0x30D040, sml);
            { wchar_t w[128]; int n = MultiByteToWideChar(CP_UTF8, 0, tt_trL("Fierce"), -1, w, 120) - 1;
              if (n < 0) n = 0; w[n] = w[n + 1] = L' '; GetTextExtentPoint32W(dc, w, n + 2, &sz); x += sz.cx; }
            tip_text(dc, s_tip_px, W, H, x, 24 * K, "Special", 0xF04828, sml);
            tip_text(dc, s_tip_px, W, H, 0, 42 * K, "Each attack fills a quarter", 0xE8E8E8, sml);
        }
        if (dc) DeleteDC(dc);
        s_tip_w = W; s_tip_h = H;
    }
#else
#include "video/host_text.h"
static void tip_text(uint32_t *px, int W, int H, int x, int y, const char *t, uint32_t col, int fpx)
{
    extern const char *tt_trL(const char *en);
    int tw = xbox_TextWidth(XFONT_SANS_BOLD, fpx, t), th = xbox_TextLineHeight(XFONT_SANS_BOLD, fpx), i, j;
    uint8_t *m;
    t = tt_trL(t);   /* (in the chosen language: lang.c) */
    tw = xbox_TextWidth(XFONT_SANS_BOLD, fpx, t);
    if (tw <= 0 || th <= 0) return;
    m = (uint8_t *)calloc((size_t)(tw + 4) * (th + 4), 1);
    if (!m) return;
    xbox_TextDraw(m, tw + 4, th + 4, 2, 2, XFONT_SANS_BOLD, fpx, t);
    for (j = 0; j < th + 4; j++)
        for (i = 0; i < tw + 4; i++) {
            float al = (float)m[j * (tw + 4) + i] / 255.0f;
            if (al > 0) put(px, W, H, x + i, y + j, col, al);
        }
    free(m);
}

static int load_tip_overlay(XboxOverlayQuad *q, int max)
{
    /* as the Windows version (GDI, Candara); here the bundled Carlito */
    const int K = 4, W = 220 * K, H = 70 * K;
    if (max < 1) return 0;
    if (!s_tip_px) {
        s_tip_px = (uint32_t *)calloc((size_t)W * H, 4);
        if (s_tip_px) {
            int x = 0, big = 17 * K, sml = 13 * K;
            tip_text(s_tip_px, W, H, 0, 0, "Combo Runes", 0xF0C838, big);
            tip_text(s_tip_px, W, H, x, 24 * K, "Speed", 0x4878FF, sml);
            { extern const char *tt_trL(const char *en); char b[256]; snprintf(b, sizeof b, "%s  ", tt_trL("Speed")); x += xbox_TextWidth(XFONT_SANS_BOLD, sml, b); }
            tip_text(s_tip_px, W, H, x, 24 * K, "Fierce", 0x30D040, sml);
            { extern const char *tt_trL(const char *en); char b[256]; snprintf(b, sizeof b, "%s  ", tt_trL("Fierce")); x += xbox_TextWidth(XFONT_SANS_BOLD, sml, b); }
            tip_text(s_tip_px, W, H, x, 24 * K, "Special", 0xF04828, sml);
            tip_text(s_tip_px, W, H, 0, 42 * K, "Each attack fills a quarter", 0xE8E8E8, sml);
        }
        s_tip_w = W; s_tip_h = H;
    }
#endif
    if (!s_tip_px) return 0;
    q[0].px = s_tip_px; q[0].w = s_tip_w; q[0].h = s_tip_h; q[0].ver = 7;
    q[0].x = 344.0f; q[0].y = 208.0f; q[0].dw = 220.0f; q[0].dh = 70.0f;
    return 1;
}
