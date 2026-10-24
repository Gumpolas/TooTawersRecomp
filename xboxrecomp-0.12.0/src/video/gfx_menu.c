/*
 * gfx_menu.c -- the in-game Graphics Settings overlay.
 *
 * F10 on the keyboard, or holding Back and pressing Start on a controller,
 * opens a panel drawn over the game by the Direct3D 11 renderer. While it is
 * open the game sees a controller at rest. Up/down chooses a line, left/right
 * (or A) changes its value, B / Esc / F10 / Start closes. Every change takes
 * effect at once (the 3D view's shape after the next load) and is handed to
 * the title's save handler, which writes the ini.
 *
 * Threads: keys arrive on the window thread (xbox_GfxMenuKey), controller
 * state on the game's input thread (xbox_GfxMenuPad), and the panel is drawn
 * on the pushbuffer thread (xbox_GfxMenuRender). One critical section
 * covers the state; the renderer notices changes by xbox_gfx_rev.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx_menu.h"

#include <windows.h>   /* the real one, or src/platform/posix_include */
#include "input/xinput_xbox.h"
#include "input/input_bind.h"

/* The game's language (set by the game, e.g. its lang.c): the panel's words
 * through xbox_gfxmenu_tr (UTF-8 in, UTF-8 out; NULL: English), drawn with
 * xbox_gfxmenu_face (Windows; NULL: Palatino Linotype), right to left when
 * xbox_gfxmenu_rtl. */
const char *(*xbox_gfxmenu_tr)(const char *en);
const char *xbox_gfxmenu_face;
int xbox_gfxmenu_rtl;
#define TR(s) (xbox_gfxmenu_tr && (s) ? xbox_gfxmenu_tr(s) : (s))

typedef struct {
    const char *label, *key;
    int n;
    const char *names[24];
    const char *vals[24];
    const char *note;
} GfxItem;

static GfxItem s_items[GFX_N] = {
    { "Frame rate", "fps", 12, { "30 fps (original)", "60 fps (experimental)", "75 fps", "90 fps", "100 fps", "120 fps",
      "144 fps", "165 fps", "200 fps", "240 fps", "300 fps", "Unlocked (experimental)" },
      { "30", "60", "75", "90", "100", "120", "144", "165", "200", "240", "300", "unlocked" },
      "Above 30 fps is experimental: some scenes can run faster than normal" },
    { "Display mode", "fullscreen", 3, { "Windowed", "Borderless full screen", "Exclusive full screen" }, { "0", "1", "2" },
      "Alt+Enter or F11 also switches" },
    { "Aspect ratio", "aspect", 5, { "4:3 (original)", "16:9", "16:10", "21:9", "32:9" },
      { "4:3", "16:9", "16:10", "21:9", "32:9" }, "The 3D view widens fully after the next load" },
    { "Internal resolution", "render_scale", 13, { "1x  (640 x 480)", "2x  (1280 x 960)", "3x  (1920 x 1440)", "4x  (2560 x 1920)", "4.5x (2160 lines: 3840 x 2160 at 16:9)",
      "5x  (3200 x 2400)", "6x  (3840 x 2880)", "7x  (4480 x 3360)", "8x  (5120 x 3840)", "9x  (5760 x 4320)", "10x (6400 x 4800)",
      "11x (7040 x 5280)", "12x (7680 x 5760)" },
      { "1", "2", "3", "4", "4.5", "5", "6", "7", "8", "9", "10", "11", "12" },
      "Sharper, but needs a stronger graphics card (9x and up: a lot of video memory)" },
    { "Anisotropic filtering", "anisotropic", 5, { "Off", "2x", "4x", "8x", "16x" }, { "1", "2", "4", "8", "16" }, NULL },
    { "Mipmaps", "mipmaps", 2, { "Off", "On" }, { "0", "1" }, "Less shimmer on distant textures" },
    { "Anti-aliasing", "antialiasing", 7, { "Off", "FXAA", "MSAA 2x", "MSAA 4x", "MSAA 8x", "MSAA 4x + FXAA", "MSAA 8x + FXAA" },
      { "off", "fxaa", "msaa2", "msaa4", "msaa8", "msaa4+fxaa", "msaa8+fxaa" },
      "MSAA smooths the edges of the 3D as it is drawn; FXAA the finished picture" },
    { "VSync", "vsync", 2, { "Off", "On" }, { "0", "1" }, "Waits for the monitor: no tearing" },
    { "Picture", "stretch", 2, { "Keep shape", "Stretch to fill" }, { "0", "1" }, NULL },
    { "Vertex programs", "vertex_programs", 2, { "CPU", "Graphics card" }, { "cpu", "gpu" }, "Graphics card is much faster" },
    { "Brightness", "brightness", 5, { "Darker", "Original", "Brighter", "Brighter +", "Brightest" },
      { "0.85", "1", "1.15", "1.3", "1.45" }, "Lifts the dark areas (gamma)" },
    { "Sharpening", "sharpening", 4, { "Off", "Low", "Medium", "High" }, { "0", "0.4", "0.7", "1" },
      "Crisper textures, best with a higher internal resolution" },
    { "Colour", "colour", 3, { "Original", "Vivid", "Muted" }, { "1", "1.25", "0.8" }, NULL },
    { "Button prompts", "prompts", 5, { "Automatic", "Xbox", "Xbox One", "PlayStation", "Keyboard / mouse" },
      { "auto", "xbox", "xboxone", "playstation", "keyboard" },
      "Automatic: follows what you last played with (pad, PlayStation pad, keys)" },
    { "HUD in widescreen", "hud", 2, { "At the screen's edges", "Centred (4:3)" }, { "edges", "centre" },
      "Where the HUD sits when the picture is wider than 4:3" },
    { "Menus", "menu_fill", 2, { "Fill the screen", "Original (with a margin)" }, { "1", "0" },
      "Menus, loading and upgrade screens: past the old TV margin" },
    { "Movies in widescreen", "movie_fill", 2, { "As made (4:3)", "Fill the width" }, { "0", "1" },
      "Fill: the films' black bands cut off, a little closer" },
    { "Mouse pointer in menus", "menu_mouse", 2, { "On", "Off" }, { "1", "0" },
      "Point and click in the menus (Off: the mouse buttons act as before)" },
    { "Mouse sensitivity", "mouse_sensitivity", 5, { "Low", "Medium", "Normal", "High", "Very high" },
      { "0.5", "0.75", "1", "1.5", "2" }, "The pointer's speed and the mouse aiming's" },
    { "Pointer size", "cursor_size", 4, { "Small", "Normal", "Large", "Extra large" }, { "small", "normal", "large", "xlarge" }, NULL },
    { "Mouse aiming (ranged)", "mouse_aim", 2, { "On", "Off" }, { "1", "0" },
      "Hold the ranged button and move the mouse to aim" },
    { "Mouse aiming: invert", "mouse_invert", 4, { "Off", "Up / down", "Left / right", "Both" }, { "0", "y", "x", "xy" }, NULL },
    { "Sauron's sprint", "sprint", 3, { "Press to toggle", "Hold", "Off" }, { "toggle", "hold", "off" },
      "Extra heroes: Sauron sprints with the left stick click (Shift on the keyboard)" },
    { "Mouse steering (movement)", "mouse_steer", 2, { "Off", "On" }, { "0", "1" },
      "Hold W: he runs the way the mouse pushes (Walk walks)" },
    { "Window size", "window_size", 11, { "Automatic", "1280 x 720", "1280 x 960", "1600 x 900", "1920 x 1080", "1920 x 1200",
      "2560 x 1080", "2560 x 1440", "3440 x 1440", "3840 x 2160", "5120 x 1440" },
      { "", "1280x720", "1280x960", "1600x900", "1920x1080", "1920x1200", "2560x1080", "2560x1440", "3440x1440", "3840x2160", "5120x1440" },
      "Windowed mode" },
    { "Ultra textures", "ultra_textures", 2, { "Off", "On" }, { "0", "1" },
      "The high-resolution texture pack (GLOS69 and textures_ultra beside the game)" },
    { "Widescreen cutscenes", "wide_scenes", 2, { "Off (4:3)", "On" }, { "0", "1" },
      "On: the in-engine scenes widened too (the sky's edges can show)" },
    { "Full-screen resolution", "fs_resolution", 1, { "The desktop's" }, { "" }, "For exclusive full screen" },
    { "Mod: ECTS 2002 HUD", "e3_hud", 2, { "Off", "On" }, { "0", "1" },
      "Portrait, rune orb and EXP readout of the ECTS 2002 demo" },
    { "Mod: Orange health pickups", "e3_pickups", 2, { "Off", "On" }, { "0", "1" },
      "Health pickups are orange, as in pre-release and making-of videos" },
    /* (1.9.7) the More page */
    { "Colour grading", "colour_look", 4, { "None", "Cinema", "Rich", "Shadow" }, { "original", "film", "vivid", "dark" },
      "Cinema: warm lights, cool shadows. Rich: deeper colour. Shadow: colder and darker" },
    { "Grading amount", "look_strength", 4, { "25%", "50%", "75%", "Full" }, { "0.25", "0.5", "0.75", "1" }, "How much of the colour grading is used" },
    { "HDR", "hdr", 2, { "Off", "On" }, { "0", "1" }, "For HDR screens with Windows HDR on: fire, sky and lights shine brighter" },
    { "HDR paper white", "hdr_paper", 5, { "160 nits", "200 nits", "250 nits", "300 nits", "400 nits" }, { "160", "200", "250", "300", "400" },
      "How bright normal scenes and the HUD are on an HDR screen" },
    { "Distant texture detail", "lod_bias", 4, { "Original", "More", "Most", "Less" }, { "0", "-0.5", "-1", "0.5" },
      "More keeps finer texture detail further away (it can shimmer)" },
    { "Movie sharpness", "movie_sharpening", 4, { "Original", "Low", "Medium", "High" }, { "0", "0.4", "0.7", "1" },
      "Crisper edges in the 640 x 480 films" },
    { "Hero damage", "damage_taken", 8, { "25%", "50%", "75%", "Original", "125%", "150%", "200%", "300%" },
      { "25", "50", "75", "100", "125", "150", "200", "300" }, "How hard enemies hit your heroes (on top of the difficulty)" },
    { "Foe damage", "enemy_damage", 8, { "25%", "50%", "75%", "Original", "125%", "150%", "200%", "300%" },
      { "25", "50", "75", "100", "125", "150", "200", "300" }, "How hard your heroes hit enemies" },
    { "Rumble strength", "vibration", 5, { "Off", "25%", "50%", "75%", "100%" }, { "0", "25", "50", "75", "100" }, "Controller rumble strength" },
    { "Stick dead zone", "stick_deadzone", 7, { "Off", "5%", "10%", "15%", "20%", "25%", "30%" }, { "0", "5", "10", "15", "20", "25", "30" },
      "How far a stick must move before it counts; raise it if your hero drifts" },
    { "Trigger pull needed", "trigger_threshold", 6, { "As the game", "10%", "20%", "30%", "40%", "50%" }, { "0", "10", "20", "30", "40", "50" },
      "How far the triggers must be pulled to count as pressed" },
    { "Pause when unfocused", "pause_unfocused", 2, { "Off", "On" }, { "0", "1" }, "The game stops while another window is in front" },
    { "Mute when unfocused", "mute_unfocused", 2, { "Off", "On" }, { "0", "1" }, "No sound while another window is in front" },
    { "Achievements", "achievements", 2, { "Off", "On" }, { "0", "1" }, "Off: none are earned and no notes appear (the list is kept)" },
};
/* the More page's defaults (index into each item's values) */
static const int s_more_def[GFX_N - GFX_MORE0] = { 0, 3, 0, 1, 0, 0, 3, 3, 4, 2, 0, 0, 0, 1 };
/* (1.9.7 release) settings taken off the More page: kept at their defaults (HDR off, original texture distance, original
 * damage both ways, the game's own trigger pull), whatever an ini says */
static int more_forced(int k) { return k == GFX_HDR || k == GFX_HDRPAPER || k == GFX_LOD || k == GFX_DMGTAKEN || k == GFX_DMGENEMY || k == GFX_TRIGGER; }
/* (also off the page: the stick dead zone -- still read from the ini, [Input] stick_deadzone) */
static int more_hidden(int k) { return more_forced(k) || k == GFX_DEADZONE; }
static int more_n(void) { int k, n = 0; for (k = GFX_MORE0; k < GFX_N; k++) n += !more_hidden(k); return n; }
static int more_item(int row) { int k, r = 0; for (k = GFX_MORE0; k < GFX_N; k++) if (!more_hidden(k) && ++r == row) return k; return GFX_MORE0; }

static int s_cur[GFX_N];
static char s_aspect_custom[32], s_aspect_name[48];
static float s_aspect_custom_v;
static CRITICAL_SECTION s_lock;
static volatile LONG s_inited;
static volatile int s_open, s_sel, s_avail, s_block_pad;
static unsigned s_ver = 1;
volatile long xbox_gfx_rev = 1;
/* Set by the title: 1 while a front-end screen (a menu) is up, 0 in play, -1 unknown.
 * The renderers' menu fill zooms such frames even when they draw 3D. */
volatile int xbox_frontend_hint = -1;
static void (*s_fps_fn)(int);
static void (*s_save_fn)(const char *, const char *);

extern void xbox_FramebufferSetFullscreen(int on);
extern int xbox_FramebufferIsFullscreen(void);

static int find_val(int item, const char *v)
{
    int i;
    for (i = 0; i < s_items[item].n; i++)
        if (!_stricmp(s_items[item].vals[i], v)) return i;
    return -1;
}

/* (1.9.3) exclusive full screen's resolutions: the monitor's own modes
 * (32-bit), largest first, each at its highest refresh rate */
static char s_fs_name[24][32], s_fs_val[24][16];
static void fs_modes_list(void)
{
#if defined(_WIN32)
    DEVMODEA dm;
    int i, n = 1, k, j;
    struct { int w, h; } m[64];
    int nm = 0;
    memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
    for (i = 0; EnumDisplaySettingsA(NULL, (DWORD)i, &dm) && i < 4000; i++) {
        int w = (int)dm.dmPelsWidth, h = (int)dm.dmPelsHeight, seen = 0;
        if (dm.dmBitsPerPel != 32 || w < 640 || h < 480) continue;
        for (k = 0; k < nm; k++) if (m[k].w == w && m[k].h == h) seen = 1;
        if (!seen && nm < 64) { m[nm].w = w; m[nm].h = h; nm++; }
    }
    for (k = 0; k < nm; k++) for (j = k + 1; j < nm; j++)
        if ((long)m[j].w * m[j].h > (long)m[k].w * m[k].h || ((long)m[j].w * m[j].h == (long)m[k].w * m[k].h && m[j].w > m[k].w)) {
            int tw = m[k].w, th = m[k].h; m[k] = m[j]; m[j].w = tw; m[j].h = th;
        }
    {   /* (1.9.3) 23 at most: the screen's own (the largest), the common
         * ones it offers (720p and below included -- before, a high-
         * resolution laptop's list held only its 15 largest), then the
         * largest of the rest; largest first */
        static const int pref[][2] = { {3840,2160}, {2560,1600}, {2560,1440}, {1920,1200}, {1920,1080}, {1680,1050}, {1600,900},
                                       {1440,900}, {1366,768}, {1280,1024}, {1280,800}, {1280,720}, {1024,768}, {960,540}, {800,600}, {640,480} };
        int keep[64] = { 0 }, nk = 0, q;
        if (nm > 0) { keep[0] = 1; nk = 1; }
        for (q = 0; q < (int)(sizeof pref / sizeof pref[0]); q++)
            for (k = 0; k < nm; k++) if (!keep[k] && m[k].w == pref[q][0] && m[k].h == pref[q][1] && nk < 23) { keep[k] = 1; nk++; }
        for (k = 0; k < nm && nk < 23; k++) if (!keep[k]) { keep[k] = 1; nk++; }
        for (k = 0, j = 0; k < nm; k++) if (keep[k]) m[j++] = m[k];
        nm = j;
        /* shown smallest first, as the window sizes: right is a higher resolution */
        for (k = 0; k < nm / 2; k++) { int tw = m[k].w, th = m[k].h; m[k] = m[nm - 1 - k]; m[nm - 1 - k].w = tw; m[nm - 1 - k].h = th; }
    }
    for (k = 0; k < nm && n < 24; k++) {
        snprintf(s_fs_name[n], sizeof s_fs_name[n], "%d x %d", m[k].w, m[k].h);
        snprintf(s_fs_val[n], sizeof s_fs_val[n], "%dx%d", m[k].w, m[k].h);
        s_items[GFX_FS_RES].names[n] = s_fs_name[n];
        s_items[GFX_FS_RES].vals[n] = s_fs_val[n];
        n++;
    }
    s_items[GFX_FS_RES].n = n;
#endif
}
static void gfx_init(void)
{
    const char *e;
    int i;
    if (s_inited) return;
    {
        static volatile LONG once;
        if (InterlockedExchange(&once, 1)) { while (!s_inited) Sleep(0); return; }
    }
    InitializeCriticalSection(&s_lock);
    e = getenv("TT_FPS");
    s_cur[GFX_FPS] = 0;
    if (e && *e) {   /* (a number from the list; unlocked -- and the old "refresh" -- the last) */
        int f = find_val(GFX_FPS, e);
        if (f >= 0) s_cur[GFX_FPS] = f;
        else if (e[0] == 'u' || e[0] == 'U' || e[0] == 'r' || e[0] == 'R') s_cur[GFX_FPS] = s_items[GFX_FPS].n - 1;
        else if (e[0] == '6') s_cur[GFX_FPS] = 1;
    }
    e = getenv("RECOMP_FULLSCREEN");
    s_cur[GFX_FULLSCREEN] = e && e[0] == '2' ? 2 : e && e[0] == '1' ? 1 : 0;
    fs_modes_list();
    e = getenv("RECOMP_FS_RES");
    s_cur[GFX_FS_RES] = 0;
    if (e && *e && (i = find_val(GFX_FS_RES, e)) > 0) s_cur[GFX_FS_RES] = i;
    e = getenv("RECOMP_ASPECT");
    {
        float a = e ? (float)atof(e) : 4.0f / 3.0f;
        static const float k[5] = { 4.0f / 3.0f, 16.0f / 9.0f, 16.0f / 10.0f, 21.0f / 9.0f, 32.0f / 9.0f };
        s_cur[GFX_ASPECT] = -1;
        for (i = 0; i < 5; i++)
            if (a > k[i] - 0.01f && a < k[i] + 0.01f) s_cur[GFX_ASPECT] = i;
        if (s_cur[GFX_ASPECT] < 0) {
            /* a shape typed into the ini (e.g. 2.37): kept as a sixth choice */
            snprintf(s_aspect_custom, sizeof s_aspect_custom, "%.4g", a);
            snprintf(s_aspect_name, sizeof s_aspect_name, "Custom (%.2f:1)", a);
            s_aspect_custom_v = a;
            s_items[GFX_ASPECT].names[5] = s_aspect_name;
            s_items[GFX_ASPECT].vals[5] = s_aspect_custom;
            s_items[GFX_ASPECT].n = 6;
            s_cur[GFX_ASPECT] = 5;
        }
    }
    e = getenv("RECOMP_RENDER_SCALE");
    {   /* (4.5: 2160 lines, 3840 x 2160 at 16:9) */
        float f = e ? (float)atof(e) : 1.0f;
        s_cur[GFX_SCALE] = f >= 11.5f ? 12 : f >= 10.5f ? 11 : f >= 9.5f ? 10 : f >= 8.5f ? 9 : f >= 7.5f ? 8 : f >= 6.5f ? 7 : f >= 5.5f ? 6
                         : f >= 4.75f ? 5 : f >= 4.25f ? 4 : f >= 3.5f ? 3 : f >= 2.5f ? 2 : f >= 1.5f ? 1 : 0;
    }
    e = getenv("RECOMP_WINDOW_SIZE");
    {   /* the window size: one of the list, or Automatic */
        int k;
        s_cur[GFX_WINSIZE] = 0;
        for (k = 1; e && k < s_items[GFX_WINSIZE].n; k++) if (!_stricmp(e, s_items[GFX_WINSIZE].vals[k])) s_cur[GFX_WINSIZE] = k;
    }
    e = getenv("RECOMP_ANISO");
    i = e ? atoi(e) : 1;
    s_cur[GFX_ANISO] = i >= 16 ? 4 : i >= 8 ? 3 : i >= 4 ? 2 : i >= 2 ? 1 : 0;
    e = getenv("RECOMP_MIPMAPS");
    s_cur[GFX_MIPS] = !(e && e[0] == '0');
    e = getenv("RECOMP_AA");
    s_cur[GFX_AA] = 0;
    if (e && (i = find_val(GFX_AA, e)) >= 0) s_cur[GFX_AA] = i;
    else if (e && (e[0] == 'f' || e[0] == 'F')) s_cur[GFX_AA] = 1;
    e = getenv("RECOMP_VSYNC");
    s_cur[GFX_VSYNC] = e && e[0] == '1';
    s_cur[GFX_STRETCH] = getenv("RECOMP_PRESENT_STRETCH") != NULL;
    e = getenv("RECOMP_HW_VP");
    s_cur[GFX_VP] = !(e && e[0] == '0');
    {
        static const struct { int item; const char *env; } fl[] = {
            { GFX_BRIGHT, "RECOMP_BRIGHTNESS" }, { GFX_SHARP, "RECOMP_SHARPEN" }, { GFX_COLOR, "RECOMP_SATURATION" },
            { GFX_E3HUD, "TT_E3HUD" }, { GFX_E3PICKUPS, "TT_E3PICKUPS" }, { GFX_PROMPTS, "TT_PROMPTS" },
            { GFX_HUDPOS, "RECOMP_HUD_ANCHOR" }, { GFX_MENUFILL, "RECOMP_MENU_FILL" }, { GFX_MOVIEFILL, "RECOMP_MOVIE_FILL" },
            { GFX_MOUSE, "TT_MENU_MOUSE" }, { GFX_MOUSE_SENS, "TT_MOUSE_SENS" }, { GFX_CURSOR_SIZE, "TT_CURSOR_SIZE" },
            { GFX_MOUSE_AIM, "TT_MOUSE_AIM" }, { GFX_MOUSE_INVERT, "TT_MOUSE_INVERT" }, { GFX_SPRINT, "TT_SPRINT" }, { GFX_MOUSE_STEER, "TT_MOUSE_STEER" }, { GFX_ULTRA, "TT_ULTRA_TEX" }, { GFX_WIDE_SCENES, "RECOMP_WIDE_SCENES" } };
        int k;
        s_cur[GFX_BRIGHT] = 1;
        s_cur[GFX_MOUSE_SENS] = 2;     /* Normal */
        s_cur[GFX_CURSOR_SIZE] = 1;    /* Normal */
        for (k = 0; k < (int)(sizeof fl / sizeof fl[0]); k++) {
            e = getenv(fl[k].env);
            if (e && (i = find_val(fl[k].item, e)) >= 0) s_cur[fl[k].item] = i;
        }
    }
    {   /* (1.9.7) the More page: RECOMP_OPT_<key> (the title sets them from its ini), else the defaults */
        int k, j; char nm[64];
        for (k = GFX_MORE0; k < GFX_N; k++) {
            s_cur[k] = s_more_def[k - GFX_MORE0];
            snprintf(nm, sizeof nm, "RECOMP_OPT_%s", s_items[k].key);
            for (j = 11; nm[j]; j++) if (nm[j] >= 'a' && nm[j] <= 'z') nm[j] = (char)(nm[j] - 32);
            e = getenv(nm);
            if (e && (i = find_val(k, e)) >= 0 && !more_forced(k)) s_cur[k] = i;
        }
        if (!getenv("RECOMP_OPT_STICK_DEADZONE") && (e = getenv("RECOMP_STICK_DEADZONE")) && (i = find_val(GFX_DEADZONE, e)) >= 0) s_cur[GFX_DEADZONE] = i;
    }
    InterlockedExchange(&s_inited, 1);
}

/* the internal resolution's scale: 1, 2, 3, 4 or 4.5 */
float xbox_GfxScaleF(void)
{
    gfx_init();
    static const float v[13] = { 1, 2, 3, 4, 4.5f, 5, 6, 7, 8, 9, 10, 11, 12 };
    return s_cur[GFX_SCALE] >= 0 && s_cur[GFX_SCALE] < 13 ? v[s_cur[GFX_SCALE]] : 1.0f;
}
/* (1.9) anti-aliasing: MSAA's samples (1: none) and FXAA on the finished picture */
int xbox_GfxMsaa(void)
{
    static const int v[7] = { 1, 1, 2, 4, 8, 4, 8 };
    gfx_init();
    return s_cur[GFX_AA] >= 0 && s_cur[GFX_AA] < 7 ? v[s_cur[GFX_AA]] : 1;
}
int xbox_GfxFxaa(void)
{
    gfx_init();
    return s_cur[GFX_AA] == 1 || s_cur[GFX_AA] == 5 || s_cur[GFX_AA] == 6;
}
int xbox_GfxGet(int id)
{
    gfx_init();
    if (id < 0 || id >= GFX_N) return 0;
    switch (id) {
    case GFX_SCALE: return s_cur[id] + 1;
    case GFX_ANISO: return atoi(s_items[id].vals[s_cur[id]]);
    case GFX_FULLSCREEN: return xbox_FramebufferIsFullscreen();
    default: return s_cur[id];
    }
}

float xbox_GfxGetF(int id)
{
    gfx_init();
    if (id < 0 || id >= GFX_N) return 0;
    return (float)atof(s_items[id].vals[s_cur[id]]);
}

float xbox_GfxAspect(void)
{
    static const float k[5] = { 4.0f / 3.0f, 16.0f / 9.0f, 16.0f / 10.0f, 21.0f / 9.0f, 32.0f / 9.0f };
    int i;
    gfx_init();
    i = s_cur[GFX_ASPECT];
    return i < 5 ? k[i] : s_aspect_custom_v;
}

void xbox_GfxFsMode(int *w, int *h)
{
    gfx_init();
    *w = *h = 0;
    if (s_cur[GFX_FS_RES] > 0) sscanf(s_items[GFX_FS_RES].vals[s_cur[GFX_FS_RES]], "%dx%d", w, h);
}
void xbox_GfxSetFpsHandler(void (*fn)(int mode)) { s_fps_fn = fn; }
void xbox_GfxSetSaveHandler(void (*fn)(const char *key, const char *value)) { s_save_fn = fn; }
void xbox_GfxMenuSetAvailable(int on) { s_avail = on; }
int xbox_GfxMenuIsOpen(void) { return s_open; }

static void gfx_set(int item, int v)
{
    const GfxItem *it = &s_items[item];
    if (v < 0) v = it->n - 1;
    if (v >= it->n) v = 0;
    if (item == GFX_FULLSCREEN) {
        xbox_FramebufferSetFullscreen(v);
    } else {
        if (v == s_cur[item]) return;
        s_cur[item] = v;
        if (item == GFX_FPS && s_fps_fn) s_fps_fn(v);
        if (item == GFX_WINSIZE && v > 0) {
            extern void xbox_FramebufferSetWindowSize(int w, int h);
            int ww = 0, wh = 0;
            if (sscanf(it->vals[v], "%dx%d", &ww, &wh) == 2) xbox_FramebufferSetWindowSize(ww, wh);
        }
        if (item == GFX_FS_RES) {
            extern void xbox_FramebufferFsModeChanged(void);
            xbox_FramebufferFsModeChanged();
        }
    }
    if (item == GFX_FULLSCREEN) s_cur[item] = v;
    s_ver++;
    InterlockedIncrement((volatile LONG *)&xbox_gfx_rev);
    if (s_save_fn) s_save_fn(it->key, it->vals[v]);
    fprintf(stderr, "  [GFXMENU] %s = %s\n", it->label, it->names[v]);
    fflush(stderr);
}

/* For a title's own settings pages: each option's choices, and setting one
 * as the F10 menu does (saved, applied). */
/* (1.9.3) a setting's id from its ini key (-1: none) */
int xbox_GfxFind(const char *key)
{
    int i;
    for (i = 0; i < GFX_N; i++) if (s_items[i].key && !strcmp(s_items[i].key, key)) return i;
    return -1;
}
int xbox_GfxItemCount(int id) { gfx_init(); return id >= 0 && id < GFX_N ? s_items[id].n : 0; }
const char *xbox_GfxItemName(int id, int v)
{
    gfx_init();
    if (id < 0 || id >= GFX_N || v < 0 || v >= s_items[id].n) return "";
    if (id == GFX_ASPECT && v == 5) return s_aspect_name;
    return s_items[id].names[v];
}
int xbox_GfxIndex(int id)
{
    gfx_init();
    if (id < 0 || id >= GFX_N) return 0;
    if (id == GFX_FULLSCREEN) return xbox_FramebufferIsFullscreen();
    return s_cur[id];
}
void xbox_GfxSetIndex(int id, int v)
{
    gfx_init();
    if (id < 0 || id >= GFX_N) return;
    EnterCriticalSection(&s_lock);
    gfx_set(id, v);
    LeaveCriticalSection(&s_lock);
}

void xbox_GfxMenuToggle(void)
{
    gfx_init();
    if (!s_open && !s_avail) {
        static int said;
        if (!said++) { fprintf(stderr, "  [GFXMENU] needs the Direct3D 11 renderer\n"); fflush(stderr); }
        return;
    }
    EnterCriticalSection(&s_lock);
    s_open = !s_open;
    s_cur[GFX_FULLSCREEN] = xbox_FramebufferIsFullscreen();
    /* Whatever closed it (Esc is also Start, B is B) must not reach the
     * game: the pad stays at rest until everything is let go. */
    s_block_pad = 1;
    s_ver++;
    LeaveCriticalSection(&s_lock);
    fprintf(stderr, "  [GFXMENU] %s\n", s_open ? "open" : "closed");
    fflush(stderr);
}

/* Pages: 0 = Graphics, 1 = Controls. Row 0 of each page picks the page.
 * Graphics rows 1..GFX_MENU_N are the options; Controls rows 1..BIND_N are the
 * actions and BIND_N + 1 resets every binding. */
static int s_page, s_row[4], s_top;          /* s_top: first action row shown */
/* (1.9.7) page 2: the title's achievements (xbox_GfxSetAchievements), a list to look through */
static int (*s_ach_n)(void), (*s_ach_got)(int);
static const char *(*s_ach_name)(int), *(*s_ach_desc)(int);
static int s_atop;
#define ACH_ROWS 11
void xbox_GfxSetAchievements(int (*count)(void), const char *(*name)(int), const char *(*desc)(int), int (*earned)(int))
{ s_ach_n = count; s_ach_name = name; s_ach_desc = desc; s_ach_got = earned; }
static int n_pages(void) { return s_ach_n && s_ach_n() > 0 ? 4 : 3; }
static const char *page_name(int p) { return p == 0 ? "Graphics" : p == 1 ? "Controls" : p == 2 ? "Achievements" : "More"; }
/* (1.9.7) the pages in order: Graphics, More, Controls, Achievements (ids 0, 3, 1, 2) */
static int page_next(int p, int d)
{
    static const int ord[4] = { 0, 3, 1, 2 };
    int n = n_pages(), i, k = 0;
    for (i = 0; i < 4; i++) if (ord[i] == p) k = i;
    if (k >= n) k = 0;
    k = (k + d % n + n) % n;
    return ord[k];
}
static int s_capture;                        /* capturing a key for action s_row[1]-1 */
static unsigned char s_cap_held[256];
#define CTRL_ROWS 9                          /* action rows visible at once */
extern int xbox_InputKeyDown(int vk);

/* (1.9.1) Sauron's sprint is gone: its item stays (the numbers after it
 * keep their places) but no row shows it */
static int row_item(int row) { int i = row - 1; return i >= GFX_SPRINT ? i + 1 : i; }
static int page_rows(int p) { return p == 0 ? GFX_MENU_N : p == 1 ? BIND_N + 2 : p == 3 ? 1 + more_n() : 1 + (s_ach_n ? s_ach_n() : 0); }

static void gfx_nav(int dy, int dx)
{
    int *row = &s_row[s_page];
    EnterCriticalSection(&s_lock);
    if (dy) {
        *row = (*row + dy + page_rows(s_page)) % page_rows(s_page);
        s_ver++;
    }
    if (dx) {
        if (*row == 0) { s_page = page_next(s_page, dx); s_ver++; }
        else if (s_page == 0) gfx_set(row_item(*row), s_cur[row_item(*row)] + dx);
        else if (s_page == 3) gfx_set(more_item(*row), s_cur[more_item(*row)] + dx);
        else if (s_page == 2) ;
        else if (*row <= BIND_N && xbox_BindIsButton(*row - 1)) {
            /* left/right: which button of your controller gives this action */
            int src = (xbox_BindPad(*row - 1) + dx + BIND_PAD_N) % BIND_PAD_N;
            xbox_BindAssignPad(*row - 1, src);
            s_ver++;
        }
    }
    LeaveCriticalSection(&s_lock);
}

/* A / Enter on a Controls row: start waiting for a key (or reset all). */
static void gfx_activate(void)
{
    int row = s_row[s_page], vk;
    EnterCriticalSection(&s_lock);
    if (s_page == 0) {
        if (row > 0) gfx_set(row_item(row), s_cur[row_item(row)] + 1);
        else s_page = page_next(s_page, 1);
    } else if (s_page == 3) {
        if (row > 0) gfx_set(more_item(row), s_cur[more_item(row)] + 1);
        else s_page = page_next(s_page, 1);
    } else if (s_page == 2) {
        if (row == 0) s_page = page_next(s_page, 1);
    } else if (row == 0) {
        s_page = page_next(s_page, 1);
    } else if (row == BIND_N + 1) {
        xbox_BindResetAll();
        fprintf(stderr, "  [GFXMENU] controls reset to the defaults\n");
    } else {
        s_capture = 1;
        /* keys already down (the one that started this) do not count */
        for (vk = 1; vk < 256; vk++) s_cap_held[vk] = (unsigned char)xbox_InputKeyDown(vk);
    }
    s_ver++;
    LeaveCriticalSection(&s_lock);
}

/* While capturing: the first key or mouse button that goes down is bound
 * (Esc cancels, Delete clears the action's keys). */
static void gfx_capture_poll(void)
{
    int vk, a = s_row[1] - 1;
    for (vk = 1; vk < 256; vk++) {
        int d = xbox_InputKeyDown(vk);
        if (d && !s_cap_held[vk]) {
            if (vk == VK_F10 || vk == VK_F11 || vk == VK_LWIN || vk == VK_RWIN) continue;
            /* generic and left/right variants both report: keep the generic one */
            if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_LCONTROL || vk == VK_RCONTROL
                || vk == VK_LMENU || vk == VK_RMENU) continue;
            EnterCriticalSection(&s_lock);
            if (vk == VK_ESCAPE) ;
            else if (vk == VK_DELETE) xbox_BindClearKeys(a);
            else {
                xbox_BindAssignKey(a, vk);
                fprintf(stderr, "  [GFXMENU] %s: %s\n", xbox_BindActionName(a), xbox_BindKeyName(vk));
            }
            s_capture = 2;            /* done; wait for every key to be let go */
            s_ver++;
            LeaveCriticalSection(&s_lock);
            return;
        }
        s_cap_held[vk] = (unsigned char)d;
    }
}

/* Keys reach the menu through the controller stand-in (arrows = d-pad,
 * WASD = stick, Esc / Enter = Start, Space / K = A, L = B), the same path as a
 * pad; only F10 is taken straight from the window. */
int xbox_GfxMenuKey(int vk)
{
    if (vk == VK_F10) {
        if (s_capture) { s_capture = 0; s_ver++; return 1; }
        xbox_GfxMenuToggle();
        return 1;
    }
    return 0;
}

void xbox_GfxMenuPad(XBOX_INPUT_STATE *st)
{
    static WORD prev;
    static DWORD rep_at;
    WORD b = st->Gamepad.wButtons, nb = 0;
    DWORD now = GetTickCount();
    int lx = st->Gamepad.sThumbLX, ly = st->Gamepad.sThumbLY, a = st->Gamepad.bAnalogButtons[XBOX_BUTTON_A] > 64,
        bb = st->Gamepad.bAnalogButtons[XBOX_BUTTON_B] > 64;
    gfx_init();
    {   /* testing: RECOMP_GFXMENU_OPEN_AT=ms (from the first pad read) opens it on page RECOMP_GFXMENU_PAGE */
        static int done; static DWORD t0; const char *e = getenv("RECOMP_GFXMENU_OPEN_AT");
        if (e && !done) { if (!t0) t0 = now; if (now - t0 >= (DWORD)atoi(e)) { done = 1; if (!s_open) xbox_GfxMenuToggle(); s_page = getenv("RECOMP_GFXMENU_PAGE") ? atoi(getenv("RECOMP_GFXMENU_PAGE")) % 4 : 0; s_row[s_page] = 1; s_ver++; } }
    }
    /* stick as d-pad */
    if (ly > 16000) b |= XBOX_GAMEPAD_DPAD_UP;
    if (ly < -16000) b |= XBOX_GAMEPAD_DPAD_DOWN;
    if (lx < -16000) b |= XBOX_GAMEPAD_DPAD_LEFT;
    if (lx > 16000) b |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (a) b |= 0x100;
    if (bb) b |= 0x200;
    if (s_open && s_capture == 1) {
        gfx_capture_poll();
        if (s_capture == 1 && (b & ~prev & 0x200)) { s_capture = 0; s_ver++; }   /* B cancels */
    } else if (s_open && s_capture == 2) {
        int vk, any = 0;
        for (vk = 1; vk < 256 && !any; vk++) any = xbox_InputKeyDown(vk);
        if (!any) s_capture = 0;
    } else if (!s_open && (b & XBOX_GAMEPAD_BACK) && (b & XBOX_GAMEPAD_START) && !(prev & XBOX_GAMEPAD_START))
        xbox_GfxMenuToggle();
    else if (s_open) {
        WORD dirs = (WORD)(b & 0x0F);
        nb = (WORD)(b & ~prev);
        /* a held direction repeats after 400 ms, every 130 ms */
        if (dirs) {
            if (dirs & ~prev) rep_at = now + 400;
            else if ((LONG)(now - rep_at) >= 0) { nb |= dirs; rep_at = now + 130; }
        }
        if (nb & XBOX_GAMEPAD_DPAD_UP) gfx_nav(-1, 0);
        if (nb & XBOX_GAMEPAD_DPAD_DOWN) gfx_nav(1, 0);
        if (nb & XBOX_GAMEPAD_DPAD_LEFT) gfx_nav(0, -1);
        if (nb & XBOX_GAMEPAD_DPAD_RIGHT) gfx_nav(0, 1);
        if (nb & 0x100) gfx_activate();
        if (nb & (0x200 | XBOX_GAMEPAD_START | XBOX_GAMEPAD_BACK)) xbox_GfxMenuToggle();
    }
    prev = b;
    if (s_open) {
        s_block_pad = 1;
        memset(&st->Gamepad, 0, sizeof st->Gamepad);
    } else if (s_block_pad) {
        /* (1.9.5) Only what was held as the menu closed is kept from the
         * game, until it is let go -- and for a second at most: a button
         * that read as held for ever (a key whose release never arrived)
         * kept every input from the game until the window lost the focus. */
        static WORD mask; static DWORD since;
        if (s_block_pad == 1) { mask = (WORD)(b & 0x3FF); since = now; s_block_pad = 2; }
        mask &= (WORD)b;
        if (!mask || now - since > 1000) {
            if (mask) { fprintf(stderr, "  [GFXMENU] buttons %04X still read as held a second after closing: passed on\n", mask); fflush(stderr); }
            s_block_pad = 0;
        } else memset(&st->Gamepad, 0, sizeof st->Gamepad);
    }
}

static void panel_body(int pw, int ph, int fp, int lh);
/* ---------------- drawing (GDI into a DIB, shown by the renderer) -------- */
#if defined(_WIN32)

static HDC s_dc;
static HBITMAP s_bmp;
static uint32_t *s_px;
static int s_pw, s_ph, s_font_px;
static HFONT s_font, s_font_b, s_font_t;
static unsigned s_drawn_ver;

static HFONT mkfont(int px, int weight, const char *face)
{
    if (xbox_gfxmenu_face) face = xbox_gfxmenu_face;
    return CreateFontA(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_ROMAN, face);
}

static void txt(const char *s, int x, int y, int w, int h, UINT fmt, COLORREF c, HFONT f)
{
    RECT r = { x, y, x + w, y + h };
    wchar_t ws[512];
    int n;
    s = TR(s);
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, ws, 512);
    if (n <= 0) n = MultiByteToWideChar(CP_ACP, 0, s, -1, ws, 512);
    if (n <= 0) return;
    SelectObject(s_dc, f);
    SetTextColor(s_dc, c);
    if (xbox_gfxmenu_rtl) {
        fmt |= DT_RTLREADING;
        if (!(fmt & DT_CENTER)) fmt |= DT_RIGHT;   /* (right to left: a label starts at the right) */
    }
    DrawTextW(s_dc, ws, -1, &r, fmt | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
}

static void box(int x, int y, int w, int h, COLORREF c)
{
    RECT r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(s_dc, &r, b);
    DeleteObject(b);
}

/* (1.9.7) a second surface for the toast (an achievement's note): drawn with the same txt() and box(), the panel's
 * globals pointed at it for the while */
static HDC s_tdc[2]; static HBITMAP s_tbmp[2]; static uint32_t *s_tpxa[2]; static int s_twa[2], s_tha[2], s_tfpx[2];
static HFONT s_tf[2], s_tfb[2];
static HDC s_sv_dc; static uint32_t *s_sv_px; static int s_sv_pw, s_sv_ph;
static int toast_begin(int k, int w, int h, int fp, HFONT *f, HFONT *fb)
{
    if (w != s_twa[k] || h != s_tha[k] || !s_tdc[k]) {
        BITMAPINFO bi;
        if (!s_tdc[k]) s_tdc[k] = CreateCompatibleDC(NULL);
        if (s_tbmp[k]) DeleteObject(s_tbmp[k]);
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = w; bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        s_tbmp[k] = CreateDIBSection(s_tdc[k], &bi, DIB_RGB_COLORS, (void **)&s_tpxa[k], NULL, 0);
        if (!s_tbmp[k]) { s_tpxa[k] = NULL; s_twa[k] = 0; return 0; }
        SelectObject(s_tdc[k], s_tbmp[k]); s_twa[k] = w; s_tha[k] = h;
    }
    if (fp != s_tfpx[k]) {
        if (s_tf[k]) DeleteObject(s_tf[k]);
        if (s_tfb[k]) DeleteObject(s_tfb[k]);
        s_tf[k] = mkfont(fp, FW_NORMAL, "Palatino Linotype"); s_tfb[k] = mkfont(fp * 12 / 10, FW_BOLD, "Palatino Linotype"); s_tfpx[k] = fp;
    }
    *f = s_tf[k]; *fb = s_tfb[k];
    s_sv_dc = s_dc; s_sv_px = s_px; s_sv_pw = s_pw; s_sv_ph = s_ph;
    s_dc = s_tdc[k]; s_px = s_tpxa[k]; s_pw = w; s_ph = h;
    SetBkMode(s_dc, TRANSPARENT);
    return 1;
}
static void toast_end(void) { GdiFlush(); s_dc = s_sv_dc; s_px = s_sv_px; s_pw = s_sv_pw; s_ph = s_sv_ph; }
int xbox_GfxMenuRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver)
{
    int fp, lh, pw, ph;
    if (!s_open) return 0;
    EnterCriticalSection(&s_lock);
    fp = sch / 34;
    if (fp < 13) fp = 13;
    lh = fp * 16 / 10;
    pw = fp * 36; ph = lh * (GFX_MENU_N + 8);
    if (pw > scw - 16) { fp = fp * (scw - 16) / pw; lh = fp * 16 / 10; pw = fp * 36; ph = lh * (GFX_MENU_N + 8); }
    if (ph > sch - 16) { fp = fp * (sch - 16) / ph; lh = fp * 16 / 10; pw = fp * 36; ph = lh * (GFX_MENU_N + 8); }
    if (fp < 6) { LeaveCriticalSection(&s_lock); return 0; }
    if (pw != s_pw || ph != s_ph || !s_dc) {
        BITMAPINFO bi;
        if (!s_dc) s_dc = CreateCompatibleDC(NULL);
        if (s_bmp) DeleteObject(s_bmp);
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader;
        bi.bmiHeader.biWidth = pw; bi.bmiHeader.biHeight = -ph;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
        s_bmp = CreateDIBSection(s_dc, &bi, DIB_RGB_COLORS, (void **)&s_px, NULL, 0);
        if (!s_bmp) { s_px = NULL; LeaveCriticalSection(&s_lock); return 0; }
        SelectObject(s_dc, s_bmp);
        s_pw = pw; s_ph = ph; s_drawn_ver = 0;
    }
    if (fp != s_font_px) {
        if (s_font) DeleteObject(s_font);
        if (s_font_b) DeleteObject(s_font_b);
        if (s_font_t) DeleteObject(s_font_t);
        s_font = mkfont(fp, FW_NORMAL, "Palatino Linotype");
        s_font_b = mkfont(fp, FW_BOLD, "Palatino Linotype");
        s_font_t = mkfont(fp * 13 / 10, FW_BOLD, "Palatino Linotype");
        s_font_px = fp; s_drawn_ver = 0;
    }
    if (s_drawn_ver != s_ver) {
        panel_body(pw, ph, fp, lh);
        s_drawn_ver = s_ver;
    }
    *px = s_px; *w = pw; *h = ph; *ver = s_drawn_ver;
    LeaveCriticalSection(&s_lock);
    return 1;
}

#else
/* POSIX: the same panel drawn into a plain pixel buffer, the text through
 * host_text (TeX Gyre Pagella standing in for Palatino Linotype). */
#include "host_text.h"
typedef DWORD COLORREF;
#define RGB(r, g, b) ((COLORREF)(((BYTE)(r)) | ((WORD)((BYTE)(g)) << 8) | (((DWORD)(BYTE)(b)) << 16)))
#define DT_LEFT 0x0000
#define DT_CENTER 0x0001
#define DT_VCENTER 0x0004
#define DT_SINGLELINE 0x0020
#define DT_NOPREFIX 0x0800
#define DT_END_ELLIPSIS 0x8000
#define FW_NORMAL 400
#define FW_BOLD 700
#define TRANSPARENT 1
typedef struct { int id, px; } TFont;
typedef TFont *HFONT;
typedef void *HDC;
static int s_pw, s_ph, s_font_px;
static uint32_t *s_px;
static HDC s_dc = (HDC)1;
static TFont s_fontv[3];
static HFONT s_font = &s_fontv[0], s_font_b = &s_fontv[1], s_font_t = &s_fontv[2];
static unsigned s_drawn_ver;
static void SetBkMode(HDC d, int m) { (void)d; (void)m; }
static void GdiFlush(void) {}

static void box(int x, int y, int w, int h, COLORREF c)
{
    int i, j;
    uint32_t v = 0xFF000000u | ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
    for (j = y < 0 ? 0 : y; j < y + h && j < s_ph; j++)
        for (i = x < 0 ? 0 : x; i < x + w && i < s_pw; i++) s_px[(size_t)j * s_pw + i] = v;
}

static void txt(const char *str, int x, int y, int w, int h, UINT fmt, COLORREF c, HFONT f)
{
    char buf[256];
    int tw, lh, tx, ty, i, j;
    uint8_t *cov;
    str = TR(str);
    snprintf(buf, sizeof buf, "%s", str);
    tw = xbox_TextWidth(f->id, f->px, buf);
    if ((fmt & DT_END_ELLIPSIS) && tw > w) {
        size_t n = strlen(buf);
        while (n > 0) {
            buf[--n] = 0;
            snprintf(buf + n, sizeof buf - n, "...");
            tw = xbox_TextWidth(f->id, f->px, buf);
            if (tw <= w) break;
            buf[n] = 0;
        }
    }
    lh = xbox_TextLineHeight(f->id, f->px);
    if (tw <= 0 || lh <= 0) return;
    tx = (fmt & DT_CENTER) ? x + (w - tw) / 2 : x;
    ty = y + (h - lh) / 2;
    cov = (uint8_t *)calloc((size_t)(tw + 2) * (lh + 2), 1);
    if (!cov) return;
    xbox_TextDraw(cov, tw + 2, lh + 2, 1, 1, f->id, f->px, buf);
    for (j = 0; j < lh + 2; j++) {
        int yy = ty - 1 + j;
        if (yy < 0 || yy >= s_ph || yy < y || yy >= y + h) continue;
        for (i = 0; i < tw + 2; i++) {
            int xx = tx - 1 + i, a = cov[(size_t)j * (tw + 2) + i];
            uint32_t d;
            int r, g, b;
            if (!a || xx < 0 || xx >= s_pw) continue;
            d = s_px[(size_t)yy * s_pw + xx];
            r = (int)((d >> 16) & 0xFF); g = (int)((d >> 8) & 0xFF); b = (int)(d & 0xFF);
            r += ((int)(c & 0xFF) - r) * a / 255;
            g += ((int)((c >> 8) & 0xFF) - g) * a / 255;
            b += ((int)((c >> 16) & 0xFF) - b) * a / 255;
            s_px[(size_t)yy * s_pw + xx] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    free(cov);
}
static uint32_t *s_tpxa[2]; static int s_twa[2], s_tha[2];
static TFont s_tfv[2][2];
static uint32_t *s_sv_px; static int s_sv_pw, s_sv_ph;
static int toast_begin(int k, int w, int h, int fp, HFONT *f, HFONT *fb)
{
    if (w != s_twa[k] || h != s_tha[k] || !s_tpxa[k]) { uint32_t *np = (uint32_t *)realloc(s_tpxa[k], (size_t)w * h * 4); if (!np) return 0; s_tpxa[k] = np; s_twa[k] = w; s_tha[k] = h; }
    s_tfv[k][0].id = XFONT_SERIF; s_tfv[k][0].px = fp; s_tfv[k][1].id = XFONT_SERIF_BOLD; s_tfv[k][1].px = fp * 12 / 10;
    *f = &s_tfv[k][0]; *fb = &s_tfv[k][1];
    s_sv_px = s_px; s_sv_pw = s_pw; s_sv_ph = s_ph;
    s_px = s_tpxa[k]; s_pw = w; s_ph = h;
    return 1;
}
static void toast_end(void) { s_px = s_sv_px; s_pw = s_sv_pw; s_ph = s_sv_ph; }
int xbox_GfxMenuRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver)
{
    int fp, lh, pw, ph;
    if (!s_open) return 0;
    EnterCriticalSection(&s_lock);
    fp = sch / 34;
    if (fp < 13) fp = 13;
    lh = fp * 16 / 10;
    pw = fp * 36; ph = lh * (GFX_MENU_N + 8);
    if (pw > scw - 16) { fp = fp * (scw - 16) / pw; lh = fp * 16 / 10; pw = fp * 36; ph = lh * (GFX_MENU_N + 8); }
    if (ph > sch - 16) { fp = fp * (sch - 16) / ph; lh = fp * 16 / 10; pw = fp * 36; ph = lh * (GFX_MENU_N + 8); }
    if (fp < 6) { LeaveCriticalSection(&s_lock); return 0; }
    if (pw != s_pw || ph != s_ph || !s_px) {
        uint32_t *np = (uint32_t *)realloc(s_px, (size_t)pw * ph * 4);
        if (!np) { LeaveCriticalSection(&s_lock); return 0; }
        s_px = np;
        s_pw = pw; s_ph = ph; s_drawn_ver = 0;
    }
    if (fp != s_font_px) {
        s_fontv[0].id = XFONT_SERIF; s_fontv[0].px = fp;
        s_fontv[1].id = XFONT_SERIF_BOLD; s_fontv[1].px = fp;
        s_fontv[2].id = XFONT_SERIF_BOLD; s_fontv[2].px = fp * 13 / 10;
        s_font_px = fp; s_drawn_ver = 0;
    }
    if (s_drawn_ver != s_ver) {
        panel_body(pw, ph, fp, lh);
        s_drawn_ver = s_ver;
    }
    *px = s_px; *w = pw; *h = ph; *ver = s_drawn_ver;
    LeaveCriticalSection(&s_lock);
    return 1;
}
#endif /* _WIN32 drawing */

/* the panel's picture: its page, drawn with the platform's box() and txt() (above) */
static void panel_body(int pw, int ph, int fp, int lh)
{
    int y, i, M;
        COLORREF gold = RGB(214, 182, 96), parch = RGB(226, 214, 186), dim = RGB(150, 138, 112);
        COLORREF selc = RGB(255, 236, 170), white = RGB(255, 255, 255);
        int border = fp / 6 < 2 ? 2 : fp / 6, lx, vx, vw, row = s_row[s_page];
        char v[96];
        const char *note = NULL;
        M = fp;
        SetBkMode(s_dc, TRANSPARENT);
        box(0, 0, pw, ph, gold);
        box(border, border, pw - 2 * border, ph - 2 * border, RGB(16, 13, 9));
        y = lh / 2;
        txt(s_page == 0 ? "GRAPHICS SETTINGS" : s_page == 1 ? "CONTROLS" : s_page == 3 ? "MORE SETTINGS" : "ACHIEVEMENTS", 0, y, pw, lh * 3 / 2, DT_CENTER, gold, s_font_t);
        y += lh * 3 / 2;
        box(M, y + lh / 4, pw - 2 * M, border / 2 + 1, RGB(110, 92, 50));
        y += lh / 2;
        lx = M + fp / 2; vx = pw / 2 + fp; vw = pw - vx - M - fp / 2;
        /* row 0: the page */
        if (row == 0) box(M, y, pw - 2 * M, lh, RGB(70, 52, 22));
        txt("Page", lx, y, vx - lx, lh, DT_LEFT, row == 0 ? selc : parch, row == 0 ? s_font_b : s_font);
        snprintf(v, sizeof v, row == 0 ? "<   %s   >" : "%s", TR(page_name(s_page)));
        txt(v, vx, y, vw, lh, DT_CENTER, row == 0 ? white : gold, row == 0 ? s_font_b : s_font);
        y += lh;
        if (s_page == 0) {
            int vis = 0;
            for (i = 0; i < GFX_MENU_N; i++) {
                int sel;
                if (i == GFX_SPRINT) continue;
                sel = ++vis == row;
                if (sel) box(M, y, pw - 2 * M, lh, RGB(70, 52, 22));
                txt(s_items[i].label, lx, y, vx - lx, lh, DT_LEFT, sel ? selc : parch, sel ? s_font_b : s_font);
                snprintf(v, sizeof v, sel ? "<   %s   >" : "%s", TR(s_items[i].names[s_cur[i]]));
                txt(v, vx, y, vw, lh, DT_CENTER, sel ? white : gold, sel ? s_font_b : s_font);
                y += lh;
            }
            note = row > 0 ? s_items[row_item(row)].note : "Left/Right: another page";
        } else if (s_page == 3) {
            int vr = 0;
            for (i = GFX_MORE0; i < GFX_N; i++) {
                int sel;
                if (more_hidden(i)) continue;
                sel = ++vr == row;
                if (sel) box(M, y, pw - 2 * M, lh, RGB(70, 52, 22));
                txt(s_items[i].label, lx, y, vx - lx, lh, DT_LEFT, sel ? selc : parch, sel ? s_font_b : s_font);
                snprintf(v, sizeof v, sel ? "<   %s   >" : "%s", TR(s_items[i].names[s_cur[i]]));
                txt(v, vx, y, vw, lh, DT_CENTER, sel ? white : gold, sel ? s_font_b : s_font);
                y += lh;
            }
            note = row > 0 ? s_items[more_item(row)].note : "Left/Right: another page.   F9: hide the HUD.   F8: screenshot";
        } else if (s_page == 2) {
            int n = s_ach_n ? s_ach_n() : 0, got = 0, k, nx = M + (pw - 2 * M) * 42 / 100;
            static char cnt[64];
            for (k = 0; k < n; k++) got += s_ach_got(k) != 0;
            if (row >= 1) {
                if (row - 1 < s_atop) s_atop = row - 1;
                if (row - 1 >= s_atop + ACH_ROWS) s_atop = row - ACH_ROWS;
            }
            snprintf(cnt, sizeof cnt, "%d of %d earned", got, n);
            txt(cnt, lx, y, pw - 2 * lx, lh * 4 / 5, DT_LEFT, dim, s_font);
            y += lh * 4 / 5;
            for (k = 0; k < ACH_ROWS; k++) {
                int a = s_atop + k, sel = a + 1 == row, e;
                if (a >= n) break;
                e = s_ach_got(a) != 0;
                if (sel) box(M, y, pw - 2 * M, lh, RGB(70, 52, 22));
                txt(e ? "\xE2\x97\x86" : "\xE2\x97\x87", lx, y, fp * 2, lh, DT_LEFT, e ? gold : dim, s_font);   /* (a filled diamond: earned) */
                txt(s_ach_name(a), lx + fp * 2, y, nx - lx - fp * 2, lh, DT_LEFT | DT_END_ELLIPSIS, e ? (sel ? selc : parch) : dim, sel ? s_font_b : s_font);
                txt(s_ach_desc(a), nx, y, pw - M - nx - fp / 2, lh, DT_LEFT | DT_END_ELLIPSIS, e ? (sel ? white : gold) : dim, s_font);
                y += lh;
            }
            note = row > 0 ? (s_ach_got(row - 1) ? "Earned" : "Not earned yet") : "Left/Right: another page";
        } else {
            int kx = M + (pw - 2 * M) * 40 / 100, px2 = M + (pw - 2 * M) * 74 / 100;
            int kw = px2 - kx - fp / 2, pw2 = pw - M - px2 - fp / 2, n;
            /* keep the selected action in view */
            if (row >= 1 && row <= BIND_N) {
                if (row - 1 < s_top) s_top = row - 1;
                if (row - 1 >= s_top + CTRL_ROWS) s_top = row - CTRL_ROWS;
            }
            txt("Action", lx, y, kx - lx, lh * 4 / 5, DT_LEFT, dim, s_font);
            txt("Keyboard / mouse", kx, y, kw, lh * 4 / 5, DT_LEFT, dim, s_font);
            txt("Controller", px2, y, pw2, lh * 4 / 5, DT_LEFT, dim, s_font);
            y += lh * 4 / 5;
            for (n = 0; n < CTRL_ROWS; n++) {
                int act = s_top + n, sel = act + 1 == row;
                if (act >= BIND_N) break;
                if (sel) box(M, y, pw - 2 * M, lh, RGB(70, 52, 22));
                txt(xbox_BindActionName(act), lx, y, kx - lx, lh, DT_LEFT, sel ? selc : parch, sel ? s_font_b : s_font);
                if (sel && s_capture == 1) snprintf(v, sizeof v, "press a key...");
                else xbox_BindFormatKeys(act, v, sizeof v);
                txt(v, kx, y, kw, lh, DT_LEFT | DT_END_ELLIPSIS, sel ? white : gold, sel ? s_font_b : s_font);
                if (xbox_BindIsButton(act))
                    snprintf(v, sizeof v, sel ? "< %s >" : "%s", TR(xbox_BindPadName(xbox_BindPad(act))));
                else snprintf(v, sizeof v, "left stick");
                if (act >= BIND_LOOK_UP && act <= BIND_LOOK_RIGHT) snprintf(v, sizeof v, "right stick");
                if (act == BIND_WALK) snprintf(v, sizeof v, "stick part way");
                txt(v, px2, y, pw2, lh, DT_LEFT, xbox_BindIsButton(act) ? (sel ? white : gold) : dim,
                    sel ? s_font_b : s_font);
                y += lh;
            }
            {
                int sel = row == BIND_N + 1;
                if (sel) box(M, y, pw - 2 * M, lh, RGB(70, 52, 22));
                txt("Reset all controls to the defaults", lx, y, pw - 2 * lx, lh, DT_LEFT, sel ? selc : parch,
                    sel ? s_font_b : s_font);
                y += lh;
            }
            if (s_capture == 1) note = "Press the key or mouse button.  Esc: cancel.  Delete: clear.";
            else if (row == BIND_N + 1) note = "A / Enter: reset";
            else if (row > 0) note = "A / Enter: set a key.   Left/Right: controller button.";
            else note = "Left/Right: another page";
        }
        y = ph - lh * 3;
        txt(note ? note : "", 0, y, pw, lh, DT_CENTER, dim, s_font);
        y += lh;
        txt("Up/Down: choose     Left/Right or A: change     B / Esc / F10: close", 0, y, pw, lh, DT_CENTER, parch, s_font);
        y += lh * 3 / 4;
        txt("Changes are saved automatically", 0, y, pw, lh, DT_CENTER, dim, s_font);
        GdiFlush();
        /* GDI leaves alpha at 0: the panel is mostly opaque */
        for (i = 0; i < pw * ph; i++) s_px[i] = (s_px[i] & 0x00FFFFFFu) | 0xF0000000u;
    }

/* (1.9.7) toasts: a short note in the bottom right corner (an achievement earned), 5 s each, one after another.
 * xbox_ToastShow from any thread; xbox_ToastRender from the renderer, as the panel's */
static struct { char t[96], l[160]; } s_toastq[8];
static int s_tq_n, s_tq_head;
static DWORD s_toast_t0;
static unsigned s_toast_ver = 1, s_toast_drawn;
void xbox_ToastShow(const char *title, const char *line)
{
    int k;
    gfx_init();
    EnterCriticalSection(&s_lock);
    if (s_tq_n < 8) { k = (s_tq_head + s_tq_n) % 8; snprintf(s_toastq[k].t, sizeof s_toastq[k].t, "%s", title ? title : ""); snprintf(s_toastq[k].l, sizeof s_toastq[k].l, "%s", line ? line : ""); s_tq_n++; if (s_tq_n == 1) { s_toast_t0 = 0; s_toast_ver++; } }
    LeaveCriticalSection(&s_lock);
}
/* a note's picture into surface k: a gold frame, its title and its line */
static int note_draw(int k, int tw, int th, int fp, const char *t, const char *l)
{
    HFONT f, fb; int i, border = fp / 6 < 2 ? 2 : fp / 6;
    if (!toast_begin(k, tw, th, fp, &f, &fb)) return 0;
    box(0, 0, tw, th, RGB(214, 182, 96));
    box(border, border, tw - 2 * border, th - 2 * border, RGB(16, 13, 9));
    txt(t, fp, fp / 3, tw - 2 * fp, fp * 2, DT_LEFT | DT_END_ELLIPSIS, RGB(214, 182, 96), fb);
    txt(l, fp, fp * 2, tw - 2 * fp, fp * 3 / 2, DT_LEFT | DT_END_ELLIPSIS, RGB(226, 214, 186), f);
    for (i = 0; i < tw * th; i++) s_tpxa[k][i] = (s_tpxa[k][i] & 0x00FFFFFFu) | 0xF0000000u;
    toast_end();
    return 1;
}
int xbox_ToastRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver, int *x, int *y)
{
    int fp, tw, th, k;
    DWORD now = GetTickCount();
    if (!s_inited || !s_tq_n) return 0;
    EnterCriticalSection(&s_lock);
    if (!s_tq_n) { LeaveCriticalSection(&s_lock); return 0; }
    if (!s_toast_t0) s_toast_t0 = now ? now : 1;
    if (now - s_toast_t0 > 5000u) {   /* the next one */
        s_tq_head = (s_tq_head + 1) % 8; s_tq_n--; s_toast_t0 = s_tq_n ? now : 0; s_toast_ver++;
        if (!s_tq_n) { LeaveCriticalSection(&s_lock); return 0; }
    }
    k = s_tq_head;
    fp = sch / 40; if (fp < 12) fp = 12;
    tw = fp * 24; th = fp * 4;
    if (tw > scw - 16) tw = scw - 16;
    if (s_toast_drawn != s_toast_ver && note_draw(0, tw, th, fp, s_toastq[k].t, s_toastq[k].l)) s_toast_drawn = s_toast_ver;
    *px = s_tpxa[0]; *w = s_twa[0]; *h = s_tha[0]; *ver = s_toast_drawn;
    *x = scw - s_twa[0] - fp; *y = sch - s_tha[0] - fp * 3;
    LeaveCriticalSection(&s_lock);
    return s_tpxa[0] != NULL;
}
/* (1.9.7) a banner: a note at the top of the screen for as long as it is set (the difficulty screen's Impossible) */
static char s_ban_t[96], s_ban_l[160];
static unsigned s_ban_ver = 1, s_ban_drawn;
void xbox_BannerSet(const char *title, const char *line)
{
    gfx_init();
    EnterCriticalSection(&s_lock);
    if (!title) title = "";
    if (!line) line = "";
    if (strcmp(s_ban_t, title) || strcmp(s_ban_l, line)) { snprintf(s_ban_t, sizeof s_ban_t, "%s", title); snprintf(s_ban_l, sizeof s_ban_l, "%s", line); s_ban_ver++; }
    LeaveCriticalSection(&s_lock);
}
int xbox_BannerRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver, int *x, int *y)
{
    int fp, tw, th;
    if (!s_inited || !s_ban_t[0]) return 0;
    EnterCriticalSection(&s_lock);
    if (!s_ban_t[0]) { LeaveCriticalSection(&s_lock); return 0; }
    fp = sch / 34; if (fp < 13) fp = 13;
    tw = fp * 26; th = fp * 4;
    if (tw > scw - 16) tw = scw - 16;
    if (s_ban_drawn != s_ban_ver && note_draw(1, tw, th, fp, s_ban_t, s_ban_l)) s_ban_drawn = s_ban_ver;
    *px = s_tpxa[1]; *w = s_twa[1]; *h = s_tha[1]; *ver = s_ban_drawn;
    *x = (scw - s_twa[1]) / 2; *y = fp * 2;
    LeaveCriticalSection(&s_lock);
    return s_tpxa[1] != NULL;
}
