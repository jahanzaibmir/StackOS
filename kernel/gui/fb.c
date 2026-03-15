/* =============================================================================
 *  StackOS — kernel/gui/fb.c
 *  Linear framebuffer driver — back-buffer rendering + flush to hardware.
 *
 *  PERFORMANCE FIX: fb_flush_rect previously wrote one uint32_t at a time
 *  to VRAM. VRAM at 0xFD000000 is uncached — each write stalls the CPU.
 *  Fix: use REP STOSD / REP MOVSD via inline asm for bulk transfers.
 *  This copies an entire row in one burst instead of pixel-by-pixel.
 *  ============================================================================= */
#include "fb.h"
#include "../mm/heap.h"
#include "../kprintf.h"
#include <stddef.h>

fb_info_t fb;
static uint32_t *back_buf = NULL;

int fb_clip_x = 0, fb_clip_y = 0;
int fb_clip_w = 0, fb_clip_h = 0;

/* ── Fast 32-bit memory copy using REP MOVSD ─────────────────────────────── */
static inline void fast_copy32(uint32_t *dst, const uint32_t *src, uint32_t count) {
    __asm__ volatile (
        "rep movsl"
        : "+D"(dst), "+S"(src), "+c"(count)
        :
        : "memory"
    );
}

/* ── Fast 32-bit memory fill using REP STOSD ─────────────────────────────── */
static inline void fast_fill32(uint32_t *dst, uint32_t val, uint32_t count) {
    __asm__ volatile (
        "rep stosl"
        : "+D"(dst), "+c"(count)
        : "a"(val)
        : "memory"
    );
}

/* ── Init ────────────────────────────────────────────────────────────────── */
int fb_init(uint32_t addr, uint32_t width, uint32_t height,
            uint32_t pitch, uint8_t bpp) {
    if (!addr || !width || !height) {
        kprintf("[fb] ERROR: invalid framebuffer parameters\n");
        return -1;
    }
    fb.hw     = (uint32_t *)(uintptr_t)addr;
    fb.width  = width;
    fb.height = height;
    fb.pitch  = pitch;
    fb.bpp    = bpp;

    uint32_t size = width * height * 4;
    back_buf = (uint32_t *)kmalloc(size);
    if (!back_buf) {
        kprintf("[fb] WARNING: back-buffer alloc failed — direct mode\n");
        fb.pixels = fb.hw;
    } else {
        fb.pixels = back_buf;
        fast_fill32(back_buf, 0, width * height);
    }
    fb_reset_clip();
    kprintf("[fb] %ux%u %ubpp at 0x%x\n", width, height, bpp, addr);
    return 0;
            }

            /* ── Clipping ────────────────────────────────────────────────────────────── */
            void fb_set_clip(int x, int y, int w, int h) {
                fb_clip_x=x; fb_clip_y=y; fb_clip_w=w; fb_clip_h=h;
            }
            void fb_reset_clip(void) {
                fb_clip_x=0; fb_clip_y=0;
                fb_clip_w=(int)fb.width; fb_clip_h=(int)fb.height;
            }

            /* ── Full flush — one REP MOVSD per row ──────────────────────────────────── */
            void fb_flush(void) {
                if (fb.pixels == fb.hw) return;
                if (fb.pitch == fb.width * 4) {
                    /* Pitch matches — copy entire buffer in one shot */
                    fast_copy32(fb.hw, back_buf, fb.width * fb.height);
                    return;
                }
                /* Pitch has padding — copy row by row */
                for (uint32_t y = 0; y < fb.height; y++) {
                    uint32_t *src = back_buf + y * fb.width;
                    uint32_t *dst = (uint32_t *)((uint8_t *)fb.hw + y * fb.pitch);
                    fast_copy32(dst, src, fb.width);
                }
            }

            /* ── Partial flush — copies only changed rectangle ──────────────────────── */
            void fb_flush_rect(int x, int y, int w, int h) {
                if (fb.pixels == fb.hw) return;
                /* Clamp to screen */
                if (x < 0) { w += x; x = 0; }
                if (y < 0) { h += y; y = 0; }
                if (x + w > (int)fb.width)  w = (int)fb.width  - x;
                if (y + h > (int)fb.height) h = (int)fb.height - y;
                if (w <= 0 || h <= 0) return;

                for (int row = y; row < y + h; row++) {
                    uint32_t *src = back_buf + row * (int)fb.width + x;
                    uint32_t *dst;
                    if (fb.bpp == 32) {
                        dst = (uint32_t *)((uint8_t *)fb.hw + row * fb.pitch) + x;
                        fast_copy32(dst, src, (uint32_t)w);
                    } else if (fb.bpp == 24) {
                        uint8_t *dst8 = (uint8_t *)fb.hw + row * fb.pitch + x * 3;
                        for (int col = 0; col < w; col++) {
                            uint32_t c = src[col];
                            dst8[col*3+0] = (uint8_t)(c & 0xFF);
                            dst8[col*3+1] = (uint8_t)((c >> 8) & 0xFF);
                            dst8[col*3+2] = (uint8_t)((c >> 16) & 0xFF);
                        }
                    }
                }
            }

            /* ── Fill whole screen ───────────────────────────────────────────────────── */
            void fb_fill(uint32_t col) {
                fast_fill32(fb.pixels, col, fb.width * fb.height);
            }

            /* ── Filled rectangle ────────────────────────────────────────────────────── */
            void fb_rect(int x, int y, int w, int h, uint32_t col) {
                int x0=x, y0=y, x1=x+w, y1=y+h;
                if (x0 < fb_clip_x) x0 = fb_clip_x;
                if (y0 < fb_clip_y) y0 = fb_clip_y;
                if (x1 > fb_clip_x+fb_clip_w) x1 = fb_clip_x+fb_clip_w;
                if (y1 > fb_clip_y+fb_clip_h) y1 = fb_clip_y+fb_clip_h;
                if (x0 >= x1 || y0 >= y1) return;
                int rw = x1 - x0;
                for (int row = y0; row < y1; row++)
                    fast_fill32(fb.pixels + row * (int)fb.width + x0, col, (uint32_t)rw);
            }

            void fb_hline(int x, int y, int w, uint32_t col) { fb_rect(x, y, w, 1, col); }
            void fb_vline(int x, int y, int h, uint32_t col) { fb_rect(x, y, 1, h, col); }

            void fb_rect_outline(int x, int y, int w, int h, uint32_t col, int thick) {
                fb_rect(x,         y,         w,     thick, col);
                fb_rect(x,         y+h-thick, w,     thick, col);
                fb_rect(x,         y,         thick, h,     col);
                fb_rect(x+w-thick, y,         thick, h,     col);
            }

            void fb_rect_round(int x, int y, int w, int h, int r, uint32_t col) {
                if (r <= 0) { fb_rect(x, y, w, h, col); return; }
                fb_rect(x+r, y,   w-2*r, h,     col);
                fb_rect(x,   y+r, w,     h-2*r, col);
                for (int dy = 0; dy < r; dy++) {
                    for (int dx = 0; dx < r; dx++) {
                        if ((dx-r)*(dx-r)+(dy-r)*(dy-r) <= r*r) {
                            fb_pixel(x+dx,       y+dy,       col);
                            fb_pixel(x+w-1-dx,   y+dy,       col);
                            fb_pixel(x+dx,       y+h-1-dy,   col);
                            fb_pixel(x+w-1-dx,   y+h-1-dy,   col);
                        }
                    }
                }
            }

            void fb_rect_round_outline(int x, int y, int w, int h, int r, uint32_t col, int thick) {
                (void)thick;
                fb_hline(x+r,   y,     w-2*r, col);
                fb_hline(x+r,   y+h-1, w-2*r, col);
                fb_vline(x,     y+r,   h-2*r, col);
                fb_vline(x+w-1, y+r,   h-2*r, col);
                for (int dy = 0; dy < r; dy++) {
                    for (int dx = 0; dx < r; dx++) {
                        int d2 = (dx-r)*(dx-r)+(dy-r)*(dy-r);
                        if (d2 <= r*r && d2 >= (r-2)*(r-2)) {
                            fb_pixel(x+dx,     y+dy,     col);
                            fb_pixel(x+w-1-dx, y+dy,     col);
                            fb_pixel(x+dx,     y+h-1-dy, col);
                            fb_pixel(x+w-1-dx, y+h-1-dy, col);
                        }
                    }
                }
            }

            void fb_line(int x0, int y0, int x1, int y1, uint32_t col) {
                int dx= x1>x0?x1-x0:x0-x1, dy=-(y1>y0?y1-y0:y0-y1);
                int sx=x0<x1?1:-1, sy=y0<y1?1:-1, err=dx+dy;
                while (1) {
                    fb_pixel(x0,y0,col);
                    if (x0==x1&&y0==y1) break;
                    int e2=2*err;
                    if (e2>=dy){err+=dy;x0+=sx;}
                    if (e2<=dx){err+=dx;y0+=sy;}
                }
            }

            void fb_circle(int cx, int cy, int radius, uint32_t col, int filled) {
                int x=0, y=radius, d=3-2*radius;
                while (y >= x) {
                    if (filled) {
                        fb_hline(cx-x,cy-y,2*x+1,col); fb_hline(cx-x,cy+y,2*x+1,col);
                        fb_hline(cx-y,cy-x,2*y+1,col); fb_hline(cx-y,cy+x,2*y+1,col);
                    } else {
                        fb_pixel(cx+x,cy+y,col); fb_pixel(cx-x,cy+y,col);
                        fb_pixel(cx+x,cy-y,col); fb_pixel(cx-x,cy-y,col);
                        fb_pixel(cx+y,cy+x,col); fb_pixel(cx-y,cy+x,col);
                        fb_pixel(cx+y,cy-x,col); fb_pixel(cx-y,cy-x,col);
                    }
                    if (d<0) d+=4*x+6; else {d+=4*(x-y)+10;y--;} x++;
                }
            }

            void fb_rect_alpha(int x, int y, int w, int h, uint32_t col, uint8_t alpha) {
                uint32_t sr=(col>>16)&0xFF, sg=(col>>8)&0xFF, sb=col&0xFF;
                int x1=x+w, y1=y+h;
                if (x<fb_clip_x) x=fb_clip_x;
                if (y<fb_clip_y) y=fb_clip_y;
                if (x1>fb_clip_x+fb_clip_w) x1=fb_clip_x+fb_clip_w;
                if (y1>fb_clip_y+fb_clip_h) y1=fb_clip_y+fb_clip_h;
                for (int row=y; row<y1; row++) {
                    uint32_t *p=fb.pixels+row*(int)fb.width+x;
                    for (int c2=x; c2<x1; c2++,p++) {
                        uint32_t dst=*p;
                        uint32_t dr=(dst>>16)&0xFF, dg=(dst>>8)&0xFF, db=dst&0xFF;
                        *p=((sr*alpha+dr*(255-alpha))>>8)<<16
                        |((sg*alpha+dg*(255-alpha))>>8)<<8
                        |((sb*alpha+db*(255-alpha))>>8);
                    }
                }
            }
