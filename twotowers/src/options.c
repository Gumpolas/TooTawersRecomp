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

enum { PG_HUB, PG_GENERAL, PG_DISPLAY, PG_CONTROLS, PG_COOP, PG_COOPMENU, PG_P2CTL, PG_GAME };
enum { K_COLS, K_CYCLE, K_SLIDER, K_BIND, K_RESET, K_DEV, K_BIND2, K_RESET2, K_PAGE, K_VSHERO };   /* (K_BIND: gfx is the action, input_bind.h; K_DEV: gfx is the player, 0-1; K_BIND2 / K_RESET2: player 2's controller; K_PAGE: A opens player 2's controls; K_VSHERO: a Versus hero, the arena or the rounds) */
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
extern int  xbox_InputPadCount(void);
extern void xbox_InputPadName(int k, char *out, int n);
extern int  xbox_InputPadPeek(int k, unsigned short *buttons, unsigned char *lt, unsigned char *rt, short *lx, short *ly, short *rx, short *ry);
extern int  xbox_InputPlayerPadIndex(int port);
#define P2BTN_N 16   /* the button actions, A to the right stick's click */
static Row s_p2ctl[P2BTN_N + 1];
static int s_np2ctl, s_ret_page = PG_COOP, s_ret_sel;

/* the co-op settings (the Co-op Options page), kept in the ini's [Coop]; the co-op code
 * (twotowers_manual.c) reads them with tt_coop_setting. Testing: TT_COOP_DIFFICULTY,
 * TT_COOP_ICONS, TT_COOP_ZOOMSET set one for the run (the ini is not changed). */
#define GFX_COOP 1000
enum { CO_DIFF, CO_ICONS, CO_ZOOM, CO_TELE, CO_BOT, CO_PICK, CO_IMP, CO_N };   /* (CO_IMP: the solo game's Impossible, 1.9.7) */
static const char *const s_coop_key[CO_N] = { "difficulty", "player_icons", "camera_zoom", "p2_teleport", "p2_bot", "double_pickups", "impossible" };
static const char *const s_coop_env[CO_N] = { "TT_COOP_DIFFICULTY", "TT_COOP_ICONS", "TT_COOP_ZOOMSET", "TT_COOP_TELEPORT", "TT_COOP_P2BOT", "TT_COOP_PICKUPS", "TT_IMPOSSIBLE" };
static const int s_coop_cnt[CO_N] = { 4, 2, 3, 2, 3, 2, 2 }, s_coop_def[CO_N] = { 1, 1, 1, 1, 0, 1, 0 };
static int s_coop_v[CO_N] = { -1, -1, -1, -1, -1, -1, -1 };
static const Row s_coop[] = {
    { "Difficulty:", GFX_COOP + CO_DIFF, K_CYCLE, { "Easy", "Normal", "Expert", "Impossible" } },
    { "Player Icons:", GFX_COOP + CO_ICONS, K_COLS, { "Off", "On" } },
    { "Camera Zoom:", GFX_COOP + CO_ZOOM, K_COLS, { "Off", "Normal", "Wide" } },
    { "P2 Teleport:", GFX_COOP + CO_TELE, K_COLS, { "Off", "On" } },   /* player 2 put beside player 1 when too far (the co-op code's catch-up) */
    { "P2 Device:", 1, K_DEV, { 0 } },   /* (1.9.6's device choice, in place of 1.9.4's P2 Controller) */
    { "P2 Bot:", GFX_COOP + CO_BOT, K_COLS, { "Off", "When Idle", "Always" } },   /* player 2 played by the co-op code (twotowers_manual.c coop_p2bot): when his controller is idle (any input hands it over for 3 s), or always */
    { "P2 Controls", 0, K_PAGE, { 0 } },   /* player 2's own buttons (A: the page) */
};
/* (1.9.6) Options > Co-op Options: the co-op settings, the pickups, each player's device and player 2's controls. The
 * Co-op menu (above) has seven of them, clear of its Help prompt */
static const Row s_coop_page[] = {
    { "Difficulty:", GFX_COOP + CO_DIFF, K_CYCLE, { "Easy", "Normal", "Expert", "Impossible" } },
    { "Player Icons:", GFX_COOP + CO_ICONS, K_COLS, { "Off", "On" } },
    { "Camera Zoom:", GFX_COOP + CO_ZOOM, K_COLS, { "Off", "Normal", "Wide" } },
    { "P2 Teleport:", GFX_COOP + CO_TELE, K_COLS, { "Off", "On" } },
    { "P2 Bot:", GFX_COOP + CO_BOT, K_COLS, { "Off", "When Idle", "Always" } },
    { "Pickups:", GFX_COOP + CO_PICK, K_COLS, { "Normal", "Double" } },   /* Double: a health or ammo pickup can be taken twice (twotowers_manual.c, sub_0008FC60) */
    { "P1 Device:", 0, K_DEV, { 0 } },
    { "P2 Device:", 1, K_DEV, { 0 } },
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
int tt_coop_setting(int i)
{
    if (i < 0 || i >= CO_N) return 0;
    if (s_coop_v[i] < 0) {
        char ini[MAX_PATH];
        const char *e = getenv(s_coop_env[i]);
        int v;
        coop_ini(ini, sizeof ini);
        v = e ? atoi(e) : (int)GetPrivateProfileIntA("Coop", s_coop_key[i], s_coop_def[i], ini);
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
extern volatile int xbox_input_dev_choice[2];
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
}
static void coop_devices_save(void)
{
    char ini[MAX_PATH], v[16];
    coop_ini(ini, sizeof ini);
    snprintf(v, sizeof v, "%d", xbox_input_dev_choice[0]); WritePrivateProfileStringA("Coop", "p1_device", v, ini);
    snprintf(v, sizeof v, "%d", xbox_input_dev_choice[1]); WritePrivateProfileStringA("Coop", "p2_device", v, ini);
    tt_dbg("[OPTIONS] co-op devices: player 1 %d, player 2 %d\n", xbox_input_dev_choice[0], xbox_input_dev_choice[1]);
}
/* each frame (twotowers_manual.c, the controller read): two players' input while a co-op game wants a human player 2 */
void tt_coop_input_sync(void)
{
    int two;
    coop_devices_load();
    two = tt_coop_active() && tt_coop_setting(CO_BOT) != 2 && !getenv("TT_COOP_PAD2SCRIPT");   /* (P2 Bot Off or When Idle: a person may play player 2) */
    if (two != xbox_input_two_players) {
        xbox_input_two_players = two;
        tt_dbg("[COOPDEV] input as %s (devices: player 1 %d, player 2 %d)\n", two ? "two players" : "one player", xbox_input_dev_choice[0], xbox_input_dev_choice[1]);
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
    if (vals[i] > 0 && vals[i] == xbox_input_dev_choice[pl ^ 1]) i = (i + d + nv) % nv;   /* (not the other player's controller) */
    xbox_input_dev_choice[pl] = vals[i];
    if (vals[i] == 0 && xbox_input_dev_choice[pl ^ 1] == 0) xbox_input_dev_choice[pl ^ 1] = -1;   /* (the keyboard is one player's) */
    coop_devices_save();
}

static const char *s_hub[4] = { "General Options", "Display Options", "Controls", "Co-op Options" };   /* (1.9.7: Gameplay Options gone -- Impossible is on General Options' Difficulty and the New Game's) */
/* Co-op Options: on the front end's Options, and the pause menu's only in a co-op game */
extern int tt_coop_active(void);
static int hub_n(void) { uint32_t fe = G32(0x4AD050u); return fe == 6 || fe == 8 ? (tt_coop_active() ? 4 : 3) : 4; }
static int hub_page(int i) { return i == 0 ? PG_GENERAL : i == 1 ? PG_DISPLAY : i == 2 ? PG_CONTROLS : PG_COOP; }
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
#define VS_NHERO 20
static const char *const s_vs_heroes[VS_NHERO][2] = {   /* (name for heroes.c and the co-op, title) */
    { "aragorn", "Aragorn" }, { "legolas", "Legolas" }, { "gimli", "Gimli" }, { "isildur", "Isildur" },
    { "boromir", "Boromir" }, { "gandalf", "Gandalf" }, { "frodo", "Frodo" }, { "lurtz", "Lurtz" },
    { "elrond", "Elrond" }, { "sauron", "Sauron" }, { "uruk", "Uruk-hai" },
    { "elvenwarrior", "Elf Warrior" }, { "elvenarcher", "Elf Archer" }, { "troll", "Cave Troll" },   /* (1.9.9, The128ReBits' six) */
    { "rohan", "Rohan Captain" }, { "urukcap", "Uruk Captain" }, { "gandalfwhite", "Gandalf the White" },
    { "nazgul", "Nazgul" }, { "orc", "Shield Orc" }, { "eomer", "Eomer" } };   /* (the last three: Versus only) */
static int s_vs_goal_i = 1;   /* (1.9.7) rounds to win: 3, 5, 7 */
static const char *const s_vs_goals[3] = { "First to 3", "First to 5", "First to 7" };
int tt_vs_goal_pick(void) { return 3 + 2 * s_vs_goal_i; }
static int s_vs_hero[2] = { 0, 1 }, s_vs_arena, s_vs_mode;   /* mode 0 Deathmatch (the smaller arenas), 1 Find the Shield (the bigger) */
static const char *const s_vs_modes[2] = { "Deathmatch", "Find the Shield" };
/* the arenas: every level but Westfold (the game's level index, its name) */
static const struct { int level; const char *name; } s_vs_arenas[] = {
    { 1, "Weathertop" }, { 3, "Balin's Tomb" }, { 4, "Amon Hen" }, { 5, "Fangorn Forest" }, { 2, "Gates of Moria" },
    { 6, "Plains of Rohan" }, { 8, "Road to Helm's Deep" }, { 9, "The Deeping Wall" }, { 10, "The Breached Wall" },
    { 11, "Hornburg Courtyard" }, { 12, "Orthanc" }, { 13, "Orthanc Summit" }, { 0, "Mount Doom" } };
/* which mode each arena is for: the smaller ones Deathmatch, the bigger (room to run, the shield to find) Find the Shield */
/* (1.9.7) Find the Shield dropped for now: every arena is a Deathmatch arena */
static int vs_arena_mode(int i) { (void)i; return 0; }
#define VS_NARENA ((int)(sizeof s_vs_arenas / sizeof s_vs_arenas[0]))
static const Row s_vsrows[] = {
    { "Player 1:", 0, K_VSHERO, { 0 } },
    { "Player 2:", 1, K_VSHERO, { 0 } },
    { "Arena:", 2, K_VSHERO, { 0 } },   /* (gfx 2: the arena, shown as the heroes are) */
    { "Rounds to win:", 3, K_VSHERO, { 0 } },   /* (1.9.7, gfx 3: first to 3, 5 or 7) */
};
extern int tt_heroes_on(void);
/* who may be which: the extra heroes with the Extra heroes mod on; player 1 on Weathertop as Aragorn or an extra
 * hero (the level has no Legolas, Gimli or Isildur of its own); never both the same */
static int vs_hero_ok(int pl, int h)
{
    if (h < 0 || h >= VS_NHERO) return 0;
    if (h >= 4 && !tt_heroes_on()) return 0;
    if (pl == 0 && s_vs_arenas[s_vs_arena].level == 1 && h >= 1 && h <= 3 && getenv("TT_VS_HEROBYARENA")) return 0;
    return h != s_vs_hero[pl ^ 1];
}
/* (1.9.8) Weathertop has no Legolas, Gimli or Isildur of its own for player 1: those heroes for him take the arena off
 * the list, rather than the arena (the first, so it seemed always) taking them off his (a playtest: "Gimli can be picked
 * only for player 2") */
static int vs_arena_ok(int i) { return !(s_vs_arenas[i].level == 1 && s_vs_hero[0] >= 1 && s_vs_hero[0] <= 3 && !tt_heroes_on()) || getenv("TT_VS_HEROBYARENA"); }   /* (with the Extra heroes mod: he is hero 4 there wearing the pick, heroes.c) */
static void vs_fix(void)
{
    int pl, k;
    for (k = 0; k < VS_NARENA && (vs_arena_mode(s_vs_arena) != s_vs_mode || !vs_arena_ok(s_vs_arena)); k++) s_vs_arena = (s_vs_arena + 1) % VS_NARENA;   /* (an arena of the mode, open to player 1's hero) */
    for (pl = 0; pl < 2; pl++) if (!vs_hero_ok(pl, s_vs_hero[pl])) for (k = 0; k < VS_NHERO; k++) if (vs_hero_ok(pl, k)) { s_vs_hero[pl] = k; break; }
}
static void vs_hero_change(int pl, int d)
{
    int k, h;
    if (pl == 3) { s_vs_goal_i = (s_vs_goal_i + d + 3) % 3; return; }   /* (rounds to win) */
    if (pl == 2) { int k; for (k = 0; k < VS_NARENA; k++) { s_vs_arena = (s_vs_arena + d + VS_NARENA) % VS_NARENA; if (vs_arena_mode(s_vs_arena) == s_vs_mode && vs_arena_ok(s_vs_arena)) break; } vs_fix(); return; }   /* (the arena row: the mode's) */
    h = s_vs_hero[pl];
    for (k = 0; k < VS_NHERO; k++) { h = (h + d + VS_NHERO) % VS_NHERO; if (vs_hero_ok(pl, h)) { s_vs_hero[pl] = h; vs_fix(); return; } }
}
/* the match asked for (twotowers_manual.c at the new game) */
const char *tt_vs_hero_name(int pl) { return s_vs_heroes[s_vs_hero[pl & 1]][0]; }
int tt_vs_mode(void) { const char *e = getenv("TT_VS_MODE"); (void)s_vs_mode; return e ? atoi(e) : 0; }   /* (Deathmatch only for now) */
int tt_vs_arena_level(void) { const char *e = getenv("TT_VS_ARENA"); return e ? atoi(e) : s_vs_arenas[s_vs_arena].level; }   /* (the game's level index, table 0x17B7A4) */
static const Row *cm_rows(int *n) { if (s_vsmenu) { *n = (int)(sizeof s_vsrows / sizeof s_vsrows[0]); return s_vsrows; } *n = (int)(sizeof s_coop / sizeof s_coop[0]); return s_coop; }
static int cm_ntop(void) { return s_vsmenu ? 1 : 2; }
static const char *cm_top(int i) { return s_vsmenu ? "Start Match" : s_cm_top[i]; }   /* (1.9.8: "Start Match", its own words -- "Start" is the button's name in the languages) */
#define CM_TITLEY 22               /* the title, then New Game and Load Game, in the middle at the top, in the main menu's font
                                    * (its table's elements 8..11 at 0x432728: their x, width and font kept) */
#define CM_TOPY 62
#define CM_TOPSTEP 36
#define MM_EL(i) (0x432728u + (uint32_t)(i) * EL_SIZE)
#define CM_ROWY 150
#define CM_STEP 38                 /* (seven rows clear of the Help prompt) */
#define CM_N (cm_ntop() + (s_vsmenu ? (int)(sizeof s_vsrows / sizeof s_vsrows[0]) : (int)(sizeof s_coop / sizeof s_coop[0])))
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
    if (s_page == PG_P2CTL) {
        if (!s_np2ctl) {   /* a row per button (player 2's controller has no keys), then Reset */
            int i;
            for (i = 0; i < P2BTN_N; i++) { Row w = { s_bind_label[i], i, K_BIND2, { 0 } }; s_p2ctl[s_np2ctl++] = w; }
            { Row w = { "Reset P2 Controls", 0, K_RESET2, { 0 } }; s_p2ctl[s_np2ctl++] = w; }
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
int tt_opt_coop_page(void) { return ours() && !s_cap && (s_page == PG_HUB || s_page == PG_COOP || s_page == PG_COOPMENU || s_page == PG_P2CTL); }
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
        snprintf(v, sizeof v, sel ? "< %s >" : "%s", xbox_BindPadName(xbox_BindPad2(w->gfx)));
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
        static uint32_t blks[2], objs[2]; static int states[2];   /* 0 not tried, 1 ready, -1 failed; [1] Versus' own (versus\menu_bg.bmp) */
        uint32_t blk = blks[s_vsmenu ? 1 : 0], obj = objs[s_vsmenu ? 1 : 0]; int state = states[s_vsmenu ? 1 : 0];
        uint32_t so = G32(T_BG + 0xB8), sd = so >= 0x80000000u && so < 0x84000000u ? G32(so + 0x10) : 0, e;
        if (!state && sd >= 0x80000000u && sd < 0x84000000u && G32(sd + 4) == 512u && G32(sd + 8) == 256u && !getenv("TT_COOP_OLDBG")) {
            extern int tt_coop_menu_bg_pixels(uint32_t *out); extern int tt_vs_image_pixels(const char *file, uint32_t *out, float k); static uint32_t px[512 * 256]; uint32_t k;
            state = -1;
            if ((s_vsmenu ? (tt_vs_image_pixels("menu_bg.bmp", px, 0.55f) || tt_coop_menu_bg_pixels(px)) : tt_coop_menu_bg_pixels(px)) && (blk = xbox_ContiguousAlloc(0x80u + sizeof px, 4096u)) != 0 && (obj = xbox_ContiguousAlloc(0x40u, 16u)) != 0) {
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
            blks[s_vsmenu ? 1 : 0] = blk; objs[s_vsmenu ? 1 : 0] = obj; states[s_vsmenu ? 1 : 0] = state;
        }
        if (state == 1 && !getenv("TT_COOP_OLDBG") && (e = el_from(T_BG)) != 0) { G32(e + 0xB8) = obj; draw(e); }
        else draw(T_BG);
    }
    if (getenv("TT_COOP_ELLOG")) { static int n; if (!n++) { uint32_t t[4] = { T_LABEL, T_MARK1, T_MARK2, MM_EL(8) }, q; int j;
        for (j = 0; j < 4; j++) { tt_dbg("[COOPEL] %08X:", t[j]); for (q = 0; q < 0x40; q += 4) tt_dbg(" %X:%08X", q, G32(t[j] + q)); tt_dbg("\n"); } } }
    mm_text(CM_TITLEY, 0xFFFFFFFFu, s_vsmenu ? "Versus" : "Co-op");
    for (i = 0; i < cm_ntop(); i++) mm_text(CM_TOPY + i * CM_TOPSTEP, s_sel == i && !s_on_back ? COL_ON : COL_OFF, cm_top(i));
    { int nr; const Row *rr = cm_rows(&nr);
    if (s_vsmenu) vs_fix();
    for (i = 0; i < nr; i++) {
        const Row *w = &rr[i];
        int y = CM_ROWY + i * CM_STEP, sel = s_sel == i + cm_ntop() && !s_on_back;
        text_el(T_LABEL, 24, y, 0, sel ? COL_ON : COL_OFF, w->label);
        if (w->kind == K_VSHERO) {
            text_el(T_VALUE, s_colx[0] - 8, y, 0, COL_OFF, "<");
            text_el(T_VALUE, s_colx[0] + 12, y, 340, sel ? COL_ON : COL_OFF, w->gfx == 3 ? s_vs_goals[s_vs_goal_i] : w->gfx == 2 ? s_vs_arenas[s_vs_arena].name : s_vs_heroes[s_vs_hero[w->gfx]][1]);
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
    int k, n = xbox_InputPadCount(), p2 = xbox_InputPlayerPadIndex(1), used = 0;
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
        used += snprintf(line + used, sizeof line - (size_t)used, "%s%d%s: %s", k ? "   " : "", k + 1, k == p2 ? " (P2)" : "", any ? pr : "-");
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
            text_el(T_LABEL, 200, 150 + i * 60, 300, i == s_hub_sel && !s_on_back ? COL_ON : COL_OFF, s_hub[i]);
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
    if (tt_coop_screen_req) { tt_coop_screen_req = 0; s_vsmenu = 0; s_page = PG_COOPMENU; s_sel = 0; s_top = 0; tt_dbg("[COOPMENU] the Co-op menu opened\n"); }
    if (tt_vs_screen_req) { tt_vs_screen_req = 0; s_vsmenu = 1; s_page = PG_COOPMENU; s_sel = 0; s_top = 0; vs_fix(); tt_dbg("[VSMENU] the Versus menu opened\n"); }
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
        if (tt_coop_menu_on && !tt_coop_menu_go) { tt_coop_menu_on = 0; tt_vs_menu_on = 0; tt_vs_start_pending = 0; tt_dbg("[COOPMENU] back: co-op off\n"); }
        if (p->wButtons & 0x20) { p->wButtons &= (uint16_t)~0x20u; p->bAnalogButtons[1] = 255; }
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
        xbox_BindAssignPad2(w->gfx, (xbox_BindPad2(w->gfx) + d + BIND_PAD_N_) % BIND_PAD_N_);
        tt_dbg("[OPTIONS] player 2's %s %s\n", w->label, xbox_BindPadName(xbox_BindPad2(w->gfx)));
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
        xbox_BindResetPad2();
        tt_dbg("[OPTIONS] player 2's controls reset\n");
        sound(0xA9D, -1.0f);
    } else if (w->kind == K_BIND2) change(w, 1);
    else if (w->kind == K_PAGE) {   /* player 2's controls; B comes back here */
        s_ret_page = s_page; s_ret_sel = s_sel;
        s_page = PG_P2CTL; s_sel = 0; s_top = 0;
        tt_dbg("[OPTIONS] player 2's controls opened\n");
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
    static uint32_t prev;
    static DWORD rep_t;
    /* (1.9.3) what the pad's last read holds, and anything pressed since the
     * last frame: at a high frame rate most frames see no read, and taken as
     * let go there, every read was a new press -- a held stick or d-pad ran
     * down the page */
    uint32_t now = s_held_acc | s_held_last, edge, rep = 0;
    s_held_acc = 0;
    static int was_up;
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
            s_page = hub_page(s_hub_sel);
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
        for (i = 0; i < nr; i++) if (x >= 10 && x < 630 && fabsf(y - (CM_ROWY + i * CM_STEP + 17)) < CM_STEP * 0.5f) hit = i + nt;
        if (hit >= 0 && (moved || clk) && hit != s_sel) { s_sel = hit; move_sound(); }
        if (hit >= 0 && clk) {
            if (hit < nt) { tt_coop_menu_go = hit + 1; s_exit_frames = 8; if (s_vsmenu) { tt_vs_start_pending = 1; tt_vs_menu_on = 1; } sound(0xA9D, -1.0f); tt_dbg("[COOPMENU] %s chosen (mouse)\n", cm_top(hit)); }
            else { const Row *w = &rr[hit - nt]; int cnt;
                if (w->kind == K_VSHERO) {   /* (1.9.7) the left half of the value (its "<" too) back, the right half (">") on */
                    if (x >= s_colx[0] - 60) { vs_hero_change(w->gfx, x < (s_colx[0] + 600) / 2 ? -1 : 1); move_sound(); } return 1; }
                if (w->kind == K_PAGE) activate(w);
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
            s_page = hub_page(hit);
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
