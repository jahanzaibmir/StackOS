/* =============================================================================
   StackOS — kernel/syscall/syscall.h
   System call interface — int 0x80
   ============================================================================= */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../arch/x86/idt.h"

#define SYSCALL_VECTOR 0x80

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_READ   2
#define SYS_GETPID 3
#define SYS_SLEEP  4
#define SYS_YIELD  5
#define SYS_SBRK   6

void syscall_init(void);
void syscall_dispatch(registers_t *regs);
