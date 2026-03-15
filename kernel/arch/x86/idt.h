/* StackOS — kernel/arch/x86/idt.h */
#pragma once
#include <stdint.h>
typedef struct __attribute__((packed)){
    uint32_t ds;
    uint32_t edi,esi,ebp,esp_dummy;
    uint32_t ebx,edx,ecx,eax;
    uint32_t int_no,err_code;
    uint32_t eip,cs,eflags;
    uint32_t useresp,ss;
} registers_t;
void idt_init(void);
void irq_register_handler(int irq,void(*handler)(registers_t*));
