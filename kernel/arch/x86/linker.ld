/* =============================================================================
   BlizzardOS — kernel/arch/x86/linker.ld
   Linker script for the BlizzardOS kernel ELF binary
   Loads kernel at 1 MiB (0x100000) — standard for Multiboot kernels
   ============================================================================= */

ENTRY(_start)

SECTIONS {
    /* Load at 1 MiB physical — below is BIOS/real-mode territory */
    . = 0x00100000;

    /* Multiboot header must be in first 8 KiB of the ELF */
    .multiboot ALIGN(4) : {
        *(.multiboot)
    }

    /* Kernel code */
    .text ALIGN(4096) : {
        *(.text)
        *(.text.*)
    }

    /* Read-only data (string literals, const globals) */
    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }

    /* Initialised read-write data */
    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    }

    /* Zero-initialised data — takes no space in the ELF file */
    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* Symbol marking end of kernel image — used by memory manager */
    . = ALIGN(4096);
    PROVIDE(kernel_end = .);
}
