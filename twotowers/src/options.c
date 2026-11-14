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
       GFX_MOUSE_AIM, GFX_MOUSE_INVERT, GFX_SPRINT, GFX_MOUSE_STEER, GFX_WINSIZE, GFX_ULTRA, GFX_WIDE_SCENES,
       GFX_FS_RES };
extern const char *xbox_GfxItemName(int id, int v);

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

enum { PG_HUB, PG_GENERAL, PG_DISPLAY, PG_CONTROLS, PG_COOP, PG_COOPMENU, PG_P2CTL, PG_GAME, PG_ONLINE };
enum { K_COLS, K_CYCLE, K_SLIDER, K_BIND, K_RESET, K_DEV, K_BIND2, K_RESET2, K_PAGE, K_VSHERO, K_NET, K_ONLINEPG };   /* (K_BIND: gfx is the action, input_bind.h; K_DEV: gfx is the player, 0-1; K_BIND2 / K_RESET2: player 2's controller; K_PAGE: A opens player 2's controls; K_VSHERO: a Versus hero, the arena or the rounds) */
typedef struct {
    const char *label;
    int gfx, kind;
    const char *names[24];    /* short names for the choices (columns, cycling) */
} Row;
static const Row s_display[] = {
    { "Frame Rate:", GFX_FPS, K_COLS, { "30", "60", "Unlocked" } },
    { "Display Mode:", GFX_FULLSCREEN, K_COLS, { "Window", "Borderless", "Exclusive" } },
    { "Full Screen Res:", GFX_FS_RES, K_CYCLE, { 0 } },   /* (the monitor's own: their names from the settings) */
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
/* player 2's controller (co-op): its own buttons (input_bind.c's second table, "p2pad_<action>" in [Controls]), which
 * controller it is, and a line showing what each connected controller presses now */
extern int  xbox_BindPad2(int action);
extern void xbox_BindAssignPad2(int action, int src);
extern void xbox_BindResetPad2(void);
extern int  xbox_BindPadP(int player, int action);   /* (2.1) players 2-4's controllers */
extern void xbox_BindAssignPadP(int player, int action, int src);
extern void xbox_BindResetPadP(int player);
extern int  xbox_InputPadCount(void);
extern void xbox_InputPadName(int k, char *out, int n);
extern int  xbox_InputPadPeek(int k, unsigned short *buttons, unsigned char *lt, unsigned char *rt, short *lx, short *ly, short *rx, short *ry);
extern int  xbox_InputPlayerPadIndex(int port);
#define P2BTN_N 16   /* the button actions, A to the right stick's click */
static Row s_p2ctl[P2BTN_N + 1];
static int s_np2ctl, s_ret_page = PG_COOP, s_ret_sel;
static int s_ctl_player = 2;   /* (2.1) whose controls the controls page shows: player 2, 3 or 4 (the K_PAGE row's gfx; 0 is 2) */

/* the co-op settings (the Co-op Options page), kept in the ini's [Coop]; the co-op code
 * (twotowers_manual.c) reads them with tt_coop_setting. Testing: TT_COOP_DIFFICULTY,
 * TT_COOP_ICONS, TT_COOP_ZOOMSET set one for the run (the ini is not changed). */
#define GFX_COOP 1000
enum { CO_DIFF, CO_ICONS, CO_ZOOM, CO_TELE, CO_BOT, CO_PICK, CO_IMP, CO_PLAYERS, CO_REPLACE, CO_P3BOT, CO_P4BOT, CO_N };   /* (2.1: CO_PLAYERS, 2 to 4: 0 to 2) */   /* (CO_IMP: the solo game's Impossible, 1.9.7) */
static const char *const s_coop_key[CO_N] = { "difficulty", "player_icons", "camera_zoom", "p2_teleport", "p2_bot", "double_pickups", "impossible", "players", "replace_companions", "p3_bot", "p4_bot" };
static const char *const s_coop_env[CO_N] = { "TT_COOP_DIFFICULTY", "TT_COOP_ICONS", "TT_COOP_ZOOMSET", "TT_COOP_TELEPORT", "TT_COOP_P2BOT", "TT_COOP_PICKUPS", "TT_IMPOSSIBLE", "TT_COOP_PLAYERSSET", "TT_COOP_REPLACESET", "TT_COOP_P3BOTSET", "TT_COOP_P4BOTSET" };
static const int s_coop_cnt[CO_N] = { 4, 2, 3, 2, 2, 2, 2, 3, 2, 3, 3 }, s_coop_def[CO_N] = { 1, 1, 1, 1, 0, 1, 0, 0, 1, 2, 2 };
static int s_coop_v[CO_N] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };
static const Row s_coop[] = {
    { "Players:", GFX_COOP + CO_PLAYERS, K_COLS, { "2", "3", "4" } },   /* (2.1) players 3 and 4 (twotowers_manual.c, coopslot.h) */
    { "Difficulty:", GFX_COOP + CO_DIFF, K_CYCLE, { "Easy", "Normal", "Expert", "Impossible" } },
    { "Player Icons:", GFX_COOP + CO_ICONS, K_COLS, { "Off", "On" } },
    { "Camera Zoom:", GFX_COOP + CO_ZOOM, K_COLS, { "Off", "Normal", "Wide" } },
    { "P2 Device:", 1, K_DEV, { 0 } },   /* (1.9.6's device choice, in place of 1.9.4's P2 Controller) */
    { "P2 Bot:", GFX_COOP + CO_BOT, K_COLS, { "Off", "On" } },   /* (When Idle and Always gone: idle cut in on a controller, a playtest) player 2 played by the co-op code (twotowers_manual.c coop_p2bot): (was: when his controller is idle, any input hands it over for 3 s), or always */
    { "P2 Controls", 2, K_PAGE, { 0 } },   /* player 2's own buttons (A: the page) */
    { "P3 Device:", 2, K_DEV, { 0 } },   /* (2.1) players 3 and 4's, as Co-op Options has them (the rows scroll) */
    { "P3 Bot:", GFX_COOP + CO_P3BOT, K_COLS, { "Off", "On", "Auto" } },
    { "P3 Controls", 3, K_PAGE, { 0 } },
    { "P4 Device:", 3, K_DEV, { 0 } },
    { "P4 Bot:", GFX_COOP + CO_P4BOT, K_COLS, { "Off", "On", "Auto" } },
    { "P4 Controls", 4, K_PAGE, { 0 } },
};
/* (1.9.6) Options > Co-op Options: the co-op settings, the pickups, each player's device and player 2's controls. The
 * Co-op menu (above) has seven of them, clear of its Help prompt */
static const Row s_coop_page[] = {
    { "Players:", GFX_COOP + CO_PLAYERS, K_COLS, { "2", "3", "4" } },   /* (2.1) */
    { "Difficulty:", GFX_COOP + CO_DIFF, K_CYCLE, { "Easy", "Normal", "Expert", "Impossible" } },
    { "Player Icons:", GFX_COOP + CO_ICONS, K_COLS, { "Off", "On" } },
    { "Camera Zoom:", GFX_COOP + CO_ZOOM, K_COLS, { "Off", "Normal", "Wide" } },
    { "P2, P3, P4 Teleport:", GFX_COOP + CO_TELE, K_COLS, { "Off", "On" } },
    { "P2 Bot:", GFX_COOP + CO_BOT, K_COLS, { "Off", "On" } },
    { "P3 Bot:", GFX_COOP + CO_P3BOT, K_COLS, { "Off", "On", "Auto" } },   /* (2.1) Auto: the bot plays him while he has no controller of his own */
    { "P4 Bot:", GFX_COOP + CO_P4BOT, K_COLS, { "Off", "On", "Auto" } },
    { "Replace Companions:", GFX_COOP + CO_REPLACE, K_COLS, { "Off", "On" } },   /* (2.1) a player's hero takes the place of the level's companion of that hero (twotowers_manual.c coop_replace_allies) */
    { "Pickups:", GFX_COOP + CO_PICK, K_COLS, { "Normal", "Double" } },   /* Double: a health or ammo pickup can be taken twice (twotowers_manual.c, sub_0008FC60) */
    { "P1 Device:", 0, K_DEV, { 0 } },
    { "P2 Device:", 1, K_DEV, { 0 } },
    { "P3 Device:", 2, K_DEV, { 0 } },   /* (2.1) */
    { "P4 Device:", 3, K_DEV, { 0 } },
    { "P2 Controls", 2, K_PAGE, { 0 } },
    { "P3 Controls", 3, K_PAGE, { 0 } },   /* (2.1) */
    { "P4 Controls", 4, K_PAGE, { 0 } },
};
/* (2.1) Online Options (the Online menu's last row; the co-op settings' own page for online play, kept apart from Co-op
 * Options'): what the host's game uses (its difficulty is the Online menu's), this machine's controls -- no bots, no
 * teleport, no graphics. Each player's device: the controller or the keys that play at this machine */
static const Row s_online_page[] = {
    { "Player Icons:", GFX_COOP + CO_ICONS, K_COLS, { "Off", "On" } },
    { "Camera Zoom:", GFX_COOP + CO_ZOOM, K_COLS, { "Off", "Normal", "Wide" } },
    { "Replace Companions:", GFX_COOP + CO_REPLACE, K_COLS, { "Off", "On" } },
    { "Pickups:", GFX_COOP + CO_PICK, K_COLS, { "Normal", "Double" } },
    { "P1 Device:", 0, K_DEV, { 0 } },
    { "P2 Device:", 1, K_DEV, { 0 } },
    { "P3 Device:", 2, K_DEV, { 0 } },
    { "P4 Device:", 3, K_DEV, { 0 } },
    { "Button Prompts:", GFX_PROMPTS, K_CYCLE, { "Automatic", "Xbox", "Xbox One", "PlayStation", "Keyboard" } },
    { "Mouse Aiming:", GFX_MOUSE_AIM, K_COLS, { "On", "Off" } },
    { "Invert Aiming:", GFX_MOUSE_INVERT, K_CYCLE, { "Off", "Up/Down", "Left/Right", "Both" } },
    { "Sensitivity:", GFX_MOUSE_SENS, K_SLIDER, { 0 } },
    { "P2 Controls", 0, K_PAGE, { 0 } },
};
static void coop_ini(char *p, int n)
{
    char *sl;
    GetModuleFileNameA(NULL, p, (DWORD)n);
    sl = strrchr(p, '\\'); if (!sl) sl = strrchr(p, '/');
    if (sl) *sl = 0;
#if defined(_WIN32)
    strncat(p, "\\twotowers_recomp.ini", (size_t)n - strlen(p) - 1);
#else
    strncat(p, "/twotowers_recomp.ini", (size_t)n - strlen(p) - 1);   /* (1.9.6: Linux, macOS -- the settings were not read) */
#endif
}
const char *tt_coop_setting_env(int i) { return i >= 0 && i < CO_N ? s_coop_env[i] : "TT_UNUSED"; }   /* (2.1, online: the host's settings for every machine) */
int tt_coop_setting(int i)
{
    if (i < 0 || i >= CO_N) return 0;
    if (s_coop_v[i] < 0) {
        char ini[MAX_PATH];
        const char *e = getenv(s_coop_env[i]);
        int v;
        coop_ini(ini, sizeof ini);
        v = e ? atoi(e) : (int)GetPrivateProfileIntA("Coop", s_coop_key[i], s_coop_def[i], ini);
        if (i == CO_BOT && v > 1) v = 1;   /* (an ini's old Always, 2: On) */
        s_coop_v[i] = v < 0 || v >= s_coop_cnt[i] ? s_coop_def[i] : v;
    }
    return s_coop_v[i];
}
static void coop_set(int i, int v);
void tt_coop_setting_set(int i, int v) { coop_set(i, v); }
static void coop_set(int i, int v)
{
    char ini[MAX_PATH], t[8];
    if (i < 0 || i >= CO_N || v < 0 || v >= s_coop_cnt[i]) return;
    s_coop_v[i] = v;
    if (getenv(s_coop_env[i])) return;
    coop_ini(ini, sizeof ini);
    snprintf(t, sizeof t, "%d", v);
    WritePrivateProfileStringA("Coop", s_coop_key[i], t, ini);
    tt_dbg("[OPTIONS] co-op %s = %d\n", s_coop_key[i], v);
}
/* a row's value: the F10 settings, or ours */
static int opt_count(int id) { return id >= GFX_COOP ? s_coop_cnt[id - GFX_COOP] : xbox_GfxItemCount(id); }
static int opt_get(int id) { return id >= GFX_COOP ? tt_coop_setting(id - GFX_COOP) : xbox_GfxIndex(id); }
static void opt_set(int id, int v) { if (id >= GFX_COOP) coop_set(id - GFX_COOP, v); else xbox_GfxSetIndex(id, v); }

/* (1.9.6) each co-op player's device (src/input/xinput_device.c): -1 automatic, 0 the keyboard and mouse, n the n-th
 * controller connected; kept in [Coop] p1_device / p2_device. In a co-op game with a human player 2 (the P2 Bot off)
 * the input runs as two players: automatic with one controller gives player 1 the keyboard and mouse and player 2
 * the controller; with two, one each. Player 2's hero reads input port 1 (twotowers_manual.c, coop_pad2) */
extern volatile int xbox_input_two_players;
extern int tt_coop_active(void);
extern volatile int xbox_input_dev_choice[4]; extern volatile int xbox_input_players;
extern int xbox_InputDeviceCount(void);
extern void xbox_InputDeviceName(int i, char *out, int n);
extern int xbox_InputPlayerDevice(int port);
extern int xbox_InputKeyboardPort(void);
static void coop_devices_load(void)
{
    static int done;
    char ini[MAX_PATH];
    if (done) return;
    done = 1;
    coop_ini(ini, sizeof ini);
    xbox_input_dev_choice[0] = (int)GetPrivateProfileIntA("Coop", "p1_device", -1, ini);
    xbox_input_dev_choice[1] = (int)GetPrivateProfileIntA("Coop", "p2_device", -1, ini);
    if (xbox_input_dev_choice[0] < -1) xbox_input_dev_choice[0] = -1;
    if (xbox_input_dev_choice[1] < -1) xbox_input_dev_choice[1] = -1;
    if (xbox_input_dev_choice[0] == 0 && xbox_input_dev_choice[1] == 0) xbox_input_dev_choice[1] = -1;
    xbox_input_dev_choice[2] = (int)GetPrivateProfileIntA("Coop", "p3_device", -1, ini);   /* (2.1) */
    xbox_input_dev_choice[3] = (int)GetPrivateProfileIntA("Coop", "p4_device", -1, ini);
    if (xbox_input_dev_choice[2] < -1) xbox_input_dev_choice[2] = -1;
    if (xbox_input_dev_choice[3] < -1) xbox_input_dev_choice[3] = -1;
}
static void coop_devices_save(void)
{
    char ini[MAX_PATH], v[16];
    coop_ini(ini, sizeof ini);
    snprintf(v, sizeof v, "%d", xbox_input_dev_choice[0]); WritePrivateProfileStringA("Coop", "p1_device", v, ini);
    snprintf(v, sizeof v, "%d", xbox_input_dev_choice[1]); WritePrivateProfileStringA("Coop", "p2_device", v, ini);
    snprintf(v, sizeof v, "%d", xbox_input_dev_choice[2]); WritePrivateProfileStringA("Coop", "p3_device", v, ini);
    snprintf(v, sizeof v, "%d", xbox_input_dev_choice[3]); WritePrivateProfileStringA("Coop", "p4_device", v, ini);
    tt_dbg("[OPTIONS] co-op devices: player 1 %d, player 2 %d\n", xbox_input_dev_choice[0], xbox_input_dev_choice[1]);
}
/* each frame (twotowers_manual.c, the controller read): two players' input while a co-op game wants a human player 2 */
void tt_coop_input_sync(void)
{
    int two;
    coop_devices_load();
    { extern int tt_coop_nplayers(void); int np = tt_coop_active() ? tt_coop_nplayers() : 2; xbox_input_players = np;   /* (2.1) three or four players: each his own device */
      two = tt_coop_active() && (!tt_coop_setting(CO_BOT) || np > 2) && !getenv("TT_COOP_PAD2SCRIPT"); }   /* (P2 Bot Off: a person plays player 2) */
    { extern int tt_net_on(void); if (tt_net_on()) { xbox_input_players = 1; two = 0; } }   /* (2.1, online: one player at each machine -- his pad and keys are player 1's port here, net.c sends them) */
    if (two != xbox_input_two_players) {
        xbox_input_two_players = two;
        tt_dbg("[COOPDEV] input as %s (devices: player 1 %d, player 2 %d)\n", two ? "two players" : "one player", xbox_input_dev_choice[0], xbox_input_dev_choice[1]);
    }
    if (two && getenv("TT_COOP_P1GUARD")) {   /* (2.0.1: off, as in 2.0.0 -- the devices as chosen) */   /* (2.0.1) player 1 left with no device (the others' choices took them all, a saved setup):
        * the keyboard back to him -- or no one could answer the menus or the pause */
        extern int xbox_InputPlayerHasDevice(int port); static DWORD lt;
        if (GetTickCount() - lt > 1000u) { int q; lt = GetTickCount();
            if (xbox_input_dev_choice[0] < 0 && !xbox_InputPlayerHasDevice(0))
                for (q = 1; q < 4; q++) if (xbox_input_dev_choice[q] == 0) { xbox_input_dev_choice[q] = -1; coop_devices_save();
                    tt_dbg("[COOPDEV] player 1 had no device: the keyboard back to him (player %d's choice now Auto)\n", q + 1); break; } }
    }
}
/* a player's device in words: what is chosen, and (automatic) what that is now */
static void dev_text(int pl, char *out, int n)
{
    int c = xbox_input_dev_choice[pl], now = xbox_InputPlayerDevice(pl);
    char nm[80];
    if (c < 0) {
        if (!xbox_input_two_players) snprintf(out, n, "Automatic");   /* (outside a co-op game every device is player 1's) */
        else if (now > 0) { xbox_InputDeviceName(now - 1, nm, sizeof nm); snprintf(out, n, "Automatic ({1})\x01%s", nm); }
        else snprintf(out, n, pl == xbox_InputKeyboardPort() ? "Automatic (Keyboard & Mouse)" : "Automatic (none connected)");
    } else if (c == 0) snprintf(out, n, "Keyboard & Mouse");
    else if (c > xbox_InputDeviceCount()) snprintf(out, n, "Controller %d (not connected)", c);
    else { xbox_InputDeviceName(c - 1, nm, sizeof nm); snprintf(out, n, "%d: {1}\x01%s", c, nm); }
}
/* the choices go round: automatic, the keyboard and mouse, each controller connected (not the other player's) */
/* (2.0.1) would player pl taking device v leave player 1 with none? (a playtest: players 2 to 4 given the keyboard and both
 * controllers, player 1 had nothing -- the menus and the pause would not answer). Player 1's own explicit device is never
 * taken; on Auto he has the keyboard when no one else chose it, else a controller no one else chose */
static int p1_left_without(int pl, int v)
{
    int ch[4], q, conn = xbox_InputDeviceCount(), used = 0, kb_taken = 0;
    for (q = 0; q < 4; q++) ch[q] = xbox_input_dev_choice[q];
    ch[pl] = v;
    if (ch[0] == 0) return v == 0;                 /* (his keyboard: not another's) */
    if (ch[0] > 0) return 0;                       /* (his controller: the duplicate check keeps it his) */
    for (q = 1; q < 4 && q < xbox_input_players; q++) { if (ch[q] == 0) kb_taken = 1; else if (ch[q] > 0 && ch[q] <= conn) used++; }
    return kb_taken && conn - used <= 0;
}
static void dev_change(int pl, int d)
{
    int vals[12], nv = 0, i, k, cur = xbox_input_dev_choice[pl], hi = xbox_InputDeviceCount();
    if (hi < 1) hi = 1;   /* (a controller can be chosen ahead of plugging it in) */
    if (hi > 8) hi = 8;
    vals[nv++] = -1;
    vals[nv++] = 0;
    for (k = 1; k <= hi; k++) vals[nv++] = k;
    if (cur > hi && nv < 12) vals[nv++] = cur;   /* (one chosen, unplugged since) */
    for (i = 0; i < nv && vals[i] != cur; i++) ;
    if (i == nv) i = 0;
    i = (i + d + nv) % nv;
    { int tries, q, taken;   /* (not another player's controller; 2.1: any of the four) */
      for (tries = 0; tries < nv; tries++) { taken = 0; for (q = 0; q < 4; q++) if (q != pl && vals[i] > 0 && vals[i] == xbox_input_dev_choice[q]) taken = 1;
          if (!taken && pl != 0 && getenv("TT_COOP_P1GUARD") && p1_left_without(pl, vals[i])) taken = 1;   /* (2.0.1) never player 1's device, nor the last one he could have */
          if (!taken) break; i = (i + d + nv) % nv; } }
    xbox_input_dev_choice[pl] = vals[i];
    if (vals[i] == 0) { int q; for (q = 0; q < 4; q++) if (q != pl && xbox_input_dev_choice[q] == 0) xbox_input_dev_choice[q] = -1; }   /* (the keyboard is one player's) */
    coop_devices_save();
}

static const char *s_hub[4] = { "General Options", "Display Options", "Controls", "Co-op Options" };   /* (1.9.7: Gameplay Options gone -- Impossible is on General Options' Difficulty and the New Game's) */
/* Co-op Options: on the front end's Options, and the pause menu's only in a co-op game */
extern int tt_coop_active(void);
/* (2.1) online (connected, or in an online game): General Options, Controls and Online Options -- no Display Options (each
 * machine's own, set before), no Co-op Options (bots, teleport: not online's) */
static const char *s_hub_net[3] = { "General Options", "Controls", "Online Options" };
static int hub_net(void) { extern int tt_net_on(void); return tt_net_on(); }
static int hub_n(void) { uint32_t fe = G32(0x4AD050u); if (hub_net()) return 3; return fe == 6 || fe == 8 ? (tt_coop_active() ? 4 : 3) : 4; }
static int hub_page(int i) { if (hub_net()) return i == 0 ? PG_GENERAL : i == 1 ? PG_CONTROLS : PG_ONLINE; return i == 0 ? PG_GENERAL : i == 1 ? PG_DISPLAY : i == 2 ? PG_CONTROLS : PG_COOP; }
static int s_online_hub;   /* (Online Options opened from the hub: Back goes there) */
static const char *hub_label(int i) { return hub_net() ? s_hub_net[i < 3 ? i : 2] : s_hub[i]; }
/* (1.9.7) Options > Gameplay Options: Impossible (the solo game's difficulty past Expert: any enemy's second blow fells
 * you; chosen on the New Game's difficulty screen too, right of Expert) */
static const Row s_game_page[] = {
    { "Impossible:", GFX_COOP + CO_IMP, K_COLS, { "Off", "On" } },
};
#define NROWS(a) ((int)(sizeof a / sizeof a[0]))
static int s_nctl;

static int s_page = PG_HUB, s_sel, s_hub_sel, s_top;
static int s_on_back;          /* (1.9.8) the Back leaf (bottom left) is the one chosen */
static uint32_t s_opt_obj;
static volatile int s_leave_pending, s_mouse_down; static volatile DWORD s_quiet_until, s_leave_t;     /* the Options screen's object (its draw's this) */
static void draw_back(void); static void back_activate(void); static int back_y(void); static int back_here(void);
static volatile uint32_t s_fe_before = 4;   /* the last screen before Options (5, both the main menu's and the pause's): tt_opt_filter */
/* the Co-op menu (menus.c: the main menu's Co-op opens the Options screen on this page): New Game and Load Game at the
 * top right, the co-op settings below. New / Load: the page leaves (B for the game) and menus.c, under a black cover,
 * presses that item of the main menu with co-op kept on (New Game: then the difficulty screen with the co-op
 * Difficulty as the game's). B: back to the main menu, co-op off */
volatile int tt_coop_screen_req, tt_coop_menu_go;
static int s_exit_frames;
static const char *const s_cm_top[2] = { "New Game", "Load Game" };
/* (1.9.7) the Versus menu (menus.c: the main menu's Versus opens Options on it): Start at the top right, then each
 * player's hero and the arena. Start goes as the Co-op menu's New Game (tt_coop_menu_go 1: the main menu's New Game
 * pressed under a cover, the difficulty confirmed); the new game then becomes the match (twotowers_manual.c,
 * sub_000D1C10: the arena's level, player 1's hero, player 2's). B: back to the main menu */
volatile int tt_vs_menu_on, tt_vs_screen_req, tt_vs_start_pending;
static int s_vsmenu;   /* the Co-op menu page shows Versus' */
/* (2.1) the Online menu (net.c's lobby) on the same page: its rows are net.c's (K_NET, gfx the row) */
static int s_netmenu; volatile int tt_net_screen_req;
extern int tt_net_lobby_state(void); extern const char *tt_net_lobby_label(int row); extern void tt_net_lobby_value(int row, char *out, int n);
extern int tt_net_lobby_change(int row, int d); extern void tt_net_lobby_status(char *out, int n); extern void tt_net_lobby_type(int c);
extern void tt_net_lobby_host(void), tt_net_lobby_join(void), tt_net_lobby_leave(void); extern int tt_net_lobby_start(void);
extern int tt_net_lobby_ready(void); extern void tt_net_lobby_toggle_ready(void); extern int tt_net_coop_flow(void);
static void sound(uint32_t id, float pan);
/* (2.1) typing the address (row 5) and the port (row 6) in the Online menu: each key once a press (the '.' gave a row of dots: its
 * "seen" flag was the loop's own last key, cleared every frame), Backspace / Delete held repeats; while typing (and 0.6 s after a
 * key) the keys' other meanings are kept off -- Backspace is Back (it left the menu), the letters move (W / S off the row, A / D
 * changed the address) -- the arrows, a controller and the mouse still move. tt_net_typing: the row typed on, or -1 */
extern void tt_net_lobby_type_port(int c);
static volatile DWORD s_net_type_t;
static int net_type_row(void);
int tt_net_typing(void) { return net_type_row(); }
static void net_type_keys(int row)
{
    static unsigned char was[256]; static DWORD rep_t; static int lrow = -99; int vk;
    if (row != lrow && getenv("TT_NET_TYPELOG")) { tt_dbg("[NETTYPE] row %d\n", row); lrow = row; }
    if (row != 5 && row != 6) { memset(was, 1, sizeof was); return; }   /* (keys held as the row is reached: no press) */
    if (tt_net_lobby_state() != 0) return;
    for (vk = 8; vk < 256; vk++) { int d = xbox_InputKeyDown(vk), c = 0;
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
        if (d && (!was[vk] || ((vk == 0x08 || vk == 0x2E) && GetTickCount() - rep_t > 60u && GetTickCount() - s_net_type_t > 400u))) {
            if (vk >= '0' && vk <= '9') c = vk; else if (vk >= 0x60 && vk <= 0x69) c = '0' + vk - 0x60;
            else if (vk == 0x08 || vk == 0x2E) c = 8;
            else if (row == 5 && (vk == 0xBE || vk == 0x6E)) c = '.';
            else if (row == 5 && vk == 0xBD) c = '-';
            else if (row == 5 && vk >= 'A' && vk <= 'Z') c = vk - 'A' + 'a';
            if (c) { if (row == 5) tt_net_lobby_type(c); else tt_net_lobby_type_port(c);
                if (getenv("TT_NET_TYPELOG")) tt_dbg("[NETTYPE] row %d key %02X -> '%c'\n", row, vk, c == 8 ? '<' : c);
                if (was[vk]) rep_t = GetTickCount(); else s_net_type_t = GetTickCount(); } }
        was[vk] = (unsigned char)d; }
}
static void net_top_action(int i)   /* Host Game / Join Game, Start Game / Leave */
{
    int st = tt_net_lobby_state(); sound(0xA9D, -1.0f);
    if (st == 0) { if (i == 0) tt_net_lobby_host(); else tt_net_lobby_join(); }
    else if (st == 1 && i == 0) { if (tt_net_lobby_start() == 2) {   /* (Co-op: the host on to the Co-op menu -- New or Load Game, his save, the select, the map) */
            extern volatile int tt_coop_menu_on; extern int tt_net_players(void); int np = tt_net_players();
            s_netmenu = 0; s_vsmenu = 0; tt_coop_menu_on = 1; s_page = PG_COOPMENU; s_sel = 0; s_top = 0;
            s_coop_v[CO_PLAYERS] = np - 2; s_coop_v[CO_BOT] = 0; s_coop_v[CO_P3BOT] = 0; s_coop_v[CO_P4BOT] = 0; s_coop_v[CO_TELE] = 0;   /* (for this game only: not written to the settings) */
            tt_dbg("[NETMENU] the host goes on to the Co-op menu (%d players)\n", np); return; } }
    else if (st == 3 && i == 0) tt_net_lobby_toggle_ready();
    else tt_net_lobby_leave();
    s_sel = 0;
}
static const Row s_netrows[] = { { "", 0, K_NET, { 0 } }, { "", 1, K_NET, { 0 } }, { "", 2, K_NET, { 0 } }, { "", 3, K_NET, { 0 } },
    { "", 4, K_NET, { 0 } }, { "", 5, K_NET, { 0 } }, { "", 6, K_NET, { 0 } }, { "Online Options", 0, K_ONLINEPG, { 0 } } };
#define VS_NHERO 19
static const char *const s_vs_heroes[VS_NHERO][2] = {   /* (name for heroes.c and the co-op, title) */
    { "aragorn", "Aragorn" }, { "legolas", "Legolas" }, { "gimli", "Gimli" }, { "isildur", "Isildur" },
    { "boromir", "Boromir" }, { "gandalf", "Gandalf" }, { "frodo", "Frodo" }, { "lurtz", "Lurtz" },
    { "elrond", "Elrond" }, { "sauron", "Sauron" }, { "uruk", "Berserker" },
    { "troll", "Cave Troll" }, { "urukcap", "Uruk Captain" }, { "saruman", "Saruman" },   /* (1.9.9) */
    { "elvenwarrior", "Elven Warrior" },
    { "nazgul", "Nazgul" }, { "orc", "Shield Orc" }, { "eomer", "Eomer" },   /* (the elven warrior and the last three: Versus only) */
    { "ent", "Ent" } };   /* (1.9.9: Fangorn's Treebeard -- Versus only, and in Troll Battle beside the cave troll) */
#define VS_ENT_HERO 18
static int s_vs_goal_i = 1;   /* (1.9.7) rounds to win: 3, 5, 7 */
static const char *const s_vs_goals[3] = { "First to 3", "First to 5", "First to 7" };
int tt_vs_goal_pick(void) { return 3 + 2 * s_vs_goal_i; }
static int s_vs_hero[4] = { 0, 1, 2, 3 }, s_vs_arena, s_vs_mode;   /* (2.1: mode 2 Team Deathmatch -- four heroes, players 1 and 3 against 2 and 4) */   /* mode 0 Deathmatch (the smaller arenas), 1 Find the Shield (the bigger) */
static const char *const s_vs_modes[4] = { "Deathmatch", "Troll Battle", "Team Deathmatch", "Free-for-all" };   /* (2.1: mode 3, three or four players each for himself) */
static int s_vs_p4on = 1, s_vs_tp3, s_vs_tp4;   /* (2.0.1, Troll Battle: players 3 and 4 in it -- off: one on one) */
static int s_vs_p4on_dummy;   /* (2.1, Free-for-all) player 4 in the match: off with three players */   /* (1.9.9: mode 1, both players the cave troll) */
#define VS_TROLL_HERO 11   /* (s_vs_heroes: the cave troll -- Troll Battle's only hero, in no other mode) */
/* the arenas: every level (the game's level index, its name; 2.0.0: Westfold too) */
static const struct { int level; const char *name; } s_vs_arenas[] = {
    { 1, "Weathertop" }, { 3, "Balin's Tomb" }, { 4, "Amon Hen" }, { 5, "Fangorn Forest" }, { 2, "Gates of Moria" },
    { 6, "Plains of Rohan" }, { 7, "Westfold" }, { 8, "Road to Helm's Deep" }, { 9, "The Deeping Wall" }, { 10, "The Breached Wall" },
    { 11, "Hornburg Courtyard" }, { 12, "Orthanc" }, { 13, "Orthanc Summit" }, { 0, "Mount Doom" } };
/* which mode each arena is for: the smaller ones Deathmatch, the bigger (room to run, the shield to find) Find the Shield */
/* (1.9.7) Find the Shield dropped for now: every arena is a Deathmatch arena */
/* (1.9.9) Troll Battle: Balin's Tomb (his own) and Amon Hen (Mount Doom taken out, a playtest); Deathmatch: every arena */
static int vs_arena_troll(int i) { return s_vs_arenas[i].level == 3 || s_vs_arenas[i].level == 4; }
static int vs_mode_kind(void) { return s_vs_mode == 2 || s_vs_mode == 3 ? 0 : s_vs_mode; }   /* (Team Deathmatch: every arena, as Deathmatch) */
static int vs_arena_mode(int i) { return s_vs_mode == 1 && vs_arena_troll(i) ? 1 : s_vs_mode == 1 ? -1 : 0; }
#define VS_NARENA ((int)(sizeof s_vs_arenas / sizeof s_vs_arenas[0]))
static const Row s_vsrows[] = {   /* (2.0.1: Mode first, as in the other modes' rows -- it jumped from the bottom to the top as the mode turned, a playtest) */
    { "Mode:", 4, K_VSHERO, { 0 } },   /* (1.9.9, gfx 4: Deathmatch or Troll Battle) */
    { "Player 1:", 0, K_VSHERO, { 0 } },
    { "Player 2:", 1, K_VSHERO, { 0 } },
    { "Arena:", 2, K_VSHERO, { 0 } },   /* (gfx 2: the arena, shown as the heroes are) */
    { "Rounds to win:", 3, K_VSHERO, { 0 } },   /* (1.9.7, gfx 3: first to 3, 5 or 7) */
};
static const Row s_vsrowsffa[] = {   /* (2.1) Free-for-all: three or four heroes (gfx 7: player 4 on or off) */
    { "Mode:", 4, K_VSHERO, { 0 } },
    { "Player 1:", 0, K_VSHERO, { 0 } },
    { "Player 2:", 1, K_VSHERO, { 0 } },
    { "Player 3:", 5, K_VSHERO, { 0 } },
    { "Player 4:", 6, K_VSHERO, { 0 } },
    { "Player 4 Plays:", 7, K_VSHERO, { 0 } },
    { "Arena:", 2, K_VSHERO, { 0 } },
    { "Rounds to win:", 3, K_VSHERO, { 0 } },
};
static const Row s_vsrows4[] = {   /* (2.1) Team Deathmatch: four heroes (gfx 5 and 6: players 3 and 4) */
    { "Mode:", 4, K_VSHERO, { 0 } },
    { "Team 1 - P1:", 0, K_VSHERO, { 0 } },
    { "Team 2 - P2:", 1, K_VSHERO, { 0 } },
    { "Team 1 - P3:", 5, K_VSHERO, { 0 } },
    { "Team 2 - P4:", 6, K_VSHERO, { 0 } },
    { "Arena:", 2, K_VSHERO, { 0 } },
    { "Rounds to win:", 3, K_VSHERO, { 0 } },
};
static const Row s_vsrowstroll[] = {   /* (2.0.1) Troll Battle: players 3 and 4 if wanted (gfx 8, 9) -- three or four, each for himself */
    { "Mode:", 4, K_VSHERO, { 0 } },
    { "Player 1:", 0, K_VSHERO, { 0 } },
    { "Player 2:", 1, K_VSHERO, { 0 } },
    { "Player 3 Plays:", 8, K_VSHERO, { 0 } },
    { "Player 4 Plays:", 9, K_VSHERO, { 0 } },
    { "Arena:", 2, K_VSHERO, { 0 } },
    { "Rounds to win:", 3, K_VSHERO, { 0 } },
};
static int vs_troll_np(void) { const char *e = getenv("TT_VS_TROLLPLAYERS"); if (e) return atoi(e) >= 4 ? 4 : atoi(e) == 3 ? 3 : 2; return s_vs_tp3 ? (s_vs_tp4 ? 4 : 3) : 2; }
int tt_vs_team_mode(void) { extern int tt_vs_on(void); return s_vs_mode == 2; }
int tt_vs_ffa_mode(void) { return s_vs_mode == 3 || ((s_vs_mode == 1 || getenv("TT_VS_TROLL")) && vs_troll_np() > 2); }   /* (2.1; 2.0.1: Troll Battle of three or four, each for himself) */
int tt_vs_ffa_players(void) { if (s_vs_mode == 1 || getenv("TT_VS_TROLL")) return vs_troll_np(); return s_vs_p4on ? 4 : 3; }
extern int tt_heroes_on(void);
/* who may be which: the extra heroes with the Extra heroes mod on; player 1 on Weathertop as Aragorn or an extra
 * hero (the level has no Legolas, Gimli or Isildur of its own); never both the same */
static int vs_hero_ok(int pl, int h)
{
    if (h < 0 || h >= VS_NHERO) return 0;
    if (h >= 4 && !tt_heroes_on()) return 0;
    if (s_vs_mode == 1) return h == VS_TROLL_HERO;   /* (1.9.9) Troll Battle: the troll against the troll only (the Ent is a Deathmatch hero) */
    if (h == VS_TROLL_HERO) return 0;                /* (and in no other mode) */
    if (pl == 0 && s_vs_arenas[s_vs_arena].level == 1 && h >= 1 && h <= 3 && getenv("TT_VS_HEROBYARENA")) return 0;
    return 1;   /* (1.9.9: the same hero for both if they like -- player 2 the other's copy) */
}
/* (1.9.8) Weathertop has no Legolas, Gimli or Isildur of its own for player 1: those heroes for him take the arena off
 * the list, rather than the arena (the first, so it seemed always) taking them off his (a playtest: "Gimli can be picked
 * only for player 2") */
static int vs_arena_ok(int i) { return !(s_vs_arenas[i].level == 1 && s_vs_hero[0] >= 1 && s_vs_hero[0] <= 3 && !tt_heroes_on()) || getenv("TT_VS_HEROBYARENA"); }   /* (with the Extra heroes mod: he is hero 4 there wearing the pick, heroes.c) */
static void vs_fix(void)
{
    int pl, k;
    for (k = 0; k < VS_NARENA && (vs_arena_mode(s_vs_arena) != vs_mode_kind() || !vs_arena_ok(s_vs_arena)); k++) s_vs_arena = (s_vs_arena + 1) % VS_NARENA;   /* (an arena of the mode, open to player 1's hero) */
    for (pl = 0; pl < 4; pl++) if (!vs_hero_ok(pl, s_vs_hero[pl])) for (k = 0; k < VS_NHERO; k++) if (vs_hero_ok(pl, k)) { s_vs_hero[pl] = k; break; }
}
static void vs_hero_change(int pl, int d)
{
    int k, h;
    if (pl == 3) { s_vs_goal_i = (s_vs_goal_i + d + 3) % 3; return; }   /* (rounds to win) */
    if (pl == 4) {   /* (1.9.9) the mode: Deathmatch, Team Deathmatch (2.1), Troll Battle (with the Extra heroes mod) */
        static const int order[4] = { 0, 2, 3, 1 }; int i, n = tt_heroes_on() ? 4 : 3;   /* (2.1: Free-for-all after Team Deathmatch) */
        for (i = 0; i < 4 && order[i] != s_vs_mode; i++) ;
        i = (i + (d < 0 ? n - 1 : 1)) % n; s_vs_mode = order[i];
        if (s_vs_mode != 1) { int k; for (k = 0; k < 4; k++) if (s_vs_hero[k] == VS_TROLL_HERO) s_vs_hero[k] = k; }
        vs_fix(); return; }
    if (pl == 7) { s_vs_p4on ^= 1; return; }
    if (pl == 8) { s_vs_tp3 ^= 1; if (!s_vs_tp3) s_vs_tp4 = 0; return; }   /* (2.0.1, Troll Battle: player 3 in or out -- player 4 only with him) */
    if (pl == 9) { if (s_vs_tp3) s_vs_tp4 ^= 1; return; }   /* (2.1, Free-for-all: player 4 on or off) */
    if (pl == 2) { int k; for (k = 0; k < VS_NARENA; k++) { s_vs_arena = (s_vs_arena + d + VS_NARENA) % VS_NARENA; if (vs_arena_mode(s_vs_arena) == vs_mode_kind() && vs_arena_ok(s_vs_arena)) break; } vs_fix(); return; }   /* (the arena row: the mode's) */
    if (pl == 5 || pl == 6) pl -= 3;   /* (2.1: players 3 and 4's rows -- after the arena's: player 3's row (5 -> 2) changed the arena, a playtest) */
    h = s_vs_hero[pl];
    for (k = 0; k < VS_NHERO; k++) { h = (h + d + VS_NHERO) % VS_NHERO; if (vs_hero_ok(pl, h)) { s_vs_hero[pl] = h; vs_fix(); return; } }
}
/* the match asked for (twotowers_manual.c at the new game) */
const char *tt_vs_hero_name(int pl) { return s_vs_heroes[s_vs_hero[pl & 3]][0]; }
/* (2.1) a 4-player match starting: players 3's and 4's picks to the co-op's (tt_coop_px_char) -- by the keyboard and now the mouse too
 * (a click on Start Match left them empty: players 3 and 4 were the co-op's stand-ins, Legolas and Gimli) */
static void vs_px_picks(void)
{
    extern char tt_coop_px_selected[2][32]; int k;
    if (!s_vsmenu || (s_vs_mode != 2 && s_vs_mode != 3 && !(s_vs_mode == 1 && vs_troll_np() > 2))) return;
    if (s_vs_mode == 1) { snprintf(tt_coop_px_selected[0], 32, "troll"); snprintf(tt_coop_px_selected[1], 32, "troll2");   /* (2.0.1) players 3 and 4 as 1 and 2: the black cave troll, the grey */
        tt_dbg("[VSMENU] Troll Battle: %d players\n", vs_troll_np()); return; }
    for (k = 0; k < 2; k++) snprintf(tt_coop_px_selected[k], 32, "%s", tt_vs_hero_name(2 + k));
    tt_dbg("[VSMENU] %s: player 3 %s, player 4 %s\n", s_vs_modes[s_vs_mode], tt_vs_hero_name(2), tt_vs_hero_name(3));
}
int tt_vs_mode(void) { const char *e = getenv("TT_VS_MODE"); return e ? atoi(e) : 0; }   /* (Find the Shield: TT_VS_MODE=1, testing only) */
int tt_vs_troll_mode(void) { return getenv("TT_VS_TROLL") ? 1 : s_vs_mode == 1; }   /* (1.9.9) Troll Battle */
int tt_vs_arena_level(void) { const char *e = getenv("TT_VS_ARENA"); return e ? atoi(e) : s_vs_arenas[s_vs_arena].level; }
/* (2.1) the Co-op menu as the online host reaches it (Start Game in the Online menu): only what this game's look is -- the players,
 * bots, devices and controls are the online game's own (each machine's own controller settings), not this menu's */
static const Row s_coop_online[] = {
    { "Player Icons:", GFX_COOP + CO_ICONS, K_COLS, { "Off", "On" } },
    { "Camera Zoom:", GFX_COOP + CO_ZOOM, K_COLS, { "Off", "Normal", "Wide" } },
};
static const Row *cm_rows(int *n) { if (!s_netmenu && !s_vsmenu && tt_net_coop_flow()) { *n = (int)(sizeof s_coop_online / sizeof s_coop_online[0]); return s_coop_online; } if (s_netmenu) { *n = (int)(sizeof s_netrows / sizeof s_netrows[0]); return s_netrows; } if (s_vsmenu && s_vs_mode == 2) { *n = (int)(sizeof s_vsrows4 / sizeof s_vsrows4[0]); return s_vsrows4; } if (s_vsmenu && s_vs_mode == 3) { *n = (int)(sizeof s_vsrowsffa / sizeof s_vsrowsffa[0]); return s_vsrowsffa; } if (s_vsmenu && s_vs_mode == 1) { *n = (int)(sizeof s_vsrowstroll / sizeof s_vsrowstroll[0]); return s_vsrowstroll; } if (s_vsmenu) { *n = (int)(sizeof s_vsrows / sizeof s_vsrows[0]); return s_vsrows; } *n = (int)(sizeof s_coop / sizeof s_coop[0]); return s_coop; }
static int cm_ntop(void) { if (s_netmenu) { int st = tt_net_lobby_state(); return st == 2 ? 1 : 2; } return s_vsmenu ? 1 : 2; }
static int net_type_row(void) { int r; if (!s_netmenu || s_page != PG_COOPMENU || tt_net_lobby_state() != 0) return -1; r = s_sel - cm_ntop(); return r == 5 || r == 6 ? r : -1; }   /* (2.1) the Online menu's address or port row chosen, not yet hosting or joined */
static const char *cm_top(int i)
{
    if (s_netmenu) { int st = tt_net_lobby_state(); return st == 0 ? (i ? "Join Game" : "Host Game") : st == 1 ? (i ? "Leave" : "Start Game") : st == 3 ? (i ? "Leave" : tt_net_lobby_ready() ? "Not Ready" : "Ready") : "Leave"; }
    return s_vsmenu ? "Start Match" : s_cm_top[i];
}   /* (1.9.8: "Start Match", its own words -- "Start" is the button's name in the languages) */
#define CM_TITLEY 22               /* the title, then New Game and Load Game, in the middle at the top, in the main menu's font
                                    * (its table's elements 8..11 at 0x432728: their x, width and font kept) */
#define CM_TOPY 62
#define CM_TOPSTEP 36
#define MM_EL(i) (0x432728u + (uint32_t)(i) * EL_SIZE)
#define CM_ROWY 150
#define CM_STEP (s_netmenu || (s_vsmenu && s_vs_mode == 3) ? 33 : 38)   /* (Free-for-all's eight rows too: clear of Help) */   /* (seven rows clear of the Help prompt; the Online menu's eight, closer) */
static int cm_nrows(void) { int n; cm_rows(&n); return n; }
/* (2.1) the Co-op menu's rows scroll: seven shown (clear of Help), the chosen one kept in view */
#define CM_SHOWN 7
static int s_cm_rtop;
static int cm_rtop(int nr)
{
    int rs = s_sel - cm_ntop();
    if (s_netmenu || s_vsmenu || nr <= CM_SHOWN) { s_cm_rtop = 0; return 0; }
    if (rs >= 0 && rs < s_cm_rtop) s_cm_rtop = rs;
    if (rs >= s_cm_rtop + CM_SHOWN) s_cm_rtop = rs - CM_SHOWN + 1;
    if (s_cm_rtop > nr - CM_SHOWN) s_cm_rtop = nr - CM_SHOWN; if (s_cm_rtop < 0) s_cm_rtop = 0;
    return s_cm_rtop;
}
#define CM_N (cm_ntop() + cm_nrows())
static DWORD s_drawn;            /* the Options page last drawn (ticks) */
static volatile unsigned s_frames, s_drawn_frame;   /* (1.9.3) the game's frames, and the last with Options drawn */
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

extern int tt_tr16(const char *en, uint16_t *out, int max); extern int tt_tr16n(const char *en, uint16_t *out, int max); extern int tt_tr16s(const char *tmpl, const char *a1, uint16_t *out, int max);
static uint32_t word(const char *t)
{
    uint32_t a = s_txt;
    uint16_t u[256];
    const char *sep = strchr(t, '\x01');   /* (1.9.8) "template\x01name": the template's {1} the name (dev_text) */
    int i, n;
    if (sep) { char tm[200]; snprintf(tm, sizeof tm, "%.*s", (int)(sep - t), t); n = tt_tr16s(tm, sep + 1, u, 256); }
    else n = tt_tr16n(t, u, 256);   /* in the chosen language (lang.c; its numbers kept: tt_tr16n) */
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
    if (ring && (s_page == PG_COOPMENU || s_page == PG_COOP) && !getenv("TT_COOP_RINGOVER")) {   /* (co-op pages: the ring drawn over the
        * words hid the chosen one -- the game draws markers after text, whatever the order -- so there it is smaller, 36
        * units, just left of the word: cx is the word's x + 64 here) TT_COOP_RINGOVER: as before */
        G32(e1 + 8) = G32(e2 + 8) = (uint32_t)(cx - 64 - 38);
        G32(e1 + 0xC) = G32(e2 + 0xC) = (uint32_t)(cy - 18);
        G32(e1 + 0x10) = G32(e2 + 0x10) = 36; G32(e1 + 0x14) = G32(e2 + 0x14) = 36;
    }
    draw(e1); draw(e2);
}

static const Row *rows(int *n)
{
    if (s_page == PG_DISPLAY) { *n = NROWS(s_display); return s_display; }
    if (s_page == PG_COOPMENU) { coop_devices_load(); return cm_rows(n); }
    if (s_page == PG_COOP) { coop_devices_load(); *n = NROWS(s_coop_page); return s_coop_page; }
    if (s_page == PG_ONLINE) { coop_devices_load(); *n = NROWS(s_online_page); return s_online_page; }
    if (s_page == PG_P2CTL) {
        if (!s_np2ctl) {   /* a row per button (players 2-4's controllers have no keys), then Reset */
            static const char *const rl[3] = { "Reset P2 Controls", "Reset P3 Controls", "Reset P4 Controls" };
            int i;
            for (i = 0; i < P2BTN_N; i++) { Row w = { s_bind_label[i], i, K_BIND2, { 0 } }; s_p2ctl[s_np2ctl++] = w; }
            { Row w = { rl[s_ctl_player - 2], 0, K_RESET2, { 0 } }; s_p2ctl[s_np2ctl++] = w; }
        }
        *n = s_np2ctl; return s_p2ctl;
    }
    if (s_page == PG_GAME) { *n = NROWS(s_game_page); return s_game_page; }
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
/* one of the co-op pages is up (the first page, which leads to Co-op Options, Co-op Options, the Co-op menu, player 2's controls) and no
 * key is being taken: every controller drives it (menus.c merges them; a playtest: only player 1's did) */
int tt_opt_coop_page(void) { return ours() && !s_cap && (s_page == PG_HUB || s_page == PG_COOP || s_page == PG_COOPMENU || s_page == PG_P2CTL || s_page == PG_ONLINE); }
static unsigned char s_cap_held[256];
#define ROW_Y0   28
#define ROW_STEP 52
#define ROW_SHOWN 7
static const int s_colx[3] = { 222, 340, 466 };   /* (the game's 191, 309, 435, moved over for longer names) */

/* a row's value(s) at height y (sel: the chosen row) */
static void draw_value(const Row *w, int y, int sel)
{
    int k, cnt = w->kind >= K_BIND ? 0 : opt_count(w->gfx), cur = w->kind >= K_BIND ? 0 : opt_get(w->gfx);
    if (w->kind == K_DEV) {   /* (1.9.6) a player's device */
        char v[120];
        dev_text(w->gfx, v, sizeof v);
        text_el(T_VALUE, s_colx[0] - 8, y, 0, COL_OFF, "<");
        text_el(T_VALUE, s_colx[0] + 12, y, 340, sel ? COL_ON : COL_OFF, v);
        text_el(T_VALUE, 585, y, 40, COL_OFF, ">");
    } else if (w->kind == K_BIND) {
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
    } else if (w->kind == K_BIND2) {   /* player 2's controller: the button on it that does this */
        char v[48];
        snprintf(v, sizeof v, sel ? "< %s >" : "%s", xbox_BindPadName(xbox_BindPadP(s_ctl_player, w->gfx)));
        text_el(T_VALUE, 300, y, 220, sel ? COL_ON : COL_OFF, v);
    } else if (w->kind == K_RESET || w->kind == K_RESET2) {
        if (sel) marker(250, y + 17, 1);
    } else if (w->kind == K_PAGE) {
        text_el(T_VALUE, s_colx[0], y, 300, sel ? COL_ON : COL_OFF, "Press A");
    } else if (w->kind == K_COLS) {
        for (k = 0; k < cnt && k < 3; k++) {
            if (k == cur) marker(s_colx[k] + 64, y + 17, sel);
            text_el(T_VALUE, s_colx[k], y, 0, COL_OFF, w->names[k] ? w->names[k] : "");
        }
    } else if (w->kind == K_CYCLE) {
        /* the choice in the middle, arrows either side */
        if (cur >= 0 && cur < 24) {
            marker(s_colx[1] + 64, y + 17, sel);
            text_el(T_VALUE, s_colx[1], y, 0, COL_OFF, w->names[cur] ? w->names[cur] : xbox_GfxItemName(w->gfx, cur));
        }
        text_el(T_VALUE, s_colx[0], y, 0, COL_OFF, "<");
        text_el(T_VALUE, s_colx[2], y, 0, COL_OFF, ">");
    } else {
        uint32_t b = el_from(T_BAR);
        if (b) { G32(b + 8) = 230; G32(b + 0xC) = (uint32_t)y; draw(b); }
        if (cnt > 1) marker(262 + (317 * cur) / (cnt - 1), y + 25, sel);
    }
}
static void draw_pad_line(int y);
/* a line in the main menu's font (a copy of its New Game element: x, width and font as the game has them) at y */
static void mm_text(int y, uint32_t col, const char *t)
{
    uint32_t e = el_from(MM_EL(8));
    if (!e) return;
    G32(e + 0xC) = (uint32_t)y;
    G32(e + 0x20) = col;
    G32(e + 0x34) = word(t);
    draw(e);
}
static void draw_coopmenu(void)
{
    int i, k;
    s_nel = 0;
    s_txt = s_mem + MAX_EL * EL_SIZE;
    if (getenv("TT_COOP_BGLOG")) { static int n; if (!n++) { uint32_t t = G32(T_BG + 0xB8), q; tt_dbg("[COOPBG] element:"); for (q = 0; q < 0xDC; q += 4) tt_dbg(" %X:%08X", q, G32(T_BG + q)); tt_dbg("\n[COOPBG] +B8 %08X:", t);
        if (t >= 0x10000u) for (q = 0; q < 0xC0; q += 4) tt_dbg(" %X:%08X", q, G32(t + q)); tt_dbg("\n"); } }
    {   /* its own background (the three hunters, UI\stills3.BFF key 2; heroes.c makes the pixels): the
         * Options background's element (640 x 448 quad, a 512 x 256 texture: +0x18/+0x1C) drawn with a texture of our
         * own -- a copy of its texture object {0x1709C4, 0, 0, 0, data} and of its data's 0x80 header (format byte
         * DXT1 0x0C -> swizzled A8R8G8B8 0x06, the pixels' physical address at +0x30), then 512 x 256 x 4 bytes.
         * Not readable: the Options one. TT_COOP_OLDBG: the Options one. */
        static uint32_t blks[3], objs[3]; static int states[3];   /* 0 not tried, 1 ready, -1 failed; [1] Versus' own (versus\menu_bg.bmp); [2] (2.1) online's (versus\online_bg.bmp) */
        int bi = s_netmenu || (!s_vsmenu && tt_net_coop_flow()) ? 2 : s_vsmenu ? 1 : 0;
        uint32_t blk = blks[bi], obj = objs[bi]; int state = states[bi];
        uint32_t so = G32(T_BG + 0xB8), sd = so >= 0x80000000u && so < 0x84000000u ? G32(so + 0x10) : 0, e;
        if (!state && sd >= 0x80000000u && sd < 0x84000000u && G32(sd + 4) == 512u && G32(sd + 8) == 256u && !getenv("TT_COOP_OLDBG")) {
            extern int tt_coop_menu_bg_pixels(uint32_t *out); extern int tt_vs_image_pixels(const char *file, uint32_t *out, float k); static uint32_t px[512 * 256]; uint32_t k;
            state = -1;
            if ((bi == 2 ? (tt_vs_image_pixels("online_bg.bmp", px, 0.6f) || tt_coop_menu_bg_pixels(px)) : s_vsmenu ? (tt_vs_image_pixels("menu_bg.bmp", px, 0.55f) || tt_coop_menu_bg_pixels(px)) : tt_coop_menu_bg_pixels(px)) && (blk = xbox_ContiguousAlloc(0x80u + sizeof px, 4096u)) != 0 && (obj = xbox_ContiguousAlloc(0x40u, 16u)) != 0) {
                for (k = 0; k < 0x80u; k += 4) G32(blk + k) = G32(sd + k);
                G32(blk + 0x38) = (G32(sd + 0x38) & ~0xFF00u) | 0x0600u;
                G32(blk + 0x3C) = 0;
                G32(blk + 0x30) = (blk + 0x80u) & 0x03FFFFFFu;
                for (k = 0; k < 512u * 256u; k++) G32(blk + 0x80u + 4 * k) = px[k];
                for (k = 0; k < 0x20u; k += 4) G32(obj + k) = G32(so + k);
                G32(obj + 0x10) = blk;
                state = 1;
                tt_dbg("[COOPMENU] background: the three hunters (stills3 key 2), texture %08X, format %08X -> %08X\n", obj, G32(sd + 0x38), G32(blk + 0x38));
            } else tt_dbg("[COOPMENU] background: the still could not be made, the Options one stays\n");
            blks[bi] = blk; objs[bi] = obj; states[bi] = state;
        }
        if (state == 1 && !getenv("TT_COOP_OLDBG") && (e = el_from(T_BG)) != 0) { G32(e + 0xB8) = obj; draw(e); }
        else draw(T_BG);
    }
    if (getenv("TT_COOP_ELLOG")) { static int n; if (!n++) { uint32_t t[4] = { T_LABEL, T_MARK1, T_MARK2, MM_EL(8) }, q; int j;
        for (j = 0; j < 4; j++) { tt_dbg("[COOPEL] %08X:", t[j]); for (q = 0; q < 0x40; q += 4) tt_dbg(" %X:%08X", q, G32(t[j] + q)); tt_dbg("\n"); } } }
    mm_text(CM_TITLEY, 0xFFFFFFFFu, s_netmenu || (!s_vsmenu && tt_net_coop_flow()) ? "Online" : s_vsmenu ? "Versus" : "Co-op");   /* (2.1: the online host's Co-op menu too) */
    if (s_netmenu) { char st[200]; tt_net_lobby_status(st, sizeof st); text_el(T_VALUE, 24, CM_ROWY - 26, 600, 0xFFFFFFFFu, st); }
    for (i = 0; i < cm_ntop(); i++) mm_text(CM_TOPY + i * CM_TOPSTEP, s_sel == i && !s_on_back ? COL_ON : COL_OFF, cm_top(i));
    { int nr; const Row *rr = cm_rows(&nr);
    if (s_vsmenu) vs_fix();
    int rt = cm_rtop(nr), shown = s_netmenu || s_vsmenu ? nr : CM_SHOWN;
    if (rt > 0) text_el(T_VALUE, 560, CM_ROWY - 22, 0, COL_OFF, "...");
    if (rt + shown < nr) text_el(T_VALUE, 560, CM_ROWY + shown * CM_STEP - 6, 0, COL_OFF, "...");
    for (i = rt; i < nr && i < rt + shown; i++) {
        const Row *w = &rr[i];
        int y = CM_ROWY + (i - rt) * CM_STEP, sel = s_sel == i + cm_ntop() && !s_on_back;
        if (w->kind == K_ONLINEPG) { text_el(T_LABEL, 24, y, 0, sel ? COL_ON : COL_OFF, w->label); continue; }
        if (w->kind == K_NET) {
            char v[160]; tt_net_lobby_value(w->gfx, v, sizeof v);
            text_el(T_LABEL, 24, y, 0, sel ? COL_ON : COL_OFF, tt_net_lobby_label(w->gfx));
            text_el(T_VALUE, s_colx[0] - 8, y, 0, COL_OFF, "<");
            text_el(T_VALUE, s_colx[0] + 12, y, 340, sel ? COL_ON : COL_OFF, v);
            text_el(T_VALUE, 585, y, 40, COL_OFF, ">");
            continue;
        }
        text_el(T_LABEL, 24, y, 0, sel ? COL_ON : COL_OFF, w->label);
        if (w->kind == K_VSHERO) {
            text_el(T_VALUE, s_colx[0] - 8, y, 0, COL_OFF, "<");
            text_el(T_VALUE, s_colx[0] + 12, y, 340, sel ? COL_ON : COL_OFF, w->gfx == 8 ? (s_vs_tp3 ? "Yes" : "No") : w->gfx == 9 ? (!s_vs_tp3 ? "-" : s_vs_tp4 ? "Yes" : "No") : w->gfx == 7 ? (s_vs_p4on ? "Yes" : "No (three players)") : w->gfx == 6 && s_vs_mode == 3 && !s_vs_p4on ? "-" : w->gfx == 4 ? s_vs_modes[s_vs_mode] : w->gfx == 3 ? s_vs_goals[s_vs_goal_i] : w->gfx == 2 ? s_vs_arenas[s_vs_arena].name : w->gfx >= 5 ? s_vs_heroes[s_vs_hero[w->gfx - 3]][1] : s_vs_heroes[s_vs_hero[w->gfx]][1]);
            text_el(T_VALUE, 585, y, 40, COL_OFF, ">");
            continue;
        }
        draw_value(w, y, sel);
    } }
    if (!getenv("TT_NOBACKLEAF")) draw_back();
    draw(T_HELPI);
    draw(T_HELPT);
}
/* the line on player 2's controls page: each connected controller and what it presses now, and which is player 2's */
static void draw_pad_line(int y)
{
    static const struct { unsigned short m; const char *n; } bn[] = {
        { 0x1000, "A" }, { 0x2000, "B" }, { 0x4000, "X" }, { 0x8000, "Y" }, { 0x0100, "LB" }, { 0x0200, "RB" },
        { 0x0010, "Start" }, { 0x0020, "Back" }, { 0x0001, "Up" }, { 0x0002, "Down" }, { 0x0004, "Left" }, { 0x0008, "Right" },
        { 0x0040, "LS" }, { 0x0080, "RS" } };
    char line[160], nm[48];
    int k, n = xbox_InputPadCount(), p2 = xbox_InputPlayerPadIndex(s_ctl_player - 1), used = 0;   /* (2.1: the player whose page it is) */
    if (n <= 0) { text_el(T_VALUE, 24, y, 600, COL_OFF, "No controller connected"); return; }
    for (k = 0; k < n && k < 4; k++) {
        unsigned short b = 0; unsigned char lt = 0, rt = 0; short lx = 0, ly = 0, rx = 0, ry = 0; int j, any = 0;
        char pr[64]; int pu = 0;
        pr[0] = 0;
        if (xbox_InputPadPeek(k, &b, &lt, &rt, &lx, &ly, &rx, &ry)) {
            for (j = 0; j < (int)(sizeof bn / sizeof bn[0]); j++) if (b & bn[j].m) { pu += snprintf(pr + pu, sizeof pr - (size_t)pu, "%s%s", any ? " " : "", bn[j].n); any = 1; if (pu > 40) break; }
            if (lt > 30 && pu < 50) { pu += snprintf(pr + pu, sizeof pr - (size_t)pu, "%sLT", any ? " " : ""); any = 1; }
            if (rt > 30 && pu < 50) { pu += snprintf(pr + pu, sizeof pr - (size_t)pu, "%sRT", any ? " " : ""); any = 1; }
            if ((lx > 16000 || lx < -16000 || ly > 16000 || ly < -16000) && pu < 50) { pu += snprintf(pr + pu, sizeof pr - (size_t)pu, "%sL stick", any ? " " : ""); any = 1; }
            if ((rx > 16000 || rx < -16000 || ry > 16000 || ry < -16000) && pu < 50) { pu += snprintf(pr + pu, sizeof pr - (size_t)pu, "%sR stick", any ? " " : ""); any = 1; }
        }
        xbox_InputPadName(k, nm, sizeof nm);
        used += snprintf(line + used, sizeof line - (size_t)used, "%s%d%s: %s", k ? "   " : "", k + 1, k == p2 ? (s_ctl_player == 3 ? " (P3)" : s_ctl_player == 4 ? " (P4)" : " (P2)") : "", any ? pr : "-");
        if (used >= (int)sizeof line - 1) break;
        (void)nm;
    }
    text_el(T_VALUE, 24, y, 600, COL_ON, line);
}
static int row_locked(const Row *w);
static void draw_page(void)
{
    int n, i;
    const Row *r = rows(&n);
    s_nel = 0;
    s_txt = s_mem + MAX_EL * EL_SIZE;
    draw(T_BG);
    if (s_page == PG_HUB) {
        for (i = 0; i < hub_n(); i++)
            text_el(T_LABEL, 200, 150 + i * 60, 300, i == s_hub_sel && !s_on_back ? COL_ON : COL_OFF, hub_label(i));
    } else if (r) {
        if (s_sel < s_top) s_top = s_sel;
        if (s_sel >= s_top + ROW_SHOWN) s_top = s_sel - ROW_SHOWN + 1;
        for (i = s_top; i < n && i < s_top + ROW_SHOWN; i++) {
            const Row *w = &r[i];
            int y = ROW_Y0 + (i - s_top) * ROW_STEP, sel = i == s_sel && !s_on_back;
            text_el(T_LABEL, 24, y, 0, row_locked(w) ? (sel ? 0xFF8A8A80u : 0xFF4C4A46u) : sel ? COL_ON : COL_OFF, w->label);   /* (locked: dim) */
            draw_value(w, y, sel);
        }
        if (s_top > 0) text_el(T_VALUE, 560, 4, 0, COL_OFF, "...");
        if (s_top + ROW_SHOWN < n) text_el(T_VALUE, 560, ROW_Y0 + ROW_SHOWN * ROW_STEP - 10, 0, COL_OFF, "...");
        if (s_page == PG_P2CTL) draw_pad_line(ROW_Y0 + ROW_SHOWN * ROW_STEP + 14);   /* (below the rows) */
    }
    if (!getenv("TT_NOBACKLEAF")) draw_back();
    draw(T_HELPI);
    draw(T_HELPT);
}

/* the Options page's drawing (0x0007A4D0): 1 = ours drawn in its place */
int tt_opt_draw(uint32_t obj)
{
    s_opt_obj = obj;
    /* Options just opened: the first page. (1.9.3: frames gone by without
     * it, not time -- a new full-screen resolution held the picture for
     * longer than 400 ms and the page went back to the first one) */
    if (!s_drawn || s_frames - s_drawn_frame > 3) { s_page = PG_HUB; s_hub_sel = 0; s_exit_frames = 0; s_on_back = 0; }
    if (tt_net_screen_req) { tt_net_screen_req = 0; s_netmenu = 1; s_vsmenu = 0; s_page = PG_COOPMENU; s_sel = 0; s_top = 0; tt_dbg("[NETMENU] the Online menu opened\n"); { extern void xbox_KeyScriptArm(void); xbox_KeyScriptArm(); } { extern void tt_net_heroes_check(void); tt_net_heroes_check(); } }
    if (tt_coop_screen_req) { tt_coop_screen_req = 0; s_netmenu = 0; s_vsmenu = 0; s_page = PG_COOPMENU; s_sel = 0; s_top = 0; tt_dbg("[COOPMENU] the Co-op menu opened\n"); }
    if (tt_vs_screen_req) { tt_vs_screen_req = 0; s_netmenu = 0; s_vsmenu = 1; s_page = PG_COOPMENU; s_sel = 0; s_top = 0; vs_fix(); tt_dbg("[VSMENU] the Versus menu opened\n"); }
    s_drawn = GetTickCount() | 1;
    s_drawn_frame = s_frames;
    if (getenv("TT_NOOPTPAGES")) s_page = PG_GENERAL;
    if (s_page == PG_GENERAL) return 0;
    if (!s_mem) {
        s_mem = xbox_ContiguousAlloc(MEM_SIZE, 4096);
        if (!s_mem) { s_page = PG_GENERAL; return 0; }
    }
    if (s_page == PG_COOPMENU) draw_coopmenu(); else draw_page();
    return 1;
}

/* ---- the controller: ours while our page is up ---- */
typedef struct { uint16_t wButtons; uint8_t bAnalogButtons[8]; int16_t sThumbLX, sThumbLY, sThumbRX, sThumbRY; } XPad;
typedef struct { uint32_t dwPacketNumber; XPad Gamepad; } XState;
enum { B_UP = 1, B_DOWN = 2, B_LEFT = 4, B_RIGHT = 8, B_A = 16, B_B = 32, B_START = 64 };
static volatile uint32_t s_held_acc;   /* what was held since the last frame (the input thread) */
static volatile uint32_t s_held_last;  /* (1.9.3) what the pad's last read held (the input thread) */
static volatile int s_b_block;         /* B took us back to the first page: the game does not see it until let go */
void tt_opt_filter(unsigned long port, XState *st)
{
    {   uint32_t fe = G32(0x4AD050u); if (fe != 5) s_fe_before = fe; }   /* (row_locked: Options opened from play) */
    XPad *p = &st->Gamepad;
    uint32_t h = 0;
    /* (just left by Back) -- (1.9.8) only once set: unset (0), "0 - now" read as later than now once the PC had been up
     * 24.9 days (GetTickCount past 2^31), and every press of every pad was zeroed here, on every screen -- Start and Enter
     * did nothing on the title (a player's PC: 2,161,357,656 ms up) */
    if (s_quiet_until && (int32_t)(s_quiet_until - GetTickCount()) > 0) { memset(p, 0, sizeof *p); s_held_last = 0; return; }
    if (!opt_up()) { s_held_last = 0; return; }
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
    if (net_type_row() >= 0 && (xbox_InputKeyDown(0x08) || xbox_InputKeyDown(0x2E) || GetTickCount() - s_net_type_t < 600u)) {   /* (2.1) typing the address or the port: Backspace
        * takes a character off, it is not Back; and the keys typed don't move -- the letters are the left stick (only the arrows, the d-pad, move) */
        h &= ~(uint32_t)B_B; p->wButtons &= (uint16_t)~0x20u; p->bAnalogButtons[1] = 0;
        if (!xbox_InputKeyDown(0x26) && !xbox_InputKeyDown(0x28)) { h &= ~(uint32_t)(B_UP | B_DOWN); p->sThumbLY = 0; p->wButtons &= (uint16_t)~3u; }
        h &= ~(uint32_t)(B_LEFT | B_RIGHT); p->sThumbLX = 0; p->wButtons &= (uint16_t)~0xCu; }
    if (net_type_row() >= 0 && (xbox_InputKeyDown(0x08) || xbox_InputKeyDown(0x2E))) { h &= ~(uint32_t)B_B; p->wButtons &= (uint16_t)~0x20u; p->bAnalogButtons[1] = 0; }
    { extern int tt_net_joiner_waiting(void);   /* (2.1) a joiner while the host picks the save, the mission and the heroes: no input at all here */
      if (s_netmenu && s_page == PG_COOPMENU && tt_net_joiner_waiting()) { memset(p, 0, sizeof *p); h = 0; } }
    s_held_acc |= h;
    s_held_last = h;
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
    if (s_page == PG_COOPMENU && s_exit_frames > 0) {   /* New / Load chosen: the game leaves Options (B held a few frames) */
        memset(p, 0, sizeof *p); p->bAnalogButtons[1] = 255; s_exit_frames--;
        return;
    }
    {   /* (1.9.8) B or Back on the first page or the Co-op / Versus menu, from any device: every pad the game reads gets
         * it for a moment. The game leaves Options on the port it listens to (the one that started the game or the pause),
         * which after a change of co-op devices could be a device other than the one in hand -- B did nothing, on the keys
         * or the controller (a playtest). TT_OPT_BACKONEPORT: as before */
        static DWORD back_until, back_t; static int one = -1;
        if (one < 0) one = getenv("TT_OPT_BACKONEPORT") != NULL;
        if (!one && (s_page == PG_HUB || s_page == PG_COOPMENU) && !(s_page == PG_COOPMENU && s_exit_frames > 0)) {
            if ((h & B_B) && !s_b_block && GetTickCount() - back_t > 400u) { back_t = GetTickCount(); back_until = (back_t + 70u) | 1u; tt_dbg("[OPTIONS] back from port %lu (the game's port %u): to every pad\n", port, G32(0x1CB8B4u)); }
            if (back_until && (int32_t)(back_until - GetTickCount()) > 0 && !s_b_block) { h |= B_B; p->bAnalogButtons[1] = 255; }
        }
    }
    if (s_page == PG_COOPMENU && (h & B_B) && !s_b_block) {   /* B: back to the main menu, co-op off */
        extern volatile int tt_coop_menu_on;
        if (s_netmenu || tt_net_coop_flow()) { tt_net_lobby_leave(); s_netmenu = 0; }
        if (tt_coop_menu_on && !tt_coop_menu_go) { tt_coop_menu_on = 0; tt_vs_menu_on = 0; tt_vs_start_pending = 0; tt_dbg("[COOPMENU] back: co-op off\n"); }
        if (p->wButtons & 0x20) { p->wButtons &= (uint16_t)~0x20u; p->bAnalogButtons[1] = 255; }
        return;
    }
    if (s_page == PG_HUB && (h & B_B) && !s_b_block) {   /* B on the first page: the game leaves Options (Back as B) */
        /* (with two players the game often missed that B, after a change of devices: a playtest, six presses to leave; left as the Back leaf does) */
        if (xbox_input_two_players && !s_leave_pending && !getenv("TT_OPT_BACKONEPORT")) { tt_dbg("[OPTIONS] B on the first page: leaving\n"); back_activate(); }
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
void tt_opt_move_sound(void) { move_sound(); }
/* (1.9.8) the Back leaf, bottom left on our pages: with the keys, a controller or the mouse. On a page it goes back to the
 * first page (player 2's controls: where they were opened from); on the first page and the Co-op / Versus menu it leaves
 * Options as the game's own B does (0x0007AF8F: its sound, the music faded unless going back to play, the next screen
 * +4 = where it came from +8, state +0x10 = 3) -- without a pad press at all: B could not get out after a change of the
 * co-op devices (a playtest: neither the keys nor the controller) */
static int back_y(void) { int y = (int)G32(T_HELPT + 0xC); return y > 300 && y < 470 ? y : 428; }
static void back_activate(void)
{
    uint32_t o = s_opt_obj;
    s_on_back = 0;
    if (s_page == PG_P2CTL) { s_page = s_ret_page; s_sel = s_ret_sel; s_top = 0; move_sound(); return; }
    if (s_page == PG_ONLINE && s_online_hub) { s_page = PG_HUB; s_online_hub = 0; move_sound(); return; }
    if (s_page == PG_ONLINE) { s_page = PG_COOPMENU; s_netmenu = 1; s_sel = cm_ntop() + NROWS(s_netrows) - 1; s_top = 0; move_sound(); return; }
    if (s_page != PG_HUB && s_page != PG_COOPMENU) { s_page = PG_HUB; s_b_block = 1; move_sound(); return; }
    if (s_page == PG_COOPMENU) return;
    /* the first page: Options is left once every button and the mouse are let go, and the next screen gets no input for
     * a moment (a playtest: the press that chose Back went on to choose on the main menu -- New Game, its difficulty) */
    s_leave_pending = 1; s_leave_t = GetTickCount();
    return;
    if (s_page == PG_COOPMENU) {
        extern volatile int tt_coop_menu_on;
        if (tt_coop_menu_go || s_exit_frames > 0) return;
        if (tt_coop_menu_on) { tt_coop_menu_on = 0; tt_vs_menu_on = 0; tt_vs_start_pending = 0; tt_dbg("[COOPMENU] back (the Back leaf): co-op off\n"); }
    }
    (void)o;
}
static void opt_leave(void)
{
    uint32_t o = s_opt_obj;
    s_leave_pending = 0;
    if (o < 0x10000u || G32(o + 0x10) != 2u) { tt_dbg("[OPTIONS] Back: the screen not ready (state %u)\n", o >= 0x10000u ? G32(o + 0x10) : 0); return; }
    {   uint32_t a[7] = { 0xA91, 0x3F800000u, 0, 0, 0x3F800000u, 0, 0 };
        tt_gcall(0x000C3680u, 0, 7, a); }
    if (G32(o + 8) != 6u) tt_gcall(0x000C35E0u, 0, 0, NULL);
    G32(o + 4) = G32(o + 8); G32(o + 0x10) = 3u; G32(o + 0x14) = 0x3F800000u;
    tt_dbg("[OPTIONS] Back: Options left for screen %u\n", G32(o + 8));
    s_quiet_until = (GetTickCount() + 350u) | 1u;
}
static int back_here(void) { return s_page != PG_COOPMENU && s_page != PG_GENERAL && !getenv("TT_NOBACKLEAF"); }
static void draw_back(void) { if (!back_here()) return; text_el(T_LABEL, 40, back_y(), 0, s_on_back ? COL_ON : COL_OFF, "Back"); }   /* (Versus: the countdown's tick) */

/* the co-op Difficulty and P2 Bot locked during play (the pause's Co-op Options: screens 6 / 8 are play's): set on the main
 * menu's Co-op menu or Options only (the owner asked). Drawn dim; left / right and clicks do nothing. TT_COOP_NOLOCK: off */
static int row_locked(const Row *w)
{
    uint32_t fe = s_fe_before;
    if (getenv("TT_COOP_LOCKLOG")) { static DWORD lt; if (GetTickCount() - lt > 1000) { lt = GetTickCount(); tt_dbg("[OPTIONS] lock check: screen %u, before it %u\n", G32(0x4AD050u), fe); } }
    return G32(0x4AD050u) == 5 && (fe == 6 || fe == 8) && (w->gfx == GFX_COOP + CO_DIFF || w->gfx == GFX_COOP + CO_BOT) && (w->kind == K_CYCLE || w->kind == K_COLS) && !getenv("TT_COOP_NOLOCK");
}
static void change(const Row *w, int d)
{
    int cnt, cur, v;
    if (row_locked(w)) { static DWORD lt; if (GetTickCount() - lt > 1000) { lt = GetTickCount(); tt_dbg("[OPTIONS] %s locked during play\n", w->label); } return; }
    if (w->kind == K_BIND) {   /* left / right: the controller's button for it */
        if (xbox_BindIsButton(w->gfx)) {
            xbox_BindAssignPad(w->gfx, (xbox_BindPad(w->gfx) + d + BIND_PAD_N_) % BIND_PAD_N_);
            move_sound();
        }
        return;
    }
    if (w->kind == K_DEV) { dev_change(w->gfx, d); move_sound(); return; }
    if (w->kind == K_BIND2) {   /* player 2's controller: the button on it for this */
        xbox_BindAssignPadP(s_ctl_player, w->gfx, (xbox_BindPadP(s_ctl_player, w->gfx) + d + BIND_PAD_N_) % BIND_PAD_N_);
        tt_dbg("[OPTIONS] player %d's %s %s\n", s_ctl_player, w->label, xbox_BindPadName(xbox_BindPadP(s_ctl_player, w->gfx)));
        move_sound();
        return;
    }
    if (w->kind == K_RESET || w->kind == K_RESET2 || w->kind == K_PAGE) return;
    cnt = opt_count(w->gfx); cur = opt_get(w->gfx); v = cur + d;
    if (cnt <= 0) return;
    if (w->kind == K_CYCLE) v = (v + cnt) % cnt;
    else if (w->kind == K_COLS && cnt > 3) { if (v < 0) v = 0; if (v > 2) v = 2; }
    if (v < 0 || v >= cnt || v == cur) return;
    opt_set(w->gfx, v);
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
    } else if (w->kind == K_RESET2) {
        xbox_BindResetPadP(s_ctl_player);
        tt_dbg("[OPTIONS] player %d's controls reset\n", s_ctl_player);
        sound(0xA9D, -1.0f);
    } else if (w->kind == K_BIND2) change(w, 1);
    else if (w->kind == K_PAGE) {   /* player 2's controls; B comes back here */
        s_ret_page = s_page; s_ret_sel = s_sel;
        s_page = PG_P2CTL; s_sel = 0; s_top = 0;
        s_ctl_player = w->gfx >= 2 && w->gfx <= 4 ? w->gfx : 2; s_np2ctl = 0;   /* (2.1: its rows made again, for this player) */
        tt_dbg("[OPTIONS] player %d's controls opened\n", s_ctl_player);
        sound(0xA9D, -1.0f);
    } else if (w->kind == K_CYCLE || w->kind == K_DEV) change(w, 1);
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
    s_frames++;
    { extern int tt_net_chat_open(void); if (tt_net_chat_open()) return; }   /* (2.1: the online chat has the keyboard) */
    static uint32_t prev;
    static DWORD rep_t;
    /* (1.9.3) what the pad's last read holds, and anything pressed since the
     * last frame: at a high frame rate most frames see no read, and taken as
     * let go there, every read was a new press -- a held stick or d-pad ran
     * down the page */
    uint32_t now = s_held_acc | s_held_last, edge, rep = 0;
    s_held_acc = 0;
    static int was_up;
    {   /* (2.1) the Online menu left (B, Escape, a click elsewhere, the menu closed): out of the online game too -- hosting or
         * joined, every player back to Host Game / Join Game */
        static int net_was;
        if (s_netmenu && opt_up() && s_page == PG_COOPMENU) net_was = 1;
        else if (net_was && tt_net_coop_flow()) net_was = 0;   /* (the host on his way to the map: still connected) */
        else if (net_was && (s_page == PG_ONLINE || (s_page == PG_P2CTL && s_ret_page == PG_ONLINE)) && opt_up()) ;   /* (its Online Options: still in the online game) */
        else if (net_was) { net_was = 0; s_netmenu = 0; s_sel = 0; if (tt_net_lobby_state()) { tt_dbg("[NETMENU] the Online menu left: out of the online game\n"); tt_net_lobby_leave(); } }
    }
    if (!opt_up()) { prev = 0; s_cap = 0; s_leave_pending = 0; was_up = 0; return; }
    if (!was_up) {   /* (1.9.8) just opened: what is held (A that chose Options on the pause page) is no press here -- it opened
                      * General Options at once, a playtest, until tried a few times */
        was_up = 1; prev = now | B_A | B_START | B_B; return; }
    if (s_leave_pending) { if ((!now && !s_mouse_down) || GetTickCount() - s_leave_t > 2000u) opt_leave(); prev = now; return; }
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
                if (v > hub_n() - 1) v = hub_n() - 1;
                if (v != s_hub_sel) { s_hub_sel = v; move_sound(); }
            } else if (s_page == PG_COOPMENU) {   /* (2.1) the Co-op menu's rows: the view moves, the choice kept in it (on New / Load Game: left there) */
                if (getenv("TT_WHEELLOG")) tt_dbg("[COOPMENU] wheel %ld: rows %d, view at %d\n", (long)dw, n, s_cm_rtop);
                if (!s_netmenu && !s_vsmenu && n > CM_SHOWN) {
                    int t = s_cm_rtop - (int)dw, mx = n - CM_SHOWN, nt = cm_ntop();
                    if (t < 0) t = 0;
                    if (t > mx) t = mx;
                    if (t != s_cm_rtop) {
                        s_cm_rtop = t;
                        if (s_sel >= nt && s_sel - nt < t) s_sel = nt + t;
                        if (s_sel >= nt && s_sel - nt > t + CM_SHOWN - 1) s_sel = nt + t + CM_SHOWN - 1;
                        move_sound();
                    }
                }
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
    if (s_on_back && back_here()) {   /* (1.9.8) the Back leaf chosen */
        if (edge & (B_UP | B_RIGHT)) { s_on_back = 0; move_sound(); }
        else if (edge & (B_A | B_START)) back_activate();
        else if (edge & B_B) { s_on_back = 0; if (s_page != PG_HUB && s_page != PG_COOPMENU) back_activate(); }
        return;
    }
    if (s_page == PG_COOPMENU) {
        if (s_exit_frames > 0 || tt_coop_menu_go || G32(0x4AD050u) != 5) return;
        if (edge & B_UP) { if (s_sel > 0) { s_sel--; move_sound(); } }
        if (edge & B_DOWN) { if (s_sel < CM_N - 1) { s_sel++; move_sound(); } else if (back_here()) { s_on_back = 1; move_sound(); return; } }
        {   int nr, nt = cm_ntop(); const Row *rr = cm_rows(&nr);
            if (s_netmenu) {
                if (s_sel >= nt && (edge & (B_LEFT | B_RIGHT)) && tt_net_lobby_change(rr[s_sel - nt].gfx, (edge & B_LEFT) ? -1 : 1)) move_sound();
                if (s_sel < nt && (edge & (B_A | B_START))) net_top_action(s_sel);
                if (s_sel >= nt && rr[s_sel - nt].kind == K_ONLINEPG && (edge & (B_A | B_START))) { s_page = PG_ONLINE; s_sel = 0; s_top = 0; sound(0xA9D, -1.0f); tt_dbg("[NETMENU] Online Options\n"); return; }
                if (s_sel > CM_N - 1) s_sel = CM_N - 1;
                net_type_keys(s_sel - nt);   /* (2.1) the address and the port typed on their rows */
                return;
            }
            if (s_sel >= nt && (edge & (B_LEFT | B_RIGHT))) {
                const Row *w = &rr[s_sel - nt]; int d = (edge & B_LEFT) ? -1 : 1;
                if (w->kind == K_VSHERO) { vs_hero_change(w->gfx, d); move_sound(); }
                else change(w, d);
            }
            if (s_sel >= nt && (edge & B_A) && rr[s_sel - nt].kind != K_VSHERO) { activate(&rr[s_sel - nt]); return; }   /* (P2 Controls: its page; the press is used up -- going on, it chose New Game on the new page's first row) */
            if (s_sel < nt && (edge & (B_A | B_START))) {
                tt_coop_menu_go = s_sel + 1; s_exit_frames = 8;
                if (s_vsmenu) { tt_vs_start_pending = 1; tt_vs_menu_on = 1; }
                sound(0xA9D, -1.0f);
                tt_dbg("[COOPMENU] %s chosen%s\n", cm_top(s_sel), s_vsmenu ? " (Versus)" : "");
                if (s_vsmenu) tt_dbg("[VSMENU] match: %s, player 1 %s, player 2 %s, arena %s\n", s_vs_modes[s_vs_mode], tt_vs_hero_name(0), tt_vs_hero_name(1), s_vs_arenas[s_vs_arena].name);
                vs_px_picks();
            }
        }
        return;
    }
    if (s_page == PG_GENERAL) {
        if (edge & B_B) { s_page = PG_HUB; s_b_block = 1; move_sound(); }
        return;
    }
    if (s_page == PG_HUB) {
        if (edge & B_UP) { if (s_hub_sel > 0) { s_hub_sel--; move_sound(); } }
        if (edge & B_DOWN) { if (s_hub_sel < hub_n() - 1) { s_hub_sel++; move_sound(); } else if (back_here()) { s_on_back = 1; move_sound(); return; } }
        if (edge & (B_A | B_START)) {
            s_page = hub_page(s_hub_sel); s_online_hub = s_page == PG_ONLINE;
            s_sel = 0; s_top = 0;
            sound(0xA9D, -1.0f);
        }
        return;
    }
    {
        int n;
        const Row *r = rows(&n);
        if (!r) return;
        if (edge & B_B) {
            if (s_page == PG_P2CTL) { s_page = s_ret_page; s_sel = s_ret_sel; s_top = 0; }   /* (back where it was opened: Co-op Options or the Co-op menu) */
            else if (s_page == PG_ONLINE && s_online_hub) { s_page = PG_HUB; s_online_hub = 0; }
            else if (s_page == PG_ONLINE) { s_page = PG_COOPMENU; s_netmenu = 1; s_sel = cm_ntop() + NROWS(s_netrows) - 1; s_top = 0; }   /* (2.1: back to the Online menu) */
            else s_page = PG_HUB;
            s_b_block = 1; move_sound(); return;
        }
        if (edge & B_UP) { if (s_sel > 0) { s_sel--; move_sound(); } }
        if (edge & B_DOWN) { if (s_sel < n - 1) { s_sel++; move_sound(); } else if (back_here()) { s_on_back = 1; move_sound(); return; } }
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
    { extern int tt_net_joiner_waiting(void); if (s_netmenu && s_page == PG_COOPMENU && tt_net_joiner_waiting()) return 1; }   /* (2.1: no input while the host chooses) */
    static int drag = -1;
    int n, i;
    const Row *r;
    s_mouse_down = down;
    if (!ours()) { drag = -1; return 0; }
    if (s_leave_pending) return 1;
    if (getenv("TT_MOUSELOG")) {   /* testing */
        static int lp = -1, lh = -1, ls = -1;
        if (lp != s_page || lh != s_hub_sel || ls != s_sel) { tt_dbg("[OPTM] page %d hub %d sel %d at %.0f,%.0f clk %d\n", s_page, s_hub_sel, s_sel, x, y, clk); lp = s_page; lh = s_hub_sel; ls = s_sel; }
    }
    if (back_here() && !s_cap) {   /* (1.9.8) the Back leaf */
        int over = x >= 20.0f && x < 150.0f && fabsf(y - (float)(back_y() + 15)) < 18.0f;
        if (over && (moved || clk) && !s_on_back) { s_on_back = 1; move_sound(); }
        if (over && clk) { back_activate(); return 1; }
        if (over) return 1;
        if (moved && s_on_back) s_on_back = 0;
    }
    if (s_page == PG_COOPMENU) {
        int hit = -1, k2;
        if (s_exit_frames > 0 || tt_coop_menu_go || G32(0x4AD050u) != 5) return 1;
        int nr, nt = cm_ntop(); const Row *rr = cm_rows(&nr);
        for (i = 0; i < nt; i++) if (x >= 200 && x < 440 && fabsf(y - (CM_TOPY + i * CM_TOPSTEP + 15)) < CM_TOPSTEP * 0.5f) hit = i;
        { int rt = cm_rtop(nr), shown = s_netmenu || s_vsmenu ? nr : CM_SHOWN;   /* (2.1: the rows in view) */
          for (i = rt; i < nr && i < rt + shown; i++) if (x >= 10 && x < 630 && fabsf(y - (CM_ROWY + (i - rt) * CM_STEP + 17)) < CM_STEP * 0.5f) hit = i + nt; }
        if (hit >= 0 && (moved || clk) && hit != s_sel) { s_sel = hit; move_sound(); }
        if (hit >= 0 && clk) {
            if (hit < nt && s_netmenu) { net_top_action(hit); return 1; }   /* (2.1: the Online menu's own -- not the Co-op menu's New / Load Game) */
            if (hit >= nt && s_netmenu && rr[hit - nt].kind == K_ONLINEPG) { s_page = PG_ONLINE; s_sel = 0; s_top = 0; sound(0xA9D, -1.0f); return 1; }
            if (hit >= nt && s_netmenu) { if (tt_net_lobby_change(rr[hit - nt].gfx, x < (s_colx[0] + 600) / 2 ? -1 : 1)) move_sound(); return 1; }
            if (hit < nt) { tt_coop_menu_go = hit + 1; s_exit_frames = 8; if (s_vsmenu) { tt_vs_start_pending = 1; tt_vs_menu_on = 1; vs_px_picks(); } sound(0xA9D, -1.0f); tt_dbg("[COOPMENU] %s chosen (mouse)\n", cm_top(hit)); }
            else { const Row *w = &rr[hit - nt]; int cnt;
                if (w->kind == K_VSHERO) {   /* (1.9.7) the left half of the value (its "<" too) back, the right half (">") on */
                    if (x >= s_colx[0] - 60) { vs_hero_change(w->gfx, x < (s_colx[0] + 600) / 2 ? -1 : 1); move_sound(); } return 1; }
                if (w->kind == K_PAGE) activate(w);
                else if (w->kind == K_DEV) { dev_change(w->gfx, x < (s_colx[0] + 600) / 2 ? -1 : 1); move_sound(); }   /* (2.1: a device row by mouse: left half back, right half on) */
                else if (w->kind == K_CYCLE) { if (x >= s_colx[0] - 8 && x < s_colx[1] - 4) change(w, -1); else if (x >= s_colx[1] - 4 && x < s_colx[2] + 60) change(w, 1); }
                else { cnt = opt_count(w->gfx);
                    for (k2 = 0; k2 < cnt && k2 < 3; k2++) if (x >= s_colx[k2] - 8 && x < s_colx[k2] + 120 && k2 != opt_get(w->gfx)) { opt_set(w->gfx, k2); move_sound(); } } }
        }
        return 1;
    }
    if (s_page == PG_HUB) {
        int hit = -1;
        for (i = 0; i < hub_n(); i++)
            if (x >= 180 && x < 520 && fabsf(y - (150 + i * 60 + 15)) < 30) hit = i;
        if (hit >= 0 && (moved || clk) && hit != s_hub_sel) { s_hub_sel = hit; move_sound(); }
        if (hit >= 0 && clk) {
            s_page = hub_page(hit); s_online_hub = s_page == PG_ONLINE;
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
        int cnt = opt_count(r[drag].gfx), v;
        if (cnt > 1) {
            v = (int)floorf((x - 262.0f) / 317.0f * (cnt - 1) + 0.5f);
            if (v < 0) v = 0;
            if (v > cnt - 1) v = cnt - 1;
            if (v != opt_get(r[drag].gfx)) { opt_set(r[drag].gfx, v); move_sound(); }
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
        if (w->kind == K_RESET || w->kind == K_RESET2) { if (x < 330) activate(w); return 1; }
        if (w->kind == K_PAGE) { activate(w); return 1; }
        if (w->kind == K_BIND2) { if (x >= 290 && x < 530) change(w, x < 410 ? -1 : 1); return 1; }
        if (w->kind == K_DEV) { if (x >= s_colx[0] - 20) change(w, x < s_colx[0] + 6 ? -1 : 1); return 1; }
        cnt = opt_count(w->gfx);
        if (w->kind == K_COLS) {
            for (k = 0; k < cnt && k < 3; k++)
                if (x >= s_colx[k] - 8 && x < s_colx[k] + 120 && k != opt_get(w->gfx)) { opt_set(w->gfx, k); move_sound(); }
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

int tt_coop_setting_replace(void) { return tt_coop_setting(CO_REPLACE); }
