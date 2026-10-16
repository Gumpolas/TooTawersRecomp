/* host_text.h -- anti-aliased text for overlays on hosts without GDI.
 *
 * Fonts are TrueType/OpenType files: fonts/<file> beside the executable,
 * then the usual system font folders. Text is drawn as 8-bit coverage, which
 * the caller tints and blends itself. Thread-safe. */
#ifndef XBOXRECOMP_HOST_TEXT_H
#define XBOXRECOMP_HOST_TEXT_H
#include <stdint.h>

enum {
    XFONT_SANS_BOLD,        /* a humanist sans, semibold/bold (tips) */
    XFONT_SERIF,            /* a Palatino-like serif (settings panel) */
    XFONT_SERIF_BOLD,
    XFONT_N
};

/* width in pixels of s at a font size of px (the em height, like a negative
 * GDI height); 0 if the font is missing */
int xbox_TextWidth(int font, int px, const char *s);
/* ascent + descent in pixels */
int xbox_TextLineHeight(int font, int px);
/* Draw s with its line box's top-left at (x, y) into cov (W x H, one byte a
 * pixel, max-combined). Returns the advance width, 0 if the font is missing. */
int xbox_TextDraw(uint8_t *cov, int W, int H, int x, int y, int font, int px, const char *s);
#endif
