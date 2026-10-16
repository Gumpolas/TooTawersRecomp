/*
 * The Two Towers host glue, included by main.c (template-derived).
 *
 *  - twotowers_recomp.ini beside the exe: [Paths] game_dir, [Developer] logging,
 *    [Display] window
 *  - logging=1 sends stdout+stderr to twotowers_log.txt beside the exe
 *  - saves go to saves\ beside the exe
 *  - the framebuffer window (RECOMP_FB_WINDOW) is on unless [Display] window=0
 */
#ifndef TWOTOWERS_HOST_H
#define TWOTOWERS_HOST_H

#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#define TT_SEP "\\"
#else
#include <unistd.h>
#include <sys/stat.h>
#define TT_SEP "/"
#endif

/* Declared in the generated recomp_types.h, which main.c does not include. */
int recomp_dispatch_init(void);

static void twotowers_write_default_ini(void)
{
    FILE *f;
    if (GetFileAttributesA(g_ini_path) != INVALID_FILE_ATTRIBUTES) return;
    f = fopen(g_ini_path, "w");
    if (!f) return;
    fputs("; The Two Towers Recompiled settings\n"
          "[Paths]\n"
          "; extracted disc folder (contains default.xbe); empty = game_files beside the exe\n"
          "game_dir=\n"
          "\n[Language]\n"
          "; the game's and the launcher's language: en, or a folder name in lang\\ (fr, de, es, ja, ru, ...)\n"
          "language=en\n"
          "\n[Display]\n"
          "; 1 = show the game's framebuffer in a window (xboxrecomp RECOMP_FB_WINDOW)\n"
          "window=1\n"
#if defined(_WIN32)
          "; d3d11 = draw with your graphics card (Direct3D 11), vulkan = the same through Vulkan with\n"
          ";   DXVK (put DXVK's x64 d3d11.dll and dxgi.dll in a folder named dxvk beside the game),\n"
          ";   software = draw on the CPU\n"
          "renderer=d3d11\n"
#else
          "; gpu = draw with your graphics card (OpenGL 3.3), software = draw on the CPU\n"
          "renderer=gpu\n"
#endif
          "; 30 = as on the Xbox. 60 / unlocked = EXPERIMENTAL: if the game moves too fast, set back to 30\n"
          "fps=30\n"
          "; gpu = run the game's vertex programs on the graphics card (faster), cpu = the old way\n"
          "vertex_programs=gpu\n"
          "; 1 = start in full screen (borderless). Alt+Enter or F11 switches at any time\n"
          "fullscreen=0\n"
          "; window size in windowed mode, e.g. 1920x1080; empty = automatic\n"
          "window_size=\n"
          "; 1 = Ultra textures: sharper menus, level select and HUD, and with the GameCube high-resolution\n"
          ";   texture pack (its GLOS69 folder, downloaded separately, put beside the game) every texture.\n"
          ";   The launcher makes the pack ready once. Needs a graphics card with BC7 textures\n"
          "ultra_textures=0\n"
          "; 1 = the in-engine cutscenes widescreen too (0 = 4:3 with side bars: their skies were made for 4:3)\n"
          "wide_scenes=0\n"
          "; shape of the picture: 4:3 (original), 16:9, 16:10, 21:9, 32:9 or a number such as 2.37.\n"
          ";   Wider than 4:3 widens the 3D view; the HUD keeps its shape. Movies and menus stay 4:3\n"
          "aspect=4:3\n"
          "; the HUD in a picture wider than 4:3: edges = at the screen's edges, centre = in the 4:3 middle\n"
          "hud=edges\n"
          "; menus, loading and upgrade screens: 1 = fill the screen (past the old TV margin), 0 = original\n"
          "menu_fill=1\n"
          "; movies in widescreen: 0 = as made (4:3), 1 = fill the width (the films' black bands cut off)\n"
          "movie_fill=0\n"
          "; internal resolution: 1 = original 640x480, 2 = 1280x960, 3 = 1920x1440, 4 = 2560x1920, up to 12 = 7680x5760,\n"
          ";   4.5 = 2160 lines (3840x2160 at 16:9, a 4K screen pixel for pixel)\n"
          ";   (higher is sharper, also smooths edges when the window is smaller; costs GPU time)\n"
          "render_scale=1\n"
          "; anisotropic filtering: 1 (off), 2, 4, 8 or 16\n"
          "anisotropic=1\n"
          "; 1 = mipmapped textures (less shimmer in the distance, like the Xbox), 0 = off\n"
          "mipmaps=1\n"
          "; anti-aliasing: off, fxaa (edge smoothing on the final picture), msaa2, msaa4, msaa8 (multisampling:\n"
          ";   the 3D's edges smoothed as it is drawn), msaa4+fxaa or msaa8+fxaa\n"
          "antialiasing=off\n"
          "; 1 = wait for the monitor's refresh (no tearing), 0 = off\n"
          "vsync=0\n"
          "; 1 = fill the whole window/screen instead of keeping the picture's shape\n"
          "stretch=0\n"
          "; brightness (gamma): 0.85, 1 (original), 1.15, 1.3 or 1.45\n"
          "brightness=1\n"
          "; sharpening: 0 (off), 0.4, 0.7 or 1\n"
          "sharpening=0\n"
          "; colour saturation: 1 (original), 1.25 (vivid) or 0.8 (muted)\n"
          "colour=1\n"
          "\n[Mods]\n"
          "; 1 = the HUD of the ECTS 2002 demo (experimental): character portrait on the health sword, rune orb\n"
          ";     showing your combo, EXP readout, blue experience gauge\n"
          "e3_hud=0\n"
          "; 1 = orange health pickups, as shown in pre-release and making-of videos\n"
          "e3_pickups=0\n"
          "; extra pickup texture hashes for e3_pickups (hex, comma separated)\n"
          "e3_pickup_textures=\n"
          "\n[Input]\n"
          "; 1 = keyboard and mouse act as controller 1. Controllers always work (Xbox pads;\n"
          ";   PlayStation 3/4/5 and other pads too, with SDL2.dll beside the game on Windows)\n"
          "keyboard=1\n"
          "; wasd    = W/A/S/D move, left click or K or Space = A, right click or J = X,\n"
          ";           L = B, I = Y, F/R = left/right trigger, Z/C = white/black,\n"
          ";           Enter or Esc = Start, Backspace or Tab = Back, arrows = d-pad,\n"
          ";           numpad 8/4/2/6 = right stick, Shift / middle click = stick clicks\n"
          ";           B (held) = walk instead of run\n"
          "; classic = Z/X/A/S = A/B/X/Y, numpad = left stick, I/J/K/L = right stick,\n"
          ";           Q/E = white/black, 1/3 = triggers\n"
          "layout=wasd\n"
          "; 1 = the controller also works while another window is in front (0 = only when the game is)\n"
          "background_input=0\n"
          "; stick dead zone in percent (stops a worn or drifting stick from walking the hero);\n"
          ";   0 = off, the pad's raw values\n"
          "stick_deadzone=10\n"
          "; button prompts: auto = what player 1 last used (keys, a PlayStation pad, an Xbox pad),\n"
          ";   xbox (the original pad), xboxone, playstation or keyboard (your bound keys and mouse\n"
          ";   buttons) = always those. Pad prompts show the button bound to each action\n"
          ";   (also in the F10 menu)\n"
          "prompts=auto\n"
          "; 1 = a mouse pointer in the menus: point at an item to choose it, click to press it\n"
          ";   (0 = off: the mouse buttons act as bound, as before; also in the F10 menu)\n"
          "menu_mouse=1\n"
          "; the mouse's speed (the pointer's and the aiming's): 0.5, 0.75, 1, 1.5 or 2\n"
          "mouse_sensitivity=1\n"
          "; the pointer's size: small, normal, large or xlarge\n"
          "cursor_size=normal\n"
          "; 1 = hold the ranged button and move the mouse to aim (0 = off)\n"
          "mouse_aim=1\n"
          "; the mouse aiming inverted: 0, y (up / down), x (left / right) or xy\n"
          "mouse_invert=0\n"
          "; mouse steering: while W is held the mouse is the stick, as aiming with the ranged button is: he runs the\n"
          ";   way the mouse has pushed it (from the camera); with Walk he walks; let go of W and he stops (1 on, 0 off)\n"
          "mouse_steer=0\n"
          "; the PlayStation pad the loading screens' controls pictures show: auto = the one\n"
          ";   player 1 holds (DualShock 4 if none), or ps3, ps4, ps5\n"
          "ps_pad=auto\n"
          "\n[Controls]\n"
          "; your own key / button choices (set them in the launcher's Controls page or in game\n"
          ";   with F10). kb_a=K, Mouse Left, Space   pad_a=A   -- delete a line for its default\n"
          "\n[Audio]\n"
          "; 0 = no sound output (for PCs without an audio device)\n"
          "output=1\n"
          "\n[Compat]\n"
          "; 1 = run on one CPU core like the Xbox (avoids thread races in the game's own code)\n"
          "single_cpu=1\n"
          "\n[Developer]\n"
          "; 1 saves each texture the game draws to texture_dump\\ (for mod makers; slow)\n"
          "texture_dump=0\n"
          "; 1 writes twotowers_log.txt beside the exe\n"
          "logging=1\n"
          "; template version (the ini is rewritten, keeping your settings, when it changes)\n"
          "ini_version=2\n", f);
    fclose(f);
}

/* The in-game settings overlay writes its changes here. */
static void tt_gfx_save(const char *key, const char *value)
{
    WritePrivateProfileStringA(!strncmp(key, "e3_", 3) ? "Mods" : (!strcmp(key, "prompts") || !strncmp(key, "menu_mouse", 10) || !strncmp(key, "mouse_", 6) || !strcmp(key, "cursor_size") || !strcmp(key, "sprint") || !strcmp(key, "mouse_steer")) ? "Input" : "Display",
                               key, value, g_ini_path);
}
/* ... and its control bindings to [Controls]. */
static void tt_bind_save(const char *key, const char *value)
{
    WritePrivateProfileStringA("Controls", key, value, g_ini_path);
}

static void twotowers_setup_paths_and_log(void)
{
    char ini_game[MAX_PATH] = "";
    const char *env_game = getenv("TWOTOWERS_GAME_DIR");
    char *slash;

    GetModuleFileNameA(NULL, g_exe_dir, MAX_PATH);
    slash = strrchr(g_exe_dir, TT_SEP[0]);
    if (slash) *slash = 0;
    snprintf(g_ini_path, sizeof(g_ini_path), "%s" TT_SEP "twotowers_recomp.ini", g_exe_dir);
    {
        /* An ini from an older build has no [Input] layout and describes the
         * old keys in its comments: keep it as .ini.old and write the current
         * template, carrying the user's settings over. */
        if (GetFileAttributesA(g_ini_path) != INVALID_FILE_ATTRIBUTES
            && GetPrivateProfileIntA("Developer", "ini_version", 0, g_ini_path) < 2) {
            static const char *keep[][2] = {
                {"Paths","game_dir"}, {"Display","window"}, {"Display","renderer"}, {"Display","fps"},
                {"Display","vertex_programs"}, {"Input","keyboard"}, {"Input","layout"}, {"Input","usb"},
                {"Audio","output"}, {"Compat","single_cpu"}, {"Developer","logging"} };
            enum { NKEEP = sizeof keep / sizeof keep[0] };
            char vals[NKEEP][MAX_PATH], old[MAX_PATH + 8];
            int have[NKEEP], k;
            for (k = 0; k < NKEEP; k++)
                have[k] = GetPrivateProfileStringA(keep[k][0], keep[k][1], "\x01", vals[k], MAX_PATH, g_ini_path)
                          && vals[k][0] != 1;
            snprintf(old, sizeof(old), "%s.old", g_ini_path);
            if (MoveFileExA(g_ini_path, old, MOVEFILE_REPLACE_EXISTING)) {
                twotowers_write_default_ini();
                for (k = 0; k < NKEEP; k++)
                    if (have[k]) WritePrivateProfileStringA(keep[k][0], keep[k][1], vals[k], g_ini_path);
            }
        }
    }
    twotowers_write_default_ini();

    GetPrivateProfileStringA("Paths", "game_dir", "", ini_game, sizeof(ini_game), g_ini_path);
    if (env_game && *env_game) snprintf(g_game_dir, sizeof(g_game_dir), "%s", env_game);
    else if (ini_game[0]) snprintf(g_game_dir, sizeof(g_game_dir), "%s", ini_game);
    else snprintf(g_game_dir, sizeof(g_game_dir), "%s" TT_SEP "game_files", g_exe_dir);
    snprintf(g_xbe_path, sizeof(g_xbe_path), "%s" TT_SEP "default.xbe", g_game_dir);
    snprintf(g_save_dir, sizeof(g_save_dir), "%s" TT_SEP "saves", g_exe_dir);
    CreateDirectoryA(g_save_dir, NULL);

    if (GetPrivateProfileIntA("Display", "window", 1, g_ini_path) && !getenv("RECOMP_FB_WINDOW"))
        _putenv_s("RECOMP_FB_WINDOW", "1");
    /* Renderer: d3d11 (GPU, the default) or software (the CPU rasteriser).
     * An ini written before this option existed simply gets the default. */
    if (!getenv("TT_FPS")) {
        char f[32] = "";
        GetPrivateProfileStringA("Display", "fps", "30", f, sizeof(f), g_ini_path);
        _putenv_s("TT_FPS", f[0] ? f : "30");
    }
    {
        /* Display options -> the runtime's switches (an env var already set
         * wins, for testing). */
        char v[64];
        int n;
        _putenv_s("RECOMP_WINDOW_TITLE", "The Lord of the Rings: The Two Towers (Recompiled)");
        if (!getenv("RECOMP_FULLSCREEN") && GetPrivateProfileIntA("Display", "fullscreen", 0, g_ini_path))
            _putenv_s("RECOMP_FULLSCREEN", "1");
        if (!getenv("RECOMP_WINDOW_SIZE") && GetPrivateProfileStringA("Display", "window_size", "", v, sizeof v, g_ini_path) && v[0])
            _putenv_s("RECOMP_WINDOW_SIZE", v);
        if (!getenv("RECOMP_ULTRA_ROOT")) _putenv_s("RECOMP_ULTRA_ROOT", g_exe_dir);   /* (Ultra textures: the pack beside the game) */
        if (!getenv("RECOMP_ULTRA_BUILDER")) {   /* (1.9: the GLOS69 pack made ready when Ultra textures are turned on in the game) */
            char b[900];
#ifdef _WIN32
            snprintf(b, sizeof b, "\"%s\\The Two Towers Recompiled.exe\" --build-ultra", g_exe_dir);
#else
            snprintf(b, sizeof b, "\"%s/twotowers-setup\" ultra", g_exe_dir);
#endif
            _putenv_s("RECOMP_ULTRA_BUILDER", b);
        }
        if (!getenv("RECOMP_ULTRA_SKIP")) {   /* (Ultra textures leave the health pickup as it is: the orange pickups mod recolours it) */
            char sk[300] = "71C950C7", ex[256] = "";
            GetPrivateProfileStringA("Mods", "e3_pickup_textures", "", ex, sizeof ex, g_ini_path);
            if (ex[0]) { strncat(sk, ",", sizeof sk - strlen(sk) - 1); strncat(sk, ex, sizeof sk - strlen(sk) - 1); }
            _putenv_s("RECOMP_ULTRA_SKIP", sk);
        }
        if (!getenv("RECOMP_ASPECT") && GetPrivateProfileStringA("Display", "aspect", "4:3", v, sizeof v, g_ini_path) && v[0]) {
            float a = 0, x = 0, y = 0;
            char num[32];
            if (sscanf(v, "%f:%f", &x, &y) == 2 && y > 0) a = x / y;
            else a = (float)atof(v);
            if (a > 1.0f && a < 6.0f) {
                snprintf(num, sizeof num, "%.4f", a);
                _putenv_s("RECOMP_ASPECT", num);
                if (!getenv("TT_ASPECT")) _putenv_s("TT_ASPECT", num);
            }
        }
        /* (a number with a fraction: 4.5 is 2160 lines, 3840 x 2160 at 16:9) */
        if (!getenv("RECOMP_RENDER_SCALE") && GetPrivateProfileStringA("Display", "render_scale", "1", v, sizeof v, g_ini_path)
            && atof(v) > 1.0) _putenv_s("RECOMP_RENDER_SCALE", v);
        n = GetPrivateProfileIntA("Display", "anisotropic", 1, g_ini_path);
        if (!getenv("RECOMP_ANISO") && n > 1) { snprintf(v, sizeof v, "%d", n); _putenv_s("RECOMP_ANISO", v); }
        if (!getenv("RECOMP_MIPMAPS") && !GetPrivateProfileIntA("Display", "mipmaps", 1, g_ini_path))
            _putenv_s("RECOMP_MIPMAPS", "0");
        if (!getenv("RECOMP_AA") && GetPrivateProfileStringA("Display", "antialiasing", "off", v, sizeof v, g_ini_path)
            && (v[0] == 'f' || v[0] == 'F' || v[0] == 'm' || v[0] == 'M'))
            _putenv_s("RECOMP_AA", v);   /* fxaa, msaa2, msaa4, msaa8, msaa4+fxaa, msaa8+fxaa */
        if (!getenv("RECOMP_VSYNC") && GetPrivateProfileIntA("Display", "vsync", 0, g_ini_path))
            _putenv_s("RECOMP_VSYNC", "1");
        if (!getenv("RECOMP_PRESENT_STRETCH") && GetPrivateProfileIntA("Display", "stretch", 0, g_ini_path))
            _putenv_s("RECOMP_PRESENT_STRETCH", "1");
        {
            static const char *map[][3] = {
                { "Display", "brightness", "RECOMP_BRIGHTNESS" }, { "Display", "sharpening", "RECOMP_SHARPEN" },
                { "Display", "colour", "RECOMP_SATURATION" }, { "Mods", "e3_hud", "TT_E3HUD" },
                { "Mods", "e3_pickups", "TT_E3PICKUPS" }, { "Mods", "e3_pickup_textures", "TT_E3PICKUP_TEX" },
                { "Input", "prompts", "TT_PROMPTS" }, { "Input", "ps_pad", "TT_PS_PAD" },
                { "Display", "hud", "RECOMP_HUD_ANCHOR" },
                { "Display", "menu_fill", "RECOMP_MENU_FILL" }, { "Display", "movie_fill", "RECOMP_MOVIE_FILL" },
                { "Input", "menu_mouse", "TT_MENU_MOUSE" }, { "Input", "mouse_sensitivity", "TT_MOUSE_SENS" },
                { "Input", "cursor_size", "TT_CURSOR_SIZE" }, { "Input", "mouse_aim", "TT_MOUSE_AIM" },
                { "Input", "mouse_invert", "TT_MOUSE_INVERT" }, { "Input", "sprint", "TT_SPRINT" }, { "Input", "mouse_steer", "TT_MOUSE_STEER" },
                { "Display", "ultra_textures", "TT_ULTRA_TEX" }, { "Display", "wide_scenes", "RECOMP_WIDE_SCENES" } };
            int k;
            for (k = 0; k < (int)(sizeof map / sizeof map[0]); k++)
                if (!getenv(map[k][2]) && GetPrivateProfileStringA(map[k][0], map[k][1], "", v, sizeof v, g_ini_path) && v[0])
                    _putenv_s(map[k][2], v);
        }
    }
    {
        extern void xbox_GfxSetSaveHandler(void (*fn)(const char *key, const char *value));
        xbox_GfxSetSaveHandler(tt_gfx_save);
    }
    {
        extern void tt_e3hud_init(void);
        tt_e3hud_init();            /* the E3 HUD / power-up mods (off unless enabled) */
    }
    {
        /* [Controls] kb_<action>= / pad_<action>= (written by the launcher's
         * Controls page or the in-game overlay); missing keys keep the
         * defaults of [Input] layout. */
        extern int xbox_BindSet(const char *key, const char *value);
        extern void xbox_BindDefaults(int which);
        extern void xbox_BindSetSaveHandler(void (*fn)(const char *key, const char *value));
        static char sec[8192];
        char lay[16] = "";
        const char *p;
        DWORD n;
        GetPrivateProfileStringA("Input", "layout", "wasd", lay, sizeof lay, g_ini_path);
        xbox_BindDefaults((lay[0] == 'c' || lay[0] == 'C') ? 1 : 0);
        n = GetPrivateProfileSectionA("Controls", sec, sizeof sec, g_ini_path);
        for (p = sec; n && *p; p += strlen(p) + 1) {
            char k[64];
            const char *eq = strchr(p, '=');
            size_t kl;
            if (!eq || *p == ';') continue;
            kl = (size_t)(eq - p);
            if (kl >= sizeof k) continue;
            memcpy(k, p, kl); k[kl] = 0;
            while (kl && k[kl - 1] == ' ') k[--kl] = 0;
            xbox_BindSet(k, eq + 1);
        }
        xbox_BindSetSaveHandler(tt_bind_save);
    }
    if (!getenv("RECOMP_HW_VP")) {
        char v[16] = "";
        GetPrivateProfileStringA("Display", "vertex_programs", "gpu", v, sizeof(v), g_ini_path);
        if (v[0] == 'c' || v[0] == 'C') _putenv_s("RECOMP_HW_VP", "0");
    }
    if (!getenv("RECOMP_RENDERER")) {
        char r[32] = "";
        GetPrivateProfileStringA("Display", "renderer", "d3d11", r, sizeof(r), g_ini_path);
        _putenv_s("RECOMP_RENDERER", r[0] ? r : "d3d11");
    }

    /* D3D waits on the NV2A vertical-blank interrupt; xboxrecomp only
     * delivers it when RECOMP_VBLANK is set. */
    if (!getenv("RECOMP_VBLANK")) _putenv_s("RECOMP_VBLANK", "1");
    /* Execute the NV2A pushbuffer: D3D's kickoff waits for the GPU-time
     * semaphore the executor writes back (see nv2a_pb_exec.c 0x1D70). */
    if (!getenv("RECOMP_PB_EXEC")) _putenv_s("RECOMP_PB_EXEC", "1");
    /* D3D (XDK 4928) keeps its GPU-idle event at device+0x2564; the device
     * pointer is the global at 0x00124180. NOP 5 in the pushbuffer sets it. */
    /* D3D's kickoff ring (0x389000, 64 KB) and its two 3 MB command
     * buffers (0x399000, 0x699000): every jump/call target is in here. */
    if (!getenv("RECOMP_PB_RANGE")) _putenv_s("RECOMP_PB_RANGE", "0x389000-0x999000");
    if (!getenv("RECOMP_D3D_IDLE_EVENT")) _putenv_s("RECOMP_D3D_IDLE_EVENT", "0x124180,0x2564");

    if (GetPrivateProfileIntA("Input", "keyboard", 1, g_ini_path) && !getenv("RECOMP_KEYBOARD"))
        _putenv_s("RECOMP_KEYBOARD", "1");
    if (GetPrivateProfileIntA("Input", "background_input", 0, g_ini_path) && !getenv("RECOMP_BACKGROUND_INPUT"))
        _putenv_s("RECOMP_BACKGROUND_INPUT", "1");
    if (!getenv("RECOMP_STICK_DEADZONE")) {
        char dz[16];
        snprintf(dz, sizeof dz, "%d", (int)GetPrivateProfileIntA("Input", "stick_deadzone", 10, g_ini_path));
        _putenv_s("RECOMP_STICK_DEADZONE", dz);
    }
    if (!getenv("RECOMP_KB_LAYOUT")) {
        char lay[32] = "";
        GetPrivateProfileStringA("Input", "layout", "wasd", lay, sizeof(lay), g_ini_path);
        _putenv_s("RECOMP_KB_LAYOUT", lay[0] ? lay : "wasd");
    }
    if (!GetPrivateProfileIntA("Audio", "output", 1, g_ini_path) && !getenv("RECOMP_AUDIO_OUT"))
        _putenv_s("RECOMP_AUDIO_OUT", "0");
    /* Audio: report the AC'97 codec and route the APU registers to the
     * emulated APU, otherwise DirectSound fails, XACT is never created and
     * the title dereferences its NULL engine at the title screen. */
    /* Controllers: the emulated USB host controller and pad (the game's own
     * XAPI driver enumerates it; host XInput pads and the keyboard feed it). */
    if (!getenv("RECOMP_USB") && GetPrivateProfileIntA("Input", "usb", 1, g_ini_path))
        _putenv_s("RECOMP_USB", "1");
    if (!getenv("RECOMP_AC97_READY")) _putenv_s("RECOMP_AC97_READY", "1");
    /* DirectSound's effect-image download (0x0012C0FF) posts command 3 in the
     * DSP scratch block and spins until the DSP clears it; the DSP is a stub
     * here, so the runtime completes that word for it. */
    if (!getenv("RECOMP_DSP_ACK")) _putenv_s("RECOMP_DSP_ACK", "0x82E90810");

    /* The Xbox has one CPU. The game's own bookkeeping (e.g. the profiler
     * at 0x000E60B0, shared by the main and loader threads) is not
     * thread-safe and only works because its threads never truly overlap. */
    /* The Xbox has one CPU, and the game's threads only work because they
     * never truly overlap. Every thread is put on CPU 0 -- this one now, each
     * new one as it starts (tt_tls_pin in main.c, a TLS callback, so it also
     * catches threads Windows libraries create) -- except the renderer's
     * raster threads, which move themselves to the other cores.
     * single_cpu=2: the old whole-process pin (no raster threads). */
    {
        extern volatile int g_tt_pin_threads;
        int sc = GetPrivateProfileIntA("Compat", "single_cpu", 1, g_ini_path);
        if (sc && !getenv("TWOTOWERS_MULTICORE")) {
            if (sc == 2) {
#if defined(_WIN32)
                SetProcessAffinityMask(GetCurrentProcess(), 1);
#else
                g_tt_pin_threads = 1;
                SetThreadAffinityMask(GetCurrentThread(), 1);
#endif
            } else {
                g_tt_pin_threads = 1;
                SetThreadAffinityMask(GetCurrentThread(), 1);
            }
        }
    }

    int tex_dump_on = 0;
    if ((GetPrivateProfileIntA("Developer", "texture_dump", 0, g_ini_path)
         || GetPrivateProfileIntA("Mods", "texture_dump", 0, g_ini_path)
         || GetPrivateProfileIntA("Display", "texture_dump", 0, g_ini_path)) && !getenv("RECOMP_TEX_DUMP")) {
        /* every texture the game draws, once, as texture_dump\<hash>_<w>x<h>_f<fmt>.bmp
         * (for finding a texture to give a mod) */
        char td[MAX_PATH + 20];
        snprintf(td, sizeof td, "%s" TT_SEP "texture_dump", g_exe_dir);
        CreateDirectoryA(td, NULL);
        _putenv_s("RECOMP_TEX_DUMP", td);
        tex_dump_on = 1;
    }
    if (GetPrivateProfileIntA("Developer", "profile", 0, g_ini_path) && !getenv("RECOMP_PROFILE"))
        _putenv_s("RECOMP_PROFILE", "all");
    if (GetPrivateProfileIntA("Developer", "logging", 1, g_ini_path)) {
        char log_path[MAX_PATH];
        FILE *sink;
        snprintf(log_path, sizeof(log_path), "%s" TT_SEP "twotowers_log.txt", g_exe_dir);
        /* A GUI-subsystem exe starts with no stdout/stderr descriptors, so
         * redirecting by descriptor alone did nothing on Windows (it only
         * worked under Wine). The usual recipe: give each stream a real
         * descriptor by reopening it on NUL, then point that descriptor at
         * one shared handle to the log. */
#if !defined(_WIN32)
        int lfd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        (void)sink;
        if (lfd >= 0) {
            /* also on the terminal when started from one */
            if (!isatty(2) || getenv("TT_LOG_ONLY")) {
                dup2(lfd, 1);
                dup2(lfd, 2);
            } else {
                /* a terminal: keep it, the log file gets a copy through tee */
                char cmd[MAX_PATH + 32];
                FILE *tee;
                snprintf(cmd, sizeof cmd, "tee -a '%s' >&2", log_path);
                close(lfd);
                tee = popen(cmd, "w");
                if (tee) { dup2(fileno(tee), 1); dup2(fileno(tee), 2); }
            }
            setvbuf(stdout, NULL, _IONBF, 0);
            setvbuf(stderr, NULL, _IONBF, 0);
        }
#else
        HANDLE h = CreateFileA(log_path, GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            int fd = _open_osfhandle((intptr_t)h, _O_WRONLY | _O_TEXT);
            if (fd >= 0) {
                freopen_s(&sink, "NUL", "w", stdout);
                freopen_s(&sink, "NUL", "w", stderr);
                _dup2(fd, _fileno(stdout));
                _dup2(fd, _fileno(stderr));
                setvbuf(stdout, NULL, _IONBF, 0);
                setvbuf(stderr, NULL, _IONBF, 0);
                SetStdHandle(STD_OUTPUT_HANDLE, h);
                SetStdHandle(STD_ERROR_HANDLE, h);
            }
        }
#endif
    }
    if (tex_dump_on) printf("[TEXDUMP] saving textures to %s\n", getenv("RECOMP_TEX_DUMP"));
}

#endif
