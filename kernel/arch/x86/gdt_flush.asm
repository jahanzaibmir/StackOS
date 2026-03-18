; kernel/arch/x86/gdt_flush.asm
; loads the GDTR and reloads all segment registers

section .text
global gdt_flush

gdt_flush:
    mov  eax, [esp + 4]     ; grab the gdtr pointer passed from C
    lgdt [eax]              ; load it into the GDTR register

    ; reload all data segment registers with kernel data selector (0x10)
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax

    ; far jump to reload CS with kernel code selector (0x08)
    ; can't just mov CS directly, far jump is the only way
    jmp  0x08:.flush_cs
.flush_cs:
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
