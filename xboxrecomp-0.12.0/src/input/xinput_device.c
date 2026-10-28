/**
 * Xbox Input Compatibility Layer
 *
 * Translates the Xbox controller API to the host's controllers. Handles the
 * structural differences between the Xbox gamepad (analog buttons as bytes,
 * separate trigger channels) and the host (digital face buttons, trigger
 * axes).
 *
 * Controllers come from two places, merged into one list in that order:
 *   - XInput (Windows): Xbox 360 / One / Series pads and anything that acts
 *     like one;
 *   - SDL2's game-controller layer: PlayStation 3, 4 and 5 pads (DualShock 3,
 *     DualShock 4, DualSense / Edge, rumble included), Switch Pro and the
 *     other pads SDL knows. On Windows SDL2.dll is loaded if it is beside the
 *     game (XInput pads are left to XInput there); on Linux SDL is the
 *     only source, Xbox pads included.
 * Player 1 is the first pad in the list, player 2 the second, and so on; the
 * keyboard and mouse act as player 1 as well (RECOMP_KEYBOARD).
 */

#include "xinput_xbox.h"
#include "input_bind.h"
#include "../video/gfx_menu.h"   /* (1.9.7) the More page: vibration, stick deadzone, trigger sensitivity */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#if defined(_WIN32)
#include <xinput.h>
#endif

#if !defined(_WIN32) || defined(XBOX_HAVE_SDL2)
#define HAVE_SDL_PADS 1
#include <SDL.h>
#else
#define HAVE_SDL_PADS 0
#endif

/* One pad's state in XInput's terms, whatever it came from. */
typedef struct {
    WORD  buttons;              /* XINPUT_GAMEPAD_* */
    BYTE  lt, rt;
    SHORT lx, ly, rx, ry;       /* +y up */
    DWORD packet;
} HostPad;

/* ======================================================================== */
/* XInput (Windows)                                                         */
/* ======================================================================== */
#if defined(_WIN32)
/* XInput loaded at run time, newest first. xinput1_4 ships with Windows 8
 * and later (and drives Xbox One / Series pads); xinput1_3 is the DirectX
 * redistributable one (Windows 7, with the DirectX runtime); 9_1_0 is the
 * minimal one every Windows from Vista on has. Linking xinput1_3 statically
 * tied the game to that redist. */
typedef DWORD (WINAPI *PFN_XIGetState)(DWORD, XINPUT_STATE *);
typedef DWORD (WINAPI *PFN_XISetState)(DWORD, XINPUT_VIBRATION *);
static PFN_XIGetState p_XInputGetState;
static PFN_XISetState p_XInputSetState;
static void xi_load(void)
{
    /* Once, and finished before anyone uses it (a second thread arriving
     * meanwhile must not see "no XInput"). */
    static INIT_ONCE once = INIT_ONCE_STATIC_INIT;
    BOOL pending = FALSE;
    static const char *const dlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    int i;
    if (!InitOnceBeginInitialize(&once, 0, &pending, NULL) || !pending) return;
    for (i = 0; i < 3 && !p_XInputGetState; i++) {
        HMODULE m = LoadLibraryA(dlls[i]);
        if (!m) continue;
        p_XInputGetState = (PFN_XIGetState)(void *)GetProcAddress(m, "XInputGetState");
        p_XInputSetState = (PFN_XISetState)(void *)GetProcAddress(m, "XInputSetState");
        if (p_XInputGetState) {
            fprintf(stderr, "  [INPUT] using %s\n", dlls[i]);
            fflush(stderr);
        }
    }
    InitOnceComplete(&once, 0, NULL);
}
static DWORD xi_get(DWORD i, XINPUT_STATE *s)
{
    xi_load();
    return p_XInputGetState ? p_XInputGetState(i, s) : ERROR_DEVICE_NOT_CONNECTED;
}
static DWORD xi_set(DWORD i, XINPUT_VIBRATION *v)
{
    xi_load();
    return p_XInputSetState ? p_XInputSetState(i, v) : ERROR_DEVICE_NOT_CONNECTED;
}

/* XInput is called on two threads of its own and nowhere else.
 *
 * The game reads its pad through the USB emulation, whose thread built each
 * report by calling XInputGetState -- and sent rumble with XInputSetState --
 * right there. Either call can block: XInputSetState in particular waits on
 * the pad's driver (Bluetooth pads, Steam Input, DS4Windows and other
 * virtual pads), and while it waited no report went out, so the game kept
 * the last one it had. Throwing Gimli's axes (Helm's Deep, the breach) sets
 * the rumble going and coming many times a second; one call that never came
 * back left the trigger held for good -- the hero stuck aiming, and the
 * level-select map scrolling by itself.
 *
 * Now a poll thread reads the pads the players have (every 2 ms) into a
 * table the reports are built from, and a rumble thread sends motor changes;
 * a stuck XInputSetState can no longer hold input up. If the poll itself
 * stops answering, the pad reads as at rest after a quarter of a second
 * rather than frozen with whatever was held, and the log says so. */
static CRITICAL_SECTION s_xi_lock;
static HostPad s_xi_pad[4];
static DWORD   s_xi_ok[4];            /* last successful read (GetTickCount) */
static volatile LONG s_xi_conn[4];
static volatile LONG s_xi_watch[4];   /* a player is mapped to the slot */
static volatile LONG s_xi_beat;       /* poll thread heartbeat */
static XINPUT_VIBRATION s_xi_rum_want[4];
static HANDLE s_xi_rum_ev;
static volatile LONG s_xi_state;      /* 0 not started, 1 running */

static DWORD WINAPI xi_poll_thread(LPVOID arg)
{
    DWORD i;
    (void)arg;
    for (;;) {
        for (i = 0; i < 4; i++) {
            XINPUT_STATE xs;
            DWORD r;
            if (!s_xi_watch[i]) continue;
            r = xi_get(i, &xs);
            EnterCriticalSection(&s_xi_lock);
            if (r == ERROR_SUCCESS) {
                HostPad *o = &s_xi_pad[i];
                o->buttons = xs.Gamepad.wButtons;
                o->lt = xs.Gamepad.bLeftTrigger; o->rt = xs.Gamepad.bRightTrigger;
                o->lx = xs.Gamepad.sThumbLX; o->ly = xs.Gamepad.sThumbLY;
                o->rx = xs.Gamepad.sThumbRX; o->ry = xs.Gamepad.sThumbRY;
                o->packet = xs.dwPacketNumber;
                s_xi_ok[i] = GetTickCount();
            }
            LeaveCriticalSection(&s_xi_lock);
            InterlockedExchange(&s_xi_conn[i], r == ERROR_SUCCESS);
        }
        InterlockedIncrement(&s_xi_beat);
        {   /* the other half: are the game's pad reports still going out? */
            extern volatile DWORD g_usb_report_ms;
            extern volatile int g_ohci_phase;
            static DWORD last_check, warned_for;
            DWORD now = GetTickCount(), at = g_usb_report_ms;
            if (now - last_check > 1000) {
                static DWORD last_state_log;
                last_check = now;
                /* every 10 s, what the pad says and when a report last went
                 * out: if the controls ever lock up, the log shows whether
                 * the pad itself still reads a stick or button held */
                if (now - last_state_log > 10000) {
                    HostPad hp;
                    last_state_log = now;
                    EnterCriticalSection(&s_xi_lock);
                    hp = s_xi_pad[0];
                    LeaveCriticalSection(&s_xi_lock);
                    if (s_xi_watch[0])
                        fprintf(stderr, "  [INPUT] pad: buttons %04X LT %u RT %u left %d,%d right %d,%d; "
                                "last report %ld ms ago\n", hp.buttons, hp.lt, hp.rt, hp.lx, hp.ly, hp.rx, hp.ry,
                                at ? (long)(now - at) : -1L);
                }
                if (at && (LONG)(now - at) > 3000 && warned_for != at) {
                    warned_for = at;
                    fprintf(stderr, "  [INPUT] no pad report has gone to the game for %lu ms "
                            "(USB thread at step %d)\n", (unsigned long)(now - at), g_ohci_phase);
                    fflush(stderr);
                }
            }
        }
        Sleep(2);
    }
    return 0;
}

static DWORD WINAPI xi_rumble_thread(LPVOID arg)
{
    XINPUT_VIBRATION sent[4];
    int known[4] = { 0, 0, 0, 0 };
    DWORD i;
    (void)arg;
    memset(sent, 0, sizeof sent);
    for (;;) {
        WaitForSingleObject(s_xi_rum_ev, 250);
        for (i = 0; i < 4; i++) {
            XINPUT_VIBRATION v;
            DWORD t0, dt;
            EnterCriticalSection(&s_xi_lock);
            v = s_xi_rum_want[i];
            LeaveCriticalSection(&s_xi_lock);
            if (known[i] && !memcmp(&v, &sent[i], sizeof v)) continue;
            if (!known[i] && !v.wLeftMotorSpeed && !v.wRightMotorSpeed && !s_xi_watch[i]) continue;
            t0 = GetTickCount();
            if (xi_set(i, &v) == ERROR_SUCCESS) { sent[i] = v; known[i] = 1; }
            dt = GetTickCount() - t0;
            if (dt > 100) {
                static int said;
                if (said++ < 5) {
                    fprintf(stderr, "  [INPUT] XInputSetState (rumble) took %lu ms on slot %lu\n",
                            (unsigned long)dt, (unsigned long)i);
                    fflush(stderr);
                }
            }
        }
    }
    return 0;
}

static void xi_start(void)
{
    static volatile LONG started;
    HANDLE h;
    if (started || InterlockedExchange(&started, 1)) return;
    InitializeCriticalSection(&s_xi_lock);
    s_xi_rum_ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    h = CreateThread(NULL, 64 * 1024, xi_poll_thread, NULL, 0, NULL);
    if (h) { SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL); CloseHandle(h); }
    h = CreateThread(NULL, 64 * 1024, xi_rumble_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
    InterlockedExchange(&s_xi_state, 1);
}

/* The latest state of XInput slot i, from the poll thread. */
static DWORD xi_read(DWORD i, HostPad *out)
{
    DWORD ok, now;
    xi_start();
    if (!s_xi_watch[i]) InterlockedExchange(&s_xi_watch[i], 1);
    if (!s_xi_conn[i]) return ERROR_DEVICE_NOT_CONNECTED;
    EnterCriticalSection(&s_xi_lock);
    *out = s_xi_pad[i];
    ok = s_xi_ok[i];
    LeaveCriticalSection(&s_xi_lock);
    now = GetTickCount();
    if (now - ok > 250) {
        /* the poll thread has not got an answer for a quarter of a second:
         * a pad at rest, not one frozen with something held */
        static DWORD said_at;
        if (!said_at || now - said_at > 10000) {
            said_at = now | 1;
            fprintf(stderr, "  [INPUT] XInput has not answered for %lu ms (slot %lu); controller read as at rest\n",
                    (unsigned long)(now - ok), (unsigned long)i);
            fflush(stderr);
        }
        memset(out, 0, sizeof *out);
        out->packet = now;
    }
    return ERROR_SUCCESS;
}

static void xi_rumble(DWORD i, WORD l, WORD r)
{
    xi_start();
    EnterCriticalSection(&s_xi_lock);
    s_xi_rum_want[i].wLeftMotorSpeed = l;
    s_xi_rum_want[i].wRightMotorSpeed = r;
    LeaveCriticalSection(&s_xi_lock);
    SetEvent(s_xi_rum_ev);
}
#endif

/* ======================================================================== */
/* SDL2 game controllers                                                    */
/* ======================================================================== */
#if HAVE_SDL_PADS
/* On Windows SDL2 is optional: loaded from SDL2.dll beside the game. */
#if defined(_WIN32)
#define SDLF(ret, name, args) static ret (SDLCALL *p_##name) args;
#else
#define SDLF(ret, name, args) static ret (SDLCALL *p_##name) args = name;
#endif
SDLF(int, SDL_InitSubSystem, (Uint32))
SDLF(SDL_bool, SDL_SetHint, (const char *, const char *))
SDLF(const char *, SDL_GetError, (void))
SDLF(int, SDL_NumJoysticks, (void))
SDLF(SDL_bool, SDL_IsGameController, (int))
SDLF(SDL_GameController *, SDL_GameControllerOpen, (int))
SDLF(void, SDL_GameControllerClose, (SDL_GameController *))
SDLF(SDL_bool, SDL_GameControllerGetAttached, (SDL_GameController *))
SDLF(void, SDL_GameControllerUpdate, (void))
SDLF(Uint8, SDL_GameControllerGetButton, (SDL_GameController *, SDL_GameControllerButton))
SDLF(Sint16, SDL_GameControllerGetAxis, (SDL_GameController *, SDL_GameControllerAxis))
SDLF(int, SDL_GameControllerRumble, (SDL_GameController *, Uint16, Uint16, Uint32))
SDLF(const char *, SDL_GameControllerName, (SDL_GameController *))
SDLF(SDL_GameControllerType, SDL_GameControllerTypeForIndex, (int))
SDLF(SDL_GameControllerType, SDL_GameControllerGetType, (SDL_GameController *))
SDLF(SDL_Joystick *, SDL_GameControllerGetJoystick, (SDL_GameController *))
SDLF(SDL_JoystickID, SDL_JoystickInstanceID, (SDL_Joystick *))
SDLF(SDL_JoystickID, SDL_JoystickGetDeviceInstanceID, (int))
SDLF(int, SDL_JoystickEventState, (int))
SDLF(int, SDL_GameControllerEventState, (int))
SDLF(SDL_bool, SDL_GameControllerHasRumble, (SDL_GameController *))
SDLF(int, SDL_GameControllerSetLED, (SDL_GameController *, Uint8, Uint8, Uint8))
SDLF(void, SDL_GameControllerSetPlayerIndex, (SDL_GameController *, int))
#undef SDLF

static int sdl_load(void)
{
#if defined(_WIN32)
    static int st;            /* 0 untried, 1 ok, -1 none */
    HMODULE m;
    if (st) return st > 0;
    st = -1;
    if (getenv("RECOMP_NO_SDL_PADS")) return 0;
    m = LoadLibraryA("SDL2.dll");
    if (!m) return 0;
#define L(name) if (!(*(FARPROC *)&p_##name = GetProcAddress(m, #name))) { \
        fprintf(stderr, "  [INPUT] SDL2.dll lacks %s; PlayStation pads off\n", #name); return 0; }
    L(SDL_InitSubSystem) L(SDL_SetHint) L(SDL_GetError) L(SDL_NumJoysticks) L(SDL_IsGameController)
    L(SDL_GameControllerOpen) L(SDL_GameControllerClose) L(SDL_GameControllerGetAttached)
    L(SDL_GameControllerUpdate) L(SDL_GameControllerGetButton) L(SDL_GameControllerGetAxis)
    L(SDL_GameControllerRumble) L(SDL_GameControllerName) L(SDL_GameControllerTypeForIndex)
    L(SDL_GameControllerGetType) L(SDL_GameControllerGetJoystick) L(SDL_JoystickInstanceID)
    L(SDL_JoystickGetDeviceInstanceID) L(SDL_JoystickEventState) L(SDL_GameControllerEventState)
    L(SDL_GameControllerHasRumble) L(SDL_GameControllerSetLED) L(SDL_GameControllerSetPlayerIndex)
#undef L
    st = 1;
    return 1;
#else
    return !getenv("RECOMP_NO_SDL_PADS");
#endif
}

/* SDL's joystick functions all run on one thread of their own: it opens the
 * pads as they are plugged in, reads them every 4 ms into the table below and
 * sends rumble. The game's polls only read the table. */
#define SP_MAX 8
static struct {
    SDL_GameController *gc;   /* owned by the pad thread */
    SDL_JoystickID id;
    int connected;            /* published */
    HostPad st;               /* published */
    WORD want_l, want_r;      /* rumble asked for */
    WORD sent_l, sent_r;
    DWORD sent_at;
    int is_ps;
    char name[64];
} s_sp[SP_MAX];
static CRITICAL_SECTION s_sp_lock;
static volatile LONG s_sp_state;   /* 0 not started, 1 running, -1 unavailable */

/* SDL's subsystems are brought up from more than one thread (the window's,
 * this one); SDL_InitSubSystem itself is not safe to call concurrently. */
static CRITICAL_SECTION s_sdl_init_cs;
static volatile LONG s_sdl_init_cs_ok;
void xbox_SDLInitLock(int lock)
{
    if (!InterlockedCompareExchange(&s_sdl_init_cs_ok, 1, 0)) {
        InitializeCriticalSection(&s_sdl_init_cs);
        InterlockedExchange(&s_sdl_init_cs_ok, 2);
    }
    while (s_sdl_init_cs_ok != 2) Sleep(0);
    if (lock) EnterCriticalSection(&s_sdl_init_cs); else LeaveCriticalSection(&s_sdl_init_cs);
}

static int sp_is_xinput_type(SDL_GameControllerType t)
{
    return t == SDL_CONTROLLER_TYPE_XBOX360 || t == SDL_CONTROLLER_TYPE_XBOXONE;
}
static int sp_is_ps_type(SDL_GameControllerType t)
{
    return t == SDL_CONTROLLER_TYPE_PS3 || t == SDL_CONTROLLER_TYPE_PS4 || t == SDL_CONTROLLER_TYPE_PS5;
}

static void sp_scan(void)
{
    int i, n = p_SDL_NumJoysticks(), k;
    /* drop the ones unplugged */
    for (k = 0; k < SP_MAX; k++)
        if (s_sp[k].gc && !p_SDL_GameControllerGetAttached(s_sp[k].gc)) {
            fprintf(stderr, "  [INPUT] controller unplugged: %s\n", s_sp[k].name);
            fflush(stderr);
            p_SDL_GameControllerClose(s_sp[k].gc);
            EnterCriticalSection(&s_sp_lock);
            memset(&s_sp[k], 0, sizeof s_sp[k]);
            LeaveCriticalSection(&s_sp_lock);
        }
    for (i = 0; i < n; i++) {
        SDL_JoystickID id = p_SDL_JoystickGetDeviceInstanceID(i);
        SDL_GameControllerType t;
        SDL_GameController *gc;
        int slot = -1;
        if (!p_SDL_IsGameController(i)) continue;
        for (k = 0; k < SP_MAX; k++) if (s_sp[k].gc && s_sp[k].id == id) break;
        if (k < SP_MAX) continue;                   /* already open */
        t = p_SDL_GameControllerTypeForIndex(i);
#if defined(_WIN32)
        if (sp_is_xinput_type(t)) continue;         /* XInput has it */
#endif
        for (k = 0; k < SP_MAX; k++) if (!s_sp[k].gc) { slot = k; break; }
        if (slot < 0) break;
        gc = p_SDL_GameControllerOpen(i);
        if (!gc) continue;
        EnterCriticalSection(&s_sp_lock);
        s_sp[slot].gc = gc;
        s_sp[slot].id = p_SDL_JoystickInstanceID(p_SDL_GameControllerGetJoystick(gc));
        {
            SDL_GameControllerType gt = p_SDL_GameControllerGetType(gc);
            s_sp[slot].is_ps = !sp_is_ps_type(gt) ? 0
                             : gt == SDL_CONTROLLER_TYPE_PS3 ? 3 : gt == SDL_CONTROLLER_TYPE_PS5 ? 5 : 4;
        }
        snprintf(s_sp[slot].name, sizeof s_sp[slot].name, "%s",
                 p_SDL_GameControllerName(gc) ? p_SDL_GameControllerName(gc) : "controller");
        LeaveCriticalSection(&s_sp_lock);
        fprintf(stderr, "  [INPUT] controller connected: %s (%s, rumble %s)\n", s_sp[slot].name,
                t == SDL_CONTROLLER_TYPE_PS3 ? "PlayStation 3" : t == SDL_CONTROLLER_TYPE_PS4 ? "PlayStation 4"
                : t == SDL_CONTROLLER_TYPE_PS5 ? "PlayStation 5" : sp_is_xinput_type(t) ? "Xbox" : "SDL",
                p_SDL_GameControllerHasRumble(gc) ? "yes" : "no");
        fflush(stderr);
        /* the light bar of a DualShock 4 / DualSense in the colour of the
         * player-1 light on an Xbox pad: green */
        if (s_sp[slot].is_ps) p_SDL_GameControllerSetLED(gc, 0, 160, 40);
    }
}

static void sp_read(int k)
{
    SDL_GameController *c = s_sp[k].gc;
    HostPad p;
    WORD b = 0;
    static const struct { SDL_GameControllerButton sb; WORD xb; } map[] = {
        { SDL_CONTROLLER_BUTTON_A, XINPUT_GAMEPAD_A }, { SDL_CONTROLLER_BUTTON_B, XINPUT_GAMEPAD_B },
        { SDL_CONTROLLER_BUTTON_X, XINPUT_GAMEPAD_X }, { SDL_CONTROLLER_BUTTON_Y, XINPUT_GAMEPAD_Y },
        { SDL_CONTROLLER_BUTTON_LEFTSHOULDER, XINPUT_GAMEPAD_LEFT_SHOULDER },
        { SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER },
        { SDL_CONTROLLER_BUTTON_START, XINPUT_GAMEPAD_START }, { SDL_CONTROLLER_BUTTON_BACK, XINPUT_GAMEPAD_BACK },
        /* the DualShock 4 / DualSense touch pad click is their Back */
        { SDL_CONTROLLER_BUTTON_TOUCHPAD, XINPUT_GAMEPAD_BACK },
        { SDL_CONTROLLER_BUTTON_DPAD_UP, XINPUT_GAMEPAD_DPAD_UP }, { SDL_CONTROLLER_BUTTON_DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN },
        { SDL_CONTROLLER_BUTTON_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT }, { SDL_CONTROLLER_BUTTON_DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT },
        { SDL_CONTROLLER_BUTTON_LEFTSTICK, XINPUT_GAMEPAD_LEFT_THUMB },
        { SDL_CONTROLLER_BUTTON_RIGHTSTICK, XINPUT_GAMEPAD_RIGHT_THUMB },
    };
    size_t i;
    int lt, rt;
    memset(&p, 0, sizeof p);
    for (i = 0; i < sizeof map / sizeof map[0]; i++)
        if (p_SDL_GameControllerGetButton(c, map[i].sb)) b |= map[i].xb;
    p.buttons = b;
    /* SDL triggers are 0..32767; XInput's 0..255 */
    lt = p_SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    rt = p_SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    p.lt = (BYTE)(lt <= 0 ? 0 : lt >= 32767 ? 255 : lt >> 7);
    p.rt = (BYTE)(rt <= 0 ? 0 : rt >= 32767 ? 255 : rt >> 7);
    /* SDL's y axes point down, XInput's up; (-1 - v) keeps -32768 in range */
    p.lx = p_SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    p.ly = (SHORT)(-1 - p_SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
    p.rx = p_SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
    p.ry = (SHORT)(-1 - p_SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));
    EnterCriticalSection(&s_sp_lock);
    if (memcmp(&p, &s_sp[k].st, sizeof p - sizeof p.packet)) p.packet = s_sp[k].st.packet + 1;
    else p.packet = s_sp[k].st.packet;
    s_sp[k].st = p;
    s_sp[k].connected = 1;
    LeaveCriticalSection(&s_sp_lock);
}

static void sp_rumble(int k)
{
    WORD l = s_sp[k].want_l, r = s_sp[k].want_r;
    DWORD now = GetTickCount();
    /* SDL rumble runs for a set time: re-sent each second while it is on */
    if (l == s_sp[k].sent_l && r == s_sp[k].sent_r && (!(l | r) || now - s_sp[k].sent_at < 1000)) return;
    p_SDL_GameControllerRumble(s_sp[k].gc, l, r, (l | r) ? 2000 : 0);
    s_sp[k].sent_l = l; s_sp[k].sent_r = r; s_sp[k].sent_at = now;
}

extern void xbox_UnpinServiceThread(void);
static DWORD WINAPI sp_thread(LPVOID arg)
{
    DWORD last_scan = 0;
    int k;
    (void)arg;
    xbox_UnpinServiceThread();
    /* PlayStation pads through SDL's own drivers, with rumble (a DualShock 4
     * or DualSense on Bluetooth needs the extended reports for that). */
    p_SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS4_RUMBLE", "1");
    p_SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS5_RUMBLE", "1");
#if defined(_WIN32) || defined(__APPLE__)
    p_SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS3", "1");
#else
    /* Linux: a DualShock 3 through the kernel's own driver (hid-sony, evdev),
     * as SDL does by default there. Forced onto SDL's HIDAPI driver it was
     * found and named, but on a system whose hidraw node is readable (a udev
     * rule) no report ever came: the kernel driver holds the pad (GitHub
     * issue 14). TT_PS3_HIDAPI=1: SDL's HIDAPI driver all the same */
    p_SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS3", getenv("TT_PS3_HIDAPI") ? getenv("TT_PS3_HIDAPI") : "0");
#endif
    p_SDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
#if defined(_WIN32)
    /* Xbox pads are XInput's; SDL must not report them a second time */
    p_SDL_SetHint("SDL_JOYSTICK_RAWINPUT", "0");
    p_SDL_SetHint("SDL_JOYSTICK_WGI", "0");
    p_SDL_SetHint("SDL_JOYSTICK_HIDAPI_XBOX", "0");
    p_SDL_SetHint("SDL_JOYSTICK_THREAD", "1");
#endif
    xbox_SDLInitLock(1);
    k = p_SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    xbox_SDLInitLock(0);
    if (k != 0) {
        fprintf(stderr, "  [INPUT] SDL game controllers unavailable: %s\n", p_SDL_GetError());
        fflush(stderr);
        InterlockedExchange(&s_sp_state, -1);
        return 0;
    }
    p_SDL_JoystickEventState(SDL_IGNORE);
    p_SDL_GameControllerEventState(SDL_IGNORE);
    InterlockedExchange(&s_sp_state, 1);
    for (;;) {
        DWORD now = GetTickCount();
        p_SDL_GameControllerUpdate();
        if (!last_scan || now - last_scan > 500) { sp_scan(); last_scan = now; }
        for (k = 0; k < SP_MAX; k++)
            if (s_sp[k].gc) { sp_read(k); sp_rumble(k); }
        Sleep(4);
    }
    return 0;
}

static void sp_start(void)
{
    static volatile LONG started;
    HANDLE h;
    if (started || InterlockedExchange(&started, 1)) return;
    InitializeCriticalSection(&s_sp_lock);
    if (!sdl_load()) { InterlockedExchange(&s_sp_state, -1); return; }
    h = CreateThread(NULL, 256 * 1024, sp_thread, NULL, 0, NULL);
    if (h) CloseHandle(h);
    else InterlockedExchange(&s_sp_state, -1);
}
#else
void xbox_SDLInitLock(int lock) { (void)lock; }
static void sp_start(void) {}
#endif

/* ======================================================================== */
/* One list of pads                                                         */
/* ======================================================================== */

/* Which pad each player has: XInput slots 0-3 are pads 0-3, SDL pads 8+k.
 * Kept up to date by a scan on a thread of its own: XInputGetState on an
 * EMPTY slot is slow on many Windows PCs (it looks for new hardware every
 * time), so the game's own polls only ever read the one pad each player is
 * mapped to. */
#define PAD_NONE 0xFFu
static volatile LONG s_player_pad[XBOX_MAX_CONTROLLERS] = { PAD_NONE, PAD_NONE, PAD_NONE, PAD_NONE };
/* (a title's two-player modes) set by the title: with one controller and both
 * players on automatic, the controller stays player 1's and player 2 has the
 * keyboard and mouse (co-op: the controller in hand kept working as the game
 * switched to two players; 1.9.6 gave it to player 2) */
volatile int xbox_input_two_players;
static volatile LONG s_pads_found;
/* (two players) each player's device: -1 automatic, 0 keyboard and mouse
 * (player 1; for player 2: none), n the n-th controller connected */
volatile int xbox_input_dev_choice[2] = { -1, -1 };
static DWORD s_dev_list[8];
static volatile int s_dev_n;
static void pad_name(DWORD p, char *out, int n);
/* the controllers connected (the last scan) and their names */
int xbox_InputDeviceCount(void) { return s_dev_n; }
void xbox_InputDeviceName(int i, char *out, int n)
{
    if (i < 0 || i >= s_dev_n) { snprintf(out, n, "(not connected)"); return; }
    pad_name(s_dev_list[i], out, n);
}
/* the player the keyboard and mouse are (two players: player 2's when chosen, or
 * when player 1 has a controller and player 2 none) */
int xbox_InputKeyboardPort(void)
{
    if (!xbox_input_two_players || xbox_input_dev_choice[0] == 0) return 0;
    if (xbox_input_dev_choice[1] == 0) return 1;
    return s_player_pad[0] != PAD_NONE && s_player_pad[1] == PAD_NONE ? 1 : 0;
}
/* the device player n (0-1) has now: 0 keyboard / none, k the k-th controller */
int xbox_InputPlayerDevice(int port)
{
    int i;
    DWORD p = port >= 0 && port < XBOX_MAX_CONTROLLERS ? (DWORD)s_player_pad[port] : PAD_NONE;
    if (p == PAD_NONE) return 0;
    for (i = 0; i < s_dev_n; i++) if (s_dev_list[i] == p) return i + 1;
    return 0;
}
/* how many controllers are connected (the last scan) */
int xbox_InputPadsConnected(void) { return (int)s_pads_found; }
/* the pad player n reads (XInput slot 0-3, SDL pad 8+k, 255 none) */
int xbox_InputPlayerPad(int port) { return port >= 0 && port < XBOX_MAX_CONTROLLERS ? (int)s_player_pad[port] : 255; }
/* has player n (0-3) a device: a controller, or for player 1 the keyboard */
int xbox_InputPlayerHasDevice(int port)
{
    if (port < 0 || port >= XBOX_MAX_CONTROLLERS) return 0;
    if (s_player_pad[port] != PAD_NONE) return 1;
    return port == xbox_InputKeyboardPort();
}
/* the connected pads in the scan's order, for a title's settings page
 * (xbox_InputPadCount / Name / Peek / PlayerPadIndex) */
static volatile LONG s_found_n, s_found[XBOX_MAX_CONTROLLERS + 8];
static void pads_scan_once(void)
{
    DWORD i, k, nf = 0, found[XBOX_MAX_CONTROLLERS + 8];
#if defined(_WIN32)
    XINPUT_STATE st;
    for (i = 0; i < 4; i++)
        if (xi_get(i, &st) == ERROR_SUCCESS) found[nf++] = i;
#endif
#if HAVE_SDL_PADS
    if (s_sp_state > 0) {
        EnterCriticalSection(&s_sp_lock);
        for (k = 0; k < SP_MAX; k++)
            if (s_sp[k].gc && s_sp[k].connected && nf < sizeof found / sizeof found[0]) found[nf++] = 8 + k;
        LeaveCriticalSection(&s_sp_lock);
    }
#endif
    {   /* the list of controllers, for the title's device choice (in order found) */
        DWORD c;
        for (c = 0; c < nf && c < 8; c++) s_dev_list[c] = found[c];
        s_dev_n = nf < 8 ? (int)nf : 8;
    }
    if (xbox_input_two_players && (xbox_input_dev_choice[0] >= 0 || xbox_input_dev_choice[1] >= 0)) {
        /* two players, devices chosen: -1 automatic, 0 the keyboard and mouse
         * (player 1) / none, n the n-th controller found */
        DWORD pad[2] = { PAD_NONE, PAD_NONE }, used = 0, p;
        for (p = 0; p < 2; p++) {
            int c = xbox_input_dev_choice[p];
            if (c >= 1 && (DWORD)c <= nf) { pad[p] = found[c - 1]; used |= 1u << (c - 1); }
        }
        for (p = 0; p < 2; p++) {
            DWORD f;
            if (xbox_input_dev_choice[p] >= 0) continue;
            /* automatic beside the other's choice: the first controller free
             * (player 1 without one: the keyboard and mouse) */
            for (f = 0; f < nf; f++) if (!(used & (1u << f))) { pad[p] = found[f]; used |= 1u << f; break; }
        }
        InterlockedExchange(&s_player_pad[0], (LONG)pad[0]);
        InterlockedExchange(&s_player_pad[1], (LONG)pad[1]);
        for (k = 2; k < XBOX_MAX_CONTROLLERS; k++) InterlockedExchange(&s_player_pad[k], (LONG)PAD_NONE);
    } else
    if (xbox_input_two_players && nf == 1) {
        /* two players, one controller: player 1 keeps it, player 2 has the
         * keyboard and mouse (xbox_InputKeyboardPort) */
        InterlockedExchange(&s_player_pad[0], (LONG)found[0]);
        InterlockedExchange(&s_player_pad[1], (LONG)PAD_NONE);
        for (k = 2; k < XBOX_MAX_CONTROLLERS; k++) InterlockedExchange(&s_player_pad[k], (LONG)PAD_NONE);
    } else
    for (k = 0; k < XBOX_MAX_CONTROLLERS; k++)
        InterlockedExchange(&s_player_pad[k], (LONG)(k < nf ? found[k] : PAD_NONE));
    InterlockedExchange(&s_pads_found, (LONG)nf);
    for (k = 0; k < nf && k < XBOX_MAX_CONTROLLERS + 8; k++) InterlockedExchange(&s_found[k], (LONG)found[k]);
    InterlockedExchange(&s_found_n, (LONG)nf);
    {   /* each player's device, logged as it changes */
        static LONG said0 = -2, said1 = -2, said_two = -1, said_kb = -1;
        LONG p0 = s_player_pad[0], p1 = s_player_pad[1], kb = xbox_InputKeyboardPort();
        if (p0 != said0 || p1 != said1 || said_two != xbox_input_two_players || said_kb != kb) {
            said0 = p0; said1 = p1; said_two = xbox_input_two_players; said_kb = kb;
            fprintf(stderr, "  [INPUT] %s: player 1 %s%ld, player 2 %s%ld, keyboard and mouse player %ld (choices %d / %d, %lu connected)\n",
                    xbox_input_two_players ? "two players" : "one player",
                    p0 == (LONG)PAD_NONE ? "no controller " : "pad ", p0 == (LONG)PAD_NONE ? 0L : (long)p0,
                    p1 == (LONG)PAD_NONE ? "no controller " : "pad ", p1 == (LONG)PAD_NONE ? 0L : (long)p1,
                    (long)kb + 1, xbox_input_dev_choice[0], xbox_input_dev_choice[1], (unsigned long)nf);
            fflush(stderr);
        }
    }
    {
        static DWORD said = 0xFFFFFFFFu, said_first = 0xFFFFFFFFu;
        if (said != nf || (nf && said_first != found[0])) {
            said = nf; said_first = nf ? found[0] : 0;
            if (!nf) fprintf(stderr, "  [INPUT] no controller\n");
            else if (found[0] < 8) fprintf(stderr, "  [INPUT] %lu controller(s); player 1 = XInput slot %lu\n",
                                           (unsigned long)nf, (unsigned long)found[0]);
#if HAVE_SDL_PADS
            else fprintf(stderr, "  [INPUT] %lu controller(s); player 1 = %s\n", (unsigned long)nf,
                         s_sp[found[0] - 8].name);
#endif
            fflush(stderr);
        }
    }
    (void)i;
}
static void pad_name(DWORD p, char *out, int n)
{
#if HAVE_SDL_PADS
    if (p >= 8 && p < 8 + SP_MAX) { snprintf(out, n, "%s", s_sp[p - 8].name[0] ? s_sp[p - 8].name : "Controller"); return; }
#endif
    snprintf(out, n, "Xbox Controller %lu", (unsigned long)p + 1);
}
static DWORD WINAPI pads_scan_thread(LPVOID p)
{
    (void)p;
    for (;;) { pads_scan_once(); Sleep(500); }
    return 0;
}
static DWORD pad_for(DWORD port)
{
    static volatile LONG started;
    if (!started && !InterlockedExchange(&started, 1)) {
        /* The first scan runs on the scan thread too. It loads XInput and
         * polls all four slots (slow when they are empty), and the first
         * caller can be any thread -- in 1.1.3 it was the USB emulation's,
         * part-way through the game connecting its pad: the connection timed
         * out and there was no input at all. Until it answers (a few ms) the
         * pad reads as not there, which the keyboard covers. */
        HANDLE h;
        sp_start();
        h = CreateThread(NULL, 64 * 1024, pads_scan_thread, NULL, 0, NULL);
        if (h) { SetThreadPriority(h, THREAD_PRIORITY_BELOW_NORMAL); CloseHandle(h); }
    }
    return port < XBOX_MAX_CONTROLLERS ? (DWORD)s_player_pad[port] : PAD_NONE;
}

/* Read pad p; ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED. */
static DWORD pad_read(DWORD p, HostPad *out)
{
#if defined(_WIN32)
    if (p < 4)
        return xi_read(p, out);
#endif
#if HAVE_SDL_PADS
    if (p >= 8 && p < 8 + SP_MAX && s_sp_state > 0) {
        DWORD r = ERROR_DEVICE_NOT_CONNECTED;
        EnterCriticalSection(&s_sp_lock);
        if (s_sp[p - 8].gc && s_sp[p - 8].connected) { *out = s_sp[p - 8].st; r = ERROR_SUCCESS; }
        LeaveCriticalSection(&s_sp_lock);
        return r;
    }
#endif
    (void)out;
    return ERROR_DEVICE_NOT_CONNECTED;
}

/* Set pad p's motors (0-65535). */
static DWORD pad_rumble(DWORD p, WORD l, WORD r)
{
#if defined(_WIN32)
    if (p < 4) {
        xi_rumble(p, l, r);           /* sent by the rumble thread */
        return ERROR_SUCCESS;
    }
#endif
#if HAVE_SDL_PADS
    if (p >= 8 && p < 8 + SP_MAX && s_sp_state > 0) {
        /* sent by the pad thread */
        s_sp[p - 8].want_l = l; s_sp[p - 8].want_r = r;
        return ERROR_SUCCESS;
    }
#endif
    (void)l; (void)r;
    return ERROR_DEVICE_NOT_CONNECTED;
}

/* The connected controllers, in the scan's order (a title's settings page):
 * how many, the k-th one's name, and its state as it is (no bindings). */
int xbox_InputPadCount(void) { pad_for(0); return (int)s_found_n; }
void xbox_InputPadName(int k, char *out, int n)
{
    DWORD p;
    if (!out || n <= 0) return;
    out[0] = 0;
    if (k < 0 || k >= (int)s_found_n) return;
    p = (DWORD)s_found[k];
    if (p < 8) { snprintf(out, (size_t)n, "XInput %lu", (unsigned long)p + 1); return; }
#if HAVE_SDL_PADS
    if (p - 8 < SP_MAX) { snprintf(out, (size_t)n, "%s", s_sp[p - 8].name); return; }
#endif
    snprintf(out, (size_t)n, "pad %lu", (unsigned long)p);
}
int xbox_InputPadPeek(int k, unsigned short *buttons, unsigned char *lt, unsigned char *rt, short *lx, short *ly, short *rx, short *ry)
{
    HostPad hp;
    if (k < 0 || k >= (int)s_found_n || pad_read((DWORD)s_found[k], &hp) != ERROR_SUCCESS) return 0;
    if (buttons) *buttons = hp.buttons;
    if (lt) *lt = hp.lt;
    if (rt) *rt = hp.rt;
    if (lx) *lx = hp.lx;
    if (ly) *ly = hp.ly;
    if (rx) *rx = hp.rx;
    if (ry) *ry = hp.ry;
    return 1;
}
/* which of the connected controllers player 'port' has now (-1: none) */
int xbox_InputPlayerPadIndex(int port)
{
    LONG p, k;
    if (port < 0 || port >= XBOX_MAX_CONTROLLERS) return -1;
    p = s_player_pad[port];
    for (k = 0; k < s_found_n; k++) if (s_found[k] == p) return (int)k;
    return -1;
}

/* Is player 1's pad a PlayStation one (for the button prompts). */
int xbox_InputPlayerPadIsPlayStation(DWORD port)
{
#if HAVE_SDL_PADS
    DWORD p = port < XBOX_MAX_CONTROLLERS ? (DWORD)s_player_pad[port] : PAD_NONE;
    return p >= 8 && p < 8 + SP_MAX && s_sp[p - 8].is_ps;
#else
    (void)port;
    return 0;
#endif
}

/* Which PlayStation pad a player holds: 3, 4 or 5, or 0 for none (an Xbox
 * pad, the keyboard, nothing). For the controls pictures. */
int xbox_InputPlayerPadPlayStationModel(DWORD port)
{
#if HAVE_SDL_PADS
    DWORD p = port < XBOX_MAX_CONTROLLERS ? (DWORD)s_player_pad[port] : PAD_NONE;
    return p >= 8 && p < 8 + SP_MAX ? s_sp[p - 8].is_ps : 0;
#else
    (void)port;
    return 0;
#endif
}

/* The last PlayStation pad that was connected, 3/4/5, or 0: the model to draw
 * when player 1 has no PlayStation pad of their own but one was seen. */
int xbox_InputAnyPlayStationModel(void)
{
#if HAVE_SDL_PADS
    int i, m = 0;
    for (i = 0; i < SP_MAX; i++) if (s_sp[i].connected && s_sp[i].is_ps) m = s_sp[i].is_ps;
    return m;
#else
    return 0;
#endif
}

/* Stick dead zone ([Input] stick_deadzone, percent; RECOMP_STICK_DEADZONE).
 * Worn and modern sticks rest a little off centre, which the game -- made
 * for the Xbox's own pads -- can read as a slow walk. Inside the zone the
 * stick reads centred; outside it the range is stretched back to full, so
 * the stick still reaches every speed. Radial, so diagonals are not cut.
 * 0 turns it off (the pad's raw values, as before). */
/* (1.9.3) player 1's last read: its left stick came from the keys (W A S D),
 * its left trigger from a key or mouse button rather than the controller --
 * for a title's mouse steering and aiming, which take the stick only then */
volatile int xbox_input_kb_lstick, xbox_input_kb_lt;
static void stick_deadzone(SHORT *px, SHORT *py)
{
    float dz;
    float x = *px, y = *py, m, k;
    {   /* (1.9.7) the F10 menu's More page (the ini's [Input] stick_deadzone, RECOMP_STICK_DEADZONE, its start) */
        float pc = xbox_GfxGetF(GFX_DEADZONE);
        if (pc < 0) pc = 0;
        if (pc > 50) pc = 50;
        dz = pc * 32767.0f / 100.0f;
    }
    if (dz <= 0) return;
    m = sqrtf(x * x + y * y);
    if (m <= dz) { *px = *py = 0; return; }
    k = (m - dz) / (32767.0f - dz);
    if (k > 1.0f) k = 1.0f;
    k = k * 32767.0f / m;
    x *= k; y *= k;
    *px = (SHORT)(x > 32767.0f ? 32767 : x < -32768.0f ? -32768 : x);
    *py = (SHORT)(y > 32767.0f ? 32767 : y < -32768.0f ? -32768 : y);
}

static BOOL  g_controller_connected[XBOX_MAX_CONTROLLERS] = { FALSE };

/* ---- keyboard, when there is no pad --------------------------------------
 *
 * A title that waits on PRESS START is unreachable on a machine with no
 * controller plugged in, which is most machines someone brings this up on.
 * The whole point of a recompilation is to be able to look at the thing
 * running, and a build nobody can press a button in cannot be looked at.
 *
 * Off by default, because a keyboard silently acting as player 1 is
 * surprising when a real pad is what you meant to use. RECOMP_KEYBOARD=1
 * turns it on. It only ever answers for port 0, and is merged on top of a
 * pad connected there, so a real controller keeps working.
 *
 * The keys are the ones a Dreamcast or Saturn emulator would pick, which is
 * the closest thing to a convention here:
 *
 *   arrows        d-pad             Enter      START
 *   Z X A S       A B X Y           Backspace  BACK
 *   Q E           white black       1 3        triggers
 *   numpad 8/2/4/6 left thumb       I/K/J/L    right thumb
 *
 * Keys come from the framebuffer window, which only receives them while it
 * has the focus, so typing in another window does not drive the game.
 */
static BOOL keyboard_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_KEYBOARD");
        on = (v && *v && *v != '0') ? 1 : 0;
    }
    return on ? TRUE : FALSE;
}

#if defined(_WIN32)
/* The framebuffer window records what is held; see src/video/fb_present.c.
 *
 * GetAsyncKeyState was the first attempt and reads nothing here -- under
 * Wine it answers about a state a GDI-drawing guest never touches. Going
 * through the window is also the better gate: a window only receives keys
 * while it has the focus, so there is no separate focus check to get wrong
 * and no way for typing in another application to drive the game.
 *
 * Declared rather than included so src/input does not depend on the video
 * module's include path. */
extern int xbox_FramebufferKeyDown(int vk);

/* Also asked of Windows directly while our window is in front: on Windows
 * GetAsyncKeyState sees every key and mouse button however the window's
 * messages are routed (a v29/v30 PC had a working pad but no keyboard), and
 * the foreground check keeps typing in other programs out of the game.
 * Under Wine it reads nothing, which is why the window's own record stays. */
extern void *xbox_FramebufferHwnd(void);
extern void xbox_FramebufferKeyClear(int vk);
static BOOL key_down(int vk)
{
    HWND fg;
    int ours;
    static volatile int async_works = -1;     /* GetAsyncKeyState has reported a key (or this is Windows itself) */
    static DWORD stale_since[256];
    if (async_works < 0) {   /* (1.9.9) on Windows itself the keyboard can always be read: a key held by the window's
                              * messages alone is a lost release from the start -- the Enter that launched the game stayed
                              * "held" as Start, so the controller's Start never paused and the menus kept choosing (a
                              * playtest; only an Alt press, which proved the keyboard readable, let it go). Under Wine,
                              * GetAsyncKeyState reads nothing: there only after it has shown a key, as before */
        HMODULE nt = GetModuleHandleA("ntdll.dll");
        async_works = (nt && GetProcAddress(nt, "wine_get_version")) ? 0 : 1;
    }
    /* Alt+Enter toggles full screen; it is not Start. */
    if (vk == VK_RETURN && (GetAsyncKeyState(VK_MENU) & 0x8000)) return FALSE;
    fg = GetForegroundWindow();
    ours = 0;
    if (fg) {
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        ours = fg == (HWND)xbox_FramebufferHwnd() || pid == GetCurrentProcessId();
    }
    if (ours && (GetAsyncKeyState(vk) & 0x8000)) {
        /* Any window of ours in front (the game window, or the console the
         * log goes to) counts: the keys are read straight from the
         * keyboard, so a missed focus message cannot lose them. */
        async_works = 1;
        if ((unsigned)vk < 256) stale_since[vk] = 0;
        return TRUE;
    }
    if (xbox_FramebufferKeyDown(vk)) {
        /* Held by the window's messages, but the keyboard says it is up: a
         * release that never arrived (the key let go while a dialog, the
         * overlay or another program had the keyboard). Left alone, that
         * key -- W, say -- stays held for ever: the hero runs on his own and
         * its stick overrides the controller's. Once the keyboard has shown
         * it can be read, a key it reports up for a quarter of a second is
         * let go. */
        if (ours && async_works && (unsigned)vk < 256) {
            DWORD now = GetTickCount();
            if (!stale_since[vk]) stale_since[vk] = now | 1;
            else if ((int)(now - stale_since[vk]) > 250) {   /* (signed: the stamp is now | 1) */
                xbox_FramebufferKeyClear(vk);
                stale_since[vk] = 0;
                fprintf(stderr, "  [INPUT] key 0x%02X was held with no release: let go\n", vk);
                fflush(stderr);
                return FALSE;
            }
        }
        return TRUE;
    }
    return FALSE;
}
#else
/* POSIX: the window (fb_present.c, SDL) records the keys while it has the
 * focus. */
extern int xbox_FramebufferKeyDown(int vk);
extern void xbox_FramebufferKeyClear(int vk);
extern void *xbox_FramebufferHwnd(void);
extern int xbox_FramebufferHasFocus(void);
static BOOL key_down(int vk)
{
    if (vk == VK_RETURN && xbox_FramebufferKeyDown(VK_MENU)) return FALSE;   /* Alt+Enter: full screen */
    return xbox_FramebufferKeyDown(vk) ? TRUE : FALSE;
}
#endif

static int game_in_front(void);
/* Is the game's window in front (input reaches the game)? For a title's pointer. */
int xbox_InputGameInFront(void) { return game_in_front(); }
/* The mouse buttons (bit 0 left, 1 right, 2 middle), for a title's pointer. */
int xbox_InputMouseButtons(void)
{
    return (key_down(VK_LBUTTON) ? 1 : 0) | (key_down(VK_RBUTTON) ? 2 : 0) | (key_down(VK_MBUTTON) ? 4 : 0);
}
/* For the settings overlay's "press a key". */
int xbox_InputKeyDown(int vk) { return key_down(vk) ? 1 : 0; }

/* Two keyboard layouts.
 *
 * Default ("wasd"): W/A/S/D move (left stick), the mouse buttons and
 * J/K/L/I are the face buttons laid out like the pad (J=X left, K=A bottom,
 * L=B right, I=Y top; left click = A, right click = X), arrows = d-pad,
 * numpad 8/4/2/6 = right stick, F/R = left/right trigger, Z/C = white/black,
 * Enter or Esc = Start, Backspace or Tab = Back, Shift / middle click = the
 * left / right stick click.
 *
 * "classic" (RECOMP_KB_LAYOUT=classic, ini [Input] layout=classic): the
 * original layout -- Z/X/A/S = A/B/X/Y, numpad = left stick, I/J/K/L =
 * right stick, Q/E white/black, 1/3 triggers. */
static int kb_classic(void) __attribute__((unused));
static int kb_classic(void)
{
    static int c = -1;
    if (c < 0) {
        const char *v = getenv("RECOMP_KB_LAYOUT");
        c = (v && (v[0] == 'c' || v[0] == 'C')) ? 1 : 0;
    }
    return c;
}

/* Test automation: RECOMP_PAD_SCRIPT=<file> with lines "<ms> <button>"
 * (ms since the first poll; button = A B X Y START BACK UP DOWN LEFT RIGHT
 * WHITE BLACK LT RT), each held for 400 ms. */
/* A line may also be "<file>+<ms> <button>": ms after the guest first
 * opens a path containing <file> (e.g. mainmenu.bff+3000 START). */
static char s_anchor_name[32][32];
static volatile DWORD s_anchor_t[32];
static int s_anchor_n;
static int s_ps_loaded, s_ps_n, s_ps_cleared; static DWORD s_ps_t0;
static volatile unsigned s_ps_flags;
int xbox_PadScriptFlag(int n) { return n > 0 && n < 32 && (s_ps_flags & (1u << n)) != 0; }
static struct { DWORD ms; int anchor; char b[8]; } s_ps_ev[512];
/* Loaded on first use: the first input poll, or the first file open (an
 * anchor's file can be opened before the game ever reads the pad). */
/* (1.9.5) testing: RECOMP_PAD_GAMETIME=1 -- the script's times in the
 * game's own time (the sum of its frame times, xbox_pad_clock), so a run at
 * a fixed frame time (TT_FAKE_FRAME_MS) presses the same buttons at the same
 * moments of the game whatever the real frame rate */
uint32_t (*volatile xbox_pad_clock)(void);
static DWORD pad_now(void)
{
    static int gt = -1;
    if (gt < 0) gt = getenv("RECOMP_PAD_GAMETIME") != NULL;
    return (gt && xbox_pad_clock) ? (DWORD)xbox_pad_clock() : GetTickCount();
}
static void pad_script_load(void)
{
    const char *f;
    FILE *fp;
    char w[64], b[16];
    if (s_ps_loaded) return;
    s_ps_loaded = 1;
    f = getenv("RECOMP_PAD_SCRIPT");
    fp = f ? fopen(f, "r") : NULL;
    if (fp) {
        while (s_ps_n < 512 && fscanf(fp, "%63s %15s", w, b) == 2) {
            char *plus = strchr(w, '+');
            s_ps_ev[s_ps_n].anchor = -1;
            if (plus) {
                int a;
                *plus = 0;
                for (a = 0; a < s_anchor_n; a++) if (!strcmp(s_anchor_name[a], w)) break;
                if (a == s_anchor_n && s_anchor_n < 32) {
                    strncpy(s_anchor_name[a], w, 31); s_anchor_n++;
                }
                if (a >= 32) continue;   /* too many different files: line ignored */
                s_ps_ev[s_ps_n].anchor = a;
                s_ps_ev[s_ps_n].ms = strtoul(plus + 1, NULL, 0);
            } else
                s_ps_ev[s_ps_n].ms = strtoul(w, NULL, 0);
            strncpy(s_ps_ev[s_ps_n].b, b, 7); s_ps_ev[s_ps_n].b[7] = 0;
            s_ps_n++;
        }
        fclose(fp);
    }
    s_ps_t0 = pad_now();
}

void xbox_PadScriptNotePath(const char *path)
{
    int i;
    pad_script_load();
    for (i = 0; i < s_anchor_n; i++)
        if (!s_anchor_t[i] && path && strstr(path, s_anchor_name[i]))
            s_anchor_t[i] = pad_now();
}

static void pad_script(XBOX_INPUT_STATE *s)
{
    DWORD now; int i;
    int n;
    pad_script_load();
    n = s_ps_n;
#define ev s_ps_ev
#define t0 s_ps_t0
    if (!n) return;
    for (i = 0; i < n; i++) {
        const char *b = ev[i].b;
        if (ev[i].anchor >= 0) {
            if (!s_anchor_t[ev[i].anchor]) continue;
            now = pad_now() - s_anchor_t[ev[i].anchor];
        } else {
            /* "CLEAR" (usually anchored): stop every untimed-to-a-file line,
             * e.g. "loadsave.bff+0 CLEAR" ends a START-spam once a screen
             * is reached. */
            if (s_ps_cleared) continue;
            now = pad_now() - t0;
        }
        if (!strncmp(ev[i].b, "FLAG", 4)) {
            /* "FLAGn": a named moment for test hooks (xbox_PadScriptFlag) */
            int k = atoi(ev[i].b + 4);
            if (now >= ev[i].ms && k > 0 && k < 32 && !(s_ps_flags & (1u << k))) {
                s_ps_flags |= 1u << k;
                fprintf(stderr, "  [PADSCRIPT] %lu ms: flag %d\n", ev[i].ms, k);
                fflush(stderr);
            }
            continue;
        }
        if (!strcmp(ev[i].b, "CLEAR")) {
            if (now >= ev[i].ms && !s_ps_cleared) {
                s_ps_cleared = 1;
                fprintf(stderr, "  [PADSCRIPT] cleared the timed lines\n");
                fflush(stderr);
            }
            continue;
        }
        if (now < ev[i].ms || now >= ev[i].ms + 400) continue;
        {
            static int last_said = -1;
            if (last_said != i) {
                last_said = i;
                fprintf(stderr, "  [PADSCRIPT] %lu ms: %s\n", ev[i].ms, b);
                fflush(stderr);
            }
        }
        if (!strcmp(b, "UP")) s->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_UP;
        else if (!strcmp(b, "DOWN")) s->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_DOWN;
        else if (!strcmp(b, "LEFT")) s->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_LEFT;
        else if (!strcmp(b, "RIGHT")) s->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_RIGHT;
        else if (!strcmp(b, "START")) s->Gamepad.wButtons |= XBOX_GAMEPAD_START;
        else if (!strcmp(b, "BACK")) s->Gamepad.wButtons |= XBOX_GAMEPAD_BACK;
        else if (!strcmp(b, "A")) s->Gamepad.bAnalogButtons[XBOX_BUTTON_A] = 255;
        else if (!strcmp(b, "B")) s->Gamepad.bAnalogButtons[XBOX_BUTTON_B] = 255;
        else if (!strcmp(b, "X")) s->Gamepad.bAnalogButtons[XBOX_BUTTON_X] = 255;
        else if (!strcmp(b, "Y")) s->Gamepad.bAnalogButtons[XBOX_BUTTON_Y] = 255;
        else if (!strcmp(b, "WHITE")) s->Gamepad.bAnalogButtons[XBOX_BUTTON_WHITE] = 255;
        else if (!strcmp(b, "BLACK")) s->Gamepad.bAnalogButtons[XBOX_BUTTON_BLACK] = 255;
        else if (!strcmp(b, "LT")) s->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER] = 255;
        else if (!strcmp(b, "RT")) s->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER] = 255;
        else if (!strncmp(b, "LSU", 3) && b[3] >= '0' && b[3] <= '9') s->Gamepad.sThumbLY = (SHORT)(32767 * atoi(b + 3) / 100);   /* LSU45: pushed 45 % */
        else if (!strncmp(b, "LSR", 3) && b[3] >= '0' && b[3] <= '9') s->Gamepad.sThumbLX = (SHORT)(32767 * atoi(b + 3) / 100);
        else if (!strncmp(b, "LSL", 3) && b[3] >= '0' && b[3] <= '9') s->Gamepad.sThumbLX = (SHORT)(-32767 * atoi(b + 3) / 100);
        else if (!strcmp(b, "L3")) s->Gamepad.wButtons |= 0x0040;   /* the left stick's click */
        else if (!strcmp(b, "LSU")) s->Gamepad.sThumbLY = 32767;     /* left stick, held 400 ms */
        else if (!strcmp(b, "LSD")) s->Gamepad.sThumbLY = -32767;
        else if (!strcmp(b, "LSL")) s->Gamepad.sThumbLX = -32767;
        else if (!strcmp(b, "LSR")) s->Gamepad.sThumbLX = 32767;
        else if (!strcmp(b, "RSU")) s->Gamepad.sThumbRY = 32767;     /* right stick */
        else if (!strcmp(b, "RSD")) s->Gamepad.sThumbRY = -32767;
        else if (!strcmp(b, "RSL")) s->Gamepad.sThumbRX = -32767;
        else if (!strcmp(b, "RSR")) s->Gamepad.sThumbRX = 32767;
    }
}

#undef ev
#undef t0

/* Keyboard and mouse through the bindings (src/input/input_bind.c; the
 * defaults are the wasd / classic layouts described above). */
/* While a title's own mouse pointer is up (its menus), the mouse buttons
 * are the pointer's: they are left out of the bindings. */
/* (1: every mouse button; or bits: 2 the left, 4 the right, 8 the others --
 * a title's pointer can leave a button bound to an action to its action) */
volatile int xbox_mouse_buttons_taken;
static int is_mouse_vk(int vk)
{
    return vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
}
static int act_down(int a)
{
    int i;
    for (i = 0; i < BIND_KEYS; i++) {
        int vk = xbox_BindKey(a, i);
        if (vk && xbox_mouse_buttons_taken && is_mouse_vk(vk)
            && (xbox_mouse_buttons_taken == 1 || (xbox_mouse_buttons_taken & (vk == VK_LBUTTON ? 2 : vk == VK_RBUTTON ? 4 : 8)))) continue;
        if (vk && key_down(vk)) return 1;
    }
    return 0;
}
#define WALK_PART 45   /* percent of a full push (RECOMP_WALK_PART, testing) */
static SHORT act_axis(int neg, int pos)
{
    int v = 0;
    if (act_down(neg)) v -= 32767;
    if (act_down(pos)) v += 32767;
    return (SHORT)v;
}
static void keyboard_state(XBOX_INPUT_STATE *pState)
{
    static DWORD packet;
    static const WORD bits[] = { XBOX_GAMEPAD_START, XBOX_GAMEPAD_BACK, XBOX_GAMEPAD_DPAD_UP, XBOX_GAMEPAD_DPAD_DOWN,
                                 XBOX_GAMEPAD_DPAD_LEFT, XBOX_GAMEPAD_DPAD_RIGHT, XBOX_GAMEPAD_LEFT_THUMB,
                                 XBOX_GAMEPAD_RIGHT_THUMB };
    static const int bit_act[] = { BIND_START, BIND_BACK, BIND_DUP, BIND_DDOWN, BIND_DLEFT, BIND_DRIGHT,
                                   BIND_LTHUMB, BIND_RTHUMB };
    static const int analog_act[8] = { BIND_A, BIND_B, BIND_X, BIND_Y, BIND_BLACK, BIND_WHITE, BIND_LT, BIND_RT };
    WORD b = 0;
    int i, lx, ly;

    memset(pState, 0, sizeof(*pState));
    for (i = 0; i < 8; i++) if (act_down(bit_act[i])) b |= bits[i];
    pState->Gamepad.wButtons = b;
    /* Analog on the console, so a key is 255 rather than a flag -- a title
     * that reads these as a pressure never sees a press if 1. */
    for (i = 0; i < 8; i++)
        pState->Gamepad.bAnalogButtons[i] = act_down(analog_act[i]) ? 255 : 0;
    lx = act_axis(BIND_MOVE_LEFT, BIND_MOVE_RIGHT);
    ly = act_axis(BIND_MOVE_DOWN, BIND_MOVE_UP);
    /* A diagonal is one full deflection, not 1.41 of one. */
    if (lx && ly) { lx = lx > 0 ? 23170 : -23170; ly = ly > 0 ? 23170 : -23170; }
    /* Walk held: the stick pushed part way, as a pad's stick pushed a
     * little -- the hero walks instead of running */
    if ((lx || ly) && act_down(BIND_WALK)) {
        static int part = -1;
        if (part < 0) { const char *e = getenv("RECOMP_WALK_PART"); part = e ? atoi(e) : WALK_PART; if (part < 5 || part > 100) part = WALK_PART; }
        lx = lx * part / 100; ly = ly * part / 100;
    }
    pState->Gamepad.sThumbLX = (SHORT)lx;
    pState->Gamepad.sThumbLY = (SHORT)ly;
    pState->Gamepad.sThumbRX = act_axis(BIND_LOOK_LEFT, BIND_LOOK_RIGHT);
    pState->Gamepad.sThumbRY = act_axis(BIND_LOOK_DOWN, BIND_LOOK_UP);

    {
        static int said;
        if (!said && (pState->Gamepad.wButtons || pState->Gamepad.sThumbLX || pState->Gamepad.sThumbLY
                      || pState->Gamepad.bAnalogButtons[0] || pState->Gamepad.bAnalogButtons[2])) {
            said = 1;
            fprintf(stderr, "  [INPUT] keyboard/mouse input seen (buttons %04X, stick %d,%d)\n",
                    pState->Gamepad.wButtons, pState->Gamepad.sThumbLX, pState->Gamepad.sThumbLY);
            fflush(stderr);
        }
    }
    if (!getenv("RECOMP_PAD_SCRIPT_RAW")) pad_script(pState);
    /* The title's input layer looks for button edges, so the packet number
     * has to move whenever the state does or a press is never noticed. */
    pState->dwPacketNumber = ++packet;
}

void xbox_InputInit(void)
{
    DWORD i;
    pad_for(0);                       /* start the scans */
    for (i = 0; i < XBOX_MAX_CONTROLLERS; i++) g_controller_connected[i] = FALSE;
}

/* Input only reaches the game while its window is in front: a controller
 * or keys used for something else (another program, a video, the desktop)
 * must not move the hero. [Input] background_input=1 (RECOMP_BACKGROUND_INPUT)
 * lets the controller through anyway. */
static int game_in_front(void)
{
    static int bg = -1;
    if (bg < 0) { const char *v = getenv("RECOMP_BACKGROUND_INPUT"); bg = v && *v && *v != '0'; }
    if (bg || getenv("RECOMP_PAD_SCRIPT")) return 1;
#if defined(_WIN32)
    {
        HWND fb = (HWND)xbox_FramebufferHwnd(), fg;
        if (!fb) return 1;                   /* no window of ours: nothing to compare */
        fg = GetForegroundWindow();
        return fg == fb;
    }
#else
    if (!xbox_FramebufferHwnd()) return 1;
    return xbox_FramebufferHasFocus();
#endif
}

/* Was player 1's last input from the keyboard or mouse (1) or a pad (0)?
 * For the button prompts' "automatic". Before anything is pressed: the
 * keyboard when no pad is there. */
static volatile int s_last_kbm = -1;
static int kb_active(const XBOX_INPUT_STATE *s)
{
    int i;
    if (s->Gamepad.wButtons || s->Gamepad.sThumbLX || s->Gamepad.sThumbLY || s->Gamepad.sThumbRX || s->Gamepad.sThumbRY) return 1;
    for (i = 0; i < 8; i++) if (s->Gamepad.bAnalogButtons[i] > 30) return 1;
    return 0;
}
/* the same, -1 before anything is pressed */
int xbox_InputLastDevice(void) { return s_last_kbm; }
int xbox_InputLastWasKeyboard(void)
{
    if (s_last_kbm >= 0) return s_last_kbm;
    return pad_for(0) == PAD_NONE;
}

static volatile int s_start_down;   /* the controller's Start just went down (for the log) */
static DWORD input_get_raw(DWORD dwPort, XBOX_INPUT_STATE *pState)
{
    HostPad hp;
    DWORD result;

    if (dwPort >= XBOX_MAX_CONTROLLERS || !pState)
        return ERROR_DEVICE_NOT_CONNECTED;

    {
        DWORD p = pad_for(dwPort);
        result = p != PAD_NONE ? pad_read(p, &hp) : ERROR_DEVICE_NOT_CONNECTED;
        if (result != ERROR_SUCCESS && dwPort == 0 && getenv("RECOMP_PAD_SCRIPT_RAW")) { memset(&hp, 0, sizeof hp); result = ERROR_SUCCESS; }   /* (testing: a pad at rest) */
    }
    if (result != ERROR_SUCCESS) {
        g_controller_connected[dwPort] = FALSE;
        if ((int)dwPort == xbox_InputKeyboardPort() && keyboard_enabled()) {
            keyboard_state(pState);
            xbox_input_kb_lstick = 1; xbox_input_kb_lt = 1;   /* (no controller: all of it is the keys') */
            if (kb_active(pState)) s_last_kbm = 1;
            return ERROR_SUCCESS;
        }
        return result;
    }

    g_controller_connected[dwPort] = TRUE;

    if (dwPort == 0 && getenv("RECOMP_PAD_SCRIPT_RAW")) {   /* testing: the pad script pressed on the controller itself */
        XBOX_INPUT_STATE t; static const WORD am[4] = { XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y };
        static HostPad last; int i;
        memset(&t, 0, sizeof t);
        pad_script(&t);
        hp.buttons |= (WORD)(t.Gamepad.wButtons & 0xFF);
        for (i = 0; i < 4; i++) if (t.Gamepad.bAnalogButtons[i]) hp.buttons |= am[i];
        if (t.Gamepad.sThumbLX) hp.lx = t.Gamepad.sThumbLX;
        if (t.Gamepad.sThumbLY) hp.ly = t.Gamepad.sThumbLY;
        if (memcmp(&hp, &last, sizeof hp - sizeof hp.packet)) hp.packet = last.packet + 1; else hp.packet = last.packet;
        last = hp;
    }

    if (dwPort == 0) {   /* the controller's Start, for the log: its presses and whether they reach the game (xbox_InputGetState) */
        static WORD prevb;
        if ((hp.buttons & XINPUT_GAMEPAD_START) && !(prevb & XINPUT_GAMEPAD_START)) s_start_down = 1;
        prevb = hp.buttons;
    }

    memset(pState, 0, sizeof(XBOX_INPUT_STATE));
    pState->dwPacketNumber = hp.packet;
    {
        /* Your controller's buttons through the bindings: source k of the
         * pad (A B X Y LB RB LT RT Start Back Up Down Left Right LS RS) gives
         * whichever action it is bound to. */
        static const WORD src_mask[BIND_PAD_N] = { 0, XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X,
            XINPUT_GAMEPAD_Y, XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER, 0, 0,
            XINPUT_GAMEPAD_START, XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_DPAD_UP, XINPUT_GAMEPAD_DPAD_DOWN,
            XINPUT_GAMEPAD_DPAD_LEFT, XINPUT_GAMEPAD_DPAD_RIGHT, XINPUT_GAMEPAD_LEFT_THUMB,
            XINPUT_GAMEPAD_RIGHT_THUMB };
        static const WORD act_bit[BIND_N] = { 0, 0, 0, 0, 0, 0, 0, 0, XBOX_GAMEPAD_START, XBOX_GAMEPAD_BACK,
            XBOX_GAMEPAD_DPAD_UP, XBOX_GAMEPAD_DPAD_DOWN, XBOX_GAMEPAD_DPAD_LEFT, XBOX_GAMEPAD_DPAD_RIGHT,
            XBOX_GAMEPAD_LEFT_THUMB, XBOX_GAMEPAD_RIGHT_THUMB };
        int act;
        for (act = 0; act < BIND_MOVE_UP; act++) {
            int src = dwPort == 1 ? xbox_BindPad2(act) : xbox_BindPad(act);   /* (player 2's controller: its own buttons) */
            BYTE v;
            if (src == 7) v = hp.lt;
            else if (src == 8) v = hp.rt;
            else v = (src && (hp.buttons & src_mask[src])) ? 255 : 0;
            if (act < 8) {
                if (v > pState->Gamepad.bAnalogButtons[act]) pState->Gamepad.bAnalogButtons[act] = v;
            } else if (v > 30) {
                pState->Gamepad.wButtons |= act_bit[act];
            }
        }
    }

    pState->Gamepad.sThumbLX = hp.lx;
    pState->Gamepad.sThumbLY = hp.ly;
    pState->Gamepad.sThumbRX = hp.rx;
    pState->Gamepad.sThumbRY = hp.ry;
    stick_deadzone(&pState->Gamepad.sThumbLX, &pState->Gamepad.sThumbLY);
    stick_deadzone(&pState->Gamepad.sThumbRX, &pState->Gamepad.sThumbRY);
    {   /* (1.9.7) trigger sensitivity: a trigger pulled less than this is not pulled (the analog buttons 6, 7) */
        int pc = (int)xbox_GfxGetF(GFX_TRIGGER), t;
        if (pc > 0) for (t = 6; t < 8; t++) if (pState->Gamepad.bAnalogButtons[t] * 100 < pc * 255) pState->Gamepad.bAnalogButtons[t] = 0;
    }

    /* Merge the keyboard on top rather than only standing in for a missing
     * pad: a real pad keeps working -- its buttons are already in pState and
     * the keyboard only adds to them. (Under Wine XInputGetState reports a
     * pad at rest even with nothing plugged in, so "keyboard when there is no
     * pad" was unreachable there.) */
    if ((int)dwPort == xbox_InputKeyboardPort() && keyboard_enabled()) {
        XBOX_INPUT_STATE kb;
        int i;
        keyboard_state(&kb);
        /* which was used last, for the button prompts: a pad's press, or a key */
        if (kb_active(&kb)) s_last_kbm = 1;
        else if (kb_active(pState) && (abs(pState->Gamepad.sThumbLX) > 16000 || abs(pState->Gamepad.sThumbLY) > 16000
                 || pState->Gamepad.wButtons || pState->Gamepad.bAnalogButtons[0] > 64 || pState->Gamepad.bAnalogButtons[1] > 64
                 || pState->Gamepad.bAnalogButtons[2] > 64 || pState->Gamepad.bAnalogButtons[3] > 64)) s_last_kbm = 0;
        pState->Gamepad.wButtons |= kb.Gamepad.wButtons;
        for (i = 0; i < 8; i++)
            if (kb.Gamepad.bAnalogButtons[i] > pState->Gamepad.bAnalogButtons[i])
                pState->Gamepad.bAnalogButtons[i] = kb.Gamepad.bAnalogButtons[i];
        /* The keyboard's stick only stands in for a controller stick at
         * rest: a pushed controller stick always wins. */
        xbox_input_kb_lstick = 0;
        xbox_input_kb_lt = kb.Gamepad.bAnalogButtons[6] > 30 && pState->Gamepad.bAnalogButtons[6] <= kb.Gamepad.bAnalogButtons[6]
                           && hp.lt <= 30;
        {
            int pl = abs(pState->Gamepad.sThumbLX) > 7849 || abs(pState->Gamepad.sThumbLY) > 7849;
            int pr = abs(pState->Gamepad.sThumbRX) > 8689 || abs(pState->Gamepad.sThumbRY) > 8689;
            if (!pl && (kb.Gamepad.sThumbLX || kb.Gamepad.sThumbLY)) {
                xbox_input_kb_lstick = 1;
                pState->Gamepad.sThumbLX = kb.Gamepad.sThumbLX;
                pState->Gamepad.sThumbLY = kb.Gamepad.sThumbLY;
            }
            if (!pr && (kb.Gamepad.sThumbRX || kb.Gamepad.sThumbRY)) {
                pState->Gamepad.sThumbRX = kb.Gamepad.sThumbRX;
                pState->Gamepad.sThumbRY = kb.Gamepad.sThumbRY;
            }
        }
        /* The input layer records edges, so an unchanged packet number is
         * read as the same state and the press never happens. */
        pState->dwPacketNumber = kb.dwPacketNumber;
    }

    return ERROR_SUCCESS;
}

/* The graphics settings overlay (src/video/gfx_menu.c) sees player 1's
 * controller first: Back+Start opens it, and while it is open (or its closing
 * button is still held) the game gets a controller at rest. */
extern void xbox_GfxMenuPad(XBOX_INPUT_STATE *st);
void (*volatile xbox_pad_observer)(DWORD port, const XBOX_INPUT_STATE *st);
void (*volatile xbox_pad_filter)(DWORD port, XBOX_INPUT_STATE *st);
static void vib_apply(DWORD port);
/* (1.9.7) the More page's Pause / Mute in the background: the window itself in front (background input aside) */
static int window_in_front(void)
{
#if defined(_WIN32)
    HWND fb = (HWND)xbox_FramebufferHwnd();
    return !fb || GetForegroundWindow() == fb || getenv("RECOMP_PAD_SCRIPT");
#else
    return 1;
#endif
}
static void bg_pause_mute(void)
{
    extern volatile int g_audio_muted; static int muted_by_us;
    int front = window_in_front(), mute = !front && (xbox_GfxGet(GFX_BGMUTE) || xbox_GfxGet(GFX_BGPAUSE));
    if (mute && !muted_by_us) { muted_by_us = 1; g_audio_muted = 1; }
    else if (!mute && muted_by_us) { muted_by_us = 0; g_audio_muted = 0; }
    if (!front && xbox_GfxGet(GFX_BGPAUSE)) {   /* the game held here (its pad poll, on its own thread) until the window is back in front */
        fprintf(stderr, "  [INPUT] paused: the window is in the background\n"); fflush(stderr);
        while (!window_in_front() && xbox_GfxGet(GFX_BGPAUSE)) Sleep(30);
        fprintf(stderr, "  [INPUT] back in front: the game goes on\n"); fflush(stderr);
        if (muted_by_us && !xbox_GfxGet(GFX_BGMUTE)) { muted_by_us = 0; g_audio_muted = 0; }
    }
}
DWORD xbox_InputGetState(DWORD dwPort, XBOX_INPUT_STATE *pState)
{
    DWORD r;
    if (dwPort == 0) bg_pause_mute();
    r = input_get_raw(dwPort, pState);
    if (r == ERROR_SUCCESS && !game_in_front()) {
        /* in the background: a controller at rest (the packet number still
         * moves, so the release of anything held is seen) */
        static DWORD bgpacket;
        memset(&pState->Gamepad, 0, sizeof pState->Gamepad);
        pState->dwPacketNumber = ++bgpacket | 0x40000000u;
    }
    if (r == ERROR_SUCCESS) vib_apply(dwPort);   /* rumble follows the focus */
    if (dwPort == 0 && r == ERROR_SUCCESS) xbox_GfxMenuPad(pState);
    if (dwPort == 0 && s_start_down) {   /* the first presses of the controller's Start in the log: did the game get it? */
        static int n;
        s_start_down = 0;
        if (n++ < 12) {
            fprintf(stderr, "  [INPUT] controller Start pressed: %s (buttons to the game %04X, window in front %d, bound to %s)\n",
                    r == ERROR_SUCCESS && (pState->Gamepad.wButtons & XBOX_GAMEPAD_START) ? "sent to the game" : "NOT sent to the game",
                    r == ERROR_SUCCESS ? pState->Gamepad.wButtons : 0, game_in_front(), xbox_BindPadName(xbox_BindPad(BIND_START)));
            fflush(stderr);
        }
    }
    /* a title's changes (its mouse pointer in the menus: the presses it stands for) */
    if (r == ERROR_SUCCESS && xbox_pad_filter) xbox_pad_filter(dwPort, pState);   /* (1.9.7: in the background too -- the title's own presses (the menus' scripted steps under a black cover) were lost while another window had the focus, and the screen stayed black; the player's own input is already at rest here) */
    /* what the game is about to see (title mods watch the buttons) */
    if (r == ERROR_SUCCESS && xbox_pad_observer) xbox_pad_observer(dwPort, pState);
    return r;
}

/* Rumble. Sent only when it changes (each send is a write to the pad), and
 * held at rest while the game is in the background so a pad does not keep
 * shaking behind another window; it resumes when the game is back in front. */
static XBOX_VIBRATION s_vib_want[XBOX_MAX_CONTROLLERS];
static struct { DWORD pad; XBOX_VIBRATION v; int known; } s_vib_sent[XBOX_MAX_CONTROLLERS];
static void vib_apply(DWORD port)
{
    DWORD p = pad_for(port);
    XBOX_VIBRATION v = s_vib_want[port];
    if (p == PAD_NONE) return;
    if (!game_in_front()) v.wLeftMotorSpeed = v.wRightMotorSpeed = 0;
    {   /* (1.9.7) the More page's vibration strength */
        int pc = (int)xbox_GfxGetF(GFX_VIB);
        if (pc < 100) { v.wLeftMotorSpeed = (WORD)((uint32_t)v.wLeftMotorSpeed * (uint32_t)pc / 100u); v.wRightMotorSpeed = (WORD)((uint32_t)v.wRightMotorSpeed * (uint32_t)pc / 100u); }
    }
    if (s_vib_sent[port].known && s_vib_sent[port].pad == p && !memcmp(&v, &s_vib_sent[port].v, sizeof v)) return;
    if (pad_rumble(p, v.wLeftMotorSpeed, v.wRightMotorSpeed) == ERROR_SUCCESS) {
        s_vib_sent[port].pad = p; s_vib_sent[port].v = v; s_vib_sent[port].known = 1;
    }
}
DWORD xbox_InputSetState(DWORD dwPort, const XBOX_VIBRATION *pVibration)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pVibration)
        return ERROR_DEVICE_NOT_CONNECTED;
    s_vib_want[dwPort] = *pVibration;
    if (pad_for(dwPort) == PAD_NONE) return ERROR_DEVICE_NOT_CONNECTED;
    vib_apply(dwPort);
    return ERROR_SUCCESS;
}
/* Rumble asked for from a thread that must not call into XInput (the USB
 * emulation, answering the game's output report): only recorded here, and
 * sent by the next input poll (xbox_InputGetState -> vib_apply), which runs
 * on the game's own input thread many times a second. */
void xbox_InputSetVibrationDeferred(DWORD dwPort, WORD wLeft, WORD wRight)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS) return;
    s_vib_want[dwPort].wLeftMotorSpeed = wLeft;
    s_vib_want[dwPort].wRightMotorSpeed = wRight;
}

/* Every motor off (the game window closing). */
void xbox_InputStopRumble(void)
{
    DWORD i;
#if defined(_WIN32)
    for (i = 0; i < 4; i++) {   /* closing: straight to the pads */
        XINPUT_VIBRATION z = { 0, 0 };
        xi_set(i, &z);
    }
#endif
#if HAVE_SDL_PADS
    if (s_sp_state > 0)
        for (i = 0; i < SP_MAX; i++)
            if (s_sp[i].gc) { s_sp[i].want_l = s_sp[i].want_r = 0; p_SDL_GameControllerRumble(s_sp[i].gc, 0, 0, 0); }
#endif
    for (i = 0; i < XBOX_MAX_CONTROLLERS; i++) { memset(&s_vib_want[i], 0, sizeof s_vib_want[i]); s_vib_sent[i].known = 0; }
}

BOOL xbox_InputIsConnected(DWORD dwPort)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS) return FALSE;
    return g_controller_connected[dwPort];
}

DWORD xbox_InputGetCapabilities(DWORD dwPort, DWORD dwFlags, XBOX_INPUT_CAPABILITIES *pCaps)
{
    HostPad hp;
    DWORD p;
    (void)dwFlags;
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pCaps)
        return ERROR_DEVICE_NOT_CONNECTED;
    p = pad_for(dwPort);
    if (p == PAD_NONE || pad_read(p, &hp) != ERROR_SUCCESS) return ERROR_DEVICE_NOT_CONNECTED;
    /* every pad is presented as a standard gamepad */
    memset(pCaps, 0, sizeof(XBOX_INPUT_CAPABILITIES));
    pCaps->Type = 1;      /* XINPUT_DEVTYPE_GAMEPAD */
    pCaps->SubType = 1;   /* XINPUT_DEVSUBTYPE_GAMEPAD */
    pCaps->Flags = 0;
    return ERROR_SUCCESS;
}
