/* options.c -- the Options menu's own pages: General (the game's), Display
 * and Controls (the F10 settings), chosen first on a page of their own.
 *
 * The Options screen (front-end screen 5, and the pause screen's Options)
 * runs a loop of its own (0x0007AAF0) that draws its page with 0x0007A4D0:
 * the background (element 0x4338B8), the rows' names (font 1, the chosen one
 * 0xFF77EAF8, the others 0xFF9C978C), the values (font 1, three columns at x
 * 191, 309, 435), the markers (two layers each, 64x64: the set value's
 * flower, or on the chosen row the ring -- textures background+0xA0/+0x80
 * and +0x60/+0x40) and the volume sliders (402x32 bars, a marker on them).
 * Our pages are drawn with copies of those elements in place of the game's
 * (the wrapper of 0x0007A4D0, twotowers_manual.c), and while one is up the
 * game's page sees a controller at rest (tt_opt_filter): the presses are
 * ours. The mouse pointer (menus.c) works on them as on the game's: it
 * presses the d-pad and A for us.
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern ptrdiff_t xbox_GetMemoryOffset(void);
#define G8(a)  (*(volatile uint8_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define G16(a) (*(volatile uint16_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
#define G32(a) (*(volatile uint32_t *)(xbox_GetMemoryOffset() + (uint32_t)(a)))
extern void tt_dbg(const char *fmt, ...);
extern uint32_t tt_gcall(uint32_t va, uint32_t thisptr, int n, const uint32_t *args);
extern uint32_t xbox_ContiguousAlloc(uint32_t size, uint32_t alignment);
extern int xbox_GfxItemCount(int id);
extern int xbox_GfxIndex(int id);
extern void xbox_GfxSetIndex(int id, int v);

/* gfx_menu.h order */
enum { GFX_FPS, GFX_FULLSCREEN, GFX_ASPECT, GFX_SCALE, GFX_ANISO, GFX_MIPS, GFX_AA,
       GFX_VSYNC, GFX_STRETCH, GFX_VP, GFX_BRIGHT, GFX_SHARP, GFX_COLOR,
       GFX_PROMPTS, GFX_HUDPOS, GFX_MENUFILL, GFX_MOVIEFILL, GFX_MOUSE, GFX_MOUSE_SENS, GFX_CURSOR_SIZE,
       GFX_MOUSE_AIM, GFX_MOUSE_INVERT, GFX_SPRINT, GFX_MOUSE_STEER, GFX_WINSIZE, GFX_ULTRA, GFX_WIDE_SCENES };

/* the game's Options page elements we copy */
#define EL_SIZE   0xDCu
#define T_BG      0x4338B8u
#define T_LABEL   0x434FECu
#define T_VALUE   0x435514u
#define T_MARK1   0x433EBCu
#define T_MARK2   0x433F98u
#define T_BAR     0x433B4Cu
#define T_HELPI   0x435DACu
#define T_HELPT   0x435E88u
#define COL_ON    0xFF77EAF8u
#define COL_OFF   0xFF9C978Cu

enum { PG_HUB, PG_GENERAL, PG_DISPLAY, PG_CONTROLS };
enum { K_COLS, K_CYCLE, K_SLIDER, K_BIND, K_RESET };   /* (K_BIND: gfx is the action, input_bind.h) */
typedef struct {
    const char *label;
    int gfx, kind;
    const char *names[16];    /* short names for the choices (columns, cycling) */
} Row;
static const Row s_display[] = {
    { "Frame Rate:", GFX_FPS, K_COLS, { "30", "60", "Unlocked" } },
    { "Display Mode:", GFX_FULLSCREEN, K_COLS, { "Window", "Full Screen" } },
    { "Window Size:", GFX_WINSIZE, K_CYCLE, { "Automatic", "1280x720", "1280x960", "1600x900", "1920x1080", "1920x1200",
                                              "2560x1080", "2560x1440", "3440x1440", "3840x2160", "5120x1440" } },
    { "Aspect Ratio:", GFX_ASPECT, K_CYCLE, { "4:3", "16:9", "16:10", "21:9", "32:9", "Custom" } },
    { "Resolution:", GFX_SCALE, K_CYCLE, { "1x", "2x", "3x", "4x", "2160p", "5x", "6x", "7x", "8x", "9x", "10x", "11x", "12x" } },
    { "Anisotropic:", GFX_ANISO, K_SLIDER, { 0 } },
    { "Mipmaps:", GFX_MIPS, K_COLS, { "Off", "On" } },
    { "Ultra Textures:", GFX_ULTRA, K_COLS, { "Off", "On" } },
    { "Wide Cutscenes:", GFX_WIDE_SCENES, K_COLS, { "Off", "On" } },
    { "Anti-aliasing:", GFX_AA, K_CYCLE, { "Off", "FXAA", "MSAA 2x", "MSAA 4x", "MSAA 8x", "MSAA 4x+FXAA", "MSAA 8x+FXAA" } },
    { "VSync:", GFX_VSYNC, K_COLS, { "Off", "On" } },
    { "Picture:", GFX_STRETCH, K_COLS, { "Keep Shape", "Stretch" } },
    { "Vertex Programs:", GFX_VP, K_COLS, { "CPU", "GPU" } },
    { "Brightness:", GFX_BRIGHT, K_SLIDER, { 0 } },
    { "Sharpening:", GFX_SHARP, K_SLIDER, { 0 } },
    { "Colour:", GFX_COLOR, K_COLS, { "Original", "Vivid", "Muted" } },
    { "HUD Widescreen:", GFX_HUDPOS, K_COLS, { "Edges", "Centred" } },
    { "Menus:", GFX_MENUFILL, K_COLS, { "Fill", "Original" } },
    { "Movies:", GFX_MOVIEFILL, K_COLS, { "As Made", "Fill" } },
};
static const Row s_controls[] = {
    { "Button Prompts:", GFX_PROMPTS, K_CYCLE, { "Automatic", "Xbox", "Xbox One", "PlayStation", "Keyboard" } },
    { "Mouse Pointer:", GFX_MOUSE, K_COLS, { "On", "Off" } },
    { "Sensitivity:", GFX_MOUSE_SENS, K_SLIDER, { 0 } },
    { "Pointer Size:", GFX_CURSOR_SIZE, K_SLIDER, { 0 } },
    { "Mouse Aiming:", GFX_MOUSE_AIM, K_COLS, { "On", "Off" } },
    { "Invert Aiming:", GFX_MOUSE_INVERT, K_CYCLE, { "Off", "Up/Down", "Left/Right", "Both" } },
    { "Mouse Steering:", GFX_MOUSE_STEER, K_COLS, { "Off", "On" } },
};
/* the key bindings, after the settings on the Controls page (input_bind.h's
 * actions, in its order) */
#define BIND_N_ 25
static const char *const s_bind_label[BIND_N_] = {
    "A Button:", "B Button:", "X Button:", "Y Button:", "Black Button:", "White Button:",
    "Left Trigger:", "Right Trigger:", "Start:", "Back:", "D-pad Up:", "D-pad Down:", "D-pad Left:", "D-pad Right:",
    "L Stick Click:", "R Stick Click:", "Move Forward:", "Move Back:", "Move Left:", "Move Right:",
    "Look Up:", "Look Down:", "Look Left:", "Look Right:", "Walk:"
};
extern int  xbox_BindPad(int action);
extern int  xbox_BindIsButton(int action);
extern const char *xbox_BindPadName(int src);
extern void xbox_BindFormatKeys(int action, char *out, int n);
extern void xbox_BindAssignKey(int action, int vk);
extern void xbox_BindClearKeys(int action);
extern void xbox_BindAssignPad(int action, int src);
extern void xbox_BindResetAll(void);
extern const char *xbox_BindKeyName(int vk);
extern int  xbox_InputKeyDown(int vk);
extern volatile LONG xbox_fb_wheel;
#define BIND_PAD_N_ 17
static Row s_ctl_all[8 + BIND_N_ + 1];
static const char *s_hub[3] = { "General Options", "Display Options", "Controls" };
#define NROWS(a) ((int)(sizeof a / sizeof a[0]))
static int s_nctl;

static int s_page = PG_HUB, s_sel, s_hub_sel, s_top;
static DWORD s_drawn;            /* the Options page last drawn (ticks) */
static uint32_t s_mem;           /* our elements and words, in the game's memory */
#define MEM_SIZE 0x10000u
#define MAX_EL   160
static int s_nel;
static uint32_t s_txt;           /* next free word */

/* (signed: the stamp is the tick | 1, a millisecond ahead of the clock while
 * it reads the same -- 15 ms at a time on Windows -- and unsigned that read
 * as four billion ms ago: the page "closed" and reset to General Options
 * many times a second, and the pad went to the game's page) */
static int since_drawn(void) { return (int)(GetTickCount() - s_drawn); }
static int opt_up(void) { return s_drawn && since_drawn() < 400; }
int tt_opt_up(void) { return opt_up(); }
static int ours(void) { return opt_up() && s_page != PG_GENERAL; }
int tt_opt_ours(void) { return ours(); }

extern int tt_tr16(const char *en, uint16_t *out, int max);
static uint32_t word(const char *t)
{
    uint32_t a = s_txt;
    uint16_t u[256];
    int i, n = tt_tr16(t, u, 256);   /* in the chosen language (lang.c) */
    for (i = 0; i < n; i++) G16(a + 2 * i) = u[i];
    G16(a + 2 * i) = 0;
    s_txt += (uint32_t)(2 * i + 2 + 2) & ~3u;
    return a;
}
static uint32_t el_from(uint32_t tmpl)
{
    uint32_t e, i;
    if (s_nel >= MAX_EL) return 0;
    e = s_mem + (uint32_t)s_nel++ * EL_SIZE;
    for (i = 0; i < EL_SIZE; i += 4) G32(e + i) = G32(tmpl + i);
    G8(e + 0x24) = 1;
    return e;
}
static void draw(uint32_t e) { if (e) tt_gcall(0x000C8940u, e, 0, NULL); }
static void text_el(uint32_t tmpl, int x, int y, int w, uint32_t col, const char *t)
{
    uint32_t e = el_from(tmpl);
    if (!e) return;
    G32(e + 8) = (uint32_t)x; G32(e + 0xC) = (uint32_t)y;
    if (w > 0) G32(e + 0x10) = (uint32_t)w;
    G32(e + 0x20) = col;
    G32(e + 0x34) = word(t);
    draw(e);
}
/* a marker (flower, or the ring on the chosen row) with its middle at (cx, cy) */
static void marker(int cx, int cy, int ring)
{
    uint32_t base = G32(T_BG + 0xB8), e1 = el_from(T_MARK1), e2 = el_from(T_MARK2);
    if (!e1 || !e2 || base < 0x10000u) return;
    G32(e1 + 8) = G32(e2 + 8) = (uint32_t)(cx - 32);
    G32(e1 + 0xC) = G32(e2 + 0xC) = (uint32_t)(cy - 32);
    G32(e1 + 0xB8) = base + (ring ? 0x60u : 0xA0u);
    G32(e2 + 0xB8) = base + (ring ? 0x40u : 0x80u);
    draw(e1); draw(e2);
}

static const Row *rows(int *n)
{
    if (s_page == PG_DISPLAY) { *n = NROWS(s_display); return s_display; }
    if (s_page == PG_CONTROLS) {
        if (!s_nctl) {   /* the settings, then a row per action, then Reset */
            int i;
            for (i = 0; i < NROWS(s_controls) && i < 8; i++) s_ctl_all[s_nctl++] = s_controls[i];
            for (i = 0; i < BIND_N_; i++) { Row w = { s_bind_label[i], i, K_BIND, { 0 } }; s_ctl_all[s_nctl++] = w; }
            { Row w = { "Reset Controls", 0, K_RESET, { 0 } }; s_ctl_all[s_nctl++] = w; }
        }
        *n = s_nctl; return s_ctl_all;
    }
    *n = 0;
    return NULL;
}
static volatile int s_cap;          /* a key being taken for s_sel's action: 1 waiting, 2 waiting for every key to be let go */
static unsigned char s_cap_held[256];
#define ROW_Y0   28
#define ROW_STEP 52
#define ROW_SHOWN 7
static const int s_colx[3] = { 222, 340, 466 };   /* (the game's 191, 309, 435, moved over for longer names) */

static void draw_page(void)
{
    int n, i;
    const Row *r = rows(&n);
    s_nel = 0;
    s_txt = s_mem + MAX_EL * EL_SIZE;
    draw(T_BG);
    if (s_page == PG_HUB) {
        for (i = 0; i < 3; i++)
            text_el(T_LABEL, 200, 150 + i * 60, 300, i == s_hub_sel ? COL_ON : COL_OFF, s_hub[i]);
    } else if (r) {
        if (s_sel < s_top) s_top = s_sel;
        if (s_sel >= s_top + ROW_SHOWN) s_top = s_sel - ROW_SHOWN + 1;
        for (i = s_top; i < n && i < s_top + ROW_SHOWN; i++) {
            const Row *w = &r[i];
            int y = ROW_Y0 + (i - s_top) * ROW_STEP, sel = i == s_sel, k;
            int cnt = w->kind >= K_BIND ? 0 : xbox_GfxItemCount(w->gfx), cur = w->kind >= K_BIND ? 0 : xbox_GfxIndex(w->gfx);
            text_el(T_LABEL, 24, y, 0, sel ? COL_ON : COL_OFF, w->label);
            if (w->kind == K_BIND) {
                /* its keys, and (a button) the controller's button for it */
                char v[96];
                if (sel && s_cap == 1) snprintf(v, sizeof v, "Press a key...  (Esc: cancel, Delete: clear)");
                else xbox_BindFormatKeys(w->gfx, v, sizeof v);
                text_el(T_VALUE, 222, y, sel && s_cap == 1 ? 400 : 260, sel && s_cap == 1 ? COL_ON : COL_OFF, v);
                if (!(sel && s_cap == 1)) {
                    if (xbox_BindIsButton(w->gfx)) {
                        snprintf(v, sizeof v, sel ? "< %s >" : "%s", xbox_BindPadName(xbox_BindPad(w->gfx)));
                        text_el(T_VALUE, 490, y, 140, sel ? COL_ON : COL_OFF, v);
                    } else if (w->gfx >= 20 && w->gfx <= 23) text_el(T_VALUE, 490, y, 140, COL_OFF, "R Stick");
                    else if (w->gfx == 24) text_el(T_VALUE, 490, y, 140, COL_OFF, "Stick");
                    else text_el(T_VALUE, 490, y, 140, COL_OFF, "L Stick");
                }
            } else if (w->kind == K_RESET) {
                if (sel) marker(250, y + 17, 1);
            } else if (w->kind == K_COLS) {
                for (k = 0; k < cnt && k < 3; k++) {
                    if (k == cur) marker(s_colx[k] + 64, y + 17, sel);
                    text_el(T_VALUE, s_colx[k], y, 0, COL_OFF, w->names[k] ? w->names[k] : "");
                }
            } else if (w->kind == K_CYCLE) {
                /* the choice in the middle, arrows either side */
                if (cur >= 0 && cur < 12) {
                    marker(s_colx[1] + 64, y + 17, sel);
                    text_el(T_VALUE, s_colx[1], y, 0, COL_OFF, w->names[cur] ? w->names[cur] : "?");
                }
                text_el(T_VALUE, s_colx[0], y, 0, COL_OFF, "<");
                text_el(T_VALUE, s_colx[2], y, 0, COL_OFF, ">");
            } else {
                uint32_t b = el_from(T_BAR);
                if (b) { G32(b + 8) = 230; G32(b + 0xC) = (uint32_t)y; draw(b); }
                if (cnt > 1) marker(262 + (317 * cur) / (cnt - 1), y + 25, sel);
            }
        }
        if (s_top > 0) text_el(T_VALUE, 560, 4, 0, COL_OFF, "...");
        if (s_top + ROW_SHOWN < n) text_el(T_VALUE, 560, ROW_Y0 + ROW_SHOWN * ROW_STEP - 10, 0, COL_OFF, "...");
    }
    draw(T_HELPI);
    draw(T_HELPT);
}

/* the Options page's drawing (0x0007A4D0): 1 = ours drawn in its place */
int tt_opt_draw(uint32_t obj)
{
    (void)obj;
    if (!s_drawn || since_drawn() > 400) { s_page = PG_HUB; s_hub_sel = 0; }   /* Options just opened: the first page */
    s_drawn = GetTickCount() | 1;
    if (getenv("TT_NOOPTPAGES")) s_page = PG_GENERAL;
    if (s_page == PG_GENERAL) return 0;
    if (!s_mem) {
        s_mem = xbox_ContiguousAlloc(MEM_SIZE, 4096);
        if (!s_mem) { s_page = PG_GENERAL; return 0; }
    }
    draw_page();
    return 1;
}

/* ---- the controller: ours while our page is up ---- */
typedef struct { uint16_t wButtons; uint8_t bAnalogButtons[8]; int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XPad;
typedef struct { uint32_t dwPacketNumber; XPad Gamepad; } XState;
enum { B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_A = 16, B_B = 32, B_START = 64 };
static volatile uint32_t s_held_acc;   /* what was held since the last frame (the input thread) */
static volatile int s_b_block;         /* B took us back to the first page: the game does not see it until let go */
void tt_opt_filter(unsigned long port, XState *st)
{
    XPad *p = &st->Gamepad;
    uint32_t h = 0;
    (void)port;
    if (!opt_up()) return;
    if (p->wButtons & 1) h |= B_UP;
    if (p->wButtons & 2) h |= B_DOWN;
    if (p->wButtons & 4) h |= B_LEFT;
    if (p->wButtons & 8) h |= B_RIGHT;
    if (p->sThumbLY > 16000) h |= B_UP;
    if (p->sThumbLY < -16000) h |= B_DOWN;
    if (p->sThumbLX < -16000) h |= B_LEFT;
    if (p->sThumbLX > 16000) h |= B_RIGHT;
    if (p->bAnalogButtons[0] > 30) h |= B_A;
    if (p->bAnalogButtons[1] > 30) h |= B_B;
    if (p->wButtons & 0x20) h |= B_B;   /* Back (Tab, Backspace by default): out of the Options pages, as B */
    if (p->wButtons & 0x10) h |= B_START;
    s_held_acc |= h;
    if (s_b_block) {
        if (h & B_B) p->bAnalogButtons[1] = 0;
        else s_b_block = 0;
    }
    if (s_page == PG_GENERAL) {
        /* the game's page: all but B and Back (back to our first page) */
        p->bAnalogButtons[1] = 0;
        p->wButtons &= (uint16_t)~0x20u;
        return;
    }
    if (s_page == PG_HUB && (h & B_B) && !s_b_block) {   /* B on the first page: the game leaves Options (Back as B) */
        if (p->wButtons & 0x20) { p->wButtons &= (uint16_t)~0x20u; p->bAnalogButtons[1] = 255; }
        return;
    }
    /* our page: the game's sees nothing */
    memset(p, 0, sizeof *p);
}

static void sound(uint32_t id, float pan)
{
    uint32_t a[6];
    float one = 1.0f, zero = 0.0f;
    memcpy(&a[1], &one, 4); memcpy(&a[2], &zero, 4); memcpy(&a[3], &pan, 4); memcpy(&a[4], &one, 4);
    a[0] = id; a[5] = 0;
    tt_gcall(0x000C3640u, 0, 6, a);
}
static void move_sound(void) { sound(0xA9D, -1.0f); sound(0xA9E, 1.0f); }

static void change(const Row *w, int d)
{
    int cnt, cur, v;
    if (w->kind == K_BIND) {   /* left / right: the controller's button for it */
        if (xbox_BindIsButton(w->gfx)) {
            xbox_BindAssignPad(w->gfx, (xbox_BindPad(w->gfx) + d + BIND_PAD_N_) % BIND_PAD_N_);
            move_sound();
        }
        return;
    }
    if (w->kind == K_RESET) return;
    cnt = xbox_GfxItemCount(w->gfx); cur = xbox_GfxIndex(w->gfx); v = cur + d;
    if (cnt <= 0) return;
    if (w->kind == K_CYCLE) v = (v + cnt) % cnt;
    else if (w->kind == K_COLS && cnt > 3) { if (v < 0) v = 0; if (v > 2) v = 2; }
    if (v < 0 || v >= cnt || v == cur) return;
    xbox_GfxSetIndex(w->gfx, v);
    move_sound();
}

/* A on a row: a binding takes the next key, Reset puts them all back */
static void activate(const Row *w)
{
    int vk;
    if (w->kind == K_BIND) {
        for (vk = 1; vk < 256; vk++) s_cap_held[vk] = (unsigned char)xbox_InputKeyDown(vk);   /* (the key or click that started it) */
        s_cap = 1;
        sound(0xA9D, -1.0f);
    } else if (w->kind == K_RESET) {
        xbox_BindResetAll();
        sound(0xA9D, -1.0f);
    } else if (w->kind == K_CYCLE) change(w, 1);
}
/* taking a key: the first key or mouse button to go down (Esc cancels,
 * Delete clears the action's keys); then nothing until every one is let go */
static void capture_poll(void)
{
    int vk, any = 0, n;
    const Row *r = rows(&n);
    for (vk = 1; vk < 256; vk++) {
        int d = xbox_InputKeyDown(vk);
        any |= d;
        if (s_cap == 1 && d && !s_cap_held[vk]) {
            if (vk == VK_F10 || vk == VK_F11 || vk == VK_LWIN || vk == VK_RWIN) continue;
            if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL
                || vk == VK_LMENU || vk == VK_RMENU) continue;   /* (the generic one reports too) */
            if (r && s_sel < n && r[s_sel].kind == K_BIND) {
                if (vk == VK_DELETE) xbox_BindClearKeys(r[s_sel].gfx);
                else if (vk != VK_ESCAPE) xbox_BindAssignKey(r[s_sel].gfx, vk);
                tt_dbg("[OPTIONS] %s %s\n", r[s_sel].label, vk == VK_ESCAPE ? "(cancelled)" : vk == VK_DELETE ? "cleared" : xbox_BindKeyName(vk));
            }
            move_sound();
            s_cap = 2;
        }
        if (s_cap == 1) s_cap_held[vk] = (unsigned char)d;
    }
    if (s_cap == 2 && !any) s_cap = 0;
}

/* a frame (menus.c's tick) */
void tt_opt_tick(void)
{
    static uint32_t prev;
    static DWORD rep_t;
    uint32_t now = s_held_acc, edge, rep = 0;
    s_held_acc = 0;
    if (!opt_up()) { prev = 0; s_cap = 0; return; }
    if (s_cap) { capture_poll(); prev = now; return; }   /* (keys held over it make no press after) */
    {   /* the mouse wheel: the page's rows up or down (on the first page, its choice) */
        static LONG wseen;
        LONG w = xbox_fb_wheel, dw = w - wseen;
        wseen = w;
        if (dw && ours()) {
            int n;
            const Row *r = rows(&n);
            if (s_page == PG_HUB) {
                int v = s_hub_sel - (int)dw;
                if (v < 0) v = 0;
                if (v > 2) v = 2;
                if (v != s_hub_sel) { s_hub_sel = v; move_sound(); }
            } else if (r && n > 0) {
                int t = s_top - (int)dw, mx = n > ROW_SHOWN ? n - ROW_SHOWN : 0;
                if (t < 0) t = 0;
                if (t > mx) t = mx;
                if (t != s_top) {
                    s_top = t;
                    if (s_sel < s_top) s_sel = s_top;
                    if (s_sel > s_top + ROW_SHOWN - 1) s_sel = s_top + ROW_SHOWN - 1;
                    move_sound();
                }
            }
        }
    }
    edge = now & ~prev;
    /* held: again after 0.4 s, then every 0.12 s */
    if ((now & (B_UP | B_DOWN | B_LEFT | B_RIGHT)) && !(edge & (B_UP | B_DOWN | B_LEFT | B_RIGHT))) {
        if (GetTickCount() - rep_t > 400) { rep = now & (B_UP | B_DOWN | B_LEFT | B_RIGHT); rep_t = GetTickCount() - 280; }
    } else rep_t = GetTickCount();
    prev = now;
    edge |= rep;
    if (s_page == PG_GENERAL) {
        if (edge & B_B) { s_page = PG_HUB; s_b_block = 1; move_sound(); }
        return;
    }
    if (s_page == PG_HUB) {
        if (edge & B_UP) { if (s_hub_sel > 0) { s_hub_sel--; move_sound(); } }
        if (edge & B_DOWN) { if (s_hub_sel < 2) { s_hub_sel++; move_sound(); } }
        if (edge & (B_A | B_START)) {
            s_page = s_hub_sel == 0 ? PG_GENERAL : s_hub_sel == 1 ? PG_DISPLAY : PG_CONTROLS;
            s_sel = 0; s_top = 0;
            sound(0xA9D, -1.0f);
        }
        return;
    }
    {
        int n;
        const Row *r = rows(&n);
        if (!r) return;
        if (edge & B_B) { s_page = PG_HUB; s_b_block = 1; move_sound(); return; }
        if (edge & B_UP) { if (s_sel > 0) { s_sel--; move_sound(); } }
        if (edge & B_DOWN) { if (s_sel < n - 1) { s_sel++; move_sound(); } }
        if (edge & B_LEFT) change(&r[s_sel], -1);
        if (edge & B_RIGHT) change(&r[s_sel], 1);
        if (edge & B_A) activate(&r[s_sel]);
    }
}

/* ---- the mouse on our pages (menus.c, each frame the pointer is up):
 * our rows are known here, so the pointer chooses and clicks them directly
 * (menus.c's way for the game's pages -- walking the chosen one there with
 * up / down presses -- went back and forth between two rows at a high frame
 * rate and a click could open a page and leave it at once). (x, y) in the
 * page's 640 x 480; moved: the pointer moved this frame; clk: a click;
 * down: the button held. 1 = ours (menus.c does nothing more). ---- */
int tt_opt_mouse(float x, float y, int moved, int clk, int down)
{
    static int drag = -1;
    int n, i;
    const Row *r;
    if (!ours()) { drag = -1; return 0; }
    if (getenv("TT_MOUSELOG")) {   /* testing */
        static int lp = -1, lh = -1, ls = -1;
        if (lp != s_page || lh != s_hub_sel || ls != s_sel) { tt_dbg("[OPTM] page %d hub %d sel %d at %.0f,%.0f clk %d\n", s_page, s_hub_sel, s_sel, x, y, clk); lp = s_page; lh = s_hub_sel; ls = s_sel; }
    }
    if (s_page == PG_HUB) {
        int hit = -1;
        for (i = 0; i < 3; i++)
            if (x >= 180 && x < 520 && fabsf(y - (150 + i * 60 + 15)) < 30) hit = i;
        if (hit >= 0 && (moved || clk) && hit != s_hub_sel) { s_hub_sel = hit; move_sound(); }
        if (hit >= 0 && clk) {
            s_page = hit == 0 ? PG_GENERAL : hit == 1 ? PG_DISPLAY : PG_CONTROLS;
            s_sel = 0; s_top = 0;
            sound(0xA9D, -1.0f);
        }
        return 1;
    }
    if (s_cap) return 1;   /* taking a key (a click is one too) */
    r = rows(&n);
    if (!r) return 1;
    if (!down) drag = -1;
    if (drag >= 0 && drag < n && r[drag].kind == K_SLIDER) {   /* a knob dragged */
        int cnt = xbox_GfxItemCount(r[drag].gfx), v;
        if (cnt > 1) {
            v = (int)floorf((x - 262.0f) / 317.0f * (cnt - 1) + 0.5f);
            if (v < 0) v = 0;
            if (v > cnt - 1) v = cnt - 1;
            if (v != xbox_GfxIndex(r[drag].gfx)) { xbox_GfxSetIndex(r[drag].gfx, v); move_sound(); }
        }
        return 1;
    }
    /* the "..." above and below: a click scrolls */
    if (clk && y < ROW_Y0 - 4 && s_top > 0) { s_sel = s_top - 1; move_sound(); return 1; }
    if (clk && y > ROW_Y0 + ROW_SHOWN * ROW_STEP - 14 && y < ROW_Y0 + ROW_SHOWN * ROW_STEP + 20 && s_top + ROW_SHOWN < n) {
        s_sel = s_top + ROW_SHOWN; move_sound(); return 1;
    }
    for (i = s_top; i < n && i < s_top + ROW_SHOWN; i++) {
        const Row *w = &r[i];
        int ry = ROW_Y0 + (i - s_top) * ROW_STEP, cnt, k;
        if (fabsf(y - (ry + 17)) >= ROW_STEP * 0.5f || x < 10 || x > 630) continue;
        if ((moved || clk) && s_sel != i) { s_sel = i; move_sound(); }
        if (!clk) return 1;
        if (w->kind == K_BIND) {   /* its keys: take a new one; the controller's button: the next */
            if (x >= 480 && xbox_BindIsButton(w->gfx)) change(w, x < 500 ? -1 : 1);
            else if (x >= 200 && x < 480) activate(w);
            return 1;
        }
        if (w->kind == K_RESET) { if (x < 330) activate(w); return 1; }
        cnt = xbox_GfxItemCount(w->gfx);
        if (w->kind == K_COLS) {
            for (k = 0; k < cnt && k < 3; k++)
                if (x >= s_colx[k] - 8 && x < s_colx[k] + 120 && k != xbox_GfxIndex(w->gfx)) { xbox_GfxSetIndex(w->gfx, k); move_sound(); }
        } else if (w->kind == K_CYCLE) {
            if (x >= s_colx[0] - 8 && x < s_colx[1] - 4) change(w, -1);
            else if (x >= s_colx[1] - 4 && x < s_colx[2] + 60) change(w, 1);
        } else if (x >= 222 && x < 600) {   /* the slider: the knob to the click, and dragged while held */
            drag = i;
            return tt_opt_mouse(x, y, 0, 0, 1);
        }
        return 1;
    }
    return 1;
}
