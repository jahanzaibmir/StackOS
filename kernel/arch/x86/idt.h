/* kernel/arch/x86/idt.h */
#pragma once
#include <stdint.h>

/*
 * register state pushed onto the stack by the CPU + our ISR stubs
 * order matters here — has to match exactly what isr_stubs.asm pushes
 */
typedef struct __attribute__((packed)) {
    uint32_t ds;                        /* data segment */
    uint32_t edi, esi, ebp, esp_dummy;  /* pushed by pusha */
    uint32_t ebx, edx, ecx, eax;        /* pushed by pusha */
    uint32_t int_no, err_code;          /* interrupt number + error code */
    uint32_t eip, cs, eflags;           /* pushed by CPU automatically */
    uint32_t useresp, ss;               /* only valid on privilege change */
} registers_t;

void idt_init(void);
void irq_register_handler(int irq, void (*handler)(registers_t *));
