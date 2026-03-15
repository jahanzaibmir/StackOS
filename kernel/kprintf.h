/* =============================================================================
   BlizzardOS — kernel/kprintf.h / kprintf.c
   Minimal kernel printf — supports %s %c %d %u %x %p %%
   No floating point — we're a kernel, not a spreadsheet.
   ============================================================================= */
#pragma once
#include <stdarg.h>

void kprintf(const char *fmt, ...);
void kputs(const char *s);
