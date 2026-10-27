/*
 * e3hud.c -- optional "ECTS 2002 demo" HUD for The Two Towers, and the
 * orange health pickups of the pre-release footage. (Files and ini keys keep
 * their first name, "e3", so existing settings files still work.)
 *
 * The pre-release build shown at ECTS 2002 had a different HUD from the
 * retail game:
 *  - the character's face in a medallion at the health sword's grip, and a
 *    more curved cross-guard;
 *  - a plain rectangular experience gauge (filling blue) over a "rune" orb,
 *    split in four quarters that fill clockwise from the top right as a
 *    combo is entered -- one quarter per attack, coloured by its kind -- with
 *    the combo's name in that colour beside it when it ends (Speed, Fierce,
 *    Special; Mixed turns the whole orb magenta);
 *  - the kill rating ("Good", "Excellent") beside the gauge's top, and an
 *    "EXP: x/y" readout after the combo name.
 * Retail kept the debug switch for it ("DEBUG/Display EXP and Runes") but
 * not the code, so this is a re-creation:
 *  - the orb, gauge outline and portrait are drawn over the frame by the
 *    renderer (xbox_overlay_cb);
 *  - the texts are HUD elements of the game's own, drawn by the game's HUD
 *    code in its own fonts (twotowers_manual.c wraps 0x000CF450);
 *  - the portrait is cut from the character model's own face texture as the
 *    game loads it, and the cross-guard is redrawn in the sword texture
 *    (xbox_tex_observer). Nothing from the game is stored here.
 * [Mods] e3_pickups=1 turns the green health vials orange, as they are in
 * pre-release and making-of videos.
 *
 * Enabled by [Mods] e3_hud=1 (set in the launcher before the game starts).
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tt_env.h"   /* (1.9.3) getenv looked up once per name */
#include <string.h>
#include <math.h>

typedef struct {
    const uint32_t *px;
    int w, h;
    unsigned ver;
    float x, y, dw, dh;
} XboxOverlayQuad;
typedef struct { uint16_t wButtons; uint8_t bAnalogButtons[8]; int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XPad;
typedef struct { uint32_t dwPacketNumber; XPad Gamepad; } XState;

extern int (*volatile xbox_overlay_cb)(XboxOverlayQuad *q, int max);
extern void (*volatile xbox_pad_observer)(unsigned long port, const XState *st);
extern void (*volatile xbox_tex_observer)(uint32_t hash, uint32_t *argb, int w, int h);
extern volatile long xbox_tex_rev;
extern int xbox_FrameUsedTexture(uint32_t hash);
extern ptrdiff_t xbox_GetMemoryOffset(void);
extern int xbox_GfxGet(int id);
extern int xbox_MoviePlaying(void);
extern const char *tt_game_dir(void);
extern FILE *tt_game_fopen(const char *rel);
extern void tt_dbg(const char *fmt, ...);

#define GFX_PROMPTS   13               /* gfx_menu.h order */
/* (1.9.3) the mods' settings found by their ini key, not a number: a
 * setting added before them moved the numbers */
extern int xbox_GfxFind(const char *key);
static int e3_id(int which)
{
    static int id[2] = { -2, -2 };
    if (id[which] == -2) id[which] = xbox_GfxFind(which ? "e3_pickups" : "e3_hud");
    return id[which];
}
#define GFX_E3HUD     e3_id(0)
#define GFX_E3PICKUPS e3_id(1)
#define G8(a)  (*(volatile uint8_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define G16(a) (*(volatile uint16_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define G32(a) (*(volatile uint32_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))

#include "e3hud_layout.h"

/* ---- combo runes from the buttons ----
 * Retail controls: A = speed attack, Y = fierce attack, B = knock back
 * (X is parry and makes no rune). */
enum { R_NONE, R_SPEED, R_FIERCE, R_SPECIAL };
static volatile int s_rune[4], s_nrune;
static volatile DWORD s_last_hit, s_label_until;
static volatile int s_label;           /* 0 none, 1 speed, 2 fierce, 3 special, 4 mixed */
static volatile int s_in_game;
static volatile unsigned s_state_ver = 1;

static void combo_end(void)
{
    int i, same = 1;
    if (!s_nrune) return;
    for (i = 1; i < s_nrune; i++) if (s_rune[i] != s_rune[0]) same = 0;
    s_label = same ? s_rune[0] : 4;
    s_label_until = GetTickCount() + 1600;
    s_state_ver++;
}

static void e3_pad(unsigned long port, const XState *st)
{
    static uint8_t prevs[4][8];
    static const int map[3][2] = { { 0, R_SPEED }, { 3, R_FIERCE }, { 1, R_SPECIAL } };   /* A, Y, B */
    DWORD now = GetTickCount();
    uint8_t *prev;
    int k;
    if (port > 3) return;
    /* (one player: any controller's presses count; the runtime's port
     * numbers are not the game's, so no matching against 0x1CB8B4) */
    prev = prevs[port];
    if (!s_in_game || !xbox_GfxGet(GFX_E3HUD)) { memcpy(prev, st->Gamepad.bAnalogButtons, 8); return; }
    for (k = 0; k < 3; k++) {
        int b = map[k][0];
        if (st->Gamepad.bAnalogButtons[b] > 64 && prev[b] <= 64) {
            if (s_label || s_nrune >= 4) { s_nrune = 0; s_label = 0; }
            s_rune[s_nrune++] = map[k][1];
            s_last_hit = now;
            s_state_ver++;
            if (s_nrune == 4) combo_end();
        }
    }
    memcpy(prev, st->Gamepad.bAnalogButtons, 8);
}

static void combo_tick(void)
{
    DWORD now = GetTickCount();
    static const char *test = (const char *)1;
    if (test == (const char *)1) test = getenv("TT_E3HUD_TEST");   /* testing: a fixed orb */
    if (test) {
        /* "ayb" = speed, fierce, special runes; "L2" = show label 2 */
        int k, n = 0, l;
        for (k = 0; test[k] && n < 4; k++)
            if (test[k] == 'a' || test[k] == 'y' || test[k] == 'b')
                s_rune[n++] = test[k] == 'a' ? R_SPEED : test[k] == 'y' ? R_FIERCE : R_SPECIAL;
        if (n != s_nrune) s_state_ver++;
        s_nrune = n;
        l = strchr(test, 'L') ? atoi(strchr(test, 'L') + 1) : 0;
        if (l != s_label) { s_label = l; s_state_ver++; }
        return;
    }
    if (s_nrune && !s_label && now - s_last_hit > 900) combo_end();
    if (s_label && (LONG)(now - s_label_until) > 0) { s_label = 0; s_nrune = 0; s_state_ver++; }
}

/* ---- drawing helpers (32-bit ARGB, straight alpha) ---- */
static void put(uint32_t *p, int w, int h, int x, int y, uint32_t c, float a)
{
    uint32_t d, da;
    float ia, oa;
    if (x < 0 || y < 0 || x >= w || y >= h || a <= 0) return;
    if (a > 1) a = 1;
    d = p[y * w + x];
    da = d >> 24;
    oa = a + (float)da / 255.0f * (1 - a);
    if (oa <= 0) return;
    ia = (float)da / 255.0f * (1 - a);
    {
        float r = (((c >> 16) & 255) * a + ((d >> 16) & 255) * ia) / oa;
        float g = (((c >> 8) & 255) * a + ((d >> 8) & 255) * ia) / oa;
        float b = ((c & 255) * a + (d & 255) * ia) / oa;
        p[y * w + x] = ((uint32_t)(oa * 255 + 0.5f) << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    }
}

static uint32_t shade(uint32_t c, float k)
{
    float r = ((c >> 16) & 255) * k, g = ((c >> 8) & 255) * k, b = (c & 255) * k;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* the gauge's plain rectangle: a thin pale outline, as in the demo */
static void draw_tube(uint32_t *p, int w, int h, float x0, float y0, float x1, float y1, float lw)
{
    int x, y;
    for (y = (int)(y0 - lw - 1); y <= (int)(y1 + 1); y++)
        for (x = (int)(x0 - lw - 1); x <= (int)(x1 + lw + 1); x++) {
            float px = x + 0.5f, py = y + 0.5f;
            /* distance outside the inner rectangle, to the outer one */
            float dl = x0 - px, dr = px - x1, dt = y0 - py;
            float dx = dl > dr ? dl : dr, d = dx > dt ? dx : dt;
            float a;
            if (py > y1) continue;                       /* open at the bottom: the orb covers it */
            if (d < -0.5f || d > lw + 0.5f) continue;
            a = d < 0 ? d + 0.5f : d > lw ? lw + 0.5f - d : 1.0f;
            put(p, w, h, x, y, 0xC8CCC0, a * 0.95f);
        }
}

/* the rune orb: quarters TR, BR, BL, TL on dark glass, thin dark seams
 * between them and a gold rim */
static void draw_orb_ex(uint32_t *p, int w, int h, float cx, float cy, float r, const volatile int *runes, int nrune,
                        int mixed)
{
    static const uint32_t col[5] = { 0, 0x2E5BFF, 0x22C83A, 0xE8361C, 0xD83CD8 };
    int x, y;
    float rw = r * 0.085f;                               /* rim width */
    for (y = (int)(cy - r - rw - 2); y <= (int)(cy + r + rw + 2); y++)
        for (x = (int)(cx - r - rw - 2); x <= (int)(cx + r + rw + 2); x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float inside = r - d;                        /* >0 inside the glass */
            if (inside > -1.0f) {
                int q = dx >= 0 ? (dy < 0 ? 0 : 1) : (dy >= 0 ? 2 : 3);
                float sh = 1.0f - 0.35f * (d / r);       /* a little depth */
                uint32_t c = 0x14100C, cc;
                float a = 0.55f, seam = r * 0.05f;       /* empty: dark glass */
                int filled = mixed || q < nrune;
                if (filled) { c = mixed ? col[4] : col[runes[q]]; a = 0.97f; }
                cc = shade(c, sh);
                if (!mixed && nrune && (fabsf(dx) < seam || fabsf(dy) < seam)) { cc = 0x080604; a = 0.9f; }
                {   /* highlight at the top left, as on a glass bead */
                    float hx = dx + r * 0.35f, hy = dy + r * 0.4f, hd = sqrtf(hx * hx + hy * hy) / (r * 0.45f);
                    if (hd < 1.0f) {
                        float k = (1.0f - hd) * 0.35f;
                        cc = ((uint32_t)(((cc >> 16) & 255) + (255 - ((cc >> 16) & 255)) * k) << 16)
                             | ((uint32_t)(((cc >> 8) & 255) + (255 - ((cc >> 8) & 255)) * k) << 8)
                             | (uint32_t)((cc & 255) + (255 - (cc & 255)) * k);
                    }
                }
                put(p, w, h, x, y, cc, a * (inside > 0 ? (inside > 1 ? 1.0f : inside) : 0.0f));
            }
            {   /* rim: gold, anti-aliased, darker underneath */
                float rim = rw + 0.5f - fabsf(d - r);
                if (rim > 0) put(p, w, h, x, y, dy < 0 ? 0xF0CC68 : 0xB08A38, rim > 1 ? 1 : rim);
            }
        }
}

static void draw_orb(uint32_t *p, int w, int h, float cx, float cy, float r)
{
    draw_orb_ex(p, w, h, cx, cy, r, s_rune, s_nrune, s_label == 4);
}

/* ---- the character's face ---- */
/* per hero 1-4, and 5-8 for the Extra heroes mod's guests (hero 4 then) */
#define N_FACE 18   /* originales y 13 extras; cero reservado */
static uint32_t *volatile s_face[N_FACE];   /* alpha-masked; FACE_PX square unless an icon */
static int s_face_w[N_FACE], s_face_h[N_FACE];   /* an icon's own size (0: FACE_PX square) */
static int s_icon_tried[N_FACE];
static volatile unsigned s_face_ver[N_FACE];
static int s_face_tried[N_FACE];
extern int tt_heroes_guest(void);        /* heroes.c: the guest playing as hero 4, or -1 */
extern int tt_level_is_prologue(void);   /* heroes.c: the level is the prologue */
extern const char *tt_heroes_guest_name(int g);
#include "e3hud_face.inc"

/* ---- the meter readout ---- */
#include "e3hud_meter.inc"

/* ---- game-drawn text ---- */
static void el_text(uint32_t el, const char *t)
{
    int i;
    for (i = 0; t[i] && i < 40; i++) G16(el + 0x38 + 2 * i) = (uint8_t)t[i];
    G16(el + 0x38 + 2 * i) = 0;
}
static void el_setup(uint32_t el, uint32_t tmpl, int x, int y, int w, int h)
{
    int i;
    for (i = 0; i < 0xDC; i += 4) G32(el + i) = G32(tmpl + i);
    G32(el + 0x08) = (uint32_t)x; G32(el + 0x0C) = (uint32_t)y;
    G32(el + 0x10) = (uint32_t)w; G32(el + 0x14) = (uint32_t)h;
    G8(el + 0x24) = 1;
    G32(el + 0x34) = 0;
}

/* game thread, around the HUD's drawing (0x000CF450 walks a list of
 * element tables at 0x1BAAE4 -- {elements, count, -} x 21 -- drawing those
 * whose switch at 0x4A559C is on). Before: hide the retail gauge's frame,
 * move the rating word, and lend the ECTS HUD's two text elements a switched-
 * off slot of the list, so the game draws them in its own fonts like the
 * rest. After: put everything back. Returns 1 if the ECTS HUD is on. */
/* ---- Westfold's villager counter ----
 * Retail: a green bar with a portrait (HUD list 11, "VillagerRescue"), shown
 * by the level script's @ShowVillagerRescue. Its count (0x4A55BC) starts at 8
 * and each @IncVillagerRescue -- two villagers lost -- takes one off; the bar
 * is drawn count/8 of its length. The demo showed 8 portraits of a villager
 * couple instead, two rows of four, a red cross over each pair lost. With
 * e3hud\villager.bmp (and villager_dead.bmp) installed, those replace the
 * bar; without them the retail bar stays. */
#define VR_LIST      11
#define VR_COUNT     0x4A55BCu
#define VR_TOTAL     8
static uint32_t *s_vr_icon, *s_vr_dead;
static int s_vr_iw, s_vr_ih, s_vr_dw, s_vr_dh, s_vr_tried;
static volatile int s_vr_shown, s_vr_left;
static int s_vr_hidden;
/* Level 10's progress bar portrait, when e3hud\level10.bmp replaced it:
 * the retail element (64x64, y 13) is centred 4 units above the bar's fill
 * (y 45-53); the picture is lowered by that much to sit level with it. */
static volatile int s_l10_icon_on;
#define L10_PORTRAIT_EL (0x4A6308u + 2 * 0xDCu)
#define L10_LIST     10
#define L10_DY       4
static uint32_t s_l10_saved_y;
static int s_l10_moved;

static void vr_load(void)
{
    if (s_vr_tried) return;
    s_vr_tried = 1;
    s_vr_icon = icon_load_named("villager", &s_vr_iw, &s_vr_ih);
    if (!s_vr_icon) return;
    s_vr_dead = icon_load_named("villager_dead", &s_vr_dw, &s_vr_dh);
    if (s_vr_dead && (s_vr_dw != s_vr_iw || s_vr_dh != s_vr_ih)) { free(s_vr_dead); s_vr_dead = NULL; }
}

#define HUD_LIST     0x1BAAE4u
#define HUD_LIST_N   21
#define HUD_LIST_ON  0x4A559Cu
static uint32_t s_saved_cap, s_saved_art, s_saved_rating_y;
static int s_slot = -1;
static uint32_t s_slot_ptr, s_slot_cnt;
int tt_e3hud_predraw(void)
{
    static const char *lbl[5] = { "", "Speed", "Fierce", "Special", "Mixed" };
    static const uint32_t lcol[5] = { 0, 0xFFFF7A48u, 0xFF40D030u, 0xFF2848F0u, 0xFFE050E0u };   /* 0xAABBGGRR */
    int v = -1, m = -1, l = s_label, k;
    s_slot = -1;
    s_vr_hidden = 0;
    s_l10_moved = 0;
    if (getenv("TT_E3HUD_ELDUMP")) {   /* testing: the gauge elements as the game has them */
        static DWORD last;
        if (GetTickCount() - last > 4000) {
            int e;
            last = GetTickCount();
            tt_dbg("[ELDUMP] screen %u list-on:", G32(0x4AD050u));
            for (e = 0; e < HUD_LIST_N; e++) tt_dbg(" %u:%08X/%u", G8(HUD_LIST_ON + e), G32(HUD_LIST + e * 12), G32(HUD_LIST + e * 12 + 4));
            tt_dbg("\n");
            for (e = 0; e < 16; e++)
                tt_dbg("[ELDUMP]  gauge %2d xy %4d,%4d wh %4d,%4d col %08X on %u\n", e,
                       (int)G32(HUD_EL(HUD_GAUGE, e) + 8), (int)G32(HUD_EL(HUD_GAUGE, e) + 0xC),
                       (int)G32(HUD_EL(HUD_GAUGE, e) + 0x10), (int)G32(HUD_EL(HUD_GAUGE, e) + 0x14),
                       G32(HUD_EL(HUD_GAUGE, e) + 0x20), G8(HUD_EL(HUD_GAUGE, e) + 0x24));
        }
    }
    if (getenv("TT_E3HUD_LISTDUMP")) {   /* testing: every element of the lists that are on */
        static DWORD last2;
        if (GetTickCount() - last2 > 2000) {
            int e, li;
            last2 = GetTickCount();
            for (li = 0; li < HUD_LIST_N; li++) {
                uint32_t tb = G32(HUD_LIST + li * 12), n = G32(HUD_LIST + li * 12 + 4);
                if ((!G8(HUD_LIST_ON + li) && li != 11 && strcmp(getenv("TT_E3HUD_LISTDUMP"), "all")) || !tb || n > 64) continue;
                for (e = 0; e < (int)n; e++)
                    tt_dbg("[LIST] %2d.%2d xy %4d,%4d wh %4d,%4d col %08X on %u tex %08X %08X\n", li, e,
                           (int)G32(HUD_EL(tb, e) + 8), (int)G32(HUD_EL(tb, e) + 0xC),
                           (int)G32(HUD_EL(tb, e) + 0x10), (int)G32(HUD_EL(tb, e) + 0x14),
                           G32(HUD_EL(tb, e) + 0x20), G8(HUD_EL(tb, e) + 0x24), G32(HUD_EL(tb, e) + 0x18), G32(HUD_EL(tb, e) + 0x1C));
            }
        }
    }
    if (getenv("TT_E3HUD_LISTON")) G8(HUD_LIST_ON + atoi(getenv("TT_E3HUD_LISTON"))) = 1;   /* testing: show a list */
    if (!xbox_GfxGet(GFX_E3HUD) || !s_in_game) { s_vr_shown = 0; return 0; }
    s_saved_cap = G8(HUD_GAUGE_CAP + 0x24);
    s_saved_art = G8(HUD_GAUGE_ART + 0x24);
    s_saved_rating_y = G32(HUD_RATING + 0x0C);
    G8(HUD_GAUGE_CAP + 0x24) = 0;
    G8(HUD_GAUGE_ART + 0x24) = 0;
    G32(HUD_RATING + 0x0C) = s_saved_rating_y + RATING_DY;
    if (getenv("TT_E3HUD_RATETEST")) {   /* testing: show the rating element */
        G8(HUD_RATING + 0x24) = 1; G32(HUD_RATING + 0x20) = 0xFFC1C5B9u;
        G32(HUD_RATING + 0x34) = 0; el_text(HUD_RATING, "Good");
    }
    /* the combo's name and the EXP readout */
    el_setup(E3_EL_LABEL, HUD_RATING, LBL_HX, LBL_HY, 220, 40);
    G8(E3_EL_LABEL + 0x24) = 0;
    if (l > 0 && l <= 4) {
        G8(E3_EL_LABEL + 0x24) = 1;
        G32(E3_EL_LABEL + 0x20) = lcol[l];
        el_text(E3_EL_LABEL, lbl[l]);
    }
    el_setup(E3_EL_EXP, HUD_ARROWS_TEXT, EXP_HX, EXP_HY, 200, 30);
    G8(E3_EL_EXP + 0x24) = 0;
    meter_read(&v, &m);
    if (v >= 0) {
        char t[40];
        snprintf(t, sizeof t, "EXP: %d/%d", v, m);
        G8(E3_EL_EXP + 0x24) = 1;
        G32(E3_EL_EXP + 0x20) = 0xFFF0F0F0u;
        /* font 0 centres its text in the box (and wraps what does not fit):
         * a wide box, moved left by half the room the text leaves, keeps the
         * readout's left edge in place (font 0 is about 9 units a letter) */
        G32(E3_EL_EXP + 0x08) = (uint32_t)(EXP_HX - (200 - (int)strlen(t) * 9) / 2);
        el_text(E3_EL_EXP, t);
    }
    /* the last switched-off slot of the list draws them, on top */
    for (k = HUD_LIST_N - 1; k >= 0; k--)
        if (!G8(HUD_LIST_ON + k)) {
            s_slot = k;
            s_slot_ptr = G32(HUD_LIST + k * 12);
            s_slot_cnt = G32(HUD_LIST + k * 12 + 4);
            G32(HUD_LIST + k * 12) = E3_EL_LABEL;
            G32(HUD_LIST + k * 12 + 4) = 2;
            G8(HUD_LIST_ON + k) = 1;
            break;
        }
    /* the villager counter: the demo's portraits (e3_overlay) instead of the
     * retail bar, switched off for this draw */
    {
        static const char *vt = (const char *)1;
        int on = G8(HUD_LIST_ON + VR_LIST) != 0, left = (int)G32(VR_COUNT);
        if (vt == (const char *)1) vt = getenv("TT_E3HUD_VRTEST");   /* testing: "n" villager pairs left */
        if (vt) { on = 1; left = atoi(vt); }
        vr_load();
        if (left < 0) left = 0;
        if (left > VR_TOTAL) left = VR_TOTAL;
        s_vr_left = left;
        s_vr_shown = on && s_vr_icon;
        if (s_vr_shown && G8(HUD_LIST_ON + VR_LIST)) {
            G8(HUD_LIST_ON + VR_LIST) = 0;
            s_vr_hidden = 1;
        }
    }
    if (s_l10_icon_on && G8(HUD_LIST_ON + L10_LIST)) {
        s_l10_saved_y = G32(L10_PORTRAIT_EL + 0x0C);
        G32(L10_PORTRAIT_EL + 0x0C) = s_l10_saved_y + L10_DY;
        s_l10_moved = 1;
    }
    return 1;
}

void tt_e3hud_postdraw(void)
{
    G8(HUD_GAUGE_CAP + 0x24) = (uint8_t)s_saved_cap;
    G8(HUD_GAUGE_ART + 0x24) = (uint8_t)s_saved_art;
    G32(HUD_RATING + 0x0C) = s_saved_rating_y;
    if (s_slot >= 0) {
        G32(HUD_LIST + s_slot * 12) = s_slot_ptr;
        G32(HUD_LIST + s_slot * 12 + 4) = s_slot_cnt;
        G8(HUD_LIST_ON + s_slot) = 0;
        s_slot = -1;
    }
    if (s_vr_hidden) { G8(HUD_LIST_ON + VR_LIST) = 1; s_vr_hidden = 0; }
    if (s_l10_moved) { G32(L10_PORTRAIT_EL + 0x0C) = s_l10_saved_y; s_l10_moved = 0; }
}

/* game thread, just before the health sword is drawn (0x000CCA50) */
void tt_e3hud_prehud(void)
{
    /* (the health bar keeps its retail green) */
}

/* ---- textures: the portrait source, the cross-guard, the power-ups ---- */
/* ---- the loading screen's experience tip ---- */
#include "e3hud_load.inc"

#include "prompts.inc"
#include "e3hud_tex.inc"

/* ---- overlay callback (renderer thread, once a frame) ---- */
static uint32_t *s_cluster;            /* gauge outline + orb */
static unsigned s_cluster_ver, s_cluster_drawn;

static void mods_tick(void)
{
    static int was_hud = -1, was_pick = -1;
    int hud = xbox_GfxGet(GFX_E3HUD) != 0, pick = xbox_GfxGet(GFX_E3PICKUPS) != 0;
    if (hud != was_hud || pick != was_pick) {
        if (was_hud >= 0) InterlockedIncrement((volatile LONG *)&xbox_tex_rev);   /* re-decode with/without */
        if (pick != was_pick) tt_dbg("[E3HUD] gold power-ups %s\n", pick ? "on" : "off");
        was_hud = hud; was_pick = pick;
    }
    if (getenv("TT_E3HUD_TEST")) {                /* testing: when is a power-up on screen */
        static DWORD last;
        int k;
        for (k = 0; k < N_PICKUP_TEX; k++)
            if (s_pickup_tex[k] && xbox_FrameUsedTexture(s_pickup_tex[k]) && GetTickCount() - last > 3000) {
                last = GetTickCount();
                tt_dbg("[E3HUD] health power-up drawn\n");
            }
    }
}

static int e3_overlay(XboxOverlayQuad *q, int max)
{
    int n = 0, hero;
    mods_tick();
    prompts_tick();
    if (getenv("TT_E3HUD_ELDUMP")) {   /* testing: the textures each front-end screen is made of */
        static DWORD last;
        if (GetTickCount() - last > 5000) {
            extern int xbox_FrameTextureList(uint32_t *out, int max);
            uint32_t hs[512];
            int nh = xbox_FrameTextureList(hs, 512), k;
            last = GetTickCount();
            tt_dbg("[ELDUMP] screen %u textures:", G32(0x4AD050u));
            for (k = 0; k < nh; k++) tt_dbg(" %08X", hs[k]);
            tt_dbg("\n");
        }
    }
    if (!xbox_GfxGet(GFX_E3HUD)) { s_in_game = 0; fills_apply(0); return 0; }
    if (xbox_FrameUsedTexture(TEX_LOAD_TIP)) { s_in_game = 0; return load_tip_overlay(q, max); }
    if (G32(FE_SCREEN) == FE_RESULTS) {
        /* mission results: the demo gauge over the retail one's place (whose
         * pieces e3_tex cleared), not the in-play one at the bottom left */
        static uint32_t *res;
        static const int none[4] = { 0, 0, 0, 0 };
        const int W = RES_CL_W * CL_SCALE, H = RES_CL_H * CL_SCALE;
        const float k = (float)CL_SCALE;
        s_in_game = 0;
        if (!E3_HUD_VISIBLE() || max < 1) return 0;
        if (!res && (res = (uint32_t *)calloc((size_t)W * H, 4))) {
            draw_tube(res, W, H, (RES_TUBE_X0 - RES_CL_X) * k, (RES_TUBE_Y0 - RES_CL_Y) * k,
                      (RES_TUBE_X1 - RES_CL_X) * k, (RES_ORB_SY - RES_CL_Y) * k, 1.1f * k);
            draw_orb_ex(res, W, H, (RES_ORB_SX - RES_CL_X) * k, (RES_ORB_SY - RES_CL_Y) * k, RES_ORB_SR * k, none, 0, 0);
        }
        if (!res) return 0;
        q[0].px = res; q[0].w = W; q[0].h = H; q[0].ver = 7;
        q[0].x = RES_CL_X; q[0].y = RES_CL_Y; q[0].dw = RES_CL_W; q[0].dh = RES_CL_H;
        return 1;
    }
    /* play: the health sword is drawn (the results/upgrade pages after a
     * mission draw the experience tube too, but never the sword) */
    s_in_game = xbox_FrameUsedTexture(TEX_SWORD) && !xbox_MoviePlaying();
    fills_apply(1);
    if (!s_in_game) return 0;
    combo_tick();
    hero = G8(0x4AB69Du);
    if (tt_level_is_prologue()) hero = 4;   /* the prologue is Isildur's, whoever the game counts as playing */
    else if (hero == 4 && tt_heroes_guest() >= 0) hero = 5 + tt_heroes_guest();   /* a guest's own portrait */
    /* portrait medallion on the health sword */
    if (hero >= 1 && hero < N_FACE && n < max) {
        /* the portrait comes from the model's face texture (e3_tex) as soon
         * as the game draws the hero; Isildur's is not known, so his comes
         * from the upgrade screen's picture */
        if (!s_icon_tried[hero]) {
            /* the demo's own portrait icons, if installed (e3hud\*.bmp) */
            int w = 0, h = 0;
            uint32_t *ic;
            s_icon_tried[hero] = 1;
            ic = hero <= 4 ? icon_load(hero, &w, &h) : icon_load_named(tt_heroes_guest_name(hero - 5), &w, &h);
            /* Retrato provisional compartido con Elrond. El propio opcional tiene prioridad. */
            if (!ic && hero == 12) ic = icon_load_named("elrond", &w, &h);
            if (ic) { s_face[hero] = ic; s_face_w[hero] = w; s_face_h[hero] = h; s_face_ver[hero] = 2000; s_face_tried[hero] = 1; }
        }
        if (!s_face[hero] && !s_face_tried[hero] && hero == 4) { s_face_tried[hero] = 1; s_face[hero] = face_build(hero); }
        if (s_face[hero]) {
            int iw = s_face_w[hero], ih = s_face_h[hero];
            q[n].px = s_face[hero]; q[n].ver = 1 + hero * 16 + s_face_ver[hero];
            if (iw > 0 && ih > 0) {
                /* an icon: the head stands a little over the medallion, as on the demo */
                /* (the extra heroes' icons are whole medallions: drawn over the game's) */
                float dh = FACE_SIZE * (hero >= 5 ? 1.0f : 1.12f), dw = dh * (float)iw / (float)ih;
                q[n].w = iw; q[n].h = ih;
                q[n].x = FACE_X + FACE_SIZE * 0.5f - dw * 0.5f; q[n].y = FACE_Y + FACE_SIZE * 0.5f - dh * (hero >= 5 ? 0.5f : 0.52f);
                q[n].dw = dw; q[n].dh = dh;
            } else {
                q[n].w = FACE_PX; q[n].h = FACE_PX;
                q[n].x = FACE_X; q[n].y = FACE_Y; q[n].dw = FACE_SIZE; q[n].dh = FACE_SIZE;
            }
            n++;
        }
    }
    /* gauge outline and rune orb */
    if (n < max) {
        const int W = CL_W * CL_SCALE, H = CL_H * CL_SCALE;
        const float k = (float)CL_SCALE;
        if (!s_cluster) s_cluster = (uint32_t *)calloc((size_t)W * H, 4);
        {   /* (co-op: the orb out, the Perfect rings of the co-op code in its place; drawn again when that changes) */
            extern int tt_coop_active(void); static int last_coop = -1; int co = tt_coop_active() && !getenv("TT_COOP_E3ORB");
            if (co != last_coop) { last_coop = co; s_state_ver++; }
        }
        if (s_cluster && s_cluster_drawn != s_state_ver) {
            extern int tt_coop_active(void);
            s_cluster_drawn = s_state_ver;
            memset(s_cluster, 0, (size_t)W * H * 4);
            draw_tube(s_cluster, W, H, (TUBE_X0 - CL_X) * k, (TUBE_Y0 - CL_Y) * k, (TUBE_X1 - CL_X) * k,
                      (ORB_SY - CL_Y) * k, 1.1f * k);
            if (!tt_coop_active() || getenv("TT_COOP_E3ORB")) draw_orb(s_cluster, W, H, (ORB_SX - CL_X) * k, (ORB_SY - CL_Y) * k, ORB_SR * k);
            s_cluster_ver++;
        }
        if (s_cluster) {
            q[n].px = s_cluster; q[n].w = W; q[n].h = H; q[n].ver = s_cluster_ver + 100;
            q[n].x = CL_X; q[n].y = CL_Y; q[n].dw = CL_W; q[n].dh = CL_H;
            n++;
            {   /* co-op: player 2's gauge outline, mirrored to the right (his fill is the co-op code's mirrored copy of the
                 * game's gauge), kept to the right edge (the version's bit 30) */
                extern int tt_coop_active(void); extern int tt_coop_p2_gauge_on(void);
                static uint32_t *mir; static unsigned mir_ver;
                if (tt_coop_active() && tt_coop_p2_gauge_on() && n < max) {
                    if (!mir) mir = (uint32_t *)calloc((size_t)W * H, 4);
                    if (mir && mir_ver != s_cluster_ver) { int x, y; mir_ver = s_cluster_ver; for (y = 0; y < H; y++) for (x = 0; x < W; x++) mir[y * W + x] = s_cluster[y * W + (W - 1 - x)]; }
                    if (mir) {
                        q[n].px = mir; q[n].w = W; q[n].h = H; q[n].ver = (s_cluster_ver + 300) | 0x40000000u;
                        q[n].x = 640.0f - CL_X - CL_W; q[n].y = CL_Y; q[n].dw = CL_W; q[n].dh = CL_H;
                        n++;
                    }
                }
            }
        }
    }
    /* the villager couples, where the retail bar and portrait were */
    if (s_vr_shown && n < max) {
        static uint32_t *grid;
        static int drawn = -1;
        static unsigned ver;
        int left = s_vr_left, gw = s_vr_iw * 4, gh = s_vr_ih * 2;
        if (!grid) grid = (uint32_t *)calloc((size_t)gw * gh, 4);
        if (grid && drawn != left) {
            int c, y;
            drawn = left;
            for (c = 0; c < VR_TOTAL; c++) {
                /* lost pairs from the first, left to right and down, as in the demo */
                int lost = c < VR_TOTAL - left;
                const uint32_t *src = lost && s_vr_dead ? s_vr_dead : s_vr_icon;
                int ox = (c % 4) * s_vr_iw, oy = (c / 4) * s_vr_ih;
                for (y = 0; y < s_vr_ih; y++)
                    memcpy(grid + (size_t)(oy + y) * gw + ox, src + (size_t)y * s_vr_iw, (size_t)s_vr_iw * 4);
                if (lost && !s_vr_dead) {          /* no crossed picture: a red cross */
                    int x;
                    float t = s_vr_iw * 0.09f;
                    for (y = 0; y < s_vr_ih; y++)
                        for (x = 0; x < s_vr_iw; x++) {
                            float u = (x + 0.5f) / s_vr_iw, v = (y + 0.5f) / s_vr_ih;
                            float d1 = fabsf(u - v) * s_vr_iw, d2 = fabsf(u + v - 1.0f) * s_vr_iw;
                            float d = d1 < d2 ? d1 : d2;
                            if (u > 0.12f && u < 0.88f && v > 0.12f && v < 0.88f && d < t + 1)
                                put(grid, gw, gh, ox + x, oy + y, 0xC81E14, d < t ? 0.85f : (t + 1 - d) * 0.85f);
                        }
                }
            }
            ver++;
        }
        if (grid) {
            q[n].px = grid; q[n].w = gw; q[n].h = gh; q[n].ver = 5000 + ver;
            /* the pictures' own shape, against the right edge of the retail art */
            float dw = VR_H * (float)gw / (float)gh;
            q[n].x = VR_X + VR_W - dw; q[n].y = VR_Y; q[n].dw = dw; q[n].dh = VR_H;
            n++;
        }
    }
    return n;
}

void tt_e3hud_init(void)
{
    xbox_overlay_cb = e3_overlay;
    xbox_pad_observer = e3_pad;
    xbox_tex_observer = e3_tex;
}
