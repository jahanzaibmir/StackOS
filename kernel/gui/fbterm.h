/* =============================================================================
 B lizzardOS — kernel/gui/fbterm.h
 ============================================================================= */
#pragma once
#include <stdint.h>

void fbterm_init(void);
void fbterm_putchar(char c);
void fbterm_write(const char *s);
void fbterm_writeline(const char *s);
void fbterm_clear(void);
void fbterm_setcolor(uint32_t fg, uint32_t bg);
void fbterm_flush(void);   /* flush accumulated dirty region to hardware */
int  fbterm_active(void);
