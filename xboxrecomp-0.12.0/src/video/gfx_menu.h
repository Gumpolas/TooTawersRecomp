/* gfx_menu.h -- in-game Graphics Settings overlay (see gfx_menu.c). */
#ifndef XBOXRECOMP_GFX_MENU_H
#define XBOXRECOMP_GFX_MENU_H
#include <stdint.h>

enum { GFX_FPS, GFX_FULLSCREEN, GFX_ASPECT, GFX_SCALE, GFX_ANISO, GFX_MIPS, GFX_AA,
       GFX_VSYNC, GFX_STRETCH, GFX_VP, GFX_BRIGHT, GFX_SHARP, GFX_COLOR,
       GFX_PROMPTS, GFX_HUDPOS, GFX_MENUFILL, GFX_MOVIEFILL, GFX_MOUSE, GFX_MOUSE_SENS, GFX_CURSOR_SIZE, GFX_MOUSE_AIM, GFX_MOUSE_INVERT, GFX_SPRINT, GFX_MOUSE_STEER, GFX_WINSIZE, GFX_ULTRA, GFX_WIDE_SCENES, GFX_FS_RES, GFX_E3HUD, GFX_E3PICKUPS,
       /* (1.9.7) the More page (after Athelas' settings): picture, gameplay, controller, window */
       GFX_LOOK, GFX_LOOKSTR, GFX_HDR, GFX_HDRPAPER, GFX_LOD, GFX_MOVIESHARP, GFX_DMGTAKEN, GFX_DMGENEMY,
       GFX_VIB, GFX_DEADZONE, GFX_TRIGGER, GFX_BGPAUSE, GFX_BGMUTE, GFX_ACHON, GFX_N };
#define GFX_MORE0 GFX_LOOK
/* the in-game menu shows the options before the mods; the title's mods are
 * set in its launcher, before the game starts */
#define GFX_MENU_N GFX_E3HUD
/* value of a float option (brightness, sharpening, colour) */
float xbox_GfxGetF(int id);

/* Bumped on every change; the renderer re-reads its options when it moves. */
#if defined(_WIN32)
extern volatile long xbox_gfx_rev;
#else
extern volatile long xbox_gfx_rev;
/* (1.9.7) the title's achievements, shown on the panel's third page */
void  xbox_GfxSetAchievements(int (*count)(void), const char *(*name)(int), const char *(*desc)(int), int (*earned)(int));
/* (1.9.7) a note in the bottom right corner for 5 s (queued) */
void  xbox_ToastShow(const char *title, const char *line);
void  xbox_ToastShow3(const char *title, const char *line, const char *desc);
int   xbox_ToastRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver, int *x, int *y);
/* (1.9.7) a banner at the top of the screen while set (NULL / "" clears) */
void  xbox_BannerSet(const char *title, const char *line);
int   xbox_BannerRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver, int *x, int *y);
#endif
float xbox_GfxScaleF(void);        /* the internal resolution: 1 to 12, or 4.5 (2160 lines) */
int xbox_GfxMsaa(void);            /* anti-aliasing: MSAA samples, 1 = none */
int xbox_GfxFxaa(void);            /* anti-aliasing: FXAA on the finished picture */
int   xbox_GfxGet(int id);          /* scale 1-4, aniso 1-16, fps 0/1/2, others 0/1 */
float xbox_GfxAspect(void);
void  xbox_GfxFsMode(int *w, int *h);   /* exclusive full screen's resolution; 0 x 0 = the desktop's */
void  xbox_GfxSetFpsHandler(void (*fn)(int mode));
void  xbox_GfxSetSaveHandler(void (*fn)(const char *key, const char *value));
void  xbox_GfxMenuSetAvailable(int on);
int   xbox_GfxMenuIsOpen(void);
void  xbox_GfxMenuToggle(void);
int   xbox_GfxMenuKey(int vk);
int   xbox_GfxMenuRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver);
/* (1.9.7) the title's achievements, shown on the panel's third page */
void  xbox_GfxSetAchievements(int (*count)(void), const char *(*name)(int), const char *(*desc)(int), int (*earned)(int));
/* (1.9.7) a note in the bottom right corner for 5 s (queued) */
void  xbox_ToastShow(const char *title, const char *line);
void  xbox_ToastShow3(const char *title, const char *line, const char *desc);
int   xbox_ToastRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver, int *x, int *y);
/* (1.9.7) a banner at the top of the screen while set (NULL / "" clears) */
void  xbox_BannerSet(const char *title, const char *line);
int   xbox_BannerRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver, int *x, int *y);
#endif
