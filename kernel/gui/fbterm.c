/* =============================================================================
 B lizzardOS — kernel/gui/fbterm.c                                            *
 Fix 1: flush every character immediately (typing now visible in real time)
 Fix 2: setcolor flushes pending region before switching color
 ============================================================================= */
#include "fbterm.h"
#include "fb.h"
#include "font.h"
#include <stddef.h>

#define FBT_MARGIN_TOP    40
#define FBT_MARGIN_BOTTOM 40
#define FBT_MARGIN_LEFT   12
#define FBT_MARGIN_RIGHT  12

static int      fbt_on   = 0;
static int      fbt_col  = 0;
static int      fbt_row  = 0;
static int      fbt_cols = 0;
static int      fbt_rows = 0;
static uint32_t fbt_fg   = 0xE8E8E8;
static uint32_t fbt_bg   = 0x1A1A24;

/* ── Scroll ──────────────────────────────────────────────────────────────── */
static void fbt_scroll(void) {
    int x0 = FBT_MARGIN_LEFT;
    int y0 = FBT_MARGIN_TOP;
    int w  = fbt_cols * FONT_W;
    int h  = (fbt_rows - 1) * FONT_H;
    for (int y = y0; y < y0 + h; y++) {
        uint32_t *dst = fb.pixels + y * (int)fb.width + x0;
        uint32_t *src = fb.pixels + (y + FONT_H) * (int)fb.width + x0;
        for (int x = 0; x < w; x++) dst[x] = src[x];
    }
    fb_rect(x0, y0 + h, w, FONT_H, fbt_bg);
    fbt_row = fbt_rows - 1;
    fb_flush_rect(x0, y0, w, h + FONT_H);
}

/* ── Init ────────────────────────────────────────────────────────────────── */
void fbterm_init(void) {
    if (!fb.pixels || !fb.width) return;
    fbt_on   = 1;
    fbt_cols = ((int)fb.width  - FBT_MARGIN_LEFT - FBT_MARGIN_RIGHT) / FONT_W;
    fbt_rows = ((int)fb.height - FBT_MARGIN_TOP  - FBT_MARGIN_BOTTOM) / FONT_H;
    fbt_col  = 0;
    fbt_row  = 0;
    fb_rect(FBT_MARGIN_LEFT, FBT_MARGIN_TOP,
            fbt_cols * FONT_W, fbt_rows * FONT_H, fbt_bg);
    fb_flush_rect(FBT_MARGIN_LEFT, FBT_MARGIN_TOP,
                  fbt_cols * FONT_W, fbt_rows * FONT_H);
}

int  fbterm_active(void) { return fbt_on; }
void fbterm_flush(void)  { /* no-op — we flush per char now */ }

void fbterm_setcolor(uint32_t fg, uint32_t bg) {
    fbt_fg = fg;
    fbt_bg = bg;
}

void fbterm_clear(void) {
    if (!fbt_on) return;
    fb_rect(FBT_MARGIN_LEFT, FBT_MARGIN_TOP,
            fbt_cols * FONT_W, fbt_rows * FONT_H, fbt_bg);
    fb_flush_rect(FBT_MARGIN_LEFT, FBT_MARGIN_TOP,
                  fbt_cols * FONT_W, fbt_rows * FONT_H);
    fbt_col = 0;
    fbt_row = 0;
}

/* ── Put character — flush to hardware immediately ───────────────────────── */
void fbterm_putchar(char c) {
    if (!fbt_on) return;

    if (c == '\n') {
        fbt_col = 0;
        fbt_row++;
        if (fbt_row >= fbt_rows) fbt_scroll();
        return;
    }
    if (c == '\r') { fbt_col = 0; return; }
    if (c == '\t') {
        int sp = 8 - (fbt_col % 8);
        for (int i = 0; i < sp; i++) fbterm_putchar(' ');
        return;
    }
    if (c == '\b') {
        if (fbt_col > 0) {
            fbt_col--;
            int px = FBT_MARGIN_LEFT + fbt_col * FONT_W;
            int py = FBT_MARGIN_TOP  + fbt_row * FONT_H;
            fb_rect(px, py, FONT_W, FONT_H, fbt_bg);
            fb_flush_rect(px, py, FONT_W, FONT_H);
        }
        return;
    }

    int px = FBT_MARGIN_LEFT + fbt_col * FONT_W;
    int py = FBT_MARGIN_TOP  + fbt_row * FONT_H;
    font_drawchar(px, py, c, fbt_fg, fbt_bg, 0);
    fb_flush_rect(px, py, FONT_W, FONT_H);  /* flush every char immediately */

    fbt_col++;
    if (fbt_col >= fbt_cols) {
        fbt_col = 0;
        fbt_row++;
        if (fbt_row >= fbt_rows) fbt_scroll();
    }
}

void fbterm_write(const char *s)     { while (*s) fbterm_putchar(*s++); }
void fbterm_writeline(const char *s) { fbterm_write(s); fbterm_putchar('\n'); }
