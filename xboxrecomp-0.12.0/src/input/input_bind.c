/*
 * input_bind.c -- remappable controls.
 *
 * Every Xbox controller input the game reads is an "action". Each action has:
 *  - up to three keyboard keys or mouse buttons (the keyboard stand-in for
 *    controller 1), and
 *  - for buttons, the button of your own controller that produces it.
 * Sticks stay sticks on a controller; on the keyboard each stick direction is
 * an action of its own.
 *
 * The title loads bindings with xbox_BindSet("kb_a", "K, Mouse Left, Space")
 * / xbox_BindSet("pad_a", "A") from its settings file and is told about every
 * change made in game through the save handler. The settings overlay
 * (src/video/gfx_menu.c) edits them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "input_bind.h"

#include <windows.h>   /* the real one, or src/platform/posix_include */
#include <ctype.h>

static const struct { const char *key, *name; int is_button; } k_act[BIND_N] = {
    { "a", "A button", 1 }, { "b", "B button", 1 }, { "x", "X button", 1 }, { "y", "Y button", 1 },
    { "black", "Black button", 1 }, { "white", "White button", 1 },
    { "lt", "Left trigger", 1 }, { "rt", "Right trigger", 1 },
    { "start", "Start", 1 }, { "back", "Back", 1 },
    { "dpad_up", "D-pad up", 1 }, { "dpad_down", "D-pad down", 1 },
    { "dpad_left", "D-pad left", 1 }, { "dpad_right", "D-pad right", 1 },
    { "ls_click", "Left stick click", 1 }, { "rs_click", "Right stick click", 1 },
    { "move_up", "Move forward", 0 }, { "move_down", "Move back", 0 },
    { "move_left", "Move left", 0 }, { "move_right", "Move right", 0 },
    { "look_up", "Right stick up", 0 }, { "look_down", "Right stick down", 0 },
    { "look_left", "Right stick left", 0 }, { "look_right", "Right stick right", 0 },
    { "walk", "Walk", 0 },
};

/* Controller sources (your pad's buttons). */
static const char *const k_pad[BIND_PAD_N] = {
    "None", "A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Start", "Back",
    "D-pad up", "D-pad down", "D-pad left", "D-pad right", "LS click", "RS click"
};

static int s_kb[BIND_N][BIND_KEYS];
static int s_pad[BIND_N];
static int s_pad2[BIND_N];   /* player 2's controller (port 1): its own buttons */
static int s_inited;
static void (*s_save)(const char *key, const char *value);
volatile long xbox_bind_rev = 1;

/* ---- key names ---- */
static const struct { int vk; const char *name; } k_names[] = {
    { VK_LBUTTON, "Mouse Left" }, { VK_RBUTTON, "Mouse Right" }, { VK_MBUTTON, "Mouse Middle" },
    { VK_XBUTTON1, "Mouse 4" }, { VK_XBUTTON2, "Mouse 5" },
    { VK_SPACE, "Space" }, { VK_RETURN, "Enter" }, { VK_ESCAPE, "Esc" }, { VK_TAB, "Tab" },
    { VK_BACK, "Backspace" }, { VK_SHIFT, "Shift" }, { VK_CONTROL, "Ctrl" }, { VK_MENU, "Alt" },
    { VK_CAPITAL, "Caps Lock" }, { VK_UP, "Up" }, { VK_DOWN, "Down" }, { VK_LEFT, "Left" },
    { VK_RIGHT, "Right" }, { VK_INSERT, "Insert" }, { VK_DELETE, "Delete" }, { VK_HOME, "Home" },
    { VK_END, "End" }, { VK_PRIOR, "Page Up" }, { VK_NEXT, "Page Down" },
    { VK_NUMPAD0, "Num 0" }, { VK_NUMPAD1, "Num 1" }, { VK_NUMPAD2, "Num 2" }, { VK_NUMPAD3, "Num 3" },
    { VK_NUMPAD4, "Num 4" }, { VK_NUMPAD5, "Num 5" }, { VK_NUMPAD6, "Num 6" }, { VK_NUMPAD7, "Num 7" },
    { VK_NUMPAD8, "Num 8" }, { VK_NUMPAD9, "Num 9" }, { VK_MULTIPLY, "Num *" }, { VK_ADD, "Num +" },
    { VK_SUBTRACT, "Num -" }, { VK_DECIMAL, "Num ." }, { VK_DIVIDE, "Num /" },
    { VK_OEM_1, "Semicolon" }, { VK_OEM_PLUS, "Equals" }, { VK_OEM_COMMA, "Comma" },
    { VK_OEM_MINUS, "Minus" }, { VK_OEM_PERIOD, "Period" }, { VK_OEM_2, "Slash" },
    { VK_OEM_3, "Backquote" }, { VK_OEM_4, "[" }, { VK_OEM_5, "Backslash" }, { VK_OEM_6, "]" },
    { VK_OEM_7, "Quote" },
};

const char *xbox_BindKeyName(int vk)
{
    static char buf[8][16];
    static int k;
    size_t i;
    char *b;
    for (i = 0; i < sizeof k_names / sizeof k_names[0]; i++)
        if (k_names[i].vk == vk) return k_names[i].name;
    b = buf[k++ & 7];
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) { b[0] = (char)vk; b[1] = 0; return b; }
    if (vk >= VK_F1 && vk <= VK_F24) { snprintf(b, 16, "F%d", vk - VK_F1 + 1); return b; }
    snprintf(b, 16, "Key %02X", vk & 0xFF);
    return b;
}

static int key_from_name(const char *s)
{
    size_t i;
    if (!s[0]) return 0;
    for (i = 0; i < sizeof k_names / sizeof k_names[0]; i++)
        if (!_stricmp(k_names[i].name, s)) return k_names[i].vk;
    if (!s[1] && (isalnum((unsigned char)s[0]))) return toupper((unsigned char)s[0]);
    if ((s[0] == 'F' || s[0] == 'f') && atoi(s + 1) >= 1 && atoi(s + 1) <= 24) return VK_F1 + atoi(s + 1) - 1;
    if (!_strnicmp(s, "Key ", 4)) return (int)strtol(s + 4, NULL, 16);
    /* a few spellings people type by hand */
    if (!_stricmp(s, "LMB") || !_stricmp(s, "Left click")) return VK_LBUTTON;
    if (!_stricmp(s, "RMB") || !_stricmp(s, "Right click")) return VK_RBUTTON;
    if (!_stricmp(s, "MMB") || !_stricmp(s, "Middle click")) return VK_MBUTTON;
    if (!_stricmp(s, "Escape")) return VK_ESCAPE;
    if (!_stricmp(s, "Return")) return VK_RETURN;
    return 0;
}

/* ---- defaults ---- */
static void set_kb(int a, int k0, int k1, int k2) { s_kb[a][0] = k0; s_kb[a][1] = k1; s_kb[a][2] = k2; }

void xbox_BindDefaults(int which)
{
    int a;
    const char *v = getenv("RECOMP_KB_LAYOUT");
    int classic = which == 1 || (which < 0 && v && (v[0] == 'c' || v[0] == 'C'));
    memset(s_kb, 0, sizeof s_kb);
    for (a = 0; a < BIND_N; a++) s_pad[a] = 0;
    /* your controller: as labelled (LB = Black, RB = White, as before) */
    s_pad[BIND_A] = 1; s_pad[BIND_B] = 2; s_pad[BIND_X] = 3; s_pad[BIND_Y] = 4;
    s_pad[BIND_BLACK] = 5; s_pad[BIND_WHITE] = 6; s_pad[BIND_LT] = 7; s_pad[BIND_RT] = 8;
    s_pad[BIND_START] = 9; s_pad[BIND_BACK] = 10; s_pad[BIND_DUP] = 11; s_pad[BIND_DDOWN] = 12;
    s_pad[BIND_DLEFT] = 13; s_pad[BIND_DRIGHT] = 14; s_pad[BIND_LTHUMB] = 15; s_pad[BIND_RTHUMB] = 16;
    if (!s_inited) memcpy(s_pad2, s_pad, sizeof s_pad2);   /* (player 2's: the same, until changed; reset on its own) */
    set_kb(BIND_WALK, 'B', 0, 0);   /* (both layouts: B is free in each) */
    set_kb(BIND_DUP, VK_UP, 0, 0); set_kb(BIND_DDOWN, VK_DOWN, 0, 0);
    set_kb(BIND_DLEFT, VK_LEFT, 0, 0); set_kb(BIND_DRIGHT, VK_RIGHT, 0, 0);
    if (classic) {
        set_kb(BIND_A, 'Z', 0, 0); set_kb(BIND_B, 'X', 0, 0); set_kb(BIND_X, 'A', 0, 0); set_kb(BIND_Y, 'S', 0, 0);
        set_kb(BIND_WHITE, 'Q', 0, 0); set_kb(BIND_BLACK, 'E', 0, 0);
        set_kb(BIND_LT, '1', 0, 0); set_kb(BIND_RT, '3', 0, 0);
        set_kb(BIND_START, VK_RETURN, 0, 0); set_kb(BIND_BACK, VK_BACK, 0, 0);
        set_kb(BIND_LTHUMB, VK_SHIFT, 0, 0); set_kb(BIND_RTHUMB, VK_CONTROL, 0, 0);
        set_kb(BIND_MOVE_UP, VK_NUMPAD8, 0, 0); set_kb(BIND_MOVE_DOWN, VK_NUMPAD2, 0, 0);
        set_kb(BIND_MOVE_LEFT, VK_NUMPAD4, 0, 0); set_kb(BIND_MOVE_RIGHT, VK_NUMPAD6, 0, 0);
        set_kb(BIND_LOOK_UP, 'I', 0, 0); set_kb(BIND_LOOK_DOWN, 'K', 0, 0);
        set_kb(BIND_LOOK_LEFT, 'J', 0, 0); set_kb(BIND_LOOK_RIGHT, 'L', 0, 0);
    } else {
        set_kb(BIND_A, 'K', VK_LBUTTON, VK_SPACE); set_kb(BIND_B, 'L', 0, 0);
        set_kb(BIND_X, 'J', VK_RBUTTON, 0); set_kb(BIND_Y, 'I', 0, 0);
        set_kb(BIND_WHITE, 'Z', 0, 0); set_kb(BIND_BLACK, 'C', 0, 0);
        set_kb(BIND_LT, 'F', 0, 0); set_kb(BIND_RT, 'R', 0, 0);
        set_kb(BIND_START, VK_RETURN, VK_ESCAPE, 0); set_kb(BIND_BACK, VK_BACK, VK_TAB, 0);
        set_kb(BIND_LTHUMB, VK_SHIFT, 0, 0); set_kb(BIND_RTHUMB, VK_MBUTTON, 0, 0);
        set_kb(BIND_MOVE_UP, 'W', 0, 0); set_kb(BIND_MOVE_DOWN, 'S', 0, 0);
        set_kb(BIND_MOVE_LEFT, 'A', 0, 0); set_kb(BIND_MOVE_RIGHT, 'D', 0, 0);
        set_kb(BIND_LOOK_UP, VK_NUMPAD8, 0, 0); set_kb(BIND_LOOK_DOWN, VK_NUMPAD2, 0, 0);
        set_kb(BIND_LOOK_LEFT, VK_NUMPAD4, 0, 0); set_kb(BIND_LOOK_RIGHT, VK_NUMPAD6, 0, 0);
    }
    s_inited = 1;
    InterlockedIncrement((volatile LONG *)&xbox_bind_rev);
}

static void bind_init(void) { if (!s_inited) xbox_BindDefaults(-1); }

/* ---- queries ---- */
int xbox_BindKey(int a, int i) { bind_init(); return (a >= 0 && a < BIND_N && i >= 0 && i < BIND_KEYS) ? s_kb[a][i] : 0; }
int xbox_BindPad(int a) { bind_init(); return (a >= 0 && a < BIND_N) ? s_pad[a] : 0; }
int xbox_BindPad2(int a) { bind_init(); return (a >= 0 && a < BIND_N) ? s_pad2[a] : 0; }
const char *xbox_BindActionName(int a) { return (a >= 0 && a < BIND_N) ? k_act[a].name : ""; }
const char *xbox_BindActionKey(int a) { return (a >= 0 && a < BIND_N) ? k_act[a].key : ""; }
int xbox_BindIsButton(int a) { return a >= 0 && a < BIND_N && k_act[a].is_button; }
const char *xbox_BindPadName(int src) { return (src >= 0 && src < BIND_PAD_N) ? k_pad[src] : "None"; }

void xbox_BindFormatKeys(int a, char *out, int n)
{
    int i, used = 0;
    bind_init();
    out[0] = 0;
    for (i = 0; i < BIND_KEYS; i++) {
        if (!s_kb[a][i]) continue;
        used += snprintf(out + used, (size_t)(n - used > 0 ? n - used : 0), "%s%s", used ? ", " : "",
                         xbox_BindKeyName(s_kb[a][i]));
        if (used >= n) break;
    }
    if (!out[0]) snprintf(out, (size_t)n, "-");
}

/* ---- changes ---- */
static void save(int a, int pad)
{
    char key[32], val[96];
    if (!s_save) return;
    if (pad == 2) {   /* player 2's controller */
        snprintf(key, sizeof key, "p2pad_%s", k_act[a].key);
        snprintf(val, sizeof val, "%s", k_pad[s_pad2[a]]);
    } else if (pad) {
        snprintf(key, sizeof key, "pad_%s", k_act[a].key);
        snprintf(val, sizeof val, "%s", k_pad[s_pad[a]]);
    } else {
        snprintf(key, sizeof key, "kb_%s", k_act[a].key);
        xbox_BindFormatKeys(a, val, sizeof val);
    }
    s_save(key, val);
}

void xbox_BindSetSaveHandler(void (*fn)(const char *key, const char *value)) { s_save = fn; }

/* A key pressed for action a: it becomes the action's first key; the
 * action's other keys move down one; the key is taken off every other
 * action so one key never does two things. */
void xbox_BindAssignKey(int a, int vk)
{
    int b, i, j;
    bind_init();
    if (a < 0 || a >= BIND_N || !vk) return;
    for (b = 0; b < BIND_N; b++)
        for (i = 0; i < BIND_KEYS; i++)
            if (s_kb[b][i] == vk) {
                for (j = i; j < BIND_KEYS - 1; j++) s_kb[b][j] = s_kb[b][j + 1];
                s_kb[b][BIND_KEYS - 1] = 0;
                if (b != a) save(b, 0);
                i--;
            }
    for (j = BIND_KEYS - 1; j > 0; j--) s_kb[a][j] = s_kb[a][j - 1];
    s_kb[a][0] = vk;
    save(a, 0);
    InterlockedIncrement((volatile LONG *)&xbox_bind_rev);
}

void xbox_BindClearKeys(int a)
{
    bind_init();
    if (a < 0 || a >= BIND_N) return;
    memset(s_kb[a], 0, sizeof s_kb[a]);
    save(a, 0);
    InterlockedIncrement((volatile LONG *)&xbox_bind_rev);
}

/* Your controller's button src now gives action a. Whatever action had src
 * gets a's old button (a swap), so nothing is lost. */
void xbox_BindAssignPad(int a, int src)
{
    int b, old;
    bind_init();
    if (a < 0 || a >= BIND_N || !k_act[a].is_button || src < 0 || src >= BIND_PAD_N) return;
    old = s_pad[a];
    if (src)
        for (b = 0; b < BIND_N; b++)
            if (b != a && s_pad[b] == src) { s_pad[b] = old; save(b, 1); }
    s_pad[a] = src;
    save(a, 1);
    InterlockedIncrement((volatile LONG *)&xbox_bind_rev);
}

void xbox_BindResetAll(void)
{
    int a;
    xbox_BindDefaults(-1);   /* (player 2's buttons are left as they are) */
    for (a = 0; a < BIND_N; a++) { save(a, 0); if (k_act[a].is_button) save(a, 1); }
}

/* player 2's controller: the same swap as player 1's (xbox_BindAssignPad) */
void xbox_BindAssignPad2(int a, int src)
{
    int b, old;
    bind_init();
    if (a < 0 || a >= BIND_N || !k_act[a].is_button || src < 0 || src >= BIND_PAD_N) return;
    old = s_pad2[a];
    if (src)
        for (b = 0; b < BIND_N; b++)
            if (b != a && s_pad2[b] == src) { s_pad2[b] = old; save(b, 2); }
    s_pad2[a] = src;
    save(a, 2);
    InterlockedIncrement((volatile LONG *)&xbox_bind_rev);
}

/* player 2's controller back to its buttons as labelled */
void xbox_BindResetPad2(void)
{
    static const int def[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };   /* A B X Y Black White LT RT Start Back Up Down Left Right LS RS */
    int a;
    bind_init();
    for (a = 0; a < BIND_N; a++) s_pad2[a] = a < 16 ? def[a] : 0;
    for (a = 0; a < BIND_N; a++) if (k_act[a].is_button) save(a, 2);
    InterlockedIncrement((volatile LONG *)&xbox_bind_rev);
}

/* From the settings file: "kb_<action>" = "K, Mouse Left" or
 * "pad_<action>" = "A". Unknown names are ignored. */
int xbox_BindSet(const char *key, const char *value)
{
    int a, pad;
    const char *name;
    bind_init();
    if (!_strnicmp(key, "kb_", 3)) { pad = 0; name = key + 3; }
    else if (!_strnicmp(key, "pad_", 4)) { pad = 1; name = key + 4; }
    else if (!_strnicmp(key, "p2pad_", 6)) { pad = 2; name = key + 6; }
    else return 0;
    for (a = 0; a < BIND_N; a++)
        if (!_stricmp(k_act[a].key, name)) break;
    if (a == BIND_N) return 0;
    if (pad) {
        int s;
        if (!k_act[a].is_button) return 0;
        for (s = 0; s < BIND_PAD_N; s++)
            if (!_stricmp(k_pad[s], value)) { if (pad == 2) s_pad2[a] = s; else s_pad[a] = s; break; }
    } else {
        char buf[128], *p, *tok;
        int i = 0;
        snprintf(buf, sizeof buf, "%s", value);
        memset(s_kb[a], 0, sizeof s_kb[a]);
        for (p = buf; p && i < BIND_KEYS; ) {
            char *comma = strchr(p, ',');
            if (comma) *comma = 0;
            while (*p == ' ') p++;
            tok = p;
            {
                size_t l = strlen(tok);
                while (l && tok[l - 1] == ' ') tok[--l] = 0;
            }
            if (tok[0] && strcmp(tok, "-")) {
                int vk = key_from_name(tok);
                if (vk) s_kb[a][i++] = vk;
            }
            p = comma ? comma + 1 : NULL;
        }
    }
    InterlockedIncrement((volatile LONG *)&xbox_bind_rev);
    return 1;
}


