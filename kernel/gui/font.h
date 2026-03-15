/* =============================================================================
   StackOS — kernel/gui/font.h
   8x16 bitmap font renderer. Each character is 8 pixels wide, 16 tall.
   ============================================================================= */
#pragma once
#include <stdint.h>
#include "fb.h"

/* Draw a single character at (x,y) */
void font_drawchar(int x, int y, char c, uint32_t fg, uint32_t bg, int transparent_bg);

/* Draw a string — returns x position after last character */
int  font_drawstr(int x, int y, const char *s, uint32_t fg, uint32_t bg, int transparent_bg);

/* Draw string with scale (1=8x16, 2=16x32, etc.) */
int  font_drawstr_scaled(int x, int y, const char *s,
                          uint32_t fg, uint32_t bg,
                          int transparent_bg, int scale);

/* Character dimensions */
#define FONT_W  8
#define FONT_H  16
