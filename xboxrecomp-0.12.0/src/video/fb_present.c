/**
 * Show the guest framebuffer in a window.
 *
 * The title renders into its own framebuffer in guest RAM and tells the kernel
 * where it is through AvSetDisplayMode; on hardware the CRTC scans that memory
 * out. Nothing here scans anything out, so however much of the GPU is
 * implemented, none of it is observable. This is the other half: a window that
 * reads that memory and puts it on screen.
 *
 * Deliberately plain GDI rather than the D3D8 layer. The point is to display
 * whatever the guest actually wrote, so the fewer stages between guest memory
 * and the screen the better -- and it must keep working while the D3D8 layer
 * is busy with something else, such as the FMV player's own window.
 *
 * Off unless RECOMP_FB_WINDOW is set.
 */
#include <stdint.h>

#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ptrdiff_t xbox_GetMemoryOffset(void);

static volatile LONG s_fb_running;
static volatile HWND s_fb_hwnd;
static volatile LONG s_fb_gpu;          /* the D3D11 backend presents; GDI stays out */

HWND xbox_FramebufferHwnd(void) { return s_fb_hwnd; }
/* The mouse over the window, for a title's pointer in its menus: where (as
 * fractions of the window's inside), 1 if it is inside. While the title
 * draws its own pointer, Windows' is hidden over the window. */
volatile int xbox_fb_hide_cursor;
/* left and right clicks so far (a click between two of the title's looks at the buttons still counts) */
volatile LONG xbox_fb_clicks[2];
volatile LONG xbox_fb_wheel;   /* mouse wheel notches, up positive (counted; readers keep their own last value) */
int xbox_FramebufferMouse(float *fx, float *fy)
{
    HWND h = s_fb_hwnd;
    POINT p; RECT r;
    if (!h || !GetCursorPos(&p) || !ScreenToClient(h, &p) || !GetClientRect(h, &r) || r.right <= 0 || r.bottom <= 0) return 0;
    *fx = ((float)p.x + 0.5f) / (float)r.right; *fy = ((float)p.y + 0.5f) / (float)r.bottom;
    return p.x >= 0 && p.y >= 0 && p.x < r.right && p.y < r.bottom;
}
/* The mouse's movement (a title's pointer at its own speed, aiming): while
 * a title asks for it, the system pointer is put back in the middle of the
 * window after each look, and what it moved is the movement. */
static int s_rel_primed;
int xbox_FramebufferMouseDelta(int *dx, int *dy, int *w, int *h)
{
    HWND hw = s_fb_hwnd;
    RECT r; POINT c, p;
    *dx = *dy = 0;
    if (!hw || !GetClientRect(hw, &r) || r.right <= 0 || r.bottom <= 0) return 0;
    *w = r.right; *h = r.bottom;
    c.x = r.right / 2; c.y = r.bottom / 2;
    ClientToScreen(hw, &c);
    {   /* (1.9.3) the middle of the part of the window on its screen: a window
         * left larger than a new display mode had its middle off the screen,
         * where Windows will not put the pointer -- every look read a move
         * that never happened, the pointer drifted and held the menus'
         * choice under it (W A S D seemed not to work) */
        MONITORINFO mi = { sizeof(mi) };
        RECT wr = { 0, 0, r.right, r.bottom }, vis;
        POINT o = { 0, 0 };
        ClientToScreen(hw, &o);
        OffsetRect(&wr, o.x, o.y);
        if (GetMonitorInfoA(MonitorFromWindow(hw, MONITOR_DEFAULTTONEAREST), &mi) && IntersectRect(&vis, &wr, &mi.rcMonitor)) {
            c.x = (vis.left + vis.right) / 2; c.y = (vis.top + vis.bottom) / 2;
        }
    }
    if (!GetCursorPos(&p)) return 0;
    if (s_rel_primed) { *dx = p.x - c.x; *dy = p.y - c.y; }
    s_rel_primed = 1;
    if (p.x != c.x || p.y != c.y) SetCursorPos(c.x, c.y);
    return 1;
}
void xbox_FramebufferMouseRelease(void) { s_rel_primed = 0; }
/* the game's own "quit" (the main menu's Exit): as closing the window */
void xbox_FramebufferRequestClose(void)
{
    extern void xbox_InputStopRumble(void);
    fprintf(stderr, "[EXIT] Exit chosen in the main menu\n");
    fflush(stderr); fflush(stdout);
    xbox_InputStopRumble();
    InterlockedExchange(&s_fb_running, 0);
    TerminateProcess(GetCurrentProcess(), 0);
}
void xbox_FramebufferSetGpuOwned(int on) { InterlockedExchange(&s_fb_gpu, on); }
static uint32_t      s_fb_va, s_fb_pitch, s_fb_width = 640, s_fb_height = 480;
static uint32_t     *s_rgb;           /* converted 32-bit copy for GDI */

/* A finished frame, taken at the flip and shown until the next one.
 *
 * The window used to convert straight out of guest memory every 16 ms. Even
 * pointed at the buffer the title had just finished, that races the executor
 * drawing the next frame into the other one and, whenever the two swap, puts
 * a half-drawn image on the screen -- which is the flicker. Copying the
 * finished frame once per flip means the window never reads memory the
 * rasteriser is writing, so what it shows cannot be half of anything.
 *
 * Two buffers and an index, swapped after the copy completes, so the window
 * thread is never reading the one being filled. */
static uint32_t     *s_present[2];
static volatile LONG s_present_idx = -1;   /* -1 until the first flip */

void xbox_FramebufferWindowSet(uint32_t fb_va, uint32_t pitch)
{
    /* RECOMP_FB_VA pins the window to one guest address instead of following
     * whichever surface is being drawn into. A black window cannot distinguish
     * "the read path is broken" from "the title rendered black", and pointing
     * it at memory known to have content settles that. */
    const char *pin = getenv("RECOMP_FB_VA");

    s_fb_va = pin ? (uint32_t)strtoul(pin, NULL, 0) : fb_va;
    if (pitch)
        s_fb_pitch = pitch;
}

/* Called by the pushbuffer executor when the title flips. */
unsigned char g_xbox_gamma[3][256];
volatile int g_xbox_gamma_set;

void xbox_FramebufferModeService(void);
void xbox_FramebufferWindowPresent(uint32_t fb_va, uint32_t pitch)
{
    const uint8_t *src;
    LONG next;
    uint32_t bpp, x, y;

    xbox_FramebufferModeService();   /* (1.9.3) a display mode asked for: changed here, between frames */
    if (!s_fb_running || !fb_va || !pitch)
        return;
    {   /* (1.9.3) Direct3D 11 draws the picture: this copy (and its gamma
         * pass over every pixel) only fed the dumps -- on the render thread,
         * every frame. As on Linux and macOS. */
        static int dump = -1;
        if (dump < 0) dump = getenv("RECOMP_FB_DUMP") != NULL;
        if (s_fb_gpu && !dump) return;
    }
    if (getenv("RECOMP_FB_VA"))
        return;                       /* pinned: leave the old path alone */
    next = (s_present_idx == 0) ? 1 : 0;
    if (!s_present[next]) {
        s_present[next] = (uint32_t *)calloc((size_t)s_fb_width * s_fb_height,
                                             4);
        if (!s_present[next])
            return;
    }
    bpp = pitch / s_fb_width;
    src = (const uint8_t *)((uintptr_t)fb_va + xbox_GetMemoryOffset());
    for (y = 0; y < s_fb_height; y++) {
        const uint8_t *row = src + (size_t)y * pitch;
        uint32_t *dst = s_present[next] + (size_t)y * s_fb_width;

        if (bpp == 4) {
            memcpy(dst, row, (size_t)s_fb_width * 4);
        } else if (bpp == 2) {
            const uint16_t *p = (const uint16_t *)row;
            for (x = 0; x < s_fb_width; x++) {
                uint16_t v = p[x];
                uint32_t r = (uint32_t)((v >> 11) & 0x1F) * 255u / 31u;
                uint32_t g = (uint32_t)((v >>  5) & 0x3F) * 255u / 63u;
                uint32_t b = (uint32_t)( v        & 0x1F) * 255u / 31u;
                dst[x] = (r << 16) | (g << 8) | b;
            }
        } else {
            memset(dst, 0, (size_t)s_fb_width * 4);
        }
    }
    /* The title's gamma ramp (D3D SetGammaRamp, uploaded to the RAMDAC
     * palette on hardware): applied on the way to the screen, as the DAC
     * would. A title that brightens its image this way otherwise comes out
     * far too dark. */
    if (g_xbox_gamma_set) {
        size_t i, n = (size_t)s_fb_width * s_fb_height;
        uint32_t *d = s_present[next];
        for (i = 0; i < n; i++) {
            uint32_t v = d[i];
            d[i] = ((uint32_t)g_xbox_gamma[0][(v >> 16) & 0xFF] << 16)
                 | ((uint32_t)g_xbox_gamma[1][(v >> 8) & 0xFF] << 8)
                 |  (uint32_t)g_xbox_gamma[2][v & 0xFF];
        }
    }
    /* Published only once it is whole. */
    InterlockedExchange(&s_present_idx, next);
}

/* Which keys are down, for the pad stand-in in src/input.
 *
 * GetAsyncKeyState looked like the cheaper way to ask and does not work
 * here: it reads a state Wine keeps for the X server, and a guest process
 * drawing through GDI never sees it change. The window that has the focus
 * is the thing that receives the keys, so that is what has to remember
 * them.
 *
 * Reading this needs no lock. Each entry is written only by the window
 * thread and read only by the USB thread, one byte at a time, and a press
 * seen a frame late is indistinguishable from one made a frame later. */
static volatile unsigned char s_key_down[256];
static volatile int s_swallow_click;

int xbox_FramebufferKeyDown(int vk)
{
    if ((unsigned)vk > 255)
        return 0;
    return s_key_down[vk] != 0;
}
/* The input layer lets go of a key whose release never arrived (see
 * key_down in xinput_device.c). */
void xbox_FramebufferKeyClear(int vk)
{
    if ((unsigned)vk <= 255) s_key_down[vk] = 0;
}

/* Borderless full screen: the window covers its monitor with no frame. With
 * the flip-model swap chain this gets the display to itself (independent
 * flip), as exclusive full screen would, without a mode change. Alt+Enter or
 * F11 toggles; RECOMP_FULLSCREEN=1 starts that way. */
static int s_fs;          /* 0 windowed, 1 borderless full screen, 2 exclusive (the renderer takes the display) */
static int s_fs_kind = 1;  /* the full-screen kind Alt+Enter / F11 goes to */
static int s_fs_back_kind; /* (1.9.8) the kind to go back to after the refresh on coming back (WM_TIMER) */
static WINDOWPLACEMENT s_fs_saved = { sizeof(WINDOWPLACEMENT) };
/* (1.9.3) Exclusive full screen: the display is switched to the chosen
 * resolution ([Display] fs_resolution; "desktop" keeps its own) at the
 * highest refresh rate it offers there, and the borderless window covers it;
 * with the flip-model swap chain the game then has the display to itself.
 * Done here on the window thread rather than through DXGI's own full-screen
 * state, which waits on the window thread from the render thread. Leaving
 * the game (Alt+Tab) puts the desktop back and minimises the window;
 * coming back switches again. */
extern void xbox_GfxFsMode(int *w, int *h);
static volatile int s_mode_set;   /* the display is in our mode */
static volatile LONG s_mode_want = -1, s_mode_seq, s_mode_done;   /* (1.9.3) asked by the window thread */
static volatile DWORD s_mode_serviced;   /* when the render thread last looked */
static void fb_mode_do(int on)
{
    if (on) {
        DEVMODEA best, dm;
        DWORD i;
        int w = 0, hh = 0;
        memset(&best, 0, sizeof best); best.dmSize = sizeof best;
        xbox_GfxFsMode(&w, &hh);
        if (w <= 0 || hh <= 0) {   /* the desktop's own resolution, at its best refresh */
            memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
            if (s_mode_set) ChangeDisplaySettingsExA(NULL, NULL, NULL, 0, NULL), s_mode_set = 0;
            if (!EnumDisplaySettingsA(NULL, ENUM_REGISTRY_SETTINGS, &dm)) return;
            w = (int)dm.dmPelsWidth; hh = (int)dm.dmPelsHeight;
        }
        for (i = 0;; i++) {
            memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
            if (!EnumDisplaySettingsA(NULL, i, &dm)) break;
            if ((int)dm.dmPelsWidth != w || (int)dm.dmPelsHeight != hh || dm.dmBitsPerPel != 32) continue;
            if (!best.dmPelsWidth || dm.dmDisplayFrequency > best.dmDisplayFrequency) best = dm;
        }
        if (!best.dmPelsWidth) {
            fprintf(stderr, "  [FBWIN] exclusive full screen: the display has no %dx%d mode; keeping its own\n", w, hh);
            fflush(stderr);
            return;
        }
        best.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
        if (ChangeDisplaySettingsExA(NULL, &best, NULL, CDS_FULLSCREEN, NULL) == DISP_CHANGE_SUCCESSFUL) {
            s_mode_set = 1;
            fprintf(stderr, "  [FBWIN] display set to %lux%lu at %lu Hz\n", (unsigned long)best.dmPelsWidth,
                    (unsigned long)best.dmPelsHeight, (unsigned long)best.dmDisplayFrequency);
        } else {
            fprintf(stderr, "  [FBWIN] the display would not change to %dx%d\n", w, hh);
        }
        fflush(stderr);
    } else if (s_mode_set) {
        ChangeDisplaySettingsExA(NULL, NULL, NULL, 0, NULL);
        s_mode_set = 0;
        fprintf(stderr, "  [FBWIN] display back to the desktop's mode\n");
        fflush(stderr);
    }
}
/* The display's mode is changed on the thread that presents the frames,
 * between two of them (xbox_FramebufferModeService): changed here, on the
 * window thread, it could meet a Present in flight -- coming back to the
 * game with Alt+Tab in exclusive full screen froze it. When no frames are
 * coming (half a second), here after all. */
#define WM_FB_COVER (WM_APP + 9)
static void fb_mode_apply(int on)
{
    s_mode_want = on;
    InterlockedIncrement(&s_mode_seq);
    if (GetTickCount() - s_mode_serviced > 500) { s_mode_done = s_mode_seq; fb_mode_do(on); }
}
/* (1.9.7) F9: the title hides its HUD while set; F8 (F12, Print Screen): the picture to Screenshots\<date>_<time>.bmp
 * beside the game (the renderer's frame dump) */
volatile int xbox_hud_hidden;
static void fb_screenshot(void)
{
    extern void xbox_PresentDumpRequest(const char *path);
    char dir[MAX_PATH], path[MAX_PATH + 64], *sl; SYSTEMTIME t;
    GetModuleFileNameA(NULL, dir, MAX_PATH); sl = strrchr(dir, '\\'); if (!sl) sl = strrchr(dir, '/');
    if (sl) *sl = 0; else strcpy(dir, ".");
    strncat(dir, "\\Screenshots", sizeof dir - strlen(dir) - 1);
    CreateDirectoryA(dir, NULL);
    GetLocalTime(&t);
    snprintf(path, sizeof path, "%s\\TwoTowers_%04d%02d%02d_%02d%02d%02d_%03d.bmp", dir, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    xbox_PresentDumpRequest(path);
    fprintf(stderr, "  [FBWIN] screenshot: %s\n", path); fflush(stderr);
}
/* testing: RECOMP_TEST_ALTTAB=ms -- that long after the window opens, the
 * game is left (as Alt+Tab) and three seconds later come back to, twice */
static DWORD WINAPI fb_alttab_test(LPVOID p)
{
    int k;
    Sleep((DWORD)(uintptr_t)p);
    for (k = 0; k < 2; k++) {
        fprintf(stderr, "  [FBWIN] test: leaving the game\n"); fflush(stderr);
        PostMessageA(s_fb_hwnd, WM_ACTIVATEAPP, 0, 0);
        Sleep(3000);
        fprintf(stderr, "  [FBWIN] test: back to the game\n"); fflush(stderr);
        PostMessageA(s_fb_hwnd, WM_ACTIVATEAPP, 1, 0);
        Sleep(6000);
    }
    return 0;
}
void xbox_FramebufferModeService(void)
{
    LONG q = s_mode_seq;
    s_mode_serviced = GetTickCount();
    if (q == s_mode_done) return;
    s_mode_done = q;
    fb_mode_do(s_mode_want == 1);
    if (s_fb_hwnd) PostMessageA(s_fb_hwnd, WM_FB_COVER, 0, 0);
}
static void fb_cover_monitor(HWND h)
{
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoA(MonitorFromWindow(h, MONITOR_DEFAULTTOPRIMARY), &mi);
    SetWindowPos(h, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                 mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
                 SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
}
static void fb_set_fullscreen(HWND h, int on)
{
    if (on) s_fs_kind = on;
    if (on == s_fs) return;
    if (on && s_fs) {   /* (borderless <-> exclusive: the window stays borderless; the display mode changes) */
        s_fs = on;
        fb_mode_apply(on == 2);
        fb_cover_monitor(h);
        fprintf(stderr, "  [FBWIN] %s full screen\n", on == 2 ? "exclusive" : "borderless");
        fflush(stderr);
        return;
    }
    if (on) {
        GetWindowPlacement(h, &s_fs_saved);
        if (on == 2) fb_mode_apply(1);
        SetWindowLongA(h, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        fb_cover_monitor(h);
    } else {
        fb_mode_apply(0);
        SetWindowLongA(h, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPlacement(h, &s_fs_saved);
        SetWindowPos(h, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    }
    s_fs = on;
    fprintf(stderr, "  [FBWIN] %s\n", on == 2 ? "exclusive full screen" : on ? "full screen" : "windowed");
    fflush(stderr);
}

/* The settings overlay asks for full screen from other threads; the window
 * thread does the switch. */
#define WM_FB_SET_FULLSCREEN (WM_APP + 7)
extern int xbox_GfxMenuKey(int vk);
void xbox_FramebufferSetFullscreen(int on)
{
    if (s_fb_hwnd) PostMessageA(s_fb_hwnd, WM_FB_SET_FULLSCREEN, (WPARAM)(on < 0 ? 0 : on > 2 ? 1 : on), 0);
}
int xbox_FramebufferIsFullscreen(void) { return s_fs; }
/* (1.9.3) the refresh rate of the monitor the window is on (0: unknown),
 * looked up again at most once a second */
int xbox_DisplayRefreshHz(void)
{
    static volatile int hz; static volatile DWORD at;
    DWORD now = GetTickCount();
    if (!at || now - at > 1000) {
        MONITORINFOEXW mi; DEVMODEW dm;
        HMONITOR mon = MonitorFromWindow(s_fb_hwnd, MONITOR_DEFAULTTOPRIMARY);
        at = now ? now : 1;
        memset(&mi, 0, sizeof mi); mi.cbSize = sizeof mi;
        memset(&dm, 0, sizeof dm); dm.dmSize = sizeof dm;
        if (mon && GetMonitorInfoW(mon, (MONITORINFO *)&mi) && EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm)
                && dm.dmDisplayFrequency > 1)
            hz = (int)dm.dmDisplayFrequency;
        else if (EnumDisplaySettingsW(NULL, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1)
            hz = (int)dm.dmDisplayFrequency;
    }
    return hz;
}
void xbox_FramebufferFsModeChanged(void) { if (s_fb_hwnd) PostMessageA(s_fb_hwnd, WM_FB_SET_FULLSCREEN, 2, 1); }
/* The window's size (its picture, w x h): now, or for when it is windowed again */
#define WM_FB_SET_SIZE (WM_APP + 8)
void xbox_FramebufferSetWindowSize(int w, int h)
{
    if (s_fb_hwnd && w >= 160 && h >= 120) PostMessageA(s_fb_hwnd, WM_FB_SET_SIZE, (WPARAM)w, (LPARAM)h);
}

static LRESULT CALLBACK fb_wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_FB_COVER:   /* the display's mode changed (render thread): the window over it again */
        if (s_fs && !IsIconic(h)) fb_cover_monitor(h);
        return 0;
    case WM_DISPLAYCHANGE:   /* (1.9.3) any new display mode: full screen covers it exactly, whatever its size was */
        if (s_fs && !IsIconic(h)) fb_cover_monitor(h);
        s_rel_primed = 0;
        return DefWindowProcA(h, m, w, l);
    case WM_FB_SET_FULLSCREEN:
        if (l == 1) {   /* the full-screen resolution changed */
            if (s_fs == 2) { fb_mode_apply(1); fb_cover_monitor(h); }
            return 0;
        }
        fb_set_fullscreen(h, (int)w);
        return 0;
    case WM_FB_SET_SIZE: {
        RECT r = { 0, 0, (LONG)w, (LONG)l };
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        if (s_fs) {   /* (full screen: the size it comes back to) */
            s_fs_saved.rcNormalPosition.right = s_fs_saved.rcNormalPosition.left + (r.right - r.left);
            s_fs_saved.rcNormalPosition.bottom = s_fs_saved.rcNormalPosition.top + (r.bottom - r.top);
        } else {
            ShowWindow(h, SW_RESTORE);
            SetWindowPos(h, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        }
        fprintf(stderr, "  [FBWIN] window size %dx%d%s\n", (int)w, (int)l, s_fs ? " (when windowed)" : "");
        fflush(stderr);
        return 0;
    }
    case WM_SETCURSOR:
        /* (xbox_fb_hide_cursor: 1 hidden, -1 shown even in full screen, 0 as the mode has it) */
        if ((xbox_fb_hide_cursor > 0 || (s_fs && xbox_fb_hide_cursor == 0)) && LOWORD(l) == HTCLIENT) { SetCursor(NULL); return TRUE; }
        if (xbox_fb_hide_cursor < 0 && LOWORD(l) == HTCLIENT) { SetCursor(LoadCursor(NULL, IDC_ARROW)); return TRUE; }
        break;
    case WM_CLOSE:
    case WM_DESTROY:
        /* in the log, so a closed window is not mistaken for a crash */
        if (s_fb_running) {
            extern void xbox_InputStopRumble(void);
            fprintf(stderr, "[EXIT] game window closed (%s)\n", m == WM_CLOSE ? "close" : "destroy"); fflush(stderr);
            xbox_InputStopRumble();       /* no pad left shaking */
        }
        InterlockedExchange(&s_fb_running, 0);
        /* Igual que Salir en el menu: detener ESTE proceso. Dejar solo la
         * ventana detenida permitia que el invitado siguiera enviando frames
         * al HWND destruido (y redimensionase el swapchain con datos invalidos). */
        if (m == WM_CLOSE) {
            fflush(stdout); fflush(stderr);
            TerminateProcess(GetCurrentProcess(), 0);
        }
        return 0;

    case WM_SETFOCUS:
    case WM_ACTIVATE: {
        static int n;
        if (n++ < 6) { fprintf(stderr, "  [FBWIN] %s\n", m == WM_SETFOCUS ? "window has keyboard focus" : (LOWORD(w) ? "window activated" : "window deactivated")); fflush(stderr); }
        /* The click that brings the window to the front is for Windows, not
         * the game: it is not a press of the button the left mouse button
         * stands for. */
        if (m == WM_ACTIVATE && LOWORD(w) == WA_CLICKACTIVE) s_swallow_click = 1;
        break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        /* F10: the graphics settings overlay (and not Windows' menu mode,
         * which F10 would otherwise enter) */
        if (w == VK_F10) {
            if (!(l & 0x40000000)) xbox_GfxMenuKey(VK_F10);
            return 0;
        }
        if ((w == VK_F9 || w == VK_F8 || w == VK_F12 || w == VK_SNAPSHOT) && !(l & 0x40000000)) {   /* (1.9.7) F9 the HUD hidden / shown; F8 (F12, Print Screen) a screenshot */
            if (w == VK_F9) { xbox_hud_hidden = !xbox_hud_hidden; fprintf(stderr, "  [FBWIN] F9: the HUD %s\n", xbox_hud_hidden ? "hidden" : "shown"); fflush(stderr); }
            else fb_screenshot();
            return 0;
        }
        if ((w == VK_RETURN && m == WM_SYSKEYDOWN && !(l & 0x40000000)) || (w == VK_F11 && !(l & 0x40000000))) {
            fb_set_fullscreen(h, s_fs ? 0 : s_fs_kind);
            return 0;
        }
        if ((unsigned)w < 256)
            s_key_down[w] = 1;
        { static int n; if (n++ < 3) { fprintf(stderr, "  [FBWIN] key down vk=0x%02X\n", (unsigned)w); fflush(stderr); } }
        /* RECOMP_KEY_TRACE: each key as it arrives, edge-triggered.
         *
         * The obvious diagnostic -- sampling which keys are held, once a
         * second, from the input path -- cannot tell a key that was never
         * pressed from one that was tapped: a 100 ms press is caught about
         * one time in ten. That ambiguity is expensive when the only way
         * to test is to ask someone to press a key and describe what
         * happened. This answers "did it arrive" on its own. */
        if (getenv("RECOMP_KEY_TRACE")) {
            static unsigned n;
            if (n++ < 40) {
                fprintf(stderr, "  [KEY] down vk=0x%02X\n", (unsigned)w);
                fflush(stderr);
            }
        }
        /* System keys still go to Windows, or Alt+F4 stops closing us. */
        return m == WM_SYSKEYDOWN ? DefWindowProcA(h, m, w, l) : 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (w == VK_F10) return 0;
        if ((unsigned)w < 256)
            s_key_down[w] = 0;
        return m == WM_SYSKEYUP ? DefWindowProcA(h, m, w, l) : 0;

    /* Mouse buttons as keys (VK_LBUTTON/RBUTTON/MBUTTON): the keyboard
     * layout maps them to pad buttons. */
    /* The window captures the mouse while a button is down, so a button let
     * go outside the window still arrives and is not held for ever. */
    case WM_LBUTTONDOWN:
        if (s_swallow_click) { s_swallow_click = 0; return 0; }
        InterlockedIncrement(&xbox_fb_clicks[0]);
        s_key_down[VK_LBUTTON] = 1; SetCapture(h); return 0;
    case WM_LBUTTONUP:   s_key_down[VK_LBUTTON] = 0; if (!s_key_down[VK_RBUTTON] && !s_key_down[VK_MBUTTON]) ReleaseCapture(); return 0;
    case WM_RBUTTONDOWN: InterlockedIncrement(&xbox_fb_clicks[1]); s_key_down[VK_RBUTTON] = 1; SetCapture(h); return 0;
    case WM_RBUTTONUP:   s_key_down[VK_RBUTTON] = 0; if (!s_key_down[VK_LBUTTON] && !s_key_down[VK_MBUTTON]) ReleaseCapture(); return 0;
    case WM_MBUTTONDOWN: s_key_down[VK_MBUTTON] = 1; SetCapture(h); return 0;
    case WM_MBUTTONUP:   s_key_down[VK_MBUTTON] = 0; if (!s_key_down[VK_LBUTTON] && !s_key_down[VK_RBUTTON]) ReleaseCapture(); return 0;
    case WM_MOUSEWHEEL: {
        static int acc;   /* (fine-grained wheels send parts of a notch) */
        acc += (short)HIWORD(w);
        while (acc >= 120) { InterlockedIncrement(&xbox_fb_wheel); acc -= 120; }
        while (acc <= -120) { InterlockedDecrement(&xbox_fb_wheel); acc += 120; }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if ((HWND)l != h) s_key_down[VK_LBUTTON] = s_key_down[VK_RBUTTON] = s_key_down[VK_MBUTTON] = 0;
        return 0;

    /* Alt-tabbing away with a key held would leave it held for ever. */
    case WM_KILLFOCUS:
        memset((void *)s_key_down, 0, sizeof s_key_down);
        return 0;
    case WM_TIMER:
        if (w == 0x7701) {   /* (1.9.8) back in the game after Alt+Tab, full screen: its kind left and taken again */
            KillTimer(h, 0x7701);
            if (s_fs && !IsIconic(h) && GetForegroundWindow() == h) {
                /* (exclusive taken again the same way left the films slow, a playtest log: borderless kept for the session.
                 * RECOMP_FS_BACK_EXCLUSIVE: exclusive again 700 ms later, as before) */
                int back = s_fs == 2 && !getenv("RECOMP_FS_BACK_EXCLUSIVE") ? 0 : s_fs;
                s_fs_back_kind = back;
                fprintf(stderr, "  [FBWIN] back in the game: full screen set again (%s%s)\n", s_fs == 2 ? "exclusive" : "borderless", back ? "" : "; borderless from now on, this session"); fflush(stderr);
                fb_set_fullscreen(h, s_fs == 2 ? 1 : 2);
                if (back) SetTimer(h, 0x7702, 700, NULL);
            }
            return 0;
        }
        if (w == 0x7702) {   /* ... and back to the kind it was */
            KillTimer(h, 0x7702);
            if (s_fs && s_fs_back_kind && s_fs != s_fs_back_kind) fb_set_fullscreen(h, s_fs_back_kind);
            s_fs_back_kind = 0;
            return 0;
        }
        break;
    case WM_ACTIVATEAPP:
        if (!w) memset((void *)s_key_down, 0, sizeof s_key_down);
        /* (1.9.8) coming back in full screen, the films played slowly until the full-screen kind was changed and changed
         * back (F10, a playtest): done here a moment after coming back. RECOMP_NO_FS_REFRESH: off */
        if (w && s_fs && !getenv("RECOMP_NO_FS_REFRESH")) SetTimer(h, 0x7701, 1200, NULL);
        if (s_fs == 2) {   /* exclusive: the desktop's mode while away */
            if (!w && s_mode_want != 0) { fb_mode_apply(0); ShowWindow(h, SW_MINIMIZE); }
            else if (w) {
                if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
                if (s_mode_want != 1) fb_mode_apply(1);
                fb_cover_monitor(h);
            }
        }
        return DefWindowProcA(h, m, w, l);
    }
    return DefWindowProcA(h, m, w, l);
}

/* The display formats an Xbox front buffer is actually set to. The pitch says
 * how wide a row is in bytes, so pitch/width gives the pixel size; the exact
 * component layout only matters for 16-bit, where 5:6:5 and 1:5:5:5 differ. */
static void fb_convert(const uint8_t *src, uint32_t bpp)
{
    uint32_t x, y;

    for (y = 0; y < s_fb_height; y++) {
        const uint8_t *row = src + (size_t)y * s_fb_pitch;
        uint32_t *dst = s_rgb + (size_t)y * s_fb_width;

        if (bpp == 4) {
            memcpy(dst, row, (size_t)s_fb_width * 4);
        } else if (bpp == 2) {
            const uint16_t *p = (const uint16_t *)row;
            for (x = 0; x < s_fb_width; x++) {
                uint16_t v = p[x];
                uint32_t r = (uint32_t)((v >> 11) & 0x1F) * 255u / 31u;
                uint32_t g = (uint32_t)((v >>  5) & 0x3F) * 255u / 63u;
                uint32_t b = (uint32_t)( v        & 0x1F) * 255u / 31u;
                dst[x] = (r << 16) | (g << 8) | b;
            }
        } else {
            memset(dst, 0, (size_t)s_fb_width * 4);
        }
    }
}

/* Write what the window is currently showing to a 24-bit BMP.
 *
 * A black window is ambiguous: it means either that the read path is wrong or
 * that the title really did render black. Dumping the same converted pixels
 * the window draws settles which, and does it without a screenshot. */
int xbox_FramebufferDumpBmp(const char *path)
{
    FILE *f;
    uint32_t row = ((s_fb_width * 3u) + 3u) & ~3u;
    uint32_t img = row * s_fb_height, total = 54u + img, y, x;
    uint8_t hdr[54], *line;

    if (!s_rgb || !s_fb_va)
        return -1;
    f = fopen(path, "wb");
    if (!f)
        return -1;
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &total, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &s_fb_width, 4);
    memcpy(hdr + 22, &s_fb_height, 4);
    hdr[26] = 1; hdr[28] = 24;
    memcpy(hdr + 34, &img, 4);
    fwrite(hdr, 1, sizeof(hdr), f);

    line = (uint8_t *)calloc(1, row);
    for (y = 0; y < s_fb_height; y++) {
        const uint32_t *src = s_rgb + (size_t)(s_fb_height - 1 - y) * s_fb_width;
        for (x = 0; x < s_fb_width; x++) {
            line[x * 3 + 0] = (uint8_t)(src[x] & 0xFF);
            line[x * 3 + 1] = (uint8_t)((src[x] >> 8) & 0xFF);
            line[x * 3 + 2] = (uint8_t)((src[x] >> 16) & 0xFF);
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    fprintf(stderr, "  [FBWIN] wrote %s (%ux%u from 0x%08X)\n",
            path, s_fb_width, s_fb_height, s_fb_va);
    return 0;
}

/* The window in real pixels (per-monitor DPI aware). Left DPI-unaware, a
 * window on a scaled display (a 4K screen at 175%) is drawn small and
 * stretched by Windows -- softer, and with Vulkan (DXVK) on some drivers
 * not shown at all: a black window. RECOMP_DPI_UNAWARE=1: as before. */
static int fb_dpi_scale100 = 100;
static void fb_dpi_aware(void)
{
    typedef BOOL (WINAPI *PFN_SPDAC)(HANDLE);
    typedef UINT (WINAPI *PFN_GDFS)(void);
    HMODULE u = GetModuleHandleA("user32.dll");
    PFN_SPDAC spdac;
    PFN_GDFS gdfs;
    static int done;
    if (done++ || getenv("RECOMP_DPI_UNAWARE") || !u) return;
    spdac = (PFN_SPDAC)(void *)GetProcAddress(u, "SetProcessDpiAwarenessContext");
    if (!(spdac && spdac((HANDLE)(INT_PTR)-4 /* PER_MONITOR_AWARE_V2 */))) SetProcessDPIAware();
    gdfs = (PFN_GDFS)(void *)GetProcAddress(u, "GetDpiForSystem");
    if (gdfs) { UINT d = gdfs(); if (d >= 96 && d <= 960) fb_dpi_scale100 = (int)(d * 100 / 96); }
    if (fb_dpi_scale100 != 100) { fprintf(stderr, "  [FBWIN] display scaling %d%%: the window in real pixels\n", fb_dpi_scale100); fflush(stderr); }
}

extern void xbox_UnpinServiceThread(void);
static DWORD WINAPI fb_thread(LPVOID unused)
{
    xbox_UnpinServiceThread();
    HWND hwnd;
    HDC hdc;
    BITMAPINFO bi;
    RECT r;

    (void)unused;

    fb_dpi_aware();
    {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.lpfnWndProc   = fb_wndproc;
        wc.hInstance     = GetModuleHandleA(NULL);
        wc.hCursor       = LoadCursorA(NULL, IDC_ARROW);
        wc.lpszClassName = "XboxRecompFramebuffer";
        RegisterClassA(&wc);
    }
    {
        /* Window size: RECOMP_WINDOW_SIZE=WxH, else a picture of the chosen
         * shape (RECOMP_ASPECT, 4:3 by default) about 960 high, kept inside
         * the work area. */
        const char *ws = getenv("RECOMP_WINDOW_SIZE"), *as = getenv("RECOMP_ASPECT");
        float a = as ? (float)atof(as) : 0.0f;
        int cw = 0, ch = 0;
        RECT wa;
        if (a < 1.0f || a > 6.0f) a = 4.0f / 3.0f;
        if (ws) sscanf(ws, "%dx%d", &cw, &ch);
        if (cw < 160 || ch < 120) {
            SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
            ch = 960 * fb_dpi_scale100 / 100;   /* (as big on the screen as before) */
            if (ch > (wa.bottom - wa.top) * 85 / 100) ch = (wa.bottom - wa.top) * 85 / 100;
            if (ch < 480) ch = 480;
            cw = (int)(ch * a + 0.5f);
            if (cw > (wa.right - wa.left) * 95 / 100) { cw = (wa.right - wa.left) * 95 / 100; ch = (int)(cw / a); }
        }
        r.left = 0; r.top = 0; r.right = cw; r.bottom = ch;
    }
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd = CreateWindowExA(0, "XboxRecompFramebuffer",
                           getenv("RECOMP_WINDOW_TITLE") ? getenv("RECOMP_WINDOW_TITLE") : "Xbox Recomp - Framebuffer",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top,
                           NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hwnd) {
        InterlockedExchange(&s_fb_running, 0);
        return 0;
    }
    hdc = GetDC(hwnd);
    s_fb_hwnd = hwnd;
    if (getenv("RECOMP_FULLSCREEN") && (getenv("RECOMP_FULLSCREEN")[0] == '1' || getenv("RECOMP_FULLSCREEN")[0] == '2'))
        fb_set_fullscreen(hwnd, getenv("RECOMP_FULLSCREEN")[0] - '0');
    SetStretchBltMode(hdc, HALFTONE);

    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth       = (LONG)s_fb_width;
    bi.bmiHeader.biHeight      = -(LONG)s_fb_height;   /* top-down */
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    s_rgb = (uint32_t *)calloc((size_t)s_fb_width * s_fb_height, 4);

    if (getenv("RECOMP_TEST_ALTTAB")) CreateThread(NULL, 0, fb_alttab_test, (LPVOID)(uintptr_t)atoi(getenv("RECOMP_TEST_ALTTAB")), 0, NULL);
    fprintf(stderr, "  [FBWIN] framebuffer window open (%ux%u)\n",
            s_fb_width, s_fb_height);

    while (InterlockedCompareExchange(&s_fb_running, 1, 1)) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (s_fb_gpu) {
            /* D3D11 owns the window; keep s_rgb current for dumps only
             * (1.9.3: and only when a frame has come since) */
            static LONG copied = -2; static unsigned seq_was;
            extern volatile unsigned g_nv2a_flips;
            LONG idx = s_present_idx;
            if (idx >= 0 && s_present[idx] && s_rgb && (idx != copied || g_nv2a_flips != seq_was)) {
                memcpy(s_rgb, s_present[idx], (size_t)s_fb_width * s_fb_height * 4);
                copied = idx; seq_was = g_nv2a_flips;
            }
        } else if (s_present_idx >= 0 && s_rgb) {
            /* A finished frame, published by the flip. Copied into s_rgb so
             * the dump path and GDI see one consistent image even if the
             * next flip lands mid-blit. */
            LONG idx = s_present_idx;
            if (s_present[idx])
                memcpy(s_rgb, s_present[idx],
                       (size_t)s_fb_width * s_fb_height * 4);
            { RECT cr; GetClientRect(hwnd, &cr);
            StretchDIBits(hdc, 0, 0, (int)cr.right, (int)cr.bottom,
                          0, 0, (int)s_fb_width, (int)s_fb_height,
                          s_rgb, &bi, DIB_RGB_COLORS, SRCCOPY); }
        } else if (s_fb_va && s_fb_pitch && s_rgb) {
            /* No flip yet, or pinned with RECOMP_FB_VA: read guest memory as
             * before, which is also what a title that never flips needs. */
            const uint8_t *src =
                (const uint8_t *)((uintptr_t)s_fb_va + xbox_GetMemoryOffset());
            fb_convert(src, s_fb_pitch / s_fb_width);
            { RECT cr; GetClientRect(hwnd, &cr);
            StretchDIBits(hdc, 0, 0, (int)cr.right, (int)cr.bottom,
                          0, 0, (int)s_fb_width, (int)s_fb_height,
                          s_rgb, &bi, DIB_RGB_COLORS, SRCCOPY); }
        }
        {
            /* One dump a few seconds in, so the title has had time to render
             * something rather than catching the first blank frame.
             *
             * RECOMP_FB_WINDOW_DUMP_EVERY=<frames> dumps repeatedly instead.
             * This window follows the address AvSetDisplayMode gave, which is
             * what the CRTC scans and therefore what a person sees; the
             * pushbuffer executor's own dump follows its draw surface. With
             * double buffering those are different buffers, and measuring
             * progress from the executor's dump reports a blank screen while
             * the window is showing the title's logo. Ask the window. */
            const char *dump = getenv("RECOMP_FB_DUMP");
            const char *every = getenv("RECOMP_FB_WINDOW_DUMP_EVERY");
            static int frames;
            int period = every ? atoi(every) : 0;
            frames++;
            if (dump && period > 0) {
                if (frames % period == 0) {
                    static int seq;
                    char nm[260];
                    snprintf(nm, sizeof(nm), "%sw%04d.bmp", dump, seq++);
                    xbox_FramebufferDumpBmp(nm);
                }
            } else if (dump && frames == 600) {
                xbox_FramebufferDumpBmp(dump);
            }
        }
        Sleep(16);
    }

    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    free(s_rgb);
    s_rgb = NULL;
    return 0;
}

void xbox_FramebufferWindowStart(void)
{
    HANDLE th;

    if (!getenv("RECOMP_FB_WINDOW"))
        return;
    if (InterlockedCompareExchange(&s_fb_running, 1, 0) != 0)
        return;
    th = CreateThread(NULL, 0, fb_thread, NULL, 0, NULL);
    if (th)
        CloseHandle(th);
    else
        InterlockedExchange(&s_fb_running, 0);
}

#else /* POSIX: an SDL2 window, with an OpenGL 3.3 context for the GL renderer */
volatile int xbox_hud_hidden;   /* (1.9.7) F1 (Windows only for now) */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <SDL.h>
#include "platform/xbox_winnt.h"

extern ptrdiff_t xbox_GetMemoryOffset(void);

static volatile LONG s_fb_running;
static SDL_Window *volatile s_win;
static SDL_GLContext volatile s_glctx;
static volatile LONG s_gl_state;        /* 0 not yet, 1 context ready, -1 none */
static volatile LONG s_fb_gpu;
static volatile int s_focus = 1;
static uint32_t s_fb_va, s_fb_pitch, s_fb_width = 640, s_fb_height = 480;
static uint32_t *s_rgb;
static uint32_t *s_present[2];
static volatile LONG s_present_idx = -1;
static Uint32 s_ev_fullscreen;           /* SDL user event: set full screen */

void *xbox_FramebufferHwnd(void) { return (void *)s_win; }
void xbox_FramebufferSetGpuOwned(int on) { InterlockedExchange(&s_fb_gpu, on); }
int xbox_FramebufferHasFocus(void) { return s_focus; }
/* the mouse over the window (fractions of its inside; 1 inside), for a
 * title's pointer; while it draws one, the system's is hidden over it */
volatile int xbox_fb_hide_cursor;
/* left and right clicks so far (a click between two of the title's looks at the buttons still counts) */
volatile LONG xbox_fb_clicks[2];
volatile LONG xbox_fb_wheel;   /* mouse wheel notches, up positive (counted; readers keep their own last value) */
static volatile int s_mouse_x = -1, s_mouse_y = -1, s_mouse_in, s_win_w = 1, s_win_h = 1;
int xbox_FramebufferMouse(float *fx, float *fy)
{
    if (s_mouse_x < 0 || s_win_w <= 0 || s_win_h <= 0) return 0;
    *fx = ((float)s_mouse_x + 0.5f) / (float)s_win_w; *fy = ((float)s_mouse_y + 0.5f) / (float)s_win_h;
    return s_mouse_in && s_mouse_x < s_win_w && s_mouse_y < s_win_h;
}
/* the mouse's movement while a title asks for it (SDL's relative mode) */
static volatile int s_rel_want, s_rel_dx, s_rel_dy;
int xbox_FramebufferMouseDelta(int *dx, int *dy, int *w, int *h)
{
    s_rel_want = 1;
    *dx = InterlockedExchange((volatile LONG *)&s_rel_dx, 0);
    *dy = InterlockedExchange((volatile LONG *)&s_rel_dy, 0);
    *w = s_win_w; *h = s_win_h;
    return 1;
}
void xbox_FramebufferMouseRelease(void) { s_rel_want = 0; }

/* The GL context the window thread made for the renderer (not current on
 * any thread). Waits up to ms for the window to come up. NULL: none. */
void xbox_FramebufferWindowStart(void);
void *xbox_FramebufferGLContext(int ms)
{
    int waited = 0;
    if (!s_fb_running && s_gl_state == 0 && getenv("RECOMP_FB_WINDOW")) xbox_FramebufferWindowStart();
    while (s_gl_state == 0 && s_fb_running && waited < ms) { Sleep(5); waited += 5; }
    return s_gl_state > 0 ? (void *)s_glctx : NULL;
}

void xbox_FramebufferWindowSet(uint32_t fb_va, uint32_t pitch)
{
    const char *pin = getenv("RECOMP_FB_VA");
    s_fb_va = pin ? (uint32_t)strtoul(pin, NULL, 0) : fb_va;
    if (pitch) s_fb_pitch = pitch;
}

unsigned char g_xbox_gamma[3][256];
volatile int g_xbox_gamma_set;

static void fb_convert_to(uint32_t *out, const uint8_t *src, uint32_t pitch)
{
    uint32_t bpp = pitch / s_fb_width, x, y;
    for (y = 0; y < s_fb_height; y++) {
        const uint8_t *row = src + (size_t)y * pitch;
        uint32_t *dst = out + (size_t)y * s_fb_width;
        if (bpp == 4) memcpy(dst, row, (size_t)s_fb_width * 4);
        else if (bpp == 2) {
            const uint16_t *p = (const uint16_t *)row;
            for (x = 0; x < s_fb_width; x++) {
                uint16_t v = p[x];
                uint32_t r = (uint32_t)((v >> 11) & 0x1F) * 255u / 31u;
                uint32_t g = (uint32_t)((v >>  5) & 0x3F) * 255u / 63u;
                uint32_t b = (uint32_t)( v        & 0x1F) * 255u / 31u;
                dst[x] = (r << 16) | (g << 8) | b;
            }
        } else memset(dst, 0, (size_t)s_fb_width * 4);
    }
}

void xbox_FramebufferWindowPresent(uint32_t fb_va, uint32_t pitch)
{
    LONG next;
    if (!s_fb_running || !fb_va || !pitch) return;
    if (getenv("RECOMP_FB_VA")) return;
    /* the GPU renderer draws the window itself; guest memory is stale then */
    if (s_fb_gpu && !getenv("RECOMP_FB_DUMP")) return;
    next = (s_present_idx == 0) ? 1 : 0;
    if (!s_present[next]) {
        s_present[next] = (uint32_t *)calloc((size_t)s_fb_width * s_fb_height, 4);
        if (!s_present[next]) return;
    }
    fb_convert_to(s_present[next], (const uint8_t *)((uintptr_t)fb_va + xbox_GetMemoryOffset()), pitch);
    if (g_xbox_gamma_set) {
        size_t i, n = (size_t)s_fb_width * s_fb_height;
        uint32_t *d = s_present[next];
        for (i = 0; i < n; i++) {
            uint32_t v = d[i];
            d[i] = ((uint32_t)g_xbox_gamma[0][(v >> 16) & 0xFF] << 16)
                 | ((uint32_t)g_xbox_gamma[1][(v >> 8) & 0xFF] << 8)
                 |  (uint32_t)g_xbox_gamma[2][v & 0xFF];
        }
    }
    InterlockedExchange(&s_present_idx, next);
}

/* Keys held, by Windows virtual-key code (see fb_present's Windows half). */
static volatile unsigned char s_key_down[256];
int xbox_FramebufferKeyDown(int vk) { return (unsigned)vk <= 255 && s_key_down[vk] != 0; }
void xbox_FramebufferKeyClear(int vk) { if ((unsigned)vk <= 255) s_key_down[vk] = 0; }

static int vk_from_sdl(SDL_Scancode sc)
{
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'A' + (sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return '1' + (sc - SDL_SCANCODE_1);
    if (sc == SDL_SCANCODE_0) return '0';
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return VK_F1 + (sc - SDL_SCANCODE_F1);
    if (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24) return VK_F1 + 12 + (sc - SDL_SCANCODE_F13);
    if (sc >= SDL_SCANCODE_KP_1 && sc <= SDL_SCANCODE_KP_9) return VK_NUMPAD1 + (sc - SDL_SCANCODE_KP_1);
    switch (sc) {
    case SDL_SCANCODE_KP_0: return VK_NUMPAD0;
    case SDL_SCANCODE_KP_PERIOD: return VK_DECIMAL;
    case SDL_SCANCODE_KP_DIVIDE: return VK_DIVIDE;
    case SDL_SCANCODE_KP_MULTIPLY: return VK_MULTIPLY;
    case SDL_SCANCODE_KP_MINUS: return VK_SUBTRACT;
    case SDL_SCANCODE_KP_PLUS: return VK_ADD;
    case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_RETURN: case SDL_SCANCODE_RETURN2: return VK_RETURN;
    case SDL_SCANCODE_ESCAPE: return VK_ESCAPE;
    case SDL_SCANCODE_BACKSPACE: return VK_BACK;
    case SDL_SCANCODE_TAB: return VK_TAB;
    case SDL_SCANCODE_SPACE: return VK_SPACE;
    case SDL_SCANCODE_LSHIFT: return VK_LSHIFT;
    case SDL_SCANCODE_RSHIFT: return VK_RSHIFT;
    case SDL_SCANCODE_LCTRL: return VK_LCONTROL;
    case SDL_SCANCODE_RCTRL: return VK_RCONTROL;
    case SDL_SCANCODE_LALT: return VK_LMENU;
    case SDL_SCANCODE_RALT: return VK_RMENU;
    case SDL_SCANCODE_LGUI: return VK_LWIN;
    case SDL_SCANCODE_RGUI: return VK_RWIN;
    case SDL_SCANCODE_APPLICATION: return VK_APPS;
    case SDL_SCANCODE_CAPSLOCK: return VK_CAPITAL;
    case SDL_SCANCODE_NUMLOCKCLEAR: return VK_NUMLOCK;
    case SDL_SCANCODE_SCROLLLOCK: return VK_SCROLL;
    case SDL_SCANCODE_PAUSE: return VK_PAUSE;
    case SDL_SCANCODE_PRINTSCREEN: return VK_SNAPSHOT;
    case SDL_SCANCODE_UP: return VK_UP;
    case SDL_SCANCODE_DOWN: return VK_DOWN;
    case SDL_SCANCODE_LEFT: return VK_LEFT;
    case SDL_SCANCODE_RIGHT: return VK_RIGHT;
    case SDL_SCANCODE_INSERT: return VK_INSERT;
    case SDL_SCANCODE_DELETE: return VK_DELETE;
    case SDL_SCANCODE_HOME: return VK_HOME;
    case SDL_SCANCODE_END: return VK_END;
    case SDL_SCANCODE_PAGEUP: return VK_PRIOR;
    case SDL_SCANCODE_PAGEDOWN: return VK_NEXT;
    case SDL_SCANCODE_SEMICOLON: return VK_OEM_1;
    case SDL_SCANCODE_EQUALS: return VK_OEM_PLUS;
    case SDL_SCANCODE_COMMA: return VK_OEM_COMMA;
    case SDL_SCANCODE_MINUS: return VK_OEM_MINUS;
    case SDL_SCANCODE_PERIOD: return VK_OEM_PERIOD;
    case SDL_SCANCODE_SLASH: return VK_OEM_2;
    case SDL_SCANCODE_GRAVE: return VK_OEM_3;
    case SDL_SCANCODE_LEFTBRACKET: return VK_OEM_4;
    case SDL_SCANCODE_BACKSLASH: return VK_OEM_5;
    case SDL_SCANCODE_RIGHTBRACKET: return VK_OEM_6;
    case SDL_SCANCODE_APOSTROPHE: return VK_OEM_7;
    case SDL_SCANCODE_NONUSBACKSLASH: return VK_OEM_102;
    default: return 0;
    }
}

/* the generic key (VK_SHIFT ...) is down while either side is */
static void key_set(int vk, int down)
{
    if (!vk) return;
    s_key_down[vk] = (unsigned char)down;
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) s_key_down[VK_SHIFT] = s_key_down[VK_LSHIFT] | s_key_down[VK_RSHIFT];
    if (vk == VK_LCONTROL || vk == VK_RCONTROL) s_key_down[VK_CONTROL] = s_key_down[VK_LCONTROL] | s_key_down[VK_RCONTROL];
    if (vk == VK_LMENU || vk == VK_RMENU) s_key_down[VK_MENU] = s_key_down[VK_LMENU] | s_key_down[VK_RMENU];
}

static int s_fs, s_fs_kind = 1;   /* (0 windowed, 1 borderless, 2 exclusive: a real mode change) */
extern void xbox_GfxFsMode(int *w, int *h);
static void fb_set_fullscreen(int on)
{
    if (on) s_fs_kind = on;
    if (!s_win || on == s_fs) return;
    if (on == 2) {   /* the chosen mode (the desktop's: as it is) */
        SDL_DisplayMode want, got;
        int w = 0, h = 0;
        xbox_GfxFsMode(&w, &h);
        if (w > 0 && h > 0 && SDL_GetDesktopDisplayMode(SDL_GetWindowDisplayIndex(s_win), &want) == 0) {
            want.w = w; want.h = h; want.refresh_rate = 0;
            if (SDL_GetClosestDisplayMode(SDL_GetWindowDisplayIndex(s_win), &want, &got)) SDL_SetWindowDisplayMode(s_win, &got);
        }
    }
    if (SDL_SetWindowFullscreen(s_win, on == 2 ? SDL_WINDOW_FULLSCREEN : on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0) != 0) {
        fprintf(stderr, "  [FBWIN] full screen switch failed: %s\n", SDL_GetError());
        return;
    }
    SDL_ShowCursor(xbox_fb_hide_cursor < 0 ? SDL_ENABLE : on || xbox_fb_hide_cursor ? SDL_DISABLE : SDL_ENABLE);
    s_fs = on;
    fprintf(stderr, "  [FBWIN] %s\n", on == 2 ? "exclusive full screen" : on ? "full screen" : "windowed");
    fflush(stderr);
}
extern int xbox_GfxMenuKey(int vk);
void xbox_FramebufferSetFullscreen(int on)
{
    SDL_Event e;
    if (!s_win || !s_ev_fullscreen) return;
    memset(&e, 0, sizeof e);
    e.type = s_ev_fullscreen;
    e.user.code = on < 0 ? 0 : on > 2 ? 1 : on;
    SDL_PushEvent(&e);
}
int xbox_FramebufferIsFullscreen(void) { return s_fs; }
int xbox_DisplayRefreshHz(void) { return 0; }   /* (unknown here: no cap) */
void xbox_FramebufferFsModeChanged(void)
{
    SDL_Event e;
    if (!s_win || !s_ev_fullscreen) return;
    memset(&e, 0, sizeof e);
    e.type = s_ev_fullscreen;
    e.user.code = 11;
    SDL_PushEvent(&e);
}
void xbox_FramebufferSetWindowSize(int w, int h)
{
    SDL_Event e;
    if (!s_win || !s_ev_fullscreen || w < 160 || h < 120) return;
    memset(&e, 0, sizeof e);
    e.type = s_ev_fullscreen;
    e.user.code = 10;   /* (the size in data1; 0-2 are the full-screen kinds) */
    e.user.data1 = (void *)(intptr_t)((w << 16) | h);
    SDL_PushEvent(&e);
}

int xbox_FramebufferDumpBmp(const char *path)
{
    FILE *f;
    uint32_t row = ((s_fb_width * 3u) + 3u) & ~3u;
    uint32_t img = row * s_fb_height, total = 54u + img, y, x;
    uint8_t hdr[54], *line;
    if (!s_rgb) return -1;
    f = fopen(path, "wb");
    if (!f) return -1;
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr + 2, &total, 4);
    hdr[10] = 54; hdr[14] = 40;
    memcpy(hdr + 18, &s_fb_width, 4);
    memcpy(hdr + 22, &s_fb_height, 4);
    hdr[26] = 1; hdr[28] = 24;
    memcpy(hdr + 34, &img, 4);
    fwrite(hdr, 1, sizeof(hdr), f);
    line = (uint8_t *)calloc(1, row);
    for (y = 0; y < s_fb_height && line; y++) {
        const uint32_t *src = s_rgb + (size_t)(s_fb_height - 1 - y) * s_fb_width;
        for (x = 0; x < s_fb_width; x++) {
            line[x * 3 + 0] = (uint8_t)(src[x] & 0xFF);
            line[x * 3 + 1] = (uint8_t)((src[x] >> 8) & 0xFF);
            line[x * 3 + 2] = (uint8_t)((src[x] >> 16) & 0xFF);
        }
        fwrite(line, 1, row, f);
    }
    free(line);
    fclose(f);
    return 0;
}

static void fb_closed(const char *why)
{
    extern void xbox_InputStopRumble(void);
    fprintf(stderr, "[EXIT] game window closed (%s)\n", why);
    fflush(stderr);
    xbox_InputStopRumble();
    InterlockedExchange(&s_fb_running, 0);
    /* the game has no way to be asked to quit: end the process */
    fflush(stdout);
    _exit(0);
}
void xbox_FramebufferRequestClose(void) { fb_closed("Exit chosen in the main menu"); }

extern void xbox_UnpinServiceThread(void);
static DWORD WINAPI fb_thread(LPVOID unused)
{
    SDL_Renderer *ren = NULL;
    SDL_Texture *tex = NULL;
    const char *rend = getenv("RECOMP_RENDERER");
    int want_gl = !(rend && !strcasecmp(rend, "software"));
    int cw = 0, ch = 0;
    Uint32 last_click_guard = 0;
    (void)unused;
    xbox_UnpinServiceThread();

    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "0");
    SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
    /* SDL turns SIGINT/SIGTERM into a quit event; the game's own handling
     * (window close) covers that. */
    {
        extern void xbox_SDLInitLock(int lock);
        int r;
        xbox_SDLInitLock(1);
        r = SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
        xbox_SDLInitLock(0);
        if (r != 0) goto sdl_failed;
    }
    if (0) {
sdl_failed:
        fprintf(stderr, "  [FBWIN] SDL video failed: %s (no window)\n", SDL_GetError());
        InterlockedExchange(&s_gl_state, -1);
        InterlockedExchange(&s_fb_running, 0);
        return 0;
    }
    s_ev_fullscreen = SDL_RegisterEvents(1);
    /* joysticks are read by the input layer on its own thread */
    SDL_JoystickEventState(SDL_IGNORE);
    SDL_GameControllerEventState(SDL_IGNORE);
    {
        const char *ws = getenv("RECOMP_WINDOW_SIZE"), *as = getenv("RECOMP_ASPECT");
        float a = as ? (float)atof(as) : 0.0f;
        SDL_Rect wa;
        if (a < 1.0f || a > 6.0f) a = 4.0f / 3.0f;
        if (ws) sscanf(ws, "%dx%d", &cw, &ch);
        if (cw < 160 || ch < 120) {
            if (SDL_GetDisplayUsableBounds(0, &wa) != 0) { wa.w = 1280; wa.h = 1024; }
            ch = 960;
            if (ch > wa.h * 85 / 100) ch = wa.h * 85 / 100;
            if (ch < 480) ch = 480;
            cw = (int)(ch * a + 0.5f);
            if (cw > wa.w * 95 / 100) { cw = wa.w * 95 / 100; ch = (int)(cw / a); }
        }
    }
    if (want_gl) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#if defined(__APPLE__)
        /* macOS gives a core context (OpenGL 4.1) only forward-compatible */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    }
    s_win = SDL_CreateWindow(getenv("RECOMP_WINDOW_TITLE") ? getenv("RECOMP_WINDOW_TITLE") : "Xbox Recomp",
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cw, ch,
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (want_gl ? SDL_WINDOW_OPENGL : 0));
    if (!s_win && want_gl) {
        fprintf(stderr, "  [FBWIN] no OpenGL window (%s); drawing on the CPU\n", SDL_GetError());
        want_gl = 0;
        s_win = SDL_CreateWindow(getenv("RECOMP_WINDOW_TITLE") ? getenv("RECOMP_WINDOW_TITLE") : "Xbox Recomp",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, cw, ch,
                                 SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    }
    if (!s_win) {
        fprintf(stderr, "  [FBWIN] cannot open a window: %s\n", SDL_GetError());
        InterlockedExchange(&s_gl_state, -1);
        InterlockedExchange(&s_fb_running, 0);
        return 0;
    }
    if (want_gl) {
        SDL_GLContext c = SDL_GL_CreateContext(s_win);
        if (!c) {
            fprintf(stderr, "  [FBWIN] no OpenGL 3.3 context (%s); drawing on the CPU\n", SDL_GetError());
            InterlockedExchange(&s_gl_state, -1);
        } else {
            /* the renderer makes it current on its own thread */
            SDL_GL_MakeCurrent(s_win, NULL);
            s_glctx = c;
            InterlockedExchange(&s_gl_state, 1);
        }
    } else InterlockedExchange(&s_gl_state, -1);
    if (getenv("RECOMP_FULLSCREEN") && (getenv("RECOMP_FULLSCREEN")[0] == '1' || getenv("RECOMP_FULLSCREEN")[0] == '2'))
        fb_set_fullscreen(getenv("RECOMP_FULLSCREEN")[0] - '0');
    s_rgb = (uint32_t *)calloc((size_t)s_fb_width * s_fb_height, 4);
    fprintf(stderr, "  [FBWIN] window open (%dx%d, %s)\n", cw, ch, s_gl_state > 0 ? "OpenGL" : "CPU picture");
    fflush(stderr);

    while (s_fb_running) {
        SDL_Event e;
        int got = SDL_WaitEventTimeout(&e, 16);
        {   /* the window's size (for the mouse) and the system's pointer */
            static int shown = -1;
            int w = 0, h = 0, want = xbox_fb_hide_cursor < 0 || !(s_fs || xbox_fb_hide_cursor);
            SDL_GetWindowSize(s_win, &w, &h);
            if (w > 0 && h > 0) { s_win_w = w; s_win_h = h; }
            if (want != shown) { SDL_ShowCursor(want ? SDL_ENABLE : SDL_DISABLE); shown = want; }
            {   /* relative mode while a title reads the mouse's movement */
                static int rel;
                int rw = s_rel_want && s_focus;
                if (rw != rel) { SDL_SetRelativeMouseMode(rw ? SDL_TRUE : SDL_FALSE); rel = rw; }
            }
        }
        while (got) {
            switch (e.type) {
            case SDL_QUIT:
                fb_closed("close");
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_CLOSE) fb_closed("close");
                else if (e.window.event == SDL_WINDOWEVENT_ENTER) s_mouse_in = 1;
                else if (e.window.event == SDL_WINDOWEVENT_LEAVE) s_mouse_in = 0;
                else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    s_focus = 0;
                    memset((void *)s_key_down, 0, sizeof s_key_down);
                } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                    s_focus = 1;
                    /* the click that brought the window to the front is not
                     * a press of the button it stands for */
                    last_click_guard = SDL_GetTicks() + 150;
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                int vk = vk_from_sdl(e.key.keysym.scancode), down = e.type == SDL_KEYDOWN;
                if (vk == VK_F10) {
                    if (down && !e.key.repeat) xbox_GfxMenuKey(VK_F10);
                    break;
                }
                if (down && !e.key.repeat && (vk == VK_F11 || (vk == VK_RETURN && (e.key.keysym.mod & KMOD_ALT)))) {
                    fb_set_fullscreen(s_fs ? 0 : s_fs_kind);
                    break;
                }
                key_set(vk, down);
                if (down && getenv("RECOMP_KEY_TRACE")) {
                    static unsigned n;
                    if (n++ < 40) fprintf(stderr, "  [KEY] down vk=0x%02X\n", (unsigned)vk);
                }
                break;
            }
            case SDL_MOUSEMOTION:
                if (s_rel_want) { InterlockedExchangeAdd((volatile LONG *)&s_rel_dx, e.motion.xrel); InterlockedExchangeAdd((volatile LONG *)&s_rel_dy, e.motion.yrel); }
                s_mouse_x = e.motion.x; s_mouse_y = e.motion.y; s_mouse_in = 1;
                break;
            case SDL_MOUSEWHEEL:
                InterlockedExchangeAdd(&xbox_fb_wheel, e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP: {
                int down = e.type == SDL_MOUSEBUTTONDOWN, vk = 0;
                switch (e.button.button) {
                case SDL_BUTTON_LEFT: vk = VK_LBUTTON; break;
                case SDL_BUTTON_RIGHT: vk = VK_RBUTTON; break;
                case SDL_BUTTON_MIDDLE: vk = VK_MBUTTON; break;
                case SDL_BUTTON_X1: vk = VK_XBUTTON1; break;
                case SDL_BUTTON_X2: vk = VK_XBUTTON2; break;
                }
                if (down && vk == VK_LBUTTON && last_click_guard && !SDL_TICKS_PASSED(SDL_GetTicks(), last_click_guard)) break;
                if (down && vk == VK_LBUTTON) InterlockedIncrement(&xbox_fb_clicks[0]);
                if (down && vk == VK_RBUTTON) InterlockedIncrement(&xbox_fb_clicks[1]);
                key_set(vk, down);
                break;
            }
            default:
                if (s_ev_fullscreen && e.type == s_ev_fullscreen && e.user.code == 11) {   /* new full-screen resolution */
                    if (s_fs == 2) { s_fs = 1; fb_set_fullscreen(2); }
                } else if (s_ev_fullscreen && e.type == s_ev_fullscreen && e.user.code == 10) {   /* a window size */
                    int ww = (int)((intptr_t)e.user.data1 >> 16), wh = (int)((intptr_t)e.user.data1 & 0xFFFF);
                    if (!s_fs) SDL_SetWindowSize(s_win, ww, wh);
                    fprintf(stderr, "  [FBWIN] window size %dx%d\n", ww, wh);
                } else if (s_ev_fullscreen && e.type == s_ev_fullscreen) fb_set_fullscreen(e.user.code);
                break;
            }
            got = SDL_PollEvent(&e);
        }
        if (s_fb_gpu) {
            LONG idx = s_present_idx;
            if (idx >= 0 && s_present[idx] && s_rgb)
                memcpy(s_rgb, s_present[idx], (size_t)s_fb_width * s_fb_height * 4);
        } else if (s_gl_state != 0 && (s_present_idx >= 0 || (s_fb_va && s_fb_pitch))) {
            /* the CPU's picture (software renderer, or before the GPU one
             * starts): through an SDL renderer of its own */
            LONG idx = s_present_idx;
            if (idx >= 0 && s_present[idx] && s_rgb)
                memcpy(s_rgb, s_present[idx], (size_t)s_fb_width * s_fb_height * 4);
            else if (s_rgb && s_fb_va && s_fb_pitch)
                fb_convert_to(s_rgb, (const uint8_t *)((uintptr_t)s_fb_va + xbox_GetMemoryOffset()), s_fb_pitch);
            if (s_gl_state < 0 || getenv("RECOMP_FB_CPU_SHOW")) {
                if (!ren) {
                    ren = SDL_CreateRenderer(s_win, -1, 0);
                    if (!ren) ren = SDL_CreateRenderer(s_win, -1, SDL_RENDERER_SOFTWARE);
                    if (ren) {
                        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
                        tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                (int)s_fb_width, (int)s_fb_height);
                    }
                }
                if (ren && tex && s_rgb) {
                    int ow = 0, oh = 0;
                    SDL_Rect dst;
                    float a = 4.0f / 3.0f;
                    SDL_UpdateTexture(tex, NULL, s_rgb, (int)s_fb_width * 4);
                    SDL_GetRendererOutputSize(ren, &ow, &oh);
                    if (getenv("RECOMP_PRESENT_STRETCH") || oh <= 0) { dst.x = 0; dst.y = 0; dst.w = ow; dst.h = oh; }
                    else if ((float)ow / oh > a) { dst.h = oh; dst.w = (int)(oh * a); dst.x = (ow - dst.w) / 2; dst.y = 0; }
                    else { dst.w = ow; dst.h = (int)(ow / a); dst.x = 0; dst.y = (oh - dst.h) / 2; }
                    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
                    SDL_RenderClear(ren);
                    SDL_RenderCopy(ren, tex, NULL, &dst);
                    SDL_RenderPresent(ren);
                }
            }
        }
        {
            const char *dump = getenv("RECOMP_FB_DUMP");
            const char *every = getenv("RECOMP_FB_WINDOW_DUMP_EVERY");
            static int frames;
            int period = every ? atoi(every) : 0;
            frames++;
            if (dump && period > 0) {
                if (frames % period == 0) {
                    static int seq;
                    char nm[260];
                    snprintf(nm, sizeof(nm), "%sw%04d.bmp", dump, seq++);
                    xbox_FramebufferDumpBmp(nm);
                }
            } else if (dump && frames == 600) xbox_FramebufferDumpBmp(dump);
        }
    }
    return 0;
}

void xbox_FramebufferWindowStart(void)
{
    HANDLE th;
    if (!getenv("RECOMP_FB_WINDOW")) {
        InterlockedExchange(&s_gl_state, -1);
        return;
    }
    if (InterlockedCompareExchange(&s_fb_running, 1, 0) != 0) return;
    th = CreateThread(NULL, 0, fb_thread, NULL, 0, NULL);
    if (th) CloseHandle(th);
    else { InterlockedExchange(&s_fb_running, 0); InterlockedExchange(&s_gl_state, -1); }
}
#endif
