/* e3hud_layout.h -- where the ECTS HUD pieces go.
 *
 * Two coordinate spaces:
 *  - screen: the 640x480 frame as shown. The overlay quads (portrait, orb,
 *    gauge outline) are placed in it.
 *  - HUD: the space of the game's HUD element tables, which the game shrinks
 *    by 0.93 about the centre (its TV safe area) when it draws them. The
 *    game-drawn text (rating, combo name, EXP) is placed in it.
 * The demo's layout was measured from ECTS 2002 footage, relative to the parts
 * the two HUDs share (the experience gauge, the sword).
 */
#ifndef E3HUD_LAYOUT_H
#define E3HUD_LAYOUT_H

#define HUD_TO_SCREEN_X(x) (320.0f + ((x) - 320.0f) * 0.93f)
#define HUD_TO_SCREEN_Y(y) (240.0f + ((y) - 240.0f) * 0.93f)
#define SCREEN_TO_HUD_X(x) ((int)(320.0f + ((x) - 320.0f) / 0.93f + 0.5f))
#define SCREEN_TO_HUD_Y(y) ((int)(240.0f + ((y) - 240.0f) / 0.93f + 0.5f))

/* portrait medallion at the sword's grip (the blade runs y 66-75) */
#define FACE_SIZE 44.0f
#define FACE_X    (67.0f - FACE_SIZE / 2)
#define FACE_Y    (70.5f - FACE_SIZE / 2)
#define FACE_PX   256                          /* bitmap size (1.8.2: 256, sharper at high resolutions) */

/* the experience gauge: the retail tube's dark inside and blue fill stay
 * (screen x 75.5-89.5, y 312-394); its ornate frame and silver medallion are
 * hidden and the demo's plain rectangle drawn around it instead */
#define TUBE_X0   74.5f
#define TUBE_X1   90.5f
#define TUBE_Y0   310.5f
/* rune orb on the gauge's foot, bigger than the retail medallion */
#define ORB_SX    82.5f
#define ORB_SY    404.0f
#define ORB_SR    23.0f

/* overlay bitmap holding gauge outline + orb (screen space) */
#define CL_X      54.0f
#define CL_Y      304.0f
#define CL_W      58
#define CL_H      128
#define CL_SCALE  8   /* (1.8.2: 8, sharper at high resolutions) */

/* game-drawn text, HUD space. The rating word ("Good", "Excellent", ...) is
 * the game's own element, moved from above the gauge to beside its top, as
 * in the demo; the combo name sits right of the orb, the EXP after it. */
#define RATING_DY (SCREEN_TO_HUD_Y(343.0f) - 285)
#define LBL_HX    SCREEN_TO_HUD_X(109.0f)
#define LBL_HY    SCREEN_TO_HUD_Y(405.0f)
#define EXP_HX    SCREEN_TO_HUD_X(248.0f)
#define EXP_HY    SCREEN_TO_HUD_Y(409.0f)

/* retail HUD textures (FNV hashes of the decoded texels): the experience
 * thermometer and the health sword. The ECTS HUD shows only while one of
 * them was drawn in the last frame, so menus, movies and cut-scenes stay clean. */
#define TEX_THERMO 0x6EB4352Fu
#define TEX_SWORD  0x9E0A4B1Au
#define TEX_GAUGE_TOP  0xF7A501DBu   /* the tube's ornate top */
#define TEX_GAUGE_RING 0xADE193F9u   /* the colour ring round the medallion */
/* the mission results screen (front-end screen 10, 0x4AD050) draws the same
 * gauge at the top right, narrower: inside x 536.5-546.5, from y 135.5,
 * medallion centred at 541.5,232 (screen space) */
#define FE_SCREEN       0x4AD050u
#define FE_RESULTS      10
#define RES_TUBE_X0     535.5f
#define RES_TUBE_X1     547.5f
#define RES_TUBE_Y0     134.5f
#define RES_ORB_SX      541.5f
#define RES_ORB_SY      232.0f
#define RES_ORB_SR      17.0f
#define RES_CL_X        515.0f
#define RES_CL_Y        126.0f
#define RES_CL_W        54
#define RES_CL_H        130
#define E3_HUD_VISIBLE() (xbox_FrameUsedTexture(TEX_THERMO) || xbox_FrameUsedTexture(TEX_SWORD))

/* live HUD element tables (copied from .data by 0x000CF4B0): 0xDC-byte
 * elements; +0x08 x, +0x0C y, +0x10 w, +0x14 h (HUD space), +0x20 colour
 * (0xAABBGGRR), +0x24 shown, +0x2C font, +0x34 text pointer or 0 for the
 * UTF-16 text at +0x38 */
#define HUD_EL(table, i)    ((table) + (i) * 0xDCu)
#define HUD_HEALTH          0x4A0398u
#define HUD_GAUGE           0x4A3D80u
#define HUD_HEALTH_FILL_COL (HUD_EL(HUD_HEALTH, 8) + 0x20)    /* retail: green */
#define HUD_EXP_FILL_COL    (HUD_EL(HUD_GAUGE, 1) + 0x20)     /* retail: slate blue */
#define HUD_GAUGE_CAP       HUD_EL(HUD_GAUGE, 3)              /* the tube's ornate top */
#define HUD_GAUGE_ART       HUD_EL(HUD_GAUGE, 4)              /* tube frame + medallion */
#define HUD_RATING          HUD_EL(HUD_GAUGE, 14)             /* "Good" etc., font 2 */
#define HUD_ARROWS_TEXT     HUD_EL(HUD_HEALTH, 26)            /* "30", font 0 */
#define RETAIL_HEALTH_COL   0xFF00C747u
#define RETAIL_EXP_COL      0xFFB9691Bu
#define E3_HEALTH_COL       0xFF20D0A8u                       /* the demo's yellow-green */
#define E3_EXP_COL          0xFFFF4A26u                       /* the demo's bright blue */

/* the villager counter (Westfold): the retail bar and portrait fill HUD
 * x 431-623, y 23-87; the demo's two rows of four couples take its right
 * part, each 74x78 like the pictures (screen space) */
#define VR_X  HUD_TO_SCREEN_X(501.0f)
#define VR_Y  HUD_TO_SCREEN_Y(23.0f)
#define VR_W  (HUD_TO_SCREEN_X(623.0f) - VR_X)
#define VR_H  (HUD_TO_SCREEN_Y(87.0f) - VR_Y)

/* ECTS HUD's own text elements, in guest memory nothing else uses (the tail
 * of the developer-switch table at 0x4B91F0, which retail never fills) */
#define E3_EL_LABEL  0x4BA000u
#define E3_EL_EXP    (0x4BA000u + 0xDCu)
#define E3_EL_LABEL2 (0x4BA000u + 0x1B8u)   /* (1.9.9) player 2's combo label: up to 0x4BA294 -- menus.c's co-op element at 0x4BA1C0 is the main menu's only, set before each of its draws */

#endif
