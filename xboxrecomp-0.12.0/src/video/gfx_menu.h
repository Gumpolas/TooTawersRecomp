/* gfx_menu.h -- in-game Graphics Settings overlay (see gfx_menu.c). */
#ifndef XBOXRECOMP_GFX_MENU_H
#define XBOXRECOMP_GFX_MENU_H
#include <stdint.h>

enum { GFX_FPS, GFX_FULLSCREEN, GFX_ASPECT, GFX_SCALE, GFX_ANISO, GFX_MIPS, GFX_AA,
       GFX_VSYNC, GFX_STRETCH, GFX_VP, GFX_BRIGHT, GFX_SHARP, GFX_COLOR,
       GFX_PROMPTS, GFX_HUDPOS, GFX_MENUFILL, GFX_MOVIEFILL, GFX_MOUSE, GFX_MOUSE_SENS, GFX_CURSOR_SIZE, GFX_MOUSE_AIM, GFX_MOUSE_INVERT, GFX_SPRINT, GFX_MOUSE_STEER, GFX_WINSIZE, GFX_ULTRA, GFX_WIDE_SCENES, GFX_E3HUD, GFX_E3PICKUPS, GFX_N };
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
#endif
float xbox_GfxScaleF(void);        /* the internal resolution: 1 to 12, or 4.5 (2160 lines) */
int xbox_GfxMsaa(void);            /* anti-aliasing: MSAA samples, 1 = none */
int xbox_GfxFxaa(void);            /* anti-aliasing: FXAA on the finished picture */
int   xbox_GfxGet(int id);          /* scale 1-4, aniso 1-16, fps 0/1/2, others 0/1 */
float xbox_GfxAspect(void);
void  xbox_GfxSetFpsHandler(void (*fn)(int mode));
void  xbox_GfxSetSaveHandler(void (*fn)(const char *key, const char *value));
void  xbox_GfxMenuSetAvailable(int on);
int   xbox_GfxMenuIsOpen(void);
void  xbox_GfxMenuToggle(void);
int   xbox_GfxMenuKey(int vk);
int   xbox_GfxMenuRender(int scw, int sch, const uint32_t **px, int *w, int *h, unsigned *ver);
#endif
