; =============================================================================
; StackOS — boot/boot.asm
; Multiboot 1 header with framebuffer request.
; flags = 0x00000007:
;   bit 0 — align modules on page boundaries
;   bit 1 — provide memory map
;   bit 2 — request video mode info (framebuffer addr in mb_info)
; NOTE: bit 16 (aout kludge) is NOT set — we are an ELF binary,
;       GRUB reads load addresses from the ELF headers directly.
; =============================================================================

MULTIBOOT_MAGIC    equ 0x1BADB002
MULTIBOOT_FLAGS    equ 0x00000007
MULTIBOOT_CHECKSUM equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

section .multiboot
align 4
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM

    ; Video mode hint — only used when bit 2 is set
    ; (no aout fields needed since bit 16 is clear)
    dd 0        ; header_addr   (ignored for ELF)
    dd 0        ; load_addr     (ignored for ELF)
    dd 0        ; load_end_addr (ignored for ELF)
    dd 0        ; bss_end_addr  (ignored for ELF)
    dd 0        ; entry_addr    (ignored for ELF)

    dd 0        ; mode_type: 0 = linear graphics, 1 = EGA text
    dd 1024     ; width
    dd 768      ; height
    dd 32       ; depth

section .bss
align 16
stack_bottom:
    resb 16384
stack_top:

section .text
global _start
extern kernel_main

_start:
    mov  esp, stack_top
    push ebx        ; multiboot info pointer
    push eax        ; multiboot magic
    push 0
    popf
    call kernel_main
.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
