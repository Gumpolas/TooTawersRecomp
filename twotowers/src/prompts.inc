/* prompts.inc -- PlayStation and keyboard / mouse button prompts
 * ([Input] prompts, F10 menu).
 *
 * The game shows its button prompts -- the combo lists on the upgrade screen,
 * "Help", the in-game hints -- with small textures of the Xbox pad's
 * buttons. With prompts=playstation (or "auto" and a PlayStation pad as
 * player 1) they are redrawn as they are decoded, in the PlayStation's
 * symbols: the pad buttons in the same places, so A is the cross, B the
 * circle, X the square, Y the triangle, and the right trigger R2.
 * Recognised by their content hash (xbox_tex_observer), as the other texture
 * mods are; drawn here, nothing from any other game is used. */

#define PR_A 0x13DF2C89u
#define PR_B 0x62764416u
#define PR_X 0x6BB46B89u
#define PR_Y 0x5E463DBFu
#define PR_R 0x71E4228Au
#define PR_L 0xDBA098CAu   /* (1.9.3) the left trigger (the Secret Codes' "Pull and hold L R") */
/* in play: the prologue's hints ("Defend", "Speed Attack", ...), the
 * level's own pictures of X, Y and A (64x64) */
#define PRG_X 0x53E9D14Fu
#define PRG_Y 0xBD2B2EB4u
#define PRG_A 0xCF4AB4D7u

extern int xbox_InputPlayerPadIsPlayStation(DWORD port);
extern int xbox_InputPlayerPadPlayStationModel(DWORD port);
extern int xbox_InputAnyPlayStationModel(void);
extern int xbox_InputLastWasKeyboard(void);
extern int xbox_BindKey(int a, int i);
extern const char *xbox_BindKeyName(int vk);
extern volatile long xbox_bind_rev;
#include "prompts_sig.h"

extern int xbox_BindPad(int action);
/* 0 Xbox, 1 PlayStation, 2 keyboard / mouse, 3 Xbox One: what the textures should show now */
static int prompts_kind(void)
{
    int m = xbox_GfxGet(GFX_PROMPTS);          /* 0 auto, 1 xbox, 2 xbox one, 3 playstation, 4 keyboard */
    const char *e = getenv("TT_PROMPTS_KIND");  /* testing */
    if (e) return atoi(e);
    if (m == 4) return 2;
    if (m == 3) return 1;
    if (m == 2) return 3;
    if (m == 1) return 0;
    if (xbox_InputLastWasKeyboard()) return 2;
    return xbox_InputPlayerPadIsPlayStation(0) ? 1 : 0;
}
/* what is drawn now: prompts_kind() latched by prompts_tick, at most one
 * change in three seconds under "automatic" (each change re-decodes the
 * game's textures: a pad and the mouse used together must not do that
 * over and over) */
static int s_pr_kind = -1;
static int prompts_kind_now(void) { return s_pr_kind >= 0 ? s_pr_kind : prompts_kind(); }

/* coverage of one pixel by a shape, 4x4 samples; inside(u, v) in -1..1 */
typedef int (*PrShape)(float u, float v, const void *ctx);
static float pr_cover(int x, int y, int w, int h, PrShape f, const void *ctx)
{
    int i, j, n = 0;
    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++) {
            float u = ((x + (i + 0.5f) / 4.0f) / w) * 2.0f - 1.0f;
            float v = ((y + (j + 0.5f) / 4.0f) / h) * 2.0f - 1.0f;
            n += f(u, v, ctx) ? 1 : 0;
        }
    return n / 16.0f;
}

static void pr_blend(uint32_t *p, float a, uint32_t rgb)
{
    uint32_t d = *p;
    float da = (float)(d >> 24) / 255.0f, oa = a + da * (1 - a), ia = da * (1 - a);
    float r, g, b;
    if (a <= 0 || oa <= 0) return;
    r = (((rgb >> 16) & 255) * a + ((d >> 16) & 255) * ia) / oa;
    g = (((rgb >> 8) & 255) * a + ((d >> 8) & 255) * ia) / oa;
    b = ((rgb & 255) * a + (d & 255) * ia) / oa;
    *p = ((uint32_t)(oa * 255 + 0.5f) << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static int pr_disc(float u, float v, const void *c) { float r = *(const float *)c; return u * u + v * v <= r * r; }
static int pr_ring(float u, float v, const void *c)
{
    const float *k = (const float *)c;            /* outer, inner radius */
    float d = u * u + v * v;
    return d <= k[0] * k[0] && d >= k[1] * k[1];
}
static float pr_seg(float u, float v, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay, t = ((u - ax) * dx + (v - ay) * dy) / (dx * dx + dy * dy);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    dx = u - (ax + t * dx); dy = v - (ay + t * dy);
    return sqrtf(dx * dx + dy * dy);
}
static int pr_cross(float u, float v, const void *c)
{
    float s = ((const float *)c)[0], t = ((const float *)c)[1];
    return pr_seg(u, v, -s, -s, s, s) <= t || pr_seg(u, v, -s, s, s, -s) <= t;
}
static int pr_square(float u, float v, const void *c)
{
    float s = ((const float *)c)[0], t = ((const float *)c)[1];
    float au = fabsf(u), av = fabsf(v), m = au > av ? au : av;
    return m <= s + t && m >= s - t;
}
static int pr_triangle(float u, float v, const void *c)
{
    float s = ((const float *)c)[0], t = ((const float *)c)[1];
    float ax = 0, ay = -s * 1.02f, bx = s * 0.95f, by = s * 0.62f, cx = -s * 0.95f, cy = s * 0.62f;
    v += s * 0.06f;
    return pr_seg(u, v, ax, ay, bx, by) <= t || pr_seg(u, v, bx, by, cx, cy) <= t || pr_seg(u, v, cx, cy, ax, ay) <= t;
}

/* "R2" in thick strokes on a rounded dark key, like the retail R key */
static int pr_glyphs(float u, float v, const void *c)
{
    const float t = 0.085f;
    (void)c;
    /* R: stem, bowl, leg -- in the left half */
    if (pr_seg(u, v, -0.62f, -0.42f, -0.62f, 0.42f) <= t) return 1;
    if (pr_seg(u, v, -0.62f, -0.42f, -0.32f, -0.42f) <= t) return 1;
    if (pr_seg(u, v, -0.62f, 0.0f, -0.32f, 0.0f) <= t) return 1;
    {   /* the bowl's right side, a half circle */
        float du = u + 0.32f, dv = v + 0.21f, d = sqrtf(du * du + dv * dv);
        if (du >= 0 && fabsf(d - 0.21f) <= t) return 1;
    }
    if (pr_seg(u, v, -0.42f, 0.0f, -0.18f, 0.42f) <= t) return 1;
    /* 2 -- in the right half */
    {
        float du = u - 0.36f, dv = v + 0.17f, d = sqrtf(du * du + dv * dv);
        if (fabsf(d - 0.23f) <= t && (dv <= 0.02f || du >= 0.10f)) return 1;
    }
    if (pr_seg(u, v, 0.54f, -0.06f, 0.12f, 0.42f) <= t) return 1;
    if (pr_seg(u, v, 0.12f, 0.42f, 0.62f, 0.42f) <= t) return 1;
    return 0;
}
static int pr_key(float u, float v, const void *c)
{
    float s = *(const float *)c, r = 0.28f;
    float au = fabsf(u) - (s - r), av = fabsf(v) - (s - r);
    if (au <= 0 || av <= 0) return fabsf(u) <= s && fabsf(v) <= s;
    return au * au + av * av <= r * r;
}

/* a dark glossy round button with the symbol in its colour */
static void pr_button(uint32_t *a, int w, int h, int sym)
{
    static const uint32_t col[4] = { 0x7FA7E8, 0xFF5F6B, 0xE88ACF, 0x3FD8A8 };   /* cross, circle, square, triangle */
    float rdisc = 0.92f, rrim[2] = { 0.92f, 0.80f };
    int x, y;
    memset(a, 0, (size_t)w * h * 4);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t *p = &a[y * w + x];
            float cv = pr_cover(x, y, w, h, pr_disc, &rdisc);
            if (cv > 0) {
                /* body: charcoal, lighter toward the top left */
                float u = ((x + 0.5f) / w) * 2 - 1, v = ((y + 0.5f) / h) * 2 - 1;
                float k = 0.55f - 0.25f * (u + v) * 0.5f;
                uint32_t g = (uint32_t)(40 + 50 * k);
                pr_blend(p, cv, (g << 16) | (g << 8) | (g + 6));
            }
            cv = pr_cover(x, y, w, h, pr_ring, rrim);
            if (cv > 0) pr_blend(p, cv * 0.85f, 0x9A9AA2);
            {
                float c[2];
                PrShape f;
                switch (sym) {
                case 0: c[0] = 0.40f; c[1] = 0.085f; f = pr_cross; break;
                case 1: c[0] = 0.42f; c[1] = 0.32f; f = pr_ring; break;
                case 2: c[0] = 0.36f; c[1] = 0.075f; f = pr_square; break;
                default: c[0] = 0.44f; c[1] = 0.075f; f = pr_triangle; break;
                }
                cv = pr_cover(x, y, w, h, f, c);
                if (cv > 0) pr_blend(p, cv, col[sym]);
            }
        }
}

static void pr_r2(uint32_t *a, int w, int h)
{
    float s = 0.86f, s2 = 0.74f;
    int x, y;
    memset(a, 0, (size_t)w * h * 4);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t *p = &a[y * w + x];
            float cv = pr_cover(x, y, w, h, pr_key, &s);
            if (cv > 0) pr_blend(p, cv, 0x9A9AA2);
            cv = pr_cover(x, y, w, h, pr_key, &s2);
            if (cv > 0) pr_blend(p, cv, 0x2C2C32);
            cv = pr_cover(x, y, w, h, pr_glyphs, NULL);
            if (cv > 0) pr_blend(p, cv, 0xF2F2F2);
        }
}

/* ---- keyboard and mouse prompts ----
 * With prompts=keyboard (or "auto" and the keys were what player 1 used
 * last) each button prompt is drawn as the key or mouse button bound to that
 * button now ([Input] kb_a ... / the F10 menu's controls): a keycap with the
 * key's name, or a mouse with the button lit. Drawn here in strokes. */

/* a stroke font: each glyph polylines on a 4 x 6 grid, "x,y x,y|x,y ..." */
static const char *pr_glyph(int c)
{
    switch (c) {
    case 'A': return "0,6 0,2 2,0 4,2 4,6|0,3.6 4,3.6";
    case 'B': return "0,3 3,3 4,4 4,5 3,6 0,6 0,0 3,0 4,1 4,2 3,3";
    case 'C': return "4,1 3,0 1,0 0,1 0,5 1,6 3,6 4,5";
    case 'D': return "0,0 0,6 2.4,6 4,4.4 4,1.6 2.4,0 0,0";
    case 'E': return "4,0 0,0 0,6 4,6|0,3 3,3";
    case 'F': return "4,0 0,0 0,6|0,3 3,3";
    case 'G': return "4,1 3,0 1,0 0,1 0,5 1,6 3,6 4,5 4,3.4 2.4,3.4";
    case 'H': return "0,0 0,6|4,0 4,6|0,3 4,3";
    case 'I': return "1,0 3,0|2,0 2,6|1,6 3,6";
    case 'J': return "4,0 4,5 3,6 1,6 0,5";
    case 'K': return "0,0 0,6|4,0 0,4|1.3,3.1 4,6";
    case 'L': return "0,0 0,6 4,6";
    case 'M': return "0,6 0,0 2,3.2 4,0 4,6";
    case 'N': return "0,6 0,0 4,6 4,0";
    case 'O': return "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0";
    case 'P': return "0,6 0,0 3,0 4,1 4,2 3,3 0,3";
    case 'Q': return "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0|2.4,4.4 4,6.2";
    case 'R': return "0,6 0,0 3,0 4,1 4,2 3,3 0,3|2,3 4,6";
    case 'S': return "4,1 3,0 1,0 0,1 0,2 1,3 3,3 4,4 4,5 3,6 1,6 0,5";
    case 'T': return "0,0 4,0|2,0 2,6";
    case 'U': return "0,0 0,5 1,6 3,6 4,5 4,0";
    case 'V': return "0,0 2,6 4,0";
    case 'W': return "0,0 1,6 2,2.6 3,6 4,0";
    case 'X': return "0,0 4,6|4,0 0,6";
    case 'Y': return "0,0 2,3 4,0|2,3 2,6";
    case 'Z': return "0,0 4,0 0,6 4,6";
    case '0': return "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0|3.6,1 0.4,5";
    case '1': return "0.8,1.2 2,0 2,6|0.8,6 3.2,6";
    case '2': return "0,1 1,0 3,0 4,1 4,2.2 0,6 4,6";
    case '3': return "0,1 1,0 3,0 4,1 4,2 3,3 1.4,3|3,3 4,4 4,5 3,6 1,6 0,5";
    case '4': return "3,6 3,0 0,4.2 4,4.2";
    case '5': return "4,0 0.4,0 0,3 3,2.6 4,3.6 4,5 3,6 1,6 0,5";
    case '6': return "3.6,0.4 2.6,0 1,0 0,1 0,5 1,6 3,6 4,5 4,4 3,3 1,3 0,4";
    case '7': return "0,0 4,0 1.4,6";
    case '8': return "1,3 0,2 0,1 1,0 3,0 4,1 4,2 3,3 1,3 0,4 0,5 1,6 3,6 4,5 4,4 3,3";
    case '9': return "4,2 3,3 1,3 0,2 0,1 1,0 3,0 4,1 4,5 3,6 1.4,6 0.4,5.6";
    case '-': return "0.8,3 3.2,3";
    case '+': return "0.6,3 3.4,3|2,1.6 2,4.4";
    case '*': return "2,1.4 2,4.6|0.6,2.2 3.4,3.8|3.4,2.2 0.6,3.8";
    case '/': return "3.4,0 0.6,6";
    case '\\': return "0.6,0 3.4,6";
    case '.': return "1.8,5.7 2.2,5.7";
    case ',': return "2.2,5 1.4,6.6";
    case ';': return "2,1.8 2,2.2|2.2,5 1.4,6.6";
    case '=': return "0.6,2 3.4,2|0.6,4 3.4,4";
    case '[': return "3,0 1.4,0 1.4,6 3,6";
    case ']': return "1,0 2.6,0 2.6,6 1,6";
    case '`': return "1.4,0 2.6,1.4";
    case '\'': return "2,0 2,1.8";
    case 1: return "2,6 2,0.2|0.4,1.8 2,0.2 3.6,1.8";   /* arrows: up, down, left, right */
    case 2: return "2,0 2,5.8|0.4,4.2 2,5.8 3.6,4.2";
    case 3: return "4,3 0.2,3|1.8,1.4 0.2,3 1.8,4.6";
    case 4: return "0,3 3.8,3|2.2,1.4 3.8,3 2.2,4.6";
    default: return "1,1 3,1 3,5 1,5 1,1";
    }
}

/* the strokes of a line of text, placed: x0, y0 the top left, s the size of
 * one grid unit (all in -1..1 texture space) */
#define PR_MAXSEG 512
typedef struct { int n; float t; float s[PR_MAXSEG][5]; } PrText;   /* t: the least thickness (the texture's size) */
static void pr_text_add(PrText *T, const char *str, float x0, float y0, float s)
{
    for (; *str; str++, x0 += 5.2f * s) {
        const char *g = pr_glyph((unsigned char)*str);
        float px = 0, py = 0, x, y;
        int have = 0, used;
        if (*str == ' ') continue;
        while (*g) {
            if (*g == '|') { have = 0; g++; continue; }
            if (*g == ' ') { g++; continue; }
            if (sscanf(g, "%f,%f%n", &x, &y, &used) != 2) break;
            g += used;
            if (have && T->n < PR_MAXSEG) {
                float *q = T->s[T->n++];
                q[0] = x0 + px * s; q[1] = y0 + py * s; q[2] = x0 + x * s; q[3] = y0 + y * s;
                q[4] = 0.82f * s > T->t ? 0.82f * s : T->t;   /* bolder as the letters grow */
            }
            px = x; py = y; have = 1;
        }
    }
}
static int pr_text_in(float u, float v, const void *c)
{
    const PrText *T = (const PrText *)c;
    int i;
    for (i = 0; i < T->n; i++) if (pr_seg(u, v, T->s[i][0], T->s[i][1], T->s[i][2], T->s[i][3]) <= T->s[i][4]) return 1;
    return 0;
}
static float pr_text_w(const char *s) { int n = (int)strlen(s); return n ? 5.2f * n - 1.2f : 0; }   /* grid units */

/* the key's name on its cap: one or two lines, the second (if any) the big one */
static void pr_key_label(int vk, char *l1, char *l2)
{
    static const struct { int vk; const char *a, *b; } names[] = {
        { VK_SPACE, "", "SPACE" }, { VK_RETURN, "", "ENTER" }, { VK_ESCAPE, "", "ESC" }, { VK_TAB, "", "TAB" },
        { VK_BACK, "BACK", "SPACE" }, { VK_SHIFT, "", "SHIFT" }, { VK_CONTROL, "", "CTRL" }, { VK_MENU, "", "ALT" },
        { VK_CAPITAL, "CAPS", "LOCK" }, { VK_UP, "", "\001" }, { VK_DOWN, "", "\002" }, { VK_LEFT, "", "\003" },
        { VK_RIGHT, "", "\004" }, { VK_INSERT, "", "INS" }, { VK_DELETE, "", "DEL" }, { VK_HOME, "", "HOME" },
        { VK_END, "", "END" }, { VK_PRIOR, "PAGE", "UP" }, { VK_NEXT, "PAGE", "DOWN" },
        { VK_MULTIPLY, "NUM", "*" }, { VK_ADD, "NUM", "+" }, { VK_SUBTRACT, "NUM", "-" }, { VK_DECIMAL, "NUM", "." },
        { VK_DIVIDE, "NUM", "/" }, { VK_OEM_1, "", ";" }, { VK_OEM_PLUS, "", "=" }, { VK_OEM_COMMA, "", "," },
        { VK_OEM_MINUS, "", "-" }, { VK_OEM_PERIOD, "", "." }, { VK_OEM_2, "", "/" }, { VK_OEM_3, "", "`" },
        { VK_OEM_4, "", "[" }, { VK_OEM_5, "", "\\" }, { VK_OEM_6, "", "]" }, { VK_OEM_7, "", "'" },
        { VK_LSHIFT, "", "SHIFT" }, { VK_RSHIFT, "", "SHIFT" }, { VK_LCONTROL, "", "CTRL" }, { VK_RCONTROL, "", "CTRL" },
        { VK_LMENU, "", "ALT" }, { VK_RMENU, "", "ALT" },
    };
    size_t i;
    l1[0] = l2[0] = 0;
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (names[i].vk == vk) { strcpy(l1, names[i].a); strcpy(l2, names[i].b); return; }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { l2[0] = (char)vk; l2[1] = 0; return; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) { strcpy(l1, "NUM"); l2[0] = (char)('0' + vk - VK_NUMPAD0); l2[1] = 0; return; }
    if (vk >= VK_F1 && vk <= VK_F24) { sprintf(l2, "F%d", vk - VK_F1 + 1); return; }
    sprintf(l2, "%02X", vk & 0xFF);
}

/* a keyboard key: a light cap on a darker base, the name in dark strokes */
static void pr_keycap(uint32_t *a, int w, int h, int vk)
{
    static PrText T;
    char l1[16], l2[16];
    float base = 0.90f, top = 0.76f, ty = -0.08f;   /* the cap's face sits a little high: the key's front shows below */
    int x, y;
    pr_key_label(vk, l1, l2);
    T.n = 0;
    T.t = w <= 32 ? 0.045f : 0.03f;   /* at least about a pixel and a half wide */
    {
        /* the big line fits the face's width (0.62 unit high at most); the small one above it */
        float fw = 1.30f, s2 = 0.62f / 6.0f, s1;
        float w2 = pr_text_w(l2) * s2;
        if (w2 > fw) s2 *= fw / w2, w2 = fw;
        if (!l1[0]) pr_text_add(&T, l2, -w2 / 2, ty - 3.0f * s2, s2);
        else {
            float w1;
            s1 = 0.30f / 6.0f;
            if (s2 > 0.42f / 6.0f) s2 = 0.42f / 6.0f, w2 = pr_text_w(l2) * s2;
            w1 = pr_text_w(l1) * s1;
            if (w1 > fw) s1 *= fw / w1, w1 = fw;
            pr_text_add(&T, l1, -w1 / 2, ty - 0.46f, s1);
            pr_text_add(&T, l2, -w2 / 2, ty - 0.04f, s2);
        }
    }
    memset(a, 0, (size_t)w * h * 4);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t *p = &a[y * w + x];
            float cv = pr_cover(x, y, w, h, pr_key, &base);
            if (cv > 0) pr_blend(p, cv, 0x55565E);
            {
                /* the face: shifted up, a soft top-to-bottom shade */
                float v = ((y + 0.5f) / h) * 2 - 1;
                int yy = y + (int)(0.10f * h / 2);   /* sample the shape lower = the face drawn higher */
                cv = pr_cover(x, yy, w, h, pr_key, &top);
                if (cv > 0) {
                    uint32_t g = (uint32_t)(236 - 26 * (v + 1) / 2);
                    pr_blend(p, cv, (g << 16) | (g << 8) | (g + 4 > 255 ? 255 : g + 4));
                }
            }
            cv = pr_cover(x, y, w, h, pr_text_in, &T);
            if (cv > 0) pr_blend(p, cv, 0x1E1F24);
        }
}

/* a mouse, the button lit: 0 left, 1 right, 2 middle (the wheel), 3/4 the side buttons */
static int pr_mouse_body(float u, float v, const void *c)
{
    float sx = 0.62f, sy = 0.90f, k;
    (void)c;
    /* an egg: rounder at the bottom */
    k = (u / sx) * (u / sx) + (v / sy) * (v / sy);
    if (v > 0) k = (u / sx) * (u / sx) + (v / (sy * 0.98f)) * (v / (sy * 0.98f));
    return k <= 1.0f;
}
static int pr_mouse_part(float u, float v, const void *c)
{
    int part = *(const int *)c;
    if (!pr_mouse_body(u / 0.86f, (v + 0.06f) / 0.88f, NULL)) return 0;
    switch (part) {
    case 0: return v < -0.12f && u < -0.045f;
    case 1: return v < -0.12f && u > 0.045f;
    case 2: return fabsf(u) <= 0.09f && v > -0.62f && v < -0.24f;
    case 3: return u < -0.30f && v > 0.0f && v < 0.22f;
    default: return u < -0.30f && v > -0.06f && v < -0.0f + 0.0f ? 0 : (u < -0.30f && v > 0.26f && v < 0.48f);
    }
}
static int pr_mouse_lines(float u, float v, const void *c)
{
    (void)c;
    if (!pr_mouse_body(u / 0.86f, (v + 0.06f) / 0.88f, NULL)) return 0;
    if (fabsf(u) < 0.03f && v < -0.12f) return 1;          /* between the buttons */
    if (fabsf(v + 0.14f) < 0.03f) return 1;                 /* under them */
    return 0;
}
static void pr_mouse(uint32_t *a, int w, int h, int part)
{
    static const uint32_t lit = 0xF2B233;   /* gold, as the game's own highlights */
    int x, y, wheel = 2;
    memset(a, 0, (size_t)w * h * 4);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t *p = &a[y * w + x];
            float cv = pr_cover(x, y, w, h, pr_mouse_body, NULL);
            if (cv > 0) pr_blend(p, cv, 0x4A4B52);
            {
                float u = ((x + 0.5f) / w) * 2 - 1, v = ((y + 0.5f) / h) * 2 - 1;
                cv = 0;
                {
                    int i, j, n = 0;
                    for (j = 0; j < 4; j++) for (i = 0; i < 4; i++) {
                        float uu = ((x + (i + 0.5f) / 4.0f) / w) * 2.0f - 1.0f, vv = ((y + (j + 0.5f) / 4.0f) / h) * 2.0f - 1.0f;
                        n += pr_mouse_body(uu / 0.86f, (vv + 0.06f) / 0.88f, NULL);
                    }
                    cv = n / 16.0f;
                }
                if (cv > 0) {
                    uint32_t g = (uint32_t)(232 - 30 * (v + 1) / 2 - 10 * (u + 1) / 2);
                    pr_blend(p, cv, (g << 16) | (g << 8) | (g + 4 > 255 ? 255 : g + 4));
                }
            }
            cv = pr_cover(x, y, w, h, pr_mouse_part, &part);
            if (cv > 0) pr_blend(p, cv, lit);
            if (part != 2) {
                cv = pr_cover(x, y, w, h, pr_mouse_part, &wheel);
                if (cv > 0) pr_blend(p, cv, 0x6A6B72);
            }
            cv = pr_cover(x, y, w, h, pr_mouse_lines, NULL);
            if (cv > 0) pr_blend(p, cv * 0.9f, 0x3A3B42);
        }
}

/* button a's prompt as its first bound key or mouse button */
extern int tt_msg_up(void);
static void pr_bound(uint32_t *a, int w, int h, int act)
{
    int i, vk = 0;
    if (act == 0 && tt_msg_up() && xbox_BindKey(8, 0)) act = 8;   /* a "Press A to continue" message: Start's key (Enter), which closes it too (menus.c) */
    for (i = 0; i < 3 && !vk; i++) vk = xbox_BindKey(act, i);
    switch (vk) {
    case 0: return;                                  /* nothing bound: the pad's button stays */
    case VK_LBUTTON: pr_mouse(a, w, h, 0); break;
    case VK_RBUTTON: pr_mouse(a, w, h, 1); break;
    case VK_MBUTTON: pr_mouse(a, w, h, 2); break;
    case VK_XBUTTON1: pr_mouse(a, w, h, 3); break;
    case VK_XBUTTON2: pr_mouse(a, w, h, 4); break;
    default: pr_keycap(a, w, h, vk); break;
    }
}

/* ---- the loading screens' controller pictures ----
 * Three loading screens explain the controls on a picture of the Xbox pad
 * (UI\loadl2..4: "Speed attack" on X, "Fierce attack"/"Fire" on Y and A, and
 * the full layout), with small "pressed" pictures flashed over its buttons.
 * With PlayStation prompts these become a DualShock 3, DualShock 4 or
 * DualSense -- whichever player 1 holds (else the last PlayStation pad seen,
 * else a DualShock 4; [Input] ps_pad / TT_PS_PAD picks one) -- from
 * prompts\ps<n>_<name>.bmp, drawn so that the game's own lines and labels
 * still point at the right buttons. Recognised by their look (prompts_sig.h),
 * not their hash: the pictures are compressed, and what a decoder makes of
 * them is not fixed to the bit. */
static int prompts_model(void)
{
    const char *e = getenv("TT_PS_PAD");
    int m;
    if (e && (*e == '3' || *e == '4' || *e == '5')) return *e - '0';
    if (e && (e[0] == 'p' || e[0] == 'P') && (e[1] == 's' || e[1] == 'S') && e[2] >= '3' && e[2] <= '5') return e[2] - '0';
    m = xbox_InputPlayerPadPlayStationModel(0);
    if (!m) m = xbox_InputAnyPlayStationModel();
    return m ? m : 4;
}

static uint32_t *pr_bmp_load(const char *rel, int *ow, int *oh)
{
    FILE *f;
    unsigned char hd[54];
    int w, h, y, bottom_up;
    uint32_t off, *out;
    if (!(f = tt_exe_fopen(rel))) return NULL;
    if (fread(hd, 1, 54, f) != 54 || hd[0] != 'B' || hd[1] != 'M' || hd[28] != 32) { fclose(f); return NULL; }
    off = hd[10] | (hd[11] << 8) | (hd[12] << 16) | ((uint32_t)hd[13] << 24);
    w = (int)(hd[18] | (hd[19] << 8) | (hd[20] << 16) | ((uint32_t)hd[21] << 24));
    h = (int)(hd[22] | (hd[23] << 8) | (hd[24] << 16) | ((uint32_t)hd[25] << 24));
    bottom_up = h > 0;
    if (h < 0) h = -h;
    if (w <= 0 || w > 2048 || h <= 0 || h > 2048) { fclose(f); return NULL; }
    out = (uint32_t *)malloc((size_t)w * h * 4);
    if (!out) { fclose(f); return NULL; }
    fseek(f, (long)off, SEEK_SET);
    for (y = 0; y < h; y++) {      /* BGRA in the file is ARGB in a little-endian dword */
        uint32_t *row = out + (size_t)(bottom_up ? h - 1 - y : y) * w;
        if (fread(row, 4, (size_t)w, f) != (size_t)w) { free(out); fclose(f); return NULL; }
    }
    fclose(f);
    *ow = w; *oh = h;
    return out;
}

/* which of the game's pictures this is, or -1 */
static int pr_which(const uint32_t *a, int w, int h)
{
    int i, best = -1;
    float bestd = 3.0f;          /* mean difference per channel, out of 255 */
    for (i = 0; i < (int)(sizeof s_pr_sig / sizeof s_pr_sig[0]); i++) {
        const PrSig *S = &s_pr_sig[i];
        int g = S->g, cw = w / g, ch = h / g, cx, cy, x, y, c;
        float d = 0;
        if (S->w != w || S->h != h) continue;
        for (cy = 0; cy < g && d < bestd * g * g * 4; cy++)
            for (cx = 0; cx < g; cx++) {
                uint32_t sum[4] = { 0, 0, 0, 0 };
                for (y = 0; y < ch; y++) {
                    const uint32_t *row = a + (size_t)(cy * ch + y) * w + cx * cw;
                    for (x = 0; x < cw; x++) {
                        uint32_t p = row[x];
                        sum[0] += p >> 24; sum[1] += (p >> 16) & 255; sum[2] += (p >> 8) & 255; sum[3] += p & 255;
                    }
                }
                for (c = 0; c < 4; c++) {
                    float m = (float)sum[c] / (float)(cw * ch);
                    d += fabsf(m - (float)S->sig[(cy * g + cx) * 4 + c]);
                }
            }
        d /= (float)(g * g * 4);
        if (d < bestd) { bestd = d; best = i; }
    }
    return best;
}

static DWORD s_kb_load_seen;   /* when a loading screen's controls picture was last drawn */
static void kb_txt_apply(int keys);
static void prompts_pad_pic(uint32_t *argb, int w, int h)
{
    static struct { int model; uint32_t *px; int w, h; } cache[8];
    int k, n = (int)(sizeof s_pr_sig / sizeof s_pr_sig[0]), model;
    if (!((w == 512 && (h == 256 || h == 512)) || (w == 32 && h == 32) || (w == 64 && h == 64))) return;
    k = pr_which(argb, w, h);
    if (k < 0 || k >= n || k >= 8) return;
    if (!strncmp(s_pr_sig[k].file, "loadl", 5)) { s_kb_load_seen = GetTickCount(); kb_txt_apply(2); }   /* the hint's words: the controller's buttons */
    model = prompts_kind_now() == 3 ? 1 : prompts_model();   /* 1: the Xbox One pad (prompts\x1_*) */
    if (cache[k].model != model) {
        char rel[96];
        free(cache[k].px);
        cache[k].px = NULL;
        cache[k].model = model;
        if (model == 1)
            snprintf(rel, sizeof rel, "prompts/x1_%s.bmp", s_pr_sig[k].file);
        else if (!strcmp(s_pr_sig[k].file, "l2_pressed"))
            snprintf(rel, sizeof rel, "prompts/ps_%s.bmp", s_pr_sig[k].file);
        else
            snprintf(rel, sizeof rel, "prompts/ps%d_%s.bmp", model, s_pr_sig[k].file);
        cache[k].px = pr_bmp_load(rel, &cache[k].w, &cache[k].h);
        tt_dbg("[PROMPTS] %s for the game's %s picture%s\n", rel, s_pr_sig[k].file,
               cache[k].px ? "" : ": not found, the Xbox picture stays");
    }
    if (cache[k].px && cache[k].w == w && cache[k].h == h)
        memcpy(argb, cache[k].px, (size_t)w * h * 4);
}

#include "prompts_kb.inc"

/* ---- controller prompts that follow the bindings (1.7.8) ----
 * Each prompt texture stands for one of the game's buttons (A B X Y, the
 * right trigger); the button of your own controller bound to it (F10 /
 * launcher Controls, [Controls] pad_a ...) is what is drawn, in the style
 * chosen: the PlayStation's symbols, the Xbox One's lettered buttons, or the
 * original Xbox's jewels (the game's own picture while nothing is rebound).
 * Pad sources: 1..16 = A B X Y LB RB LT RT Start Back Up Down Left Right LS RS. */
static const int k_pr_def_src[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };   /* the game's A B X Y Black White LT RT, as bound by default */

/* a few glyphs of our own in the stroke font: 5 the menu (three lines), 6 the view (two windows) */
static const char *pr_glyph_icon(int c)
{
    switch (c) {
    case 5: return "0.4,1.6 3.6,1.6|0.4,3 3.6,3|0.4,4.4 3.6,4.4";
    case 6: return "0.2,2 2.6,2 2.6,5 0.2,5 0.2,2|1.4,2 1.4,1 3.8,1 3.8,4 2.6,4";
    default: return NULL;
    }
}
static void pr_text_fit(PrText *T, const char *str, float maxw, float maxh, float cy)
{
    float sc = maxh / 6.0f, w = pr_text_w(str) * sc;
    if (w > maxw) { sc *= maxw / w; w = maxw; }
    if (str[0] == 5 || str[0] == 6) {   /* an icon: drawn from its own strokes */
        const char *g = pr_glyph_icon(str[0]);
        float px = 0, py = 0, x, y, x0 = -2.0f * sc, y0 = cy - 3.0f * sc; int have = 0, used;
        while (*g) {
            if (*g == '|') { have = 0; g++; continue; }
            if (*g == ' ') { g++; continue; }
            if (sscanf(g, "%f,%f%n", &x, &y, &used) != 2) break;
            g += used;
            if (have && T->n < PR_MAXSEG) {
                float *q = T->s[T->n++];
                q[0] = x0 + px * sc; q[1] = y0 + py * sc; q[2] = x0 + x * sc; q[3] = y0 + y * sc;
                q[4] = 0.36f * sc > T->t ? 0.36f * sc : T->t;   /* (thinner than the letters: fine lines) */
            }
            px = x; py = y; have = 1;
        }
        return;
    }
    pr_text_add(T, str, -w / 2, cy - 3.0f * sc, sc);
}

/* a round button: body colour (with a soft top light), rim, the text on it */
static void pr_round(uint32_t *a, int w, int h, uint32_t body, uint32_t rim, const char *txt, uint32_t tcol, float tsize)
{
    static PrText T;
    float rdisc = 0.92f, rrim[2] = { 0.92f, 0.82f };
    int x, y;
    T.n = 0; T.t = w <= 32 ? 0.05f : 0.035f;
    if (txt && *txt) pr_text_fit(&T, txt, 1.05f, tsize, 0.0f);
    memset(a, 0, (size_t)w * h * 4);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t *p = &a[y * w + x];
            float cv = pr_cover(x, y, w, h, pr_disc, &rdisc);
            if (cv > 0) {
                float u = ((x + 0.5f) / w) * 2 - 1, v = ((y + 0.5f) / h) * 2 - 1, k = 1.0f + 0.22f * (-(u + v) * 0.5f);
                uint32_t r = (uint32_t)fminf(255.0f, ((body >> 16) & 255) * k), g = (uint32_t)fminf(255.0f, ((body >> 8) & 255) * k),
                         b = (uint32_t)fminf(255.0f, (body & 255) * k);
                pr_blend(p, cv, (r << 16) | (g << 8) | b);
            }
            cv = pr_cover(x, y, w, h, pr_ring, rrim);
            if (cv > 0) pr_blend(p, cv * 0.85f, rim);
            if (T.n) { cv = pr_cover(x, y, w, h, pr_text_in, &T); if (cv > 0) pr_blend(p, cv, tcol); }
        }
}
/* a rounded key (a shoulder button, a trigger, Start...): the text on it */
static void pr_keytxt(uint32_t *a, int w, int h, uint32_t edge, uint32_t face, const char *txt, uint32_t tcol)
{
    static PrText T;
    float s = 0.86f, s2 = 0.74f;
    int x, y;
    T.n = 0; T.t = w <= 32 ? 0.05f : 0.035f;
    pr_text_fit(&T, txt, 1.25f, strlen(txt) <= 2 ? 0.80f : 0.56f, 0.0f);
    memset(a, 0, (size_t)w * h * 4);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            uint32_t *p = &a[y * w + x];
            float cv = pr_cover(x, y, w, h, pr_key, &s);
            if (cv > 0) pr_blend(p, cv, edge);
            cv = pr_cover(x, y, w, h, pr_key, &s2);
            if (cv > 0) pr_blend(p, cv, face);
            cv = pr_cover(x, y, w, h, pr_text_in, &T);
            if (cv > 0) pr_blend(p, cv, tcol);
        }
}

/* pad source src drawn in style kind (0 the original Xbox, 1 PlayStation, 3 Xbox One) */
static void pr_pad_src(uint32_t *a, int w, int h, int src, int kind)
{
    static const char *const arrows[4] = { "\001", "\002", "\003", "\004" };
    if (kind == 1) {                                        /* PlayStation */
        int model = prompts_model();
        switch (src) {
        case 1: case 2: case 3: case 4: pr_button(a, w, h, src - 1); return;
        case 5: pr_keytxt(a, w, h, 0x9A9AA2, 0x2C2C32, "L1", 0xF2F2F2); return;
        case 6: pr_keytxt(a, w, h, 0x9A9AA2, 0x2C2C32, "R1", 0xF2F2F2); return;
        case 7: pr_keytxt(a, w, h, 0x9A9AA2, 0x2C2C32, "L2", 0xF2F2F2); return;
        case 8: pr_r2(a, w, h); return;
        case 9: pr_keytxt(a, w, h, 0x9A9AA2, 0x2C2C32, model == 3 ? "START" : "OPTIONS", 0xF2F2F2); return;
        case 10: pr_keytxt(a, w, h, 0x9A9AA2, 0x2C2C32, model == 3 ? "SELECT" : model == 5 ? "CREATE" : "SHARE", 0xF2F2F2); return;
        case 15: pr_round(a, w, h, 0x2C2C32, 0x9A9AA2, "L3", 0xF2F2F2, 0.62f); return;
        case 16: pr_round(a, w, h, 0x2C2C32, 0x9A9AA2, "R3", 0xF2F2F2, 0.62f); return;
        default: if (src >= 11 && src <= 14) pr_keytxt(a, w, h, 0x9A9AA2, 0x2C2C32, arrows[src - 11], 0xF2F2F2); return;
        }
    }
    if (kind == 3) {                                        /* Xbox One / Series: black buttons, the letter in its colour */
        static const uint32_t lc[4] = { 0x5DC21E, 0xE8413B, 0x2F8BEA, 0xF2C21B };   /* A green, B red, X blue, Y yellow */
        static const char *const lt[4] = { "A", "B", "X", "Y" };
        static const char *const sh[4] = { "LB", "RB", "LT", "RT" };
        static const char menu[2] = { 5, 0 }, view[2] = { 6, 0 };
        if (src >= 1 && src <= 4) { pr_round(a, w, h, 0x1C1D21, 0x4A4B52, lt[src - 1], lc[src - 1], 0.95f); return; }
        if (src >= 5 && src <= 8) { pr_keytxt(a, w, h, 0x4A4B52, 0x1C1D21, sh[src - 5], 0xEDEDED); return; }
        if (src == 9) { pr_round(a, w, h, 0x1C1D21, 0x4A4B52, menu, 0xEDEDED, 0.78f); return; }
        if (src == 10) { pr_round(a, w, h, 0x1C1D21, 0x4A4B52, view, 0xEDEDED, 0.78f); return; }
        if (src >= 11 && src <= 14) { pr_keytxt(a, w, h, 0x4A4B52, 0x1C1D21, arrows[src - 11], 0xEDEDED); return; }
        if (src == 15) { pr_round(a, w, h, 0x1C1D21, 0x4A4B52, "LS", 0xEDEDED, 0.62f); return; }
        if (src == 16) { pr_round(a, w, h, 0x1C1D21, 0x4A4B52, "RS", 0xEDEDED, 0.62f); return; }
        return;
    }
    {                                                       /* the original Xbox: jewel buttons, Black and White */
        static const uint32_t jc[4] = { 0x3FAE3A, 0xD2312C, 0x2C62C8, 0xE6C21E };
        static const char *const lt[4] = { "A", "B", "X", "Y" };
        if (src >= 1 && src <= 4) { pr_round(a, w, h, jc[src - 1], 0xD8D8D8, lt[src - 1], 0x202020, 0.95f); return; }
        if (src == 5) { pr_round(a, w, h, 0x18181A, 0xA0A0A8, "", 0, 0); return; }        /* Black */
        if (src == 6) { pr_round(a, w, h, 0xE8E8EC, 0x808088, "", 0, 0); return; }        /* White */
        if (src == 7) { pr_keytxt(a, w, h, 0x9A9AA2, 0x3A3B42, "L", 0xF2F2F2); return; }
        if (src == 8) { pr_keytxt(a, w, h, 0x9A9AA2, 0x3A3B42, "R", 0xF2F2F2); return; }
        if (src == 9) { pr_keytxt(a, w, h, 0x9A9AA2, 0x3A3B42, "START", 0xF2F2F2); return; }
        if (src == 10) { pr_keytxt(a, w, h, 0x9A9AA2, 0x3A3B42, "BACK", 0xF2F2F2); return; }
        if (src >= 11 && src <= 14) { pr_keytxt(a, w, h, 0x9A9AA2, 0x3A3B42, arrows[src - 11], 0xF2F2F2); return; }
        if (src == 15) { pr_round(a, w, h, 0x3A3B42, 0x9A9AA2, "LS", 0xF2F2F2, 0.62f); return; }
        if (src == 16) { pr_round(a, w, h, 0x3A3B42, 0x9A9AA2, "RS", 0xF2F2F2, 0.62f); return; }
    }
}
/* the game's button act's prompt: the controller button bound to it */
static void pr_pad_bound(uint32_t *a, int w, int h, int act, int kind)
{
    int src = xbox_BindPad(act);
    if (getenv("TT_PADSRC")) {   /* testing: "act:src,act:src" -- as if bound so */
        const char *e = getenv("TT_PADSRC"); int ta, ts, used;
        while (sscanf(e, "%d:%d%n", &ta, &ts, &used) == 2) { if (ta == act) src = ts; e += used; if (*e == ',') e++; }
    }
    if (src <= 0 || src > 16) return;                       /* nothing bound: the game's picture stays */
    if (kind == 0 && src == k_pr_def_src[act]) return;      /* the original Xbox, as bound by default: the game's own */
    pr_pad_src(a, w, h, src, kind);
}

static void prompts_tex(uint32_t hash, uint32_t *argb, int w, int h)
{
    int kind;
    if (w < 8 || h < 8) return;
    kind = prompts_kind_now();
    if (kind == 2) {
        prompts_kb_pic(argb, w, h);
        switch (hash) {
        case PR_A: case PRG_A: pr_bound(argb, w, h, 0); break;
        case PR_B: pr_bound(argb, w, h, 1); break;
        case PR_X: case PRG_X: pr_bound(argb, w, h, 2); break;
        case PR_Y: case PRG_Y: pr_bound(argb, w, h, 3); break;
        case PR_R: pr_bound(argb, w, h, 7); break;
        case PR_L: pr_bound(argb, w, h, 6); break;
        default: break;
        }
        return;
    }
    if (kind == 1 || kind == 3) prompts_pad_pic(argb, w, h);   /* the loading screens' pad pictures: PlayStation, Xbox One */
    switch (hash) {
    case PR_A: case PRG_A: pr_pad_bound(argb, w, h, 0, kind); break;
    case PR_B: pr_pad_bound(argb, w, h, 1, kind); break;
    case PR_X: case PRG_X: pr_pad_bound(argb, w, h, 2, kind); break;
    case PR_Y: case PRG_Y: pr_pad_bound(argb, w, h, 3, kind); break;
    case PR_R: pr_pad_bound(argb, w, h, 7, kind); break;
    case PR_L: pr_pad_bound(argb, w, h, 6, kind); break;
    default: break;
    }
}


/* re-decode the textures when the choice (or player 1's pad) changes */
static void prompts_tick(void)
{
    if (getenv("TT_PRDUMP")) {   /* testing: every pad button in each style, 64 and 32 px, to prdump.bmp */
        static int done;
        if (!done) {
            static uint32_t big[3 * 96][16 * 64];
            static uint32_t t[64 * 64];
            int kd, sz, src, y; FILE *f;
            static const int kinds[3] = { 0, 1, 3 };
            done = 1;
            memset(big, 0x40, sizeof big);
            for (kd = 0; kd < 3; kd++)
                for (sz = 0; sz < 2; sz++)
                    for (src = 1; src <= 16; src++) {
                        int w = sz ? 32 : 64, yy, xx;
                        memset(t, 0, sizeof t);
                        pr_pad_src(t, w, w, src, kinds[kd]);
                        for (yy = 0; yy < w; yy++) for (xx = 0; xx < w; xx++) {
                            uint32_t c = t[yy * w + xx], a = c >> 24, bg = 0x404040;
                            uint32_t r = (((c >> 16) & 255) * a + ((bg >> 16) & 255) * (255 - a)) / 255, g = (((c >> 8) & 255) * a + ((bg >> 8) & 255) * (255 - a)) / 255, b = ((c & 255) * a + (bg & 255) * (255 - a)) / 255;
                            big[kd * 96 + (sz ? 64 : 0) + yy][(src - 1) * 64 + xx] = 0xFF000000u | (r << 16) | (g << 8) | b;
                        }
                    }
            if ((f = fopen("prdump.bmp", "wb"))) {
                unsigned char hd[54] = { 'B', 'M' };
                int W = 16 * 64, H = 3 * 96; uint32_t sz2 = (uint32_t)(W * H * 4);
                *(uint32_t *)(hd + 2) = 54 + sz2; *(uint32_t *)(hd + 10) = 54; *(uint32_t *)(hd + 14) = 40;
                *(int32_t *)(hd + 18) = W; *(int32_t *)(hd + 22) = -H; *(uint16_t *)(hd + 26) = 1; *(uint16_t *)(hd + 28) = 32;
                fwrite(hd, 1, 54, f);
                for (y = 0; y < H; y++) fwrite(big[y], 4, (size_t)W, f);
                fclose(f);
            }
        }
    }
    static int was = -1;
    static long bind_was;
    static DWORD changed_at;
    int kind = prompts_kind();
    if (s_pr_kind >= 0 && kind != s_pr_kind && xbox_GfxGet(GFX_PROMPTS) == 0 && GetTickCount() - changed_at < 3000u)
        kind = s_pr_kind;                                    /* (automatic: not again so soon) */
    if (kind != s_pr_kind) { s_pr_kind = kind; changed_at = GetTickCount(); }
    int ps = kind == 2 ? 9 : kind == 3 ? 10 : kind ? prompts_model() : 0;   /* 0 Xbox, 9 keys, 10 Xbox One, else the PlayStation pad drawn */
    long br = xbox_bind_rev;                                 /* drawn again when a key or a pad button is bound anew */
    {   /* (a "Press A to continue" message up or gone: A's picture again) */
        static int msg_was;
        int m = tt_msg_up();
        if (m != msg_was) { msg_was = m; if (ps == 9) InterlockedIncrement((volatile LONG *)&xbox_tex_rev); }
    }
    if (ps != was || br != bind_was) {
        if (was >= 0) InterlockedIncrement((volatile LONG *)&xbox_tex_rev);
        if ((was >= 0 || ps) && ps != was) tt_dbg("[PROMPTS] %s button prompts\n", ps == 3 ? "PlayStation (DualShock 3)"
                                   : ps == 4 ? "PlayStation (DualShock 4)" : ps == 5 ? "PlayStation (DualSense)"
                                   : ps == 9 ? "keyboard / mouse" : ps == 10 ? "Xbox One" : "Xbox");
        was = ps;
        bind_was = br;
        if (s_kb_txt_state >= 0 || ps == 9) kb_txt_apply(ps == 9 ? 1 : 2);   /* the hints' words, too (the controller's: its buttons as bound) */
    }
    {   /* now and then: the words again (a message's text may have been loaded anew) */
        static DWORD chk;
        if ((s_kb_txt_state >= 0 || ps == 9) && (int)(GetTickCount() - chk) > 2000) { chk = GetTickCount(); kb_txt_apply(ps == 9 ? 1 : 2); }
    }
    /* on a loading screen with the keys' picture: its words, once they are loaded */
    if (s_kb_txt_n == 0 && s_kb_load_seen && GetTickCount() - s_kb_load_seen < 30000u) kb_txt_apply(ps == 9 ? 1 : 2);
}
