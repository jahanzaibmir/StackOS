/* =============================================================================
   StackOS — kernel/gui/fb.h
   Linear framebuffer driver.
   Supports 32bpp (0xAARRGGBB) and 24bpp packed pixel modes.
   All drawing is to a back-buffer; fb_flush() blits to the real framebuffer.
   ============================================================================= */
#pragma once
#include <stdint.h>
#include <stddef.h>

/* ── Colour helpers ──────────────────────────────────────────────────────── */
#define RGB(r,g,b)   (((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define RGBA(r,g,b,a)(((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))

/* StackOS colour palette */
#define COL_BLACK       RGB(0x0E,0x0E,0x12)
#define COL_WHITE       RGB(0xF0,0xF0,0xF0)
#define COL_STACK    RGB(0x4A,0x9E,0xFF)   /* brand blue              */
#define COL_ICE       RGB(0xA0,0xD8,0xFF)   /* lighter brand           */
#define COL_DARK        RGB(0x1A,0x1A,0x24)   /* dark surface            */
#define COL_SURFACE     RGB(0x26,0x26,0x36)   /* window background       */
#define COL_PANEL       RGB(0x1C,0x1C,0x2C)   /* taskbar / header        */
#define COL_ACCENT      RGB(0x4A,0x9E,0xFF)   /* selection / highlight   */
#define COL_TEXT        RGB(0xE8,0xE8,0xE8)   /* primary text            */
#define COL_TEXT_DIM    RGB(0x88,0x88,0xA0)   /* secondary text          */
#define COL_BORDER      RGB(0x3A,0x3A,0x5A)   /* widget borders          */
#define COL_BTN         RGB(0x30,0x30,0x48)   /* button background       */
#define COL_BTN_HOT     RGB(0x4A,0x4A,0x6A)   /* button hover            */
#define COL_BTN_PRESS   RGB(0x20,0x20,0x38)   /* button pressed          */
#define COL_SUCCESS     RGB(0x40,0xC0,0x70)
#define COL_WARN        RGB(0xFF,0xB0,0x30)
#define COL_ERROR       RGB(0xFF,0x4A,0x4A)
#define COL_TITLEBAR    RGB(0x22,0x22,0x38)
#define COL_TITLEBAR_A  RGB(0x2A,0x5A,0xA0)   /* active window titlebar  */
#define COL_DESKTOP     RGB(0x10,0x14,0x24)   /* desktop background      */

/* ── Framebuffer info (filled by fb_init from Multiboot data) ────────────── */
typedef struct {
    uint32_t *pixels;      /* pointer to back-buffer                  */
    uint32_t *hw;          /* pointer to hardware framebuffer         */
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;       /* bytes per row in hardware framebuffer   */
    uint8_t   bpp;         /* bits per pixel (32 or 24)               */
} fb_info_t;

extern fb_info_t fb;

/* ── Init / flush ────────────────────────────────────────────────────────── */
int      fb_init(uint32_t addr, uint32_t width, uint32_t height,
                 uint32_t pitch, uint8_t bpp);
void     fb_flush(void);                        /* blit back-buffer → hw  */
void     fb_flush_rect(int x, int y, int w, int h);

/* ── Clipping ────────────────────────────────────────────────────────────── */
void     fb_set_clip(int x, int y, int w, int h);
void     fb_reset_clip(void);

/* ── Pixel ───────────────────────────────────────────────────────────────── */
static inline void fb_pixel(int x, int y, uint32_t col) {
    extern fb_info_t fb;
    extern int fb_clip_x, fb_clip_y, fb_clip_w, fb_clip_h;
    if (x < fb_clip_x || y < fb_clip_y ||
        x >= fb_clip_x + fb_clip_w || y >= fb_clip_y + fb_clip_h) return;
    if (x < 0 || y < 0 || (uint32_t)x >= fb.width || (uint32_t)y >= fb.height) return;
    fb.pixels[y * fb.width + x] = col;
}

/* ── Drawing ─────────────────────────────────────────────────────────────── */
void     fb_fill(uint32_t col);
void     fb_rect(int x, int y, int w, int h, uint32_t col);
void     fb_rect_outline(int x, int y, int w, int h, uint32_t col, int thick);
void     fb_rect_round(int x, int y, int w, int h, int r, uint32_t col);
void     fb_rect_round_outline(int x, int y, int w, int h, int r,
                                uint32_t col, int thick);
void     fb_line(int x0, int y0, int x1, int y1, uint32_t col);
void     fb_circle(int cx, int cy, int radius, uint32_t col, int filled);
void     fb_hline(int x, int y, int w, uint32_t col);
void     fb_vline(int x, int y, int h, uint32_t col);

/* ── Blending ────────────────────────────────────────────────────────────── */
void     fb_rect_alpha(int x, int y, int w, int h, uint32_t col, uint8_t alpha);
