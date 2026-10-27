/* menus.c -- the main menu's Exit, and the mouse in the menus.
 *
 * The front end draws its screens from "elements" (0xDC bytes each, the
 * same kind the HUD uses): +0x08 x, +0x0C y, +0x10 w, +0x14 h (640x480),
 * +0x20 colour (0xAARRGGBB), +0x24 on, +0x2C font, +0x30 1 for text,
 * +0x34 the text (UTF-16, in the string table). Each is drawn by
 * 0x000C8940 (thiscall); the wrapper there (twotowers_manual.c) tells
 * tt_menu_el which ones were drawn this frame.
 *
 * The main menu (front-end screen 4; its object at 0x4AD050 + 0x3864:
 * +0x10 its state, 0 the menu, 1 "press start", 3 the difficulty; +0x14
 * the item chosen, 0..3) draws New Game, Load Game, Options and Credits as
 * elements 8..11 of its table at 0x432728 (y 260, 290, 320, 350; the chosen
 * one in 0xFF9FD2F0, the others 0xFF28364E) and a glowing swoosh behind the
 * chosen one (the object at 0x4A02F8, moved by 0x000CB590). Exit is item 4:
 * an element of ours (a copy of Credits' at y 380, the text "Exit") drawn
 * with them; the menu's own code treats a choice past 3 as nothing, so only
 * moving down from Credits (0x00079180) and the colours (0x00079870) need
 * help, and pressing A or Start on it ends the game.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "tt_env.h"   /* (1.9.3) getenv looked up once per name */
#include <string.h>
#include <math.h>

extern ptrdiff_t xbox_GetMemoryOffset(void);
#define G8(a)  (*(volatile uint8_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define G16(a) (*(volatile uint16_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define G32(a) (*(volatile uint32_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define GF(a)  (*(volatile float *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
extern void tt_dbg(const char *fmt, ...);
extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);

#define FE_SCREEN   0x4AD050u
#define MM_OBJ      (0x4AD050u + 0x3864u)   /* the main menu screen */
#define MM_TABLE    0x432728u               /* its elements */
#define EL_SIZE     0xDCu
#define MM_CREDITS  (MM_TABLE + 11 * EL_SIZE)
#define MM_SWOOSH   0x4A02F8u
#define COL_ON      0xFF9FD2F0u
#define COL_OFF     0xFF28364Eu
/* ours, in guest memory nothing else uses (after the ECTS HUD's two elements
 * at 0x4BA000, the tail of the developer-switch table retail never fills) */
#define EXIT_EL     0x4BA400u
#define EXIT_TXT    0x4BA500u

static int exit_on(void)
{
    static int on = -1;
    if (on < 0) { const char *e = getenv("TT_NOEXIT"); on = !(e && *e && *e != '0'); }
    return on;
}

/* ---- the elements drawn this frame ---- */
#define MAXEL 256
static uint32_t s_el[2][MAXEL], s_ret[2][MAXEL];
static int s_nel[2], s_cur;
static unsigned s_frame_seen;
static volatile uint32_t s_tint_el, s_tint_col, s_tint_saved, s_tint_on;
/* and its leaf drawn lit: the leaf's picture (+0xB8) swapped for the lit one */
static volatile uint32_t s_tint2_el, s_tint2_tex, s_tint2_dim, s_tint2_saved, s_tint2_on;
/* the title screen's "Press START button": the key when it is played with
 * the keyboard ("Press Enter", the key bound to Start) */
#define PRESS_EL   (0x432728u + 4 * 0xDCu)
#define PRESS_TXT  0x4BA540u
static uint32_t s_press_saved;
static int s_press_on;
static int press_kb(void)
{
    extern int xbox_GfxGet(int id);
    extern int xbox_InputLastDevice(void);
    int pr = xbox_GfxGet(13);      /* Button prompts: 0 automatic .. 4 keyboard */
    if (pr == 4) return 1;
    return pr == 0 && xbox_InputLastDevice() != 0;   /* (automatic: unless a pad was used last) */
}
void tt_menu_el(uint32_t el, uint32_t ret)
{
    if (el < 0x10000u || (el >= 0x04000000u && (el < 0x80000000u || el >= 0x84000000u))) return;
    if (el == PRESS_EL && G32(0x4AD050u) == 4 && press_kb()) {
        extern const char *xbox_BindKeyName(int vk);
        extern int xbox_BindKey(int a, int i);
        extern int tt_tr16(const char *en, uint16_t *out, int max);
        uint16_t t[64], kw[32];
        const char *k = xbox_BindKey(8, 0) ? xbox_BindKeyName(xbox_BindKey(8, 0)) : "Enter";
        int i, n = tt_tr16("Press {k}", t, 64), kn = tt_tr16(k && *k ? k : "Enter", kw, 32), o = 0;
        for (i = 0; i < n && o < 30; i++) {   /* (the words in the chosen language: lang.c) */
            if (t[i] == '{' && i + 2 < n && t[i + 1] == 'k' && t[i + 2] == '}') {
                int j;
                for (j = 0; j < kn && o < 30; j++) G16(PRESS_TXT + 2 * o++) = kw[j];
                i += 2;
            } else G16(PRESS_TXT + 2 * o++) = t[i];
        }
        G16(PRESS_TXT + 2 * o) = 0;
        s_press_saved = G32(el + 0x34); G32(el + 0x34) = PRESS_TXT; s_press_on = 1;
    }
    if (el == s_tint_el && s_tint_el) { s_tint_saved = G32(el + 0x20); G32(el + 0x20) = s_tint_col; s_tint_on = 1; }
    if (el == s_tint2_el && s_tint2_el && s_tint2_tex && G32(el + 0xB8) == s_tint2_dim) { s_tint2_saved = G32(el + 0xB8); G32(el + 0xB8) = s_tint2_tex; s_tint2_on = 1; }
    if (s_nel[s_cur] < MAXEL) { s_ret[s_cur][s_nel[s_cur]] = ret; s_el[s_cur][s_nel[s_cur]++] = el; }
}
/* text drawn outside the elements (0x0003F1C0, in the box last set by
 * 0x0007E500, in the colour at 0x3A783C) */
typedef struct { int x, y, w, h; uint32_t col, txt, ret; } TextDraw;
#define MAXTX 128
static TextDraw s_tx[2][MAXTX];
static int s_ntx[2];
static int s_box[4];
void tt_menu_box(int x, int y, int w, int h) { s_box[0] = x; s_box[1] = y; s_box[2] = w; s_box[3] = h; }
void tt_menu_text(uint32_t txt, uint32_t ret)
{
    TextDraw *t;
    if (ret >= 0x000C8940u && ret < 0x000C9054u) return;   /* an element's (tt_menu_el) */
    if (s_ntx[s_cur] >= MAXTX) return;
    t = &s_tx[s_cur][s_ntx[s_cur]++];
    t->x = s_box[0]; t->y = s_box[1]; t->w = s_box[2]; t->h = s_box[3];
    t->col = G32(0x3A783C); t->txt = txt; t->ret = ret;
}
/* lists drawn by the game's list (0x000D0700: dialogs' Yes / No and the
 * like): +0xF0 the chosen item, +0xF4 the first shown, +0xF8 how many,
 * +0x100 how many shown, +0xC0 a byte each (0: can't be chosen), +0x1C4 the
 * font (the rows' height) */
typedef struct { uint32_t obj; int x, y; } ListDraw;
#define MAXLS 8
static ListDraw s_ls[2][MAXLS];
static int s_nls[2];
void tt_menu_list(uint32_t obj, int x, int y)
{
    int i;
    for (i = 0; i < s_nls[s_cur]; i++) if (s_ls[s_cur][i].obj == obj) { s_ls[s_cur][i].x = x; s_ls[s_cur][i].y = y; return; }
    if (s_nls[s_cur] < MAXLS) { s_ls[s_cur][s_nls[s_cur]].obj = obj; s_ls[s_cur][s_nls[s_cur]].x = x; s_ls[s_cur][s_nls[s_cur]].y = y; s_nls[s_cur]++; }
}
/* after it is drawn: its own colour back */
void tt_menu_el_done(uint32_t el)
{
    if (s_tint_on && el == s_tint_el) { G32(el + 0x20) = s_tint_saved; s_tint_on = 0; }
    if (s_press_on && el == PRESS_EL) { G32(el + 0x34) = s_press_saved; s_press_on = 0; }
    if (s_tint2_on && el == s_tint2_el) { G32(el + 0xB8) = s_tint2_saved; s_tint2_on = 0; }
}
/* a frame of the front end begins (the main loop's tick): the list just drawn becomes "last frame's" */
static void el_flip(void)
{
    s_cur ^= 1;
    s_nel[s_cur] = 0;
    s_ntx[s_cur] = 0;
    s_nls[s_cur] = 0;
    s_frame_seen++;
}
static void el_log(void)
{
    static DWORD last;
    int i, p = s_cur ^ 1;
    if (!getenv("TT_MENULOG") || GetTickCount() - last < 1500) return;
    last = GetTickCount();
    tt_dbg("[MENU] screen %u: %d elements drawn\n", G32(FE_SCREEN), s_nel[p]);
    for (i = 0; i < s_nel[p]; i++) {
        uint32_t e = s_el[p][i], t = G32(e + 0x34);
        char s[48]; int k = 0;
        if (G32(e + 0x30) == 1 && t >= 0x10000u && t < 0x84000000u)
            for (; k < 40 && G16(t + 2 * k); k++) s[k] = (char)(G16(t + 2 * k) < 128 ? G16(t + 2 * k) : '?');
        s[k] = 0;
        tt_dbg("[MENU]  %08X r%06X xy %d,%d wh %d,%d col %08X on %u f %u t %u 18 %X 1C %X %s\n", e,
               s_ret[p][i], (int)G32(e + 8), (int)G32(e + 0xC), (int)G32(e + 0x10), (int)G32(e + 0x14), G32(e + 0x20), G8(e + 0x24),
               G32(e + 0x2C), G32(e + 0x30), G32(e + 0x18), G32(e + 0x1C), s);
        if (getenv("TT_MENULOG_B8") && !G32(e + 0x30)) tt_dbg("[MENU]   tex %08X +4 %u +B8 %08X +BC %08X +28 %08X\n", e, G32(e + 4), G32(e + 0xB8), G32(e + 0xBC), G32(e + 0x28));
        if (G32(e + 0x10) == 96 && G32(e + 0x14) == 75) {   /* a leaf: all of it */
            char hx[0xDC * 3 + 1]; int b;
            for (b = 0; b < 0xDC; b++) snprintf(hx + 3 * b, 4, "%02X ", G8(e + b));
            tt_dbg("[MENU]   leaf %08X: %s\n", e, hx);
        }
    }
    for (i = 0; i < s_nls[p]; i++) {
        uint32_t o = s_ls[p][i].obj;
        tt_dbg("[MENU]  list %08X at %d,%d: chosen %u, first %u, %u items, %u shown, font %u, flags %02X %02X\n", o, s_ls[p][i].x, s_ls[p][i].y,
               G32(o + 0xF0), G32(o + 0xF4), G32(o + 0xF8), G32(o + 0x100), G32(o + 0x1C4), G8(o + 0xFC), G8(o + 0xFD));
    }
    for (i = 0; i < s_ntx[p]; i++) {
        TextDraw *t = &s_tx[p][i];
        char s[48]; int k = 0;
        if (t->txt >= 0x10000u && t->txt < 0x84000000u)
            for (; k < 40 && G16(t->txt + 2 * k); k++) s[k] = (char)(G16(t->txt + 2 * k) < 128 ? G16(t->txt + 2 * k) : '?');
        s[k] = 0;
        tt_dbg("[MENU]  text r%06X xy %d,%d wh %d,%d col %08X %s\n", t->ret, t->x, t->y, t->w, t->h, t->col, s);
    }
}

/* ---- the main menu's Exit ---- */
static volatile int s_exit_go;   /* A / Start on Exit (the input thread) -> the game thread ends the game */
static int mm_here(uint32_t scr) { return scr == MM_OBJ && G32(FE_SCREEN) == 4 && G32(scr + 0x10) == 0; }
static void exit_el_setup(void)
{
    extern int tt_tr16(const char *en, uint16_t *out, int max);
    uint16_t txt[24];
    uint32_t i, n = (uint32_t)tt_tr16("Exit", txt, 24);   /* (in the chosen language: lang.c) */
    for (i = 0; i < EL_SIZE; i += 4) G32(EXIT_EL + i) = G32(MM_CREDITS + i);
    G32(EXIT_EL + 0x0C) = G32(MM_CREDITS + 0x0C) + 30;
    for (i = 0; i < n; i++) G16(EXIT_TXT + 2 * i) = txt[i];
    G16(EXIT_TXT + 2 * i) = 0;
    G32(EXIT_EL + 0x34) = EXIT_TXT;
}
static void play_sound(uint32_t id, float pan)
{
    uint32_t a[6];
    float one = 1.0f, zero = 0.0f;
    memcpy(&a[1], &one, 4); memcpy(&a[2], &zero, 4); memcpy(&a[3], &pan, 4); memcpy(&a[4], &one, 4);
    a[0] = id; a[5] = 0;
    tt_gcall(0x000C3640u, 0, 6, a);
}
static void swoosh_to(int idx)
{
    uint32_t a[3] = { 0x1A, 0x140, (uint32_t)(idx * 0x1C + 0x104) };
    tt_gcall(0x000CB590u, MM_SWOOSH, 3, a);
}
/* the main menu's "down" (0x00079180): from Credits to Exit; 1 = done here */
int tt_mm_down(uint32_t scr)
{
    if (!exit_on() || !mm_here(scr) || G32(scr + 0x14) != 3) return 0;
    G32(scr + 0x14) = 4;
    swoosh_to(4);
    play_sound(0xA9D, -1.0f);   /* (the game's two cues for a move) */
    play_sound(0xA9E, 1.0f);
    return 1;
}
/* its colours (0x00079870), after the game's: Credits dimmed, Exit lit (the
 * game's own write for a choice of 4 lands past its table: put back) */
static uint32_t s_keep;
void tt_mm_colors_pre(uint32_t scr) { s_keep = G32(MM_TABLE + 12 * EL_SIZE + 0x20); (void)scr; }
void tt_mm_colors_post(uint32_t scr)
{
    if (G32(scr + 0x14) == 4) G32(MM_TABLE + 12 * EL_SIZE + 0x20) = s_keep;
    if (!exit_on() || !mm_here(scr)) return;
    if (G32(scr + 0x14) == 4) G32(MM_CREDITS + 0x20) = COL_OFF;
    if (s_exit_go) {
        extern void xbox_FramebufferRequestClose(void);
        s_exit_go = 0;
        tt_dbg("[MENU] Exit chosen: the game ends\n");
        xbox_FramebufferRequestClose();
    }
}
/* drawn with the menu's items (0x00078F50, after the game's) */
void tt_mm_draw(uint32_t scr)
{
    if (!exit_on() || !mm_here(scr)) return;
    exit_el_setup();
    G32(EXIT_EL + 0x20) = G32(scr + 0x14) == 4 ? COL_ON : COL_OFF;
    G8(EXIT_EL + 0x24) = 1;
    tt_gcall(0x000C8940u, EXIT_EL, 0, NULL);
}
/* the input (any thread): A or Start pressed on Exit; 1 = take it from the game */
static int tt_mm_input(unsigned long port, int a_down, int start_down)
{
    static int prevs[4];
    int *prev = &prevs[port & 3];
    int now = a_down || start_down, edge = now && !*prev;
    *prev = now;
    if (!exit_on() || G32(FE_SCREEN) != 4 || G32(MM_OBJ + 0x10) != 0 || G32(MM_OBJ + 0x14) != 4) return 0;
    if (edge) s_exit_go = 1;
    return 1;
}

/* ======================================================================
 * The mouse pointer in the menus (F10 / launcher "Mouse pointer in menus").
 *
 * Nothing in the game knows of a mouse: the pointer stands in for the
 * controller. Pointing at an item presses the d-pad until the game's own
 * selection is on it (so it lights up, swooshes and sounds just as with the
 * pad); a click presses A on it; on a setting's value or slider, left/right
 * until the value or the slider is where the click was; a right click is B.
 * What is on the screen comes from the elements the game drew in the last
 * frame (tt_menu_el): a list's chosen item is the one drawn in a colour of
 * its own. The mission select's places and their neighbours come from the
 * game's own tables; the character select steps left or right.
 * ====================================================================== */
#define GFX_MOUSE 17                       /* gfx_menu.h order */
#define GFX_MOUSE_SENS 18
#define GFX_CURSOR_SIZE 19
#define GFX_MOUSE_AIM 20
#define GFX_MOUSE_INVERT 21
#define GFX_MOUSE_STEER 23                 /* (22 Sauron's sprint) */
extern int xbox_GfxGet(int id);
extern int xbox_FramebufferMouseDelta(int *dx, int *dy, int *w, int *h);
extern void xbox_FramebufferMouseRelease(void);
static float mouse_sens(void) { static const float v[5] = { 0.5f, 0.75f, 1.0f, 1.5f, 2.0f }; int i = xbox_GfxGet(GFX_MOUSE_SENS); return i >= 0 && i < 5 ? v[i] : 1.0f; }
extern int xbox_GfxGet(int id);
extern int xbox_MoviePlaying(void);
extern int xbox_FramebufferMouse(float *fx, float *fy);
extern int xbox_InputMouseButtons(void);
extern int xbox_InputGameInFront(void);
extern void xbox_PresentMap(float m[5]);
extern volatile int xbox_fb_hide_cursor;
extern volatile int xbox_mouse_buttons_taken;
extern volatile int32_t xbox_fb_clicks[2];   /* (LONG) */
static int32_t s_seen_clicks[2];

/* the controller's bits (XINPUT) and analog buttons */
#define PAD_UP     0x0001
#define PAD_DOWN   0x0002
#define PAD_LEFT   0x0004
#define PAD_RIGHT  0x0008
#define PAD_START  0x0010
#define INJ_A      0x10000
#define INJ_B      0x20000

typedef struct { uint32_t a, ret, col, font, txt; int x, y, w, h, on, text; } Item;
static Item s_it[MAXEL];
static int s_nit;
static float icx(const Item *t) { return t->x + t->w * 0.5f; }
static float icy(const Item *t) { return t->y + t->h * 0.5f; }
/* In a wide picture (play: the pause screens) the game's 2D is narrowed
 * about the middle or kept to an edge, as the renderer grouped it
 * (xbox_HudWidePlace): the pointer's x in the game's 640 for an item there. */
extern int xbox_HudWidePlace(float cx, float cy, float *k);
static float s_px_frame = 320;   /* the pointer's x on the picture (0..640) */
static float pointer_x_for(float cx, float cy)
{
    float k, fx = s_px_frame;
    int place = xbox_HudWidePlace(cx, cy, &k);
    if (k <= 1.001f) return fx;
    return place == 1 ? fx * k : place == 2 ? 640.0f - (640.0f - fx) * k : 320.0f + (fx - 320.0f) * k;
}
static int hit(const Item *t, float x, float y)
{
    (void)x;
    x = pointer_x_for(t->x + t->w * 0.5f, t->y + t->h * 0.5f);
    return x >= t->x && x < t->x + t->w && y >= t->y && y < t->y + t->h;
}
static int is_help(const Item *t) { return t->x == 325 && t->y == 430 && t->w == 100 && t->h == 25; }

/* the last frame's elements, each once */
static void snapshot(void)
{
    int p = s_cur ^ 1, i, k;
    s_nit = 0;
    for (i = 0; i < s_nel[p]; i++) {
        uint32_t e = s_el[p][i];
        Item *t = NULL;
        for (k = 0; k < s_nit; k++) if (s_it[k].a == e) { t = &s_it[k]; break; }
        if (!t) { if (s_nit >= MAXEL) break; t = &s_it[s_nit++]; }
        t->a = e; t->ret = s_ret[p][i];
        if (e == EXIT_EL) t->ret = 0x00079017u;   /* (drawn by us, one of the main menu's) */
        t->x = (int)G32(e + 8); t->y = (int)G32(e + 0xC); t->w = (int)G32(e + 0x10); t->h = (int)G32(e + 0x14);
        t->col = G32(e + 0x20); t->on = G8(e + 0x24) != 0; t->font = G32(e + 0x2C);
        t->text = G32(e + 0x30) == 1; t->txt = G32(e + 0x34);
    }
}

/* ---- lists: the chosen item is the one in a colour of its own ---- */
#define MAXG 16
typedef struct { uint32_t ret, font; int n, sel; int m[48]; } Group;
static Group s_g[MAXG];
static int s_ng;
/* colours seen as "chosen" against "not chosen" in a list of three or more,
 * to tell which of two is chosen (a Yes / No) */
static struct { uint32_t off, on; } s_pairs[16];
static int s_npairs;
static uint32_t s_wassel[64];   /* items seen chosen: the ones the selection can reach */
static int s_nwassel;
static int was_sel(uint32_t a) { int i; for (i = 0; i < s_nwassel; i++) if (s_wassel[i] == a) return 1; return 0; }
static void pair_add(uint32_t off, uint32_t on)
{
    int i;
    for (i = 0; i < s_npairs; i++) if (s_pairs[i].off == off && s_pairs[i].on == on) return;
    if (s_npairs < 16) { s_pairs[s_npairs].off = off; s_pairs[s_npairs].on = on; s_npairs++; }
}
static int pair_known(uint32_t off, uint32_t on)
{
    int i;
    for (i = 0; i < s_npairs; i++) if (s_pairs[i].off == off && s_pairs[i].on == on) return 1;
    return 0;
}
static void groups(void)
{
    int i, k;
    s_ng = 0;
    for (i = 0; i < s_nit; i++) {
        Item *t = &s_it[i];
        Group *g = NULL;
        if (!t->text || !t->on || is_help(t) || t->w <= 0 || t->h <= 0) continue;
        if (t->ret == 0x000CF48Cu) continue;              /* the HUD in play */
        if (((t->col >> 24) & 0xFF) < 0x80) continue;      /* fading */
        for (k = 0; k < s_ng; k++) if (s_g[k].ret == t->ret && s_g[k].font == t->font) { g = &s_g[k]; break; }
        if (!g) { if (s_ng >= MAXG) continue; g = &s_g[s_ng++]; g->ret = t->ret; g->font = t->font; g->n = 0; }
        if (g->n < 48) g->m[g->n++] = i;
    }
    for (k = 0; k < s_ng; k++) {
        Group *g = &s_g[k];
        int a, b, uniq = -1, nuniq = 0, maj = -1, majn = 0;
        g->sel = -1;
        if (g->n < 2) continue;
        for (a = 0; a < g->n; a++) {
            int c = 0;
            for (b = 0; b < g->n; b++) if (s_it[g->m[b]].col == s_it[g->m[a]].col) c++;
            if (c == 1) { uniq = a; nuniq++; }
            else if (c > majn) { majn = c; maj = a; }
        }
        if (nuniq == 1 && majn >= 2) {
            g->sel = g->m[uniq];
            pair_add(s_it[g->m[maj]].col, s_it[g->sel].col);
        } else if (g->n == 2 && nuniq == 2) {
            if (pair_known(s_it[g->m[1]].col, s_it[g->m[0]].col)) g->sel = g->m[0];
            else if (pair_known(s_it[g->m[0]].col, s_it[g->m[1]].col)) g->sel = g->m[1];
        }
        if (g->sel >= 0 && !was_sel(s_it[g->sel].a)) {
            if (s_nwassel < 64) s_wassel[s_nwassel++] = s_it[g->sel].a;
        }
    }
}
static Group *group_of(int item)
{
    int k, j;
    for (k = 0; k < s_ng; k++) for (j = 0; j < s_g[k].n; j++) if (s_g[k].m[j] == item) return &s_g[k];
    return NULL;
}

/* ---- the injected presses ---- */
static volatile uint32_t s_inj;          /* what the controller also holds now (PAD_ bits, INJ_A/B) */
static volatile unsigned s_inj_serial;   /* moves when s_inj does (the packet number) */
static int s_phase, s_left;              /* 1 pressing, 2 letting go; frames left */
static volatile unsigned s_polls;   /* the game's looks at the pad (pad_filter) */
static unsigned s_polls0;
static int s_need;
static DWORD s_press_t;
static void press_n(uint32_t what, int frames)
{
    s_inj = what; s_inj_serial++;
    s_phase = 1; s_left = frames;
    s_need = frames > 1 ? 2 : 1;          /* the game must have seen it (at any frame rate) */
    s_polls0 = s_polls; s_press_t = GetTickCount();
}
static void press(uint32_t what) { press_n(what, 2); }
/* a frame: 1 = idle (ready for the next press) */
static int press_tick(void)
{
    int seen, late;
    if (!s_phase) return 1;
    --s_left;
    seen = (int)(s_polls - s_polls0) >= s_need;
    late = GetTickCount() - s_press_t > 250;
    if (s_left > 0 && !late) return 0;
    if (!seen && !late) return 0;
    /* (1.8.1) held, and let go, at least 70 ms each: at a high frame rate a
     * press of two pad reads was a few milliseconds, and the end-of-level
     * screens' leaves and level leaves took no left / right that short --
     * only A on the one already chosen worked */
    if ((int)(GetTickCount() - s_press_t) < 70) return 0;
    if (s_phase == 1) {
        s_inj = 0; s_inj_serial++; s_phase = 2; s_left = 3;
        s_need = 2; s_polls0 = s_polls; s_press_t = GetTickCount();
        return 0;
    }
    s_phase = 0;
    return 1;
}

/* ---- a message "... Press the A button to continue." (Game Saved and the
 * like): its words name Start's key with the keyboard (Enter), so Start
 * closes it as A does (prompts_kb.inc) ---- */
static volatile int s_msg_up;
int tt_msg_up(void) { return s_msg_up; }
static int utf16_has(uint32_t t, const char *w)
{
    int i, j, n = (int)strlen(w);
    if (t < 0x10000u || t >= 0x84000000u - 512u) return 0;
    for (i = 0; i < 200 && G16(t + 2 * i); i++) {
        for (j = 0; j < n && G16(t + 2 * (i + j)) == (uint16_t)(unsigned char)w[j]; j++) ;
        if (j == n) return 1;
    }
    return 0;
}
static void msg_scan(void)
{
    int p = s_cur ^ 1, i, up = 0;
    for (i = 0; i < s_ntx[p] && !up; i++) if (utf16_has(s_tx[p][i].txt, " to continue")) up = 1;
    for (i = 0; i < s_nit && !up; i++) if (s_it[i].text && s_it[i].on && utf16_has(s_it[i].txt, " to continue")) up = 1;
    s_msg_up = up;
}

/* the right mouse button bound to an action other than B (back)? */
extern int xbox_BindKey(int action, int i);
static int rmb_bound(void)   /* 1 + the action (a pad button's, 0..15), or 0 */
{
    int a, i;
    for (a = 0; a < 16; a++)
        if (a != 1)   /* (B: back anyway) */
            for (i = 0; i < 3; i++) if (xbox_BindKey(a, i) == 0x02 /* VK_RBUTTON */) return a + 1;
    return 0;
}

#define MAP_SCREEN_ 1
static int rmb_own(void)
{
    /* (1.8.2) not while the Options pages are up over the mission select: the
     * right button there is Back -- as its own action (Options) it opened
     * the pages again as soon as they closed */
    extern int tt_opt_ours(void);
    extern int tt_rmb_charsel(void);
    if (tt_opt_ours()) return 0;
    if (tt_rmb_charsel()) return 0;   /* (1.8.2) the character select: the right button is Back (unselects the hero chosen) */
    return G32(FE_SCREEN) == MAP_SCREEN_ && G32(FE_SCREEN) != 5 ? rmb_bound() : 0;
}

/* ---- the films before the title: a left click skips them (1.9.1) ----
 * The game plays them to their end (A and Start do nothing). Bink's movie
 * (HBINK: +0 width, +4 height, +8 frames, +0xC the frame now, +0x10 the last
 * shown, +0x14 / +0x18 the rate) is found in memory while a film plays and
 * put at its last frame: the game's loop (0x000E8B70) ends at frame ==
 * frames - 1, as at its end. */
#include <stddef.h>
extern ptrdiff_t xbox_GetMemoryOffset(void);
static uint32_t bink_find(void)
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)xbox_GetMemoryOffset();
    uint32_t a;
    for (a = 0x10000u; a < 0x84000000u - 0x40u; a += 4) {   /* (found low: 0x011AECE0 for the logos) */
        const uint32_t *q;
        uint32_t w, h, fr, fn, rate, div;
        if (a == 0x04000000u) a = 0x80010000u;
        q = (const uint32_t *)(m + a);
        w = q[0];
        if (w < 320 || w > 1280) continue;
        h = q[1]; if (h < 200 || h > 720) continue;
        fr = q[2]; if (fr < 2 || fr > 200000) continue;
        fn = q[3]; if (fn < 1 || fn > fr) continue;
        if (q[4] > fr) continue;
        rate = q[5]; div = q[6];
        if (!div || rate / div < 10 || rate / div > 61) continue;
        return a;
    }
    return 0;
}
static void bink_dump(void)   /* testing: TT_FILMCLICK -- where 640, 480 and a frame count sit together */
{
    const uint8_t *m = (const uint8_t *)(uintptr_t)xbox_GetMemoryOffset();
    uint32_t a; int n = 0;
    for (a = 0x10000u; a < 0x84000000u - 0x40u && n < 20; a += 4) {
        const uint32_t *q;
        if (a == 0x04000000u) a = 0x80000000u;
        q = (const uint32_t *)(m + a);
        if (q[0] == 640 && q[1] == 480 && q[2] >= 100 && q[2] < 20000) {
            tt_dbg("[BINK] %08X: %u %u %u %u %u %u %u %u %u %u\n", a, q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9]);
            n++;
        }
    }
}
static void film_skip(void)
{
    if (getenv("TT_BINKDUMP")) bink_dump();
    uint32_t b = bink_find();
    if (b) {
        uint32_t fr = G32(b + 8);
        tt_dbg("[MOUSE] film skipped by a click (frame %u of %u)\n", G32(b + 0xC), fr);
        G32(b + 0xC) = fr - 1;   /* (the game's film loop ends at frame == frames - 1, 0x000E8D69) */
    } else tt_dbg("[MOUSE] a click on a film: its movie not found\n");
}

/* ---- the pointer ---- */
static int s_active;            /* the pointer is the mouse's (a menu is up) */
static int s_shown = 1;         /* drawn (not put away by the pad or keys since the mouse last moved) */
static float s_gx = 320, s_gy = 240, s_k = 1;   /* where, in the game's 640x480 (and the wide frame's narrowing) */
static float s_fx = 320, s_fy = 240;            /* where on the picture */
static volatile int s_pad_used;  /* (the input thread) the pad or keys moved */
/* aiming with the mouse: the ranged button held (the input thread sees it),
 * the stick the mouse has made (-1..1, y down) */
static volatile DWORD s_lt_tick;
volatile int tt_l3_acc, tt_push_acc, tt_in_polls;
static volatile int s_aim_on;
static volatile int s_steer_on;            /* mouse steering: the mouse is the stick while forward is held */
static volatile DWORD s_fwd_tick;          /* (1.9.1) forward (W, the stick pushed up) last held, with steering on */
static volatile float s_steer_x, s_steer_y = -1.0f;   /* the mouse's stick for steering (up: forward) */
static volatile float s_aim_x, s_aim_y;

/* what the pointer is after */
enum { GO_NONE, GO_ITEM, GO_NODE, GO_LIST, GO_LEAF, GO_TAB };
static struct {
    int kind;
    uint32_t item;        /* GO_ITEM: the element to make the chosen one (or whose row) */
    float tx, ty;         /* where it is */
    int node;             /* GO_NODE */
    int then;             /* 0, 'A' (press A when there), 'V' (a value / slider: left/right to tx) */
    int steps, fails;
    uint32_t last_sel;    /* the chosen element (or node) before the last press */
    int last_dir;
    float knob_last;
    DWORD step_t;         /* the last step's press (ticks) */
    int last_cur;         /* a value's set one before the last press */
} s_go;

static void go_clear(void) { memset(&s_go, 0, sizeof s_go); }

/* ---- the mission select (front-end screen 1): its places and neighbours ---- */
#define MAP_SCREEN   1
#define MAP_OBJ      (0x4AD050u + 8u)
#define MAP_NODES    28           /* (27: Secret Codes, the ring at the right) */
#define MAP_ELBASE   0x429B20u
#define MAP_NODETAB  0x1A4348u    /* 32 bytes a place: +0 the highlight's element count, +4 its first */
#define MAP_NEIGH    0x1A4114u    /* 20 bytes a place: left, right, up, down */
static unsigned char s_blocked[MAP_NODES][4];
static int map_here(void)
{
    int i;
    if (G32(FE_SCREEN) != MAP_SCREEN) return 0;
    for (i = 0; i < s_nit; i++) if (s_it[i].ret == 0x00076F17u) return 1;   /* the map's drawing */
    return 0;
}
static int map_cur(void) { int c = (int)G32(MAP_OBJ + 0x10); return c >= 0 && c < MAP_NODES ? c : -1; }
static int map_node_at(float x, float y)
{
    int n, best = -1;
    float ba = 1e9f;
    for (n = 0; n < MAP_NODES; n++) {
        int cnt = (int)G32(MAP_NODETAB + n * 32), first = (int)G32(MAP_NODETAB + n * 32 + 4), j;
        if (cnt < 1 || cnt > 4 || first < 0 || first > 80) continue;
        for (j = 0; j < cnt; j++) {
            uint32_t e = MAP_ELBASE + (uint32_t)(first + j) * EL_SIZE;
            int ex = (int)G32(e + 8), ey = (int)G32(e + 0xC), ew = (int)G32(e + 0x10), eh = (int)G32(e + 0x14);
            if (x >= ex && x < ex + ew && y >= ey && y < ey + eh && (float)(ew * eh) < ba) { ba = (float)(ew * eh); best = n; }
        }
    }
    if (best < 0) {
        /* any of the map's pictures there (a place's icon): the place whose
         * highlight is nearest it */
        int i;
        float bx = -1, by = -1, bd = 1e9f;
        for (i = 0; i < s_nit; i++) {
            Item *t = &s_it[i];
            if (t->ret != 0x00076F17u || t->text || !t->on || t->w > 200 || t->h > 200 || !hit(t, x, y)) continue;
            if ((float)(t->w * t->h) < bd) { bd = (float)(t->w * t->h); bx = icx(t); by = icy(t); }
        }
        if (bx >= 0) {
            bd = 48.0f * 48.0f;
            for (n = 0; n < MAP_NODES; n++) {
                int first = (int)G32(MAP_NODETAB + n * 32 + 4), cnt = (int)G32(MAP_NODETAB + n * 32);
                uint32_t e;
                float cx, cy, d;
                if (cnt < 1 || cnt > 4 || first < 0 || first > 80) continue;
                e = MAP_ELBASE + (uint32_t)first * EL_SIZE;
                cx = (float)(int)G32(e + 8) + (float)(int)G32(e + 0x10) * 0.5f;
                cy = (float)(int)G32(e + 0xC) + (float)(int)G32(e + 0x14) * 0.5f;
                d = (cx - bx) * (cx - bx) + (cy - by) * (cy - by);
                if (d < bd) { bd = d; best = n; }
            }
        }
    }
    return best;
}
/* the place n chosen, as the game's move does (0x000776EC..): the place,
 * its highlight (0x00076440), the move's two sounds */
static void play_sound(uint32_t id, float pan);
static void map_jump(int n)
{
    int lev = (int)G32(MAP_NODETAB - 4 + n * 32) == 1 && n < 12;
    G32(MAP_OBJ + 0x10) = (uint32_t)n;
    tt_gcall(0x00076440u, MAP_OBJ, 0, NULL);
    play_sound(lev ? 0xAB4 : 0xAB8, -1.0f);
    play_sound(lev ? 0xAB5 : 0xAB9, 1.0f);
}
/* the first step from place a towards place b (0 left, 1 right, 2 up, 3 down; -1 none) */
static int map_route(int a, int b)
{
    int prev[MAP_NODES], how[MAP_NODES], q[MAP_NODES], qh = 0, qt = 0, i, d;
    for (i = 0; i < MAP_NODES; i++) prev[i] = -2;
    prev[a] = -1; q[qt++] = a;
    while (qh < qt) {
        int c = q[qh++];
        if (c == b) break;
        for (d = 0; d < 4; d++) {
            int nb = (int)G32(MAP_NEIGH + c * 20 + d * 4);
            if (s_blocked[c][d] || nb < 0 || nb >= MAP_NODES || nb == c || prev[nb] != -2) continue;
            prev[nb] = c; how[nb] = d; q[qt++] = nb;
        }
    }
    if (prev[b] == -2) return -1;
    for (i = b; prev[i] != a; i = prev[i]) if (prev[i] < 0) return -1;
    return how[i];
}

/* ---- the character select (the mission select's screen, its own drawing) ---- */
extern int tt_opt_up(void);
int tt_rmb_charsel(void);
static int charsel_here(void)
{
    int i;
    if (G32(FE_SCREEN) != MAP_SCREEN) return 0;
    for (i = 0; i < s_nit; i++) if (s_it[i].ret == 0x00076F37u) return 1;
    return 0;
}
int tt_rmb_charsel(void) { return charsel_here(); }

/* ---- the game's lists (tt_menu_list) ---- */
static int list_rowh(uint32_t o) { uint32_t f = G32(o + 0x1C4); return f == 0 ? 0x19 : f == 2 ? 0x28 : 0x1E; }
static int list_shown(uint32_t o) { int n = (int)G32(o + 0xF8), v = (int)G32(o + 0x100); return n > v && v > 0 ? v : n; }
/* the item at (x, y) of a list drawn last frame, -1 none */
static int list_at(float x, float y, uint32_t *obj)
{
    int p = s_cur ^ 1, i;
    for (i = s_nls[p] - 1; i >= 0; i--) {   /* (the last drawn first: a dialog is over the page's own list) */
        ListDraw *l = &s_ls[p][i];
        int n = list_shown(l->obj), h = list_rowh(l->obj), r;
        if (n <= 0 || n > 32) continue;
        x = pointer_x_for(l->x + 120.0f, l->y + n * h * 0.5f);
        if (x < l->x - 24 || x >= l->x + 360 || y < l->y || y >= l->y + n * h) continue;
        r = (int)((y - l->y) / h);
        *obj = l->obj;
        return (int)G32(l->obj + 0xF4) + r;
    }
    return -1;
}

/* the pointer's art: an arrow, gold with a dark edge (made once) */
#define CUR_W 24
#define CUR_H 36
static uint32_t s_cur_px[CUR_W * CUR_H];
static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay, t = (vx * wx + vy * wy) / (vx * vx + vy * vy);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    wx -= t * vx; wy -= t * vy;
    return sqrtf(wx * wx + wy * wy);
}
static void cursor_art(void)
{
    /* the arrow, in pixels of the 24x36 picture */
    static const float P[][2] = { { 2, 2 }, { 2, 27 }, { 8, 21.5f }, { 12.5f, 32 }, { 17, 30 }, { 12.8f, 20.2f }, { 20.5f, 20.2f } };
    const int N = (int)(sizeof P / sizeof P[0]);
    int x, y, i;
    for (y = 0; y < CUR_H; y++)
        for (x = 0; x < CUR_W; x++) {
            float px = x + 0.5f, py = y + 0.5f, d = 1e9f, a, r, g, b, t;
            int inside = 0;
            for (i = 0; i < N; i++) {
                const float *p0 = P[i], *p1 = P[(i + 1) % N];
                float sd = seg_dist(px, py, p0[0], p0[1], p1[0], p1[1]);
                if (sd < d) d = sd;
                if (((p0[1] > py) != (p1[1] > py)) && px < (p1[0] - p0[0]) * (py - p0[1]) / (p1[1] - p0[1]) + p0[0]) inside = !inside;
            }
            if (!inside) d = -d;            /* d > 0 inside */
            /* 1.6 px of dark edge outside the fill, faded at its rim */
            a = d + 1.6f + 0.5f;
            if (a <= 0) { s_cur_px[y * CUR_W + x] = 0; continue; }
            if (a > 1) a = 1;
            t = d - 0.2f;                   /* the fill, smoothly inside the edge */
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            /* gold, lighter at the top (as the menus' lettering) */
            r = 255 - py * 1.6f; g = 222 - py * 2.6f; b = 140 - py * 2.6f;
            if (b < 30) b = 30;
            r = r * t + 34 * (1 - t); g = g * t + 22 * (1 - t); b = b * t + 10 * (1 - t);
            s_cur_px[y * CUR_W + x] = ((uint32_t)(a * 255) << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
}
typedef struct { const uint32_t *px; int w, h; unsigned ver; float x, y, dw, dh; } XboxOverlayQuad;
extern int (*volatile xbox_overlay_cb)(XboxOverlayQuad *q, int max);
static int (*s_prev_ovl)(XboxOverlayQuad *q, int max);
static volatile int s_draw;
static volatile float s_draw_x, s_draw_y, s_draw_k = 1;
static int ptr_overlay(XboxOverlayQuad *q, int max)
{
    int n = s_prev_ovl ? s_prev_ovl(q, max) : 0;
    extern int tt_lang_srt_overlay(void *quads, int max, int n);
    n = tt_lang_srt_overlay(q, max, n);
    if (s_draw && n < max) {
        float k = s_draw_k;
        q[n].px = s_cur_px; q[n].w = CUR_W; q[n].h = CUR_H; q[n].ver = 1;
        q[n].x = s_draw_x - 1.0f * k; q[n].y = s_draw_y - 1.0f;
        {
            static const float sz[4] = { 0.75f, 1.0f, 1.4f, 1.85f };
            int i = xbox_GfxGet(GFX_CURSOR_SIZE);
            float z = i >= 0 && i < 4 ? sz[i] : 1.0f;
            q[n].dw = 12.0f * k * z; q[n].dh = 18.0f * z;
        }
        n++;
    }
    return n;
}

static int mouse_log(void) { static int on = -1; if (on < 0) on = getenv("TT_MOUSELOG") != NULL; return on; }
/* ---- the controller, as the game is about to see it ---- */
typedef struct { uint16_t wButtons; uint8_t bAnalogButtons[8]; int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XPad;
typedef struct { uint32_t dwPacketNumber; XPad Gamepad; } XState;
extern void (*volatile xbox_pad_filter)(unsigned long port, XState *st);
static void pad_filter(unsigned long port, XState *st)
{
    uint32_t prim = G32(0x1CB8B4u), inj = s_inj;
    if (port == 0 || port == G32(0x1CB8B4u)) s_polls++;
    if (prim > 3) prim = 0;
    {
        static unsigned nc[4];
        if (mouse_log() && (nc[port & 3]++ % 600) == 0) tt_dbg("[MOUSE] filter port %lu (player 1 on %u), inj %05X\n", port, prim, inj);
    }
    /* (every pad the game reads: which one is player 1's, as the pads are
     * numbered here, is not known; the menus listen to player 1) */
    if (port < 4) {
        /* the pad or keys used: the pointer is put away until the mouse moves */
        static XPad last;
        XPad *p = &st->Gamepad;
        int used = (p->wButtons & 0x0F3F) || p->bAnalogButtons[0] > 30 || p->bAnalogButtons[1] > 30
                   || p->bAnalogButtons[2] > 30 || p->bAnalogButtons[3] > 30
                   || abs(p->sThumbLX) > 12000 || abs(p->sThumbLY) > 12000;
        if (used && memcmp(&last, p, sizeof last)) s_pad_used = 1;
        last = *p;
        if (p->bAnalogButtons[6] > 30) s_lt_tick = GetTickCount() | 1;   /* the ranged button */
        if (s_msg_up && (p->wButtons & 0x0010)) {   /* a message's "Press Enter to continue": Start as A */
            p->wButtons &= (uint16_t)~0x0010u;
            p->bAnalogButtons[0] = 255;
        }
        {   /* for Sauron's sprint (heroes.c): the left stick's click and push,
             * over every pad read since heroes.c last took them (a controller
             * and the keys can come as two pads: the one read last would
             * otherwise say "not held" for the other) */
            extern volatile int tt_l3_acc, tt_push_acc, tt_in_polls;
            int sx = p->sThumbLX, sy = p->sThumbLY, push = (int)sqrtf((float)sx * sx + (float)sy * sy);
            if (p->wButtons & 0x0040) tt_l3_acc = 1;
            if (push > tt_push_acc) tt_push_acc = push;
            tt_in_polls++;
        }
        /* (1.9.1) mouse steering, as aiming with no bow (the prologue's
         * Isildur): while forward is held (W, or the stick pushed up), the
         * mouse is the stick -- pushed by the mouse and kept where it was
         * pushed, so the hero runs the way the mouse points (from the
         * camera's view); let go of forward and he stops */
        /* (1.9.2) scaled by forward's own push: Walk (W pushed part way)
         * walks while steering -- before, any push past 12000 took the
         * mouse's whole stick, and Walk's half push did not steer at all */
        /* (1.9.3) only when forward is the keys' (W): a controller's stick
         * pushed up was turned the mouse's way -- the stick seemed not to
         * work with mouse steering on. Likewise aiming takes the stick only
         * for a key's or mouse button's ranged button, not the controller's LT */
        extern volatile int xbox_input_kb_lstick, xbox_input_kb_lt;
        int kb_aim = s_aim_on && p->bAnalogButtons[6] > 30 && xbox_input_kb_lt;
        if (s_steer_on && !kb_aim && xbox_input_kb_lstick && p->sThumbLY > 3000) {
            float m = p->sThumbLY / 32767.0f;
            if (m > 1.0f) m = 1.0f;
            s_fwd_tick = GetTickCount() | 1;
            p->sThumbLX = (int16_t)(s_steer_x * m * 32767.0f);
            p->sThumbLY = (int16_t)(-s_steer_y * m * 32767.0f);
        }
        if (kb_aim) {
            /* aiming: the mouse's stick for the left stick (WASD's) */
            float x = s_aim_x, y = s_aim_y;
            p->sThumbLX = (int16_t)(x * 32767.0f);
            p->sThumbLY = (int16_t)(-y * 32767.0f);
        }
        {   /* (1.9.3) the front end's menus: the stick as a d-pad -- pushed
             * past about a quarter, it is pushed all the way along its main
             * direction. The game's menus move only for a hard push (the main
             * menu's up took a full one), and a push a little to the side
             * lost more of it; the d-pad always worked */
            static int dir[4];   /* per pad: 0 none, 1 up, 2 down, 3 left, 4 right */
            uint32_t scr = G32(FE_SCREEN);
            int *d = &dir[port & 3], sx = p->sThumbLX, sy = p->sThumbLY;
            int m2 = sx * sx + sy * sy;
            if (scr == 6 || scr == 8 || getenv("TT_NO_STICKPAD")) *d = 0;
            else {
                int on = *d ? m2 > 5000 * 5000 : m2 > 7000 * 7000;
                if (!on) *d = 0;
                else if (!*d || m2 > 7000 * 7000)   /* (the direction it is mostly pushed; kept as it eases back) */
                    *d = abs(sy) >= abs(sx) ? (sy > 0 ? 1 : 2) : (sx < 0 ? 3 : 4);
                if (*d) {
                    p->sThumbLX = *d == 3 ? -32767 : *d == 4 ? 32767 : 0;
                    p->sThumbLY = *d == 1 ? 32767 : *d == 2 ? -32767 : 0;
                }
            }
        }
        if (inj) {
            static unsigned nlog;
            if (mouse_log() && nlog++ < 400) tt_dbg("[MOUSE] pad port %lu: +%05X (packet %08X)\n", port, inj, st->dwPacketNumber);
            p->wButtons |= (uint16_t)(inj & 0xFFFF);
            {   /* (the analog buttons: A B X Y Black White LT RT, bits 16..23) */
                int k;
                for (k = 0; k < 8; k++) if (inj & (0x10000u << k)) p->bAnalogButtons[k] = 255;
            }
        }
        /* (the packet number moves with what is added, or the game sees no change) */
        st->dwPacketNumber += s_inj_serial << 20;
    }
    if (tt_mm_input(port, st->Gamepad.bAnalogButtons[0] > 30, (st->Gamepad.wButtons & PAD_START) != 0)) {
        st->Gamepad.bAnalogButtons[0] = 0;
        st->Gamepad.wButtons &= (uint16_t)~PAD_START;
    }
    {   /* the Options pages of ours (options.c) */
        extern void tt_opt_filter(unsigned long port, XState *st);
        tt_opt_filter(port, st);
    }
    { extern void tt_hero_size_filter(unsigned long port,void *state);
      tt_hero_size_filter(port,st); }
}


/* a direction towards (tx, ty) from the chosen item s */
static uint32_t dir_to(const Item *s, float tx, float ty)
{
    float dy = ty - icy(s), dx = tx - icx(s);
    if (fabsf(dy) > s->h * 0.5f) return dy > 0 ? PAD_DOWN : PAD_UP;
    return dx > 0 ? PAD_RIGHT : PAD_LEFT;
}

/* the chosen item of the list that (x, y) points at, or of the busiest list */
static int find_sel(void)
{
    int k, best = -1, bn = 0;
    for (k = 0; k < s_ng; k++) if (s_g[k].sel >= 0 && s_g[k].n > bn) { bn = s_g[k].n; best = s_g[k].sel; }
    return best;
}
static int item_index(uint32_t a) { int i; for (i = 0; i < s_nit; i++) if (s_it[i].a == a) return i; return -1; }

/* a list across (all its items level: Easy / Normal / Expert) */
static int group_flat(const Group *g)
{
    int j;
    for (j = 1; j < g->n; j++) if (s_it[g->m[j]].y != s_it[g->m[0]].y) return 0;
    return 1;
}
/* what is under (x, y): a list's item (*row = 0), or a list's row whose item
 * is to the left (*row = 1: a setting's values or slider); -1 nothing */
static int target_at(float x, float y, int *row)
{
    int k, j, best = -1;
    *row = 0;
    for (k = 0; k < s_ng; k++) {
        if (s_g[k].sel < 0) continue;
        for (j = 0; j < s_g[k].n; j++) {
            Item *t = &s_it[s_g[k].m[j]];
            if (hit(t, x, y) && (was_sel(t->a) || group_flat(&s_g[k]) || t->x == s_it[s_g[k].sel].x)) return s_g[k].m[j];
        }
    }
    /* a row: the item that has been chosen, level with the pointer, left of it */
    for (k = 0; k < s_ng; k++) {
        float bd = 1e9f;
        if (s_g[k].sel < 0) continue;
        for (j = 0; j < s_g[k].n; j++) {
            Item *t = &s_it[s_g[k].m[j]];
            Item *sl = &s_it[s_g[k].sel];
            if (t->x != sl->x) continue;                      /* the column of the chosen one */
            if (fabsf(y - icy(t)) <= t->h * 0.5f + 4 && x >= t->x && x - t->x < bd) { bd = x - t->x; best = s_g[k].m[j]; }
        }
        if (best >= 0) { *row = 1; return best; }
    }
    return -1;
}

/* a setting's row: its values (text in the row, right of the name) and which is set */
static int row_values(const Item *lab, int *vals, int maxv, int *cur)
{
    int i, n = 0, k;
    *cur = -1;
    for (i = 0; i < s_nit && n < maxv; i++) {
        Item *t = &s_it[i];
        if (!t->text || !t->on || t == lab || t->x <= lab->x + 40 || fabsf(icy(t) - icy(lab)) > 6) continue;
        vals[n++] = i;
    }
    /* the set one: a marker drawn over it */
    for (k = 0; k < n; k++) {
        Item *v = &s_it[vals[k]];
        for (i = 0; i < s_nit; i++) {
            Item *m = &s_it[i];
            if (m->text || !m->on || m->w > 96 || m->w < 16 || fabsf(icy(m) - icy(v)) > 20) continue;
            if (icx(m) >= v->x && icx(m) < v->x + v->w) { *cur = k; break; }
        }
        if (*cur >= 0) break;
    }
    return n;
}
/* a slider's knob in the row: (x of its middle, or -1) */
static float row_knob(const Item *lab, float *bar0, float *bar1)
{
    int i;
    float kx = -1;
    *bar0 = *bar1 = -1;
    for (i = 0; i < s_nit; i++) {
        Item *t = &s_it[i];
        if (t->text || !t->on || fabsf(icy(t) - icy(lab)) > 24) continue;
        if (t->w >= 200 && t->w < 640) { *bar0 = (float)t->x; *bar1 = (float)(t->x + t->w); }
        else if (t->w >= 16 && t->w <= 96 && t->x > lab->x + 100 && kx < 0) kx = icx(t);
    }
    return *bar0 >= 0 ? kx : -1;
}

/* a picture's descriptor address that can be one (in the game's memory) */
static int tex_ok(uint32_t t) { return (t >= 0x80010000u && t < 0x88000000u - 0x40u) || (t >= 0x10000u && t < 0x04000000u - 0x40u); }
/* ---- the leaves at the bottom corners: Back (left) and Continue (right) ---- */
#define LEAF_DIM 0xFF678192u
#define LEAF_LIT 0xFF4DDAF4u   /* (the game's own lit colour: 0xAABBGGRR, gold) */
static Item *leaf_label(int which)
{
    int i;
    for (i = 0; i < s_nit; i++) {
        Item *t = &s_it[i];
        if (t->text && t->on && t->w == 100 && t->h == 25 && t->y == 419 && t->x == (which ? 515 : 11)
            && t->txt >= 0x10000u && G16(t->txt) != 0) return t;
    }
    return NULL;
}
static Item *leaf_icon(int which)
{
    int i;
    for (i = 0; i < s_nit; i++) {
        Item *t = &s_it[i];
        if (!t->text && t->on && t->w == 96 && t->h == 75 && t->y == 373 && t->x == (which ? 484 : 42)) return t;
    }
    return NULL;
}
/* the leaf at (x, y): the leaf picture itself (not its whole square) or its word */
static int leaf_at(float x, float y)
{
    int w;
    for (w = 0; w < 2; w++) {
        Item *lb = leaf_label(w), *ic = leaf_icon(w);
        if (!lb) continue;
        if (hit(lb, x, y)) return w;
        if (ic) {
            float ix = pointer_x_for(ic->x + ic->w * 0.5f, ic->y + ic->h * 0.5f);
            if (ix >= ic->x + 14 && ix < ic->x + ic->w - 14 && y >= ic->y + 10 && y < ic->y + ic->h - 4) return w;
        }
    }
    return -1;
}
static int leaf_focused(int w) { Item *lb = leaf_label(w); return lb && lb->col != LEAF_DIM; }

/* ---- the upgrade screens' level leaves at the top, and the mark on the chosen one ---- */
static int upg_tabs(Item **tabs, int max)
{
    int i, n = 0;
    for (i = 0; i < s_nit && n < max; i++) {
        Item *t = &s_it[i];
        if (!t->text && t->on && t->w == 128 && t->h == 64 && t->y == 90) tabs[n++] = t;
    }
    return n;
}
static Item *upg_mark(void)
{
    int i;
    for (i = 0; i < s_nit; i++) {
        Item *t = &s_it[i];
        if (!t->text && t->w == 128 && t->h == 64 && t->y == 95) return t;
    }
    return NULL;
}
static int tab_cur(Item **tabs, int n, const Item *mk)
{
    int i, best = -1, bd = 1 << 30;
    if ((!mk || !mk->on) && n >= 2) {
        /* (the upgrades after a level: no marker, and the chosen leaf's glow
         * is not one of the pictures -- the game's own index of the chosen
         * leaf, 0 the leftmost, 0x4B7054) */
        int want = (int)G32(0x4B7054u);
        if (want >= 0 && want < n) {
            for (i = 0; i < n; i++) {
                int rank = 0, j;
                for (j = 0; j < n; j++) if (tabs[j]->x < tabs[i]->x) rank++;
                if (rank == want) return i;
            }
        }
    }
    if ((!mk || !mk->on) && n >= 3) {
        /* (the upgrades after a level: no marker; the chosen leaf has a
         * picture of its own, apart from the others' run of pictures) */
        int odd = -1, nodd = 0, j;
        for (i = 0; i < n; i++) {
            uint32_t ti = G32(tabs[i]->a + 0xB8);
            int nclose = 0;
            for (j = 0; j < n; j++) if (j != i) { uint32_t tj = G32(tabs[j]->a + 0xB8); if (ti - tj + 0x100u <= 0x200u) nclose++; }
            if (!nclose) { odd = i; nodd++; }
        }
        return nodd == 1 ? odd : -1;
    }
    if (!mk || !mk->on) return -1;
    for (i = 0; i < n; i++) { int d = abs(tabs[i]->x - (mk->x + 19)); if (d < bd) { bd = d; best = i; } }
    return best;
}

/* testing: TT_MOUSE_SCRIPT=file, lines "ms x y buttons" -- the mouse at
 * (x, y) of the 640x480 picture with those buttons (1 left, 2 right; 8 / 16
 * a wheel notch down / up), ms
 * after the main menu first shows; replaces the real mouse */
static void mouse_script(int *inside, float *mx, float *my, int *btn)
{
    static int loaded, n, at;
    static struct { unsigned t; float x, y; int b; } ev[256];
    static DWORD t0;
    float m[5];
    if (!loaded) {
        const char *f = getenv("TT_MOUSE_SCRIPT");
        FILE *fp = f ? fopen(f, "r") : NULL;
        loaded = 1;
        if (fp) {
            char line[128];
            while (n < 256 && fgets(line, sizeof line, fp))
                if (sscanf(line, "%u %f %f %d", &ev[n].t, &ev[n].x, &ev[n].y, &ev[n].b) == 4) n++;
            fclose(fp);
        }
    }
    if (!n) return;
    if (!t0) { if ((G32(FE_SCREEN) == 4 && G32(MM_OBJ + 0x10) == 0 && s_nel[s_cur ^ 1] >= 10)
                   || (getenv("TT_MOUSE_PLAY") && (int)G32(FE_SCREEN) == atoi(getenv("TT_MOUSE_PLAY")))) t0 = GetTickCount();   /* (or that front-end screen) */ else { *inside = 0; *btn = 0; return; } }
    while (at < n && GetTickCount() - t0 >= ev[at].t) {
        if ((ev[at].b & 1) && !(at > 0 && (ev[at - 1].b & 1))) xbox_fb_clicks[0]++;
        if ((ev[at].b & 2) && !(at > 0 && (ev[at - 1].b & 2))) xbox_fb_clicks[1]++;
        {   /* (8: a wheel notch down, 16: up) */
            extern volatile LONG xbox_fb_wheel;
            if (ev[at].b & 8) xbox_fb_wheel--;
            if (ev[at].b & 16) xbox_fb_wheel++;
        }
        tt_dbg("[MOUSE] script %u ms: %.0f,%.0f buttons %d\n", ev[at].t, ev[at].x, ev[at].y, ev[at].b);
        at++;
    }
    if (!at) { *inside = 0; *btn = 0; return; }
    xbox_PresentMap(m);
    *mx = m[0] + ev[at - 1].x / 640.0f * m[2];
    *my = m[1] + ev[at - 1].y / 480.0f * m[3];
    *btn = ev[at - 1].b & 3;
    *inside = 1;
}

/* a frame of the pointer (the game thread, at the frame's end) */
static void pointer_frame(void)
{
    static int inited, was_down, was_rdown, lclk, lrclk, rel_on;
    static float vfx = 320, vfy = 240;
    int held, clk, rclk, want_rel = 0, script = getenv("TT_MOUSE_SCRIPT") != NULL;
    static float lmx = -1, lmy = -1;
    float mx, my, m[5];
    int inside, btn, down, rdown, moved = 0, scr = (int)G32(FE_SCREEN), menu, front = xbox_InputGameInFront();
    if (!inited) {
        inited = 1;
        cursor_art();
        s_prev_ovl = xbox_overlay_cb;
        xbox_overlay_cb = ptr_overlay;
    }
    if (xbox_overlay_cb != ptr_overlay) { s_prev_ovl = xbox_overlay_cb; xbox_overlay_cb = ptr_overlay; }
    snapshot();
    groups();
    msg_scan();
    xbox_PresentMap(m);
    {   /* the F10 settings open: the system's pointer for it (even in full screen), nothing of ours */
        extern int xbox_GfxMenuIsOpen(void);
        if (xbox_GfxMenuIsOpen()) {
            s_draw = 0; s_active = 0; xbox_mouse_buttons_taken = 0; s_aim_on = 0;
            xbox_fb_hide_cursor = -1;
            if (s_phase) { s_inj = 0; s_inj_serial++; s_phase = 0; }
            go_clear(); s_tint_el = 0; s_tint2_el = 0;
            if (rel_on) { xbox_FramebufferMouseRelease(); rel_on = 0; }
            s_seen_clicks[0] = xbox_fb_clicks[0]; s_seen_clicks[1] = xbox_fb_clicks[1];
            return;
        }
    }
    /* a menu: the front end (not play, not a film or loading), or a list in play (the pause screens) */
    menu = scr != 6 && scr != 8 && !xbox_MoviePlaying();
    /* (1.8.2) the title and the main menu play a film behind them: still a
     * menu when it shows something to choose (it was not, and a click went
     * to the game as A -- the item chosen was pressed) */
    if (!menu && scr != 6 && scr != 8 && (find_sel() >= 0 || s_nls[s_cur ^ 1] > 0 || leaf_label(0) || leaf_label(1))) menu = 1;
    if (!menu && scr == 6 && !xbox_MoviePlaying() && (find_sel() >= 0 || s_nls[s_cur ^ 1] > 0)) menu = 1;
    if (xbox_GfxGet(GFX_MOUSE) != 0 || !front) menu = 0;

    /* aiming with the mouse in play: while the ranged button is held the
     * mouse's movement is a stick, kept where the mouse has pushed it */
    {
        static float ax, ay;
        int lt = s_lt_tick && (int)(GetTickCount() - s_lt_tick) < 150;   /* (signed: the stamp is the tick | 1) */
        int aim = !menu && front && scr == 6 && !xbox_MoviePlaying() && lt && xbox_GfxGet(GFX_MOUSE_AIM) == 0;
        if (aim) {
            int dx, dy, w, h;
            want_rel = 1;
            int got = 0;
            if (script) {   /* (testing: the script's moves as the mouse's, in pixels of a 1280x960 window) */
                static float sx = -1, sy = -1;
                float fx, fy;
                int in = 0, b2 = 0;
                mouse_script(&in, &fx, &fy, &b2);
                dx = dy = 0;
                if (in && m[2] > 0.01f) {
                    float px = (fx - m[0]) / m[2] * 1280.0f, py = (fy - m[1]) / m[3] * 960.0f;
                    if (sx >= 0) { dx = (int)(px - sx); dy = (int)(py - sy); }
                    sx = px; sy = py;
                }
                got = 1; w = 1280; h = 960;
            } else got = xbox_FramebufferMouseDelta(&dx, &dy, &w, &h);
            if (got && (dx || dy)) {
                float k = mouse_sens() / 160.0f, l;
                int inv = xbox_GfxGet(GFX_MOUSE_INVERT);
                if (!rel_on && !script) dx = dy = 0;   /* (the first look only puts the pointer in the middle) */
                if (mouse_log()) tt_dbg("[MOUSE] aim %+d %+d -> stick %.2f %.2f\n", dx, dy, ax + dx * k, ay + dy * k);
                if (inv == 2 || inv == 3) dx = -dx;
                if (inv == 1 || inv == 3) dy = -dy;
                ax += dx * k; ay += dy * k;
                l = sqrtf(ax * ax + ay * ay);
                if (l > 1.0f) { ax /= l; ay /= l; }
                if (dx || dy) s_aim_on = 1;
            }
        } else { ax = ay = 0; s_aim_on = 0; }
        s_aim_x = ax; s_aim_y = ay;
        {   /* (1.9.1) mouse steering: the mouse pushes a stick, as aiming does
             * (a stick that stays where it was pushed), used while forward
             * is held (pad_filter); it starts pointing ahead */
            extern volatile int tt_steer_active;
            int steer = !aim && !menu && front && scr == 6 && !xbox_MoviePlaying() && xbox_GfxGet(GFX_MOUSE_STEER) == 1;
            int fwd = s_fwd_tick && (int)(GetTickCount() - s_fwd_tick) < 150;
            if (steer) {
                int dx = 0, dy = 0, w = 0, h = 0, got;
                want_rel = 1;
                got = script ? 0 : xbox_FramebufferMouseDelta(&dx, &dy, &w, &h);
                if (getenv("TT_STEERTEST")) {   /* (testing: "dx,dy" -- the mouse moved so much every frame) */
                    float a = 0, b = 0;
                    sscanf(getenv("TT_STEERTEST"), "%f,%f", &a, &b);
                    got = 1; dx = (int)a; dy = (int)b;
                }
                if (got && (dx || dy) && fwd && (rel_on || script || getenv("TT_STEERTEST"))) {
                    float k = mouse_sens() / 160.0f, x = s_steer_x, y = s_steer_y, l;
                    int inv = xbox_GfxGet(GFX_MOUSE_INVERT);
                    if (inv == 2 || inv == 3) dx = -dx;
                    if (inv == 1 || inv == 3) dy = -dy;
                    x += dx * k; y += dy * k;
                    l = sqrtf(x * x + y * y);
                    if (l > 1.0f) { x /= l; y /= l; }
                    s_steer_x = x; s_steer_y = y;
                    if (mouse_log()) tt_dbg("[MOUSE] steer %+d %+d -> stick %.2f %.2f\n", dx, dy, x, y);
                }
            }
            if (!steer) { s_steer_x = 0; s_steer_y = -1.0f; }   /* (next time: ahead) */
            tt_steer_active = 0;   /* (the 1.8.2 turning in place: heroes.c, no longer) */
            s_steer_on = steer;
        }
    }

    s_active = menu;
    /* the pointer's mouse buttons: the left always (a click); the right
     * (back), but on the mission select and the character select -- whose
     * prompts name the keys (Help, Options) -- a right button bound to an
     * action is that action; the others keep their bindings */
    /* (1.8.2) and in the whole front end -- films, the title, loading, a
     * menu over a film -- the buttons are the pointer's only: a click that
     * skipped a film reached the title as A (Start), and the left button
     * pressed the menu's chosen item as the game's A while a film was
     * counted as playing */
    xbox_mouse_buttons_taken = menu || (scr != 6 && xbox_GfxGet(GFX_MOUSE) == 0 && front) ? 2 | 4 : 0;
    {   /* from the start: the pointer over the films and screens before the first level too */
        static int played;
        if (scr == 6 && !xbox_MoviePlaying()) played = 1;
        if (!menu && !played && front && xbox_GfxGet(GFX_MOUSE) == 0) {
            float bx, by;
            int bin = xbox_FramebufferMouse(&bx, &by);
            s_draw = bin && s_shown && m[2] > 0.01f && m[3] > 0.01f;
            if (s_draw) {
                s_fx = (bx - m[0]) / m[2] * 640.0f; s_fy = (by - m[1]) / m[3] * 480.0f;
                s_draw_x = s_fx; s_draw_y = s_fy; s_draw_k = 1.0f;
            }
            xbox_fb_hide_cursor = front;
            xbox_mouse_buttons_taken = scr != 6 ? 2 | 4 : 0; s_active = 0;
            {   /* a left click on a film before the title has shown: skipped */
                static int title_seen;
                int clicked = xbox_fb_clicks[0] != s_seen_clicks[0] || (getenv("TT_FILMCLICK") && xbox_MoviePlaying() && GetTickCount() % 4000 < 20);
                if (scr == 4) title_seen = 1;
                if (clicked && !title_seen && scr == 8 && xbox_MoviePlaying() && (bin || getenv("TT_FILMCLICK"))) film_skip();   /* (the intro films only: not the title's) */
            }
            if (s_phase) { s_inj = 0; s_inj_serial++; s_phase = 0; }
            go_clear(); was_down = was_rdown = 0; lclk = lrclk = 0;
            s_seen_clicks[0] = xbox_fb_clicks[0]; s_seen_clicks[1] = xbox_fb_clicks[1];
            if (!want_rel && rel_on) xbox_FramebufferMouseRelease();
            rel_on = want_rel;
            return;
        }
    }
    if (!menu) {
        s_draw = 0;
        xbox_fb_hide_cursor = front;     /* (in play the system's pointer is not shown either) */
        if (s_phase) { s_inj = 0; s_inj_serial++; s_phase = 0; }
        go_clear(); was_down = was_rdown = 0; lclk = lrclk = 0;
        s_seen_clicks[0] = xbox_fb_clicks[0]; s_seen_clicks[1] = xbox_fb_clicks[1];
        s_tint_el = 0; s_tint2_el = 0;
        if (!want_rel && rel_on) xbox_FramebufferMouseRelease();
        rel_on = want_rel;
        return;
    }
    inside = xbox_FramebufferMouse(&mx, &my);
    btn = xbox_InputMouseButtons();
    mouse_script(&inside, &mx, &my, &btn);
    if (!script && xbox_GfxGet(GFX_MOUSE_SENS) != 2 && m[2] > 0.01f && m[3] > 0.01f) {
        /* a speed of its own: the pointer moves by the mouse's movement, scaled */
        int dx, dy, w, h;
        if (!rel_on) {   /* from where the mouse is */
            vfx = inside ? (mx - m[0]) / m[2] * 640.0f : 320.0f;
            vfy = inside ? (my - m[1]) / m[3] * 480.0f : 240.0f;
        }
        want_rel = 1;
        if (xbox_FramebufferMouseDelta(&dx, &dy, &w, &h) && w > 0 && h > 0 && rel_on) {
            float sc = mouse_sens();
            vfx += dx * 640.0f / (m[2] * w) * sc;
            vfy += dy * 480.0f / (m[3] * h) * sc;
        }
        /* (kept to the picture, or to the window where the picture is wider) */
        {
            float x0 = -m[0] / m[2] * 640.0f, x1 = (1.0f - m[0]) / m[2] * 640.0f;
            float y0 = -m[1] / m[3] * 480.0f, y1 = (1.0f - m[1]) / m[3] * 480.0f;
            if (x0 < 0) x0 = 0;
            if (x1 > 639) x1 = 639;
            if (y0 < 0) y0 = 0;
            if (y1 > 479) y1 = 479;
            if (vfx < x0) vfx = x0;
            if (vfx > x1) vfx = x1;
            if (vfy < y0) vfy = y0;
            if (vfy > y1) vfy = y1;
        }
        mx = m[0] + vfx / 640.0f * m[2];
        my = m[1] + vfy / 480.0f * m[3];
        inside = 1;
    }
    if (!want_rel && rel_on) xbox_FramebufferMouseRelease();
    rel_on = want_rel;
    down = (btn & 1) != 0; rdown = (btn & 2) != 0;
    if (inside && (lmx < 0 || fabsf(mx - lmx) > 0.002f || fabsf(my - lmy) > 0.002f)) moved = 1;
    if (inside) { lmx = mx; lmy = my; }
    if (m[2] > 0.01f && m[3] > 0.01f) {
        s_fx = (mx - m[0]) / m[2] * 640.0f; s_fy = (my - m[1]) / m[3] * 480.0f;
        s_k = m[4] > 0.5f ? m[4] : 1.0f;
        s_gx = 320.0f + (s_fx - 320.0f) * s_k; s_gy = s_fy;
        s_px_frame = s_fx;
    }
    if (s_pad_used) { s_pad_used = 0; if (!s_phase) { s_shown = 0; if (!moved) go_clear(); } }
    if (moved || (down && !was_down) || (rdown && !was_rdown)) s_shown = 1;
    /* clicks are kept until the presses before them are done */
    {   /* (counted by the window, so a click quicker than a frame counts too) */
        int32_t *seen = s_seen_clicks, c0 = xbox_fb_clicks[0], c1 = xbox_fb_clicks[1];
        if (c0 != seen[0] && inside) lclk = 1;
        if (c1 != seen[1] && inside) lrclk = 1;
        if (c0 != seen[0] || c1 != seen[1]) s_shown = 1;
        seen[0] = c0; seen[1] = c1;
    }
    held = down && was_down;
    was_down = down; was_rdown = rdown;
    s_draw = s_shown && inside;
    s_draw_x = s_gx; s_draw_y = s_gy; s_draw_k = s_k;
    xbox_fb_hide_cursor = 1;

    /* a leaf under the pointer lights up as the game lights the chosen one:
     * its word gold, its leaf the lit picture (the game's: the dim one's
     * next, 0x20 on, unless the game has been seen lighting it) */
    {
        static uint32_t seen_dim[2], seen_lit[2], seen_scr = ~0u;
        int lf = s_shown && inside ? leaf_at(s_gx, s_gy) : -1, w;
        Item *lb = lf >= 0 ? leaf_label(lf) : NULL, *ic = lf >= 0 ? leaf_icon(lf) : NULL;
        if (seen_scr != G32(FE_SCREEN)) {   /* a new screen: its pictures are its own */
            seen_scr = G32(FE_SCREEN);
            seen_dim[0] = seen_dim[1] = seen_lit[0] = seen_lit[1] = 0;
        }
        for (w = 0; w < 2; w++) {
            Item *l2 = leaf_label(w), *i2 = leaf_icon(w);
            uint32_t tex = i2 ? G32(i2->a + 0xB8) : 0;
            if (!l2 || !tex || !tex_ok(tex)) continue;
            if (l2->col == LEAF_DIM) { if (seen_dim[w] != tex) seen_lit[w] = 0; seen_dim[w] = tex; }   /* (a new dim one: the lit one is to be seen again) */
            else seen_lit[w] = tex;
        }
        s_tint_col = LEAF_LIT;
        s_tint_el = lb && lb->col == LEAF_DIM ? lb->a : 0;
        s_tint2_el = 0;
        if (lb && ic && lb->col == LEAF_DIM && G32(ic->a + 0xB8) >= 0x10000u && G32(ic->a + 0xB8) < 0xF0000000u) {
            uint32_t dim = G32(ic->a + 0xB8);
            /* a lit picture the game has shown on this screen, or the guess
             * (the dim one's next) on the character select and the upgrades,
             * where it holds; swapped at the draw only while the leaf still
             * shows that dim one - a leaf being torn down after a level showed
             * freed memory, and its "next" crashed the game */
            s_tint2_tex = seen_lit[lf] && seen_dim[lf] == dim ? seen_lit[lf]
                        : charsel_here() || G32(FE_SCREEN) == 11 ? dim + 0x20 : 0;
            /* (and only a picture like the dim one, next to it: a stale one
             * learned on the results screen crashed the upgrades after it) */
            if (s_tint2_tex && (!tex_ok(dim) || !tex_ok(s_tint2_tex) || s_tint2_tex - dim + 0x400u > 0x800u
                                || (G32(s_tint2_tex) & 0xFFFF0000u) != (G32(dim) & 0xFFFF0000u))) s_tint2_tex = 0;
            s_tint2_dim = dim;
            if (s_tint2_tex) s_tint2_el = ic->a;
        }
    }

    if (!press_tick()) return;
    clk = lclk; rclk = lrclk; lclk = lrclk = 0;

    /* right click: back */
    if (rclk) {
        int a = rmb_own();
        go_clear();
        if (a > 0) {   /* the right button's own action here (Help, Options): its button */
            static const uint32_t inj[16] = { 0x10000u, 0x20000u, 0x40000u, 0x80000u, 0x100000u, 0x200000u, 0x400000u, 0x800000u,
                                              0x10, 0x20, 0x1, 0x2, 0x4, 0x8, 0x40, 0x80 };
            press(a - 1 < 16 ? inj[a - 1] : INJ_B);
            if (mouse_log()) tt_dbg("[MOUSE] right button: action %d\n", a - 1);
        } else press(INJ_B);
        return;
    }
    {   /* the mouse wheel in the game's own menus: up / down, a step a notch
         * (our options pages take it themselves, options.c) */
        extern volatile LONG xbox_fb_wheel;
        extern int tt_opt_ours(void);
        static LONG wseen, pend;
        LONG w = xbox_fb_wheel;
        if (tt_opt_ours() || !s_shown || !inside || map_here() || charsel_here() || (find_sel() < 0 && s_nls[s_cur ^ 1] == 0)) pend = 0;   /* (a list's menu only) */
        else pend += w - wseen;
        wseen = w;
        if (pend > 3) pend = 3;
        if (pend < -3) pend = -3;
        if (pend) { go_clear(); press(pend > 0 ? PAD_UP : PAD_DOWN); pend += pend > 0 ? -1 : 1; return; }
    }
    {   /* the picture galleries (the mission select's Concept Art, Production
         * Photos: a screen of their own, one big picture drawn by 0x0007F3D7):
         * a click on the left half is the previous picture, on the right half
         * the next */
        int i, gal = 0;
        for (i = 0; i < s_nit; i++)
            if (s_it[i].ret == 0x0007F3D7u && !s_it[i].text && s_it[i].on && s_it[i].w >= 600 && s_it[i].h >= 400) gal = 1;
        if (gal) {
            if (clk && s_shown && inside && !(s_gy >= 425 && s_gx >= 300 && s_gx < 450)) {
                go_clear();
                press(s_gx < 320.0f ? PAD_LEFT : PAD_RIGHT);
                if (mouse_log()) tt_dbg("[MOUSE] gallery: %s\n", s_gx < 320.0f ? "previous" : "next");
            }
            return;
        }
    }
    {   /* our options pages: their own pointer handling (options.c) */
        extern int tt_opt_mouse(float x, float y, int moved, int clk, int down);
        if (tt_opt_mouse(s_gx, s_gy, moved && s_shown && inside, clk && s_shown && inside, down && inside)) { go_clear(); return; }
    }

    {
        Item *tabs[8], *mk = upg_mark();
        int ntab = upg_tabs(tabs, 8), upg = ntab >= 2 && mk != NULL;
        int lf = (moved || clk) && s_shown ? leaf_at(s_gx, s_gy) : -1;

        /* the leaves: on the character select (where left / right turn the
         * ring) a click is B or A at once; on the upgrade screens the game's
         * own choice goes to the leaf (left / right from the list), then A;
         * elsewhere a click is B (Back) or A (Continue) */
        if (lf >= 0 && clk) {
            go_clear();
            /* (the results screen after a level: Replay is the left leaf, the
             * game's choice goes there with left, then A -- B is not Replay) */
            if (upg || (G32(FE_SCREEN) == 10 && lf == 0 && !leaf_focused(0))) { s_go.kind = GO_LEAF; s_go.node = lf; s_go.then = 'A'; }
            else if (G32(FE_SCREEN) == 10 && lf == 0) { press(INJ_A); return; }
            else { press(lf ? INJ_A : INJ_B); if (mouse_log()) tt_dbg("[MOUSE] leaf %s\n", lf ? "Continue" : "Back"); return; }
        } else if (lf >= 0 && upg && !(s_go.kind == GO_LEAF && s_go.node == lf)) {
            go_clear(); s_go.kind = GO_LEAF; s_go.node = lf;
        }
        if (s_go.kind == GO_LEAF) {
            int w = s_go.node;
            if (!leaf_label(w)) go_clear();
            else if (leaf_focused(w)) {
                if (s_go.then == 'A') { press(INJ_A); if (mouse_log()) tt_dbg("[MOUSE] A on the leaf %d\n", w); }
                go_clear();
            } else if (mk && mk->on && s_go.steps < 6) { s_go.steps++; press(PAD_DOWN); }   /* off the level leaves first */
            else if (++s_go.steps <= 4) { press(w ? PAD_RIGHT : PAD_LEFT); if (mouse_log()) tt_dbg("[MOUSE] to the leaf %d\n", w); }
            else {
                if (s_go.then == 'A') press(w ? PAD_START : INJ_B);   /* (the game would not go there) */
                go_clear();
            }
            return;
        }
        /* the upgrade screens' level leaves: up to them, then left / right */
        if (upg) {
            int at = -1, i;
            if ((moved || clk) && s_shown) {
                float bd = 1e9f;   /* (they overlap: the one whose middle is nearest) */
                for (i = 0; i < ntab; i++)
                    if (hit(tabs[i], s_gx, s_gy) && s_gy < tabs[i]->y + 48) {
                        float d = fabsf(pointer_x_for(icx(tabs[i]), icy(tabs[i])) - icx(tabs[i]));
                        if (d < bd) { bd = d; at = i; }
                    }
            }
            if (at >= 0 && !(s_go.kind == GO_TAB && s_go.node == at)) { go_clear(); s_go.kind = GO_TAB; s_go.node = at; }
            if (s_go.kind == GO_TAB) {
                int cur = tab_cur(tabs, ntab, mk), k, lfoc = 0;
                for (k = 0; k < s_nls[s_cur ^ 1]; k++) {   /* (the list or a leaf chosen: up to the level leaves first) */
                    uint32_t o = s_ls[s_cur ^ 1][k].obj;
                    if (G8(o + 0xFC) || G8(o + 0xFD)) lfoc = 1;
                }
                if (leaf_focused(0) || leaf_focused(1)) lfoc = 1;
                if (lfoc && (!mk || !mk->on)) cur = -1;
                if (s_go.node >= ntab) go_clear();
                else if (cur < 0) {
                    if (++s_go.steps > 8) go_clear(); else { press(PAD_UP); if (mouse_log()) tt_dbg("[MOUSE] level leaf %d: up to them\n", s_go.node); }
                } else if (cur == s_go.node) go_clear();
                else {
                    if (s_go.last_dir && (int)s_go.last_sel == cur && ++s_go.fails >= 3) go_clear();
                    else if (++s_go.steps > 16) go_clear();
                    else {
                        s_go.last_sel = (uint32_t)cur; s_go.last_dir = 1;
                        press(tabs[s_go.node]->x < tabs[cur]->x ? PAD_LEFT : PAD_RIGHT);
                        if (mouse_log()) tt_dbg("[MOUSE] level leaf %d -> %d\n", cur, s_go.node);
                    }
                }
                return;
            }
        }

    if (s_nls[s_cur ^ 1] > 0) {
        /* a list of the game's own (over everything else: a dialog) */
        uint32_t obj = 0;
        int at = (moved || clk) && s_shown ? list_at(s_gx, s_gy, &obj) : -1;
        if (at >= 0) {
            if (s_go.kind != GO_LIST || s_go.item != obj || s_go.node != at) { go_clear(); s_go.kind = GO_LIST; s_go.item = obj; s_go.node = at; }
            if (clk) s_go.then = 'A';
        }
        if (s_go.kind == GO_LIST) {
            int cur = (int)G32(s_go.item + 0xF0), n = (int)G32(s_go.item + 0xF8);
            int focused = G8(s_go.item + 0xFC) || G8(s_go.item + 0xFD);
            if (s_go.node < 0 || s_go.node >= n || !G8(s_go.item + 0xC0 + s_go.node)) go_clear();
            else if (!focused && upg && list_rowh(s_go.item) == 0x19 && n > 3) {
                /* (only the upgrades' own list -- not a dialog over the screen,
                 * "Are you finished purchasing upgrades?", whose Yes / No the
                 * up presses turned over and over) */
                /* the list is not the one chosen (a level leaf or a leaf is): back to it */
                uint32_t d = mk && mk->on ? PAD_DOWN : leaf_focused(0) || leaf_focused(1) ? PAD_UP : PAD_DOWN;   /* (up from a leaf) */
                if (++s_go.steps > 6) go_clear();
                else press(d);
                if (mouse_log()) tt_dbg("[MOUSE] back to the list: %s (list %08X n %d rowh %d shown %d, %d lists)\n", d == PAD_DOWN ? "down" : "up", s_go.item, n, list_rowh(s_go.item), list_shown(s_go.item), s_nls[s_cur ^ 1]);
            } else if (cur == s_go.node) {
                if (s_go.then == 'A') { press(INJ_A); if (mouse_log()) tt_dbg("[MOUSE] A on list item %d\n", cur); }
                go_clear();
            } else {
                if (s_go.steps > 0 && (int)s_go.last_sel == cur && GetTickCount() - s_go.step_t < 250) return;   /* (the last step not shown yet) */
                if (s_go.steps > 0 && (int)s_go.last_sel == cur && ++s_go.fails >= 3) go_clear();
                else if (++s_go.steps > 40) go_clear();
                else {
                    s_go.last_sel = (uint32_t)cur; s_go.step_t = GetTickCount();
                    press(s_go.node > cur ? PAD_DOWN : PAD_UP);
                    if (mouse_log()) tt_dbg("[MOUSE] list %08X: %d -> %d\n", s_go.item, cur, s_go.node);
                }
            }
        }
        return;
    }
    }
    if (map_here()) {
        /* the mission select: the place pointed at is chosen at once (as the
         * game's own move does: the place, its highlight, the move's sound),
         * any place the game's paths reach, wherever it is on the map */
        int cur = map_cur(), at = (moved || clk) && s_shown ? map_node_at(s_gx, s_gy) : -1;
        if (at >= 0 && cur >= 0 && at != cur && map_route(cur, at) >= 0) {
            map_jump(at);
            if (mouse_log()) tt_dbg("[MOUSE] map %d -> %d (at %.0f,%.0f)\n", cur, at, s_gx, s_gy);
            if (clk) { s_go.kind = GO_NODE; s_go.node = at; s_go.then = 'A'; }   /* A once it is there */
        } else if (at >= 0 && at == cur && clk) {
            press(INJ_A);
            if (mouse_log()) tt_dbg("[MOUSE] map: A on %d\n", cur);
        } else if (s_go.kind == GO_NODE && s_go.then == 'A') {
            if (cur == s_go.node) press(INJ_A);
            go_clear();
        }
        return;
    }
    if (G32(FE_SCREEN) == MAP_SCREEN && !charsel_here() && s_nls[s_cur ^ 1] == 0 && find_sel() < 0 && !tt_opt_up()) {
        /* the mission select's pictures (production photos, concept art):
         * a click on the left half is the previous picture, on the right
         * half the next */
        if (clk) {
            press(s_gx < 320.0f ? PAD_LEFT : PAD_RIGHT);
            if (mouse_log()) tt_dbg("[MOUSE] pictures: %s\n", s_gx < 320.0f ? "left" : "right");
        }
        return;
    }
    if (charsel_here()) {
        /* the character select: a click left or right of the one in front
         * turns the ring that way; on him, A */
        /* clicks while the ring is still turning are kept and done in turn:
         * a step is pressed again if the name at the top has not changed */
        static int queue, tries, wait, lastdir;
        static uint32_t name_at_press;
        uint32_t name = 0;
        int i;
        for (i = 0; i < s_nit; i++) if (s_it[i].ret == 0x00076F37u && s_it[i].text && s_it[i].font == 2) name = s_it[i].txt ^ (uint32_t)G16(s_it[i].txt) ^ ((uint32_t)G16(s_it[i].txt + 2) << 16);
        {
            extern int tt_select_place(void);
            name ^= (uint32_t)(tt_select_place() + 1) << 24;
        }
        if (clk) {
            /* Back and Continue at the bottom corners; above them, the one in
             * front (a little left of the middle) and those either side */
            uint32_t what;
            /* (the leaves are seen to before: here, the characters) */
            if (s_gy >= 425 && s_gx >= 280 && s_gx < 430) what = 0;      /* "Help" */
            else what = s_gx < 225 ? PAD_LEFT : s_gx > 395 ? PAD_RIGHT : INJ_A;
            if (what == PAD_LEFT) queue = (queue > 0 ? 0 : queue) - 1;
            else if (what == PAD_RIGHT) queue = (queue < 0 ? 0 : queue) + 1;
            else if (what && !queue && !wait) press(what);
            if (mouse_log()) tt_dbg("[MOUSE] select: %s (queue %d)\n", what == PAD_LEFT ? "left" : what == PAD_RIGHT ? "right" : what == INJ_B ? "B" : what ? "A" : "-", queue);
        }
        if (wait) {
            if (name != name_at_press) { wait = 0; tries = 0; }
            else if (--wait == 0 && ++tries < 3) { queue += lastdir; if (mouse_log()) tt_dbg("[MOUSE] select: not taken, again (%08X)\n", name); }   /* not taken: again */
            else if (!wait) tries = 0;
        }
        if (!wait && queue && !s_phase) {
            press_n(queue > 0 ? PAD_RIGHT : PAD_LEFT, 1);   /* (held longer, the ring turns on) */
            lastdir = queue > 0 ? 1 : -1;
            queue -= lastdir;
            name_at_press = name; wait = 45;
            if (mouse_log()) tt_dbg("[MOUSE] select: step %d (%08X)\n", lastdir, name);
        }
        return;
    }

    /* the lists */
    {
        int sel = find_sel(), row = 0, at;
        int click = clk;
        at = (moved || click) && s_shown ? target_at(s_gx, s_gy, &row) : -1;
        if (at >= 0) {
            if (s_go.kind != GO_ITEM || s_go.item != s_it[at].a) {
                go_clear(); s_go.kind = GO_ITEM; s_go.item = s_it[at].a;
            }
            s_go.tx = row ? s_gx : icx(&s_it[at]); s_go.ty = icy(&s_it[at]);
            if (click) s_go.then = row ? 'V' : 'A';
            if (click && row) s_go.tx = s_gx;
        }
        /* dragging a slider */
        if (held && s_go.kind == GO_ITEM && s_go.then == 'V' && inside) s_go.tx = s_gx;
        if (s_go.kind == GO_ITEM && sel >= 0) {
            Item *s = &s_it[sel];
            int ti = item_index(s_go.item);
            if (ti < 0) { go_clear(); goto done; }
            {
                Item *t = &s_it[ti];
                int there = s->a == t->a || (s->x == t->x && fabsf(icy(s) - icy(t)) <= s->h * 0.5f);
                /* (a step pressed: the game shows the new choice a frame or two
                 * later -- until it does, or a quarter second, no next step;
                 * at a high frame rate the old choice was still seen and the
                 * pointer pressed again, back and forth between two items) */
                if (s_go.steps > 0 && s_go.last_sel == s->a && !there && GetTickCount() - s_go.step_t < 250) goto done;
                if (s_go.steps > 0 && s_go.last_sel == s->a && !there && s_go.then != 'V') {
                    if (++s_go.fails >= 3) { go_clear(); goto done; }   /* the selection does not move there */
                }
                if (!there) {
                    uint32_t d = dir_to(s, icx(t), icy(t));
                    if (++s_go.steps > 24) { go_clear(); goto done; }
                    s_go.last_sel = s->a; s_go.step_t = GetTickCount();
                    press(d);
                    if (mouse_log()) tt_dbg("[MOUSE] list: %08X -> %08X, %s\n", s->a, t->a,
                                            d == PAD_UP ? "up" : d == PAD_DOWN ? "down" : d == PAD_LEFT ? "left" : "right");
                } else if (s_go.then == 'A') {
                    /* (a setting's name: chosen, nothing pressed -- A could leave the page) */
                    int vals[8], cur;
                    float b0, b1;
                    if (row_values(t, vals, 8, &cur) > 0 || row_knob(t, &b0, &b1) >= 0) { go_clear(); goto done; }
                    press(INJ_A);
                    if (mouse_log()) tt_dbg("[MOUSE] A on %08X\n", t->a);
                    go_clear();
                } else if (s_go.then == 'V') {
                    /* a value: left/right until it is the set one; a slider: until the knob is there */
                    int vals[8], cur, nv = row_values(s, vals, 8, &cur), want = -1, k;
                    for (k = 0; k < nv; k++) if (s_go.tx >= s_it[vals[k]].x && s_go.tx < s_it[vals[k]].x + s_it[vals[k]].w) want = k;
                    if (want >= 0 && s_it[vals[want]].txt >= 0x10000u && G16(s_it[vals[want]].txt + 2) == 0
                        && (G16(s_it[vals[want]].txt) == '<' || G16(s_it[vals[want]].txt) == '>')) {
                        /* an arrow (a setting that goes round its choices): one step */
                        press(G16(s_it[vals[want]].txt) == '<' ? PAD_LEFT : PAD_RIGHT);
                        go_clear();
                        goto done;
                    }
                    if (nv > 0 && want >= 0 && cur >= 0) {
                        if (cur != want && s_go.steps > 0 && cur == s_go.last_cur && GetTickCount() - s_go.step_t < 250) goto done;   /* (as above) */
                        if (cur == want || ++s_go.steps > 24) go_clear();
                        else {
                            s_go.last_cur = cur; s_go.step_t = GetTickCount();
                            press(want > cur ? PAD_RIGHT : PAD_LEFT);
                            if (mouse_log()) tt_dbg("[MOUSE] value %d -> %d\n", cur, want);
                        }
                    } else {
                        float b0, b1, kx = row_knob(s, &b0, &b1);
                        if (kx < 0 || ++s_go.steps > 60) { if (!(down)) go_clear(); }
                        else {
                            float step = s_go.knob_last > 0 ? fabsf(kx - s_go.knob_last) : 0;
                            float tol = step > 2 ? step * 0.5f : 6;
                            if (s_go.steps > 2 && s_go.knob_last > 0 && fabsf(kx - s_go.knob_last) < 0.5f && s_go.last_dir) {
                                /* at an end: no further */
                                if (!down) go_clear(); else s_go.steps--;
                            } else if (fabsf(kx - s_go.tx) <= tol) { if (!down) go_clear(); else s_go.steps--; }
                            else {
                                s_go.last_dir = kx < s_go.tx ? 1 : -1;
                                press(kx < s_go.tx ? PAD_RIGHT : PAD_LEFT);
                                if (mouse_log()) tt_dbg("[MOUSE] slider %.0f -> %.0f\n", kx, s_go.tx);
                            }
                            s_go.knob_last = kx;
                        }
                    }
                } else go_clear();
            }
        }
    }
done:
    ;
}

/* once a frame (twotowers_manual.c: the game's wait for the next frame) */
void tt_menus_tick(void)
{
    {
        extern void tt_dlss5_notify_movie(int movie);
        tt_dlss5_notify_movie(xbox_MoviePlaying() ? 1 : ((G32(FE_SCREEN) != 6 && G32(FE_SCREEN) != 8) || tt_opt_up()) ? 2 : 0);
    }
    {
        extern int tt_lang_srt_ready(void);
        extern void tt_lang_srt_tick(uint32_t bink);
        extern unsigned tt_lang_srt_generation(void);
        static uint32_t movie;
        static unsigned generation;
        if (generation != tt_lang_srt_generation()) { generation = tt_lang_srt_generation(); movie = 0; }
        if (tt_lang_srt_ready() && xbox_MoviePlaying()) {
            if (!movie) movie = bink_find();
            tt_lang_srt_tick(movie);
        } else { movie = 0; tt_lang_srt_tick(0); }
    }
    {   /* testing: TT_MMSEL -- the main menu's choice as it changes */
        static int on = -1; static uint32_t last = 0xFFFFFFFFu;
        if (on < 0) on = getenv("TT_MMSEL") != NULL;
        if (on && G32(FE_SCREEN) == 4 && G32(MM_OBJ + 0x14) != last) { last = G32(MM_OBJ + 0x14); tt_dbg("[MMSEL] %u at %lu\n", last, (unsigned long)GetTickCount()); }
    }
    if (!xbox_pad_filter) xbox_pad_filter = pad_filter;
    el_log();
    el_flip();
    pointer_frame();
    {
        extern void tt_opt_tick(void);
        tt_opt_tick();
    }
}
