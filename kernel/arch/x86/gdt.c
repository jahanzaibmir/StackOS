/* =============================================================================
 *  StackOS — kernel/arch/x86/gdt.c
 *  Sets up the Global Descriptor Table (GDT) with:
 *    0 — Null descriptor  (required by CPU spec)
 *    1 — Kernel code      (ring 0, executable, readable)
 *    2 — Kernel data      (ring 0, writable)
 *    3 — User code        (ring 3, executable, readable)
 *    4 — User data        (ring 3, writable)
 *  ============================================================================= */
#include "gdt.h"

/* A GDT entry is 8 bytes, split into several bitfields */
typedef struct __attribute__((packed)) {
    uint16_t limit_low;    /* lower 16 bits of segment limit        */
    uint16_t base_low;     /* lower 16 bits of base address         */
    uint8_t  base_mid;     /* middle 8 bits of base address         */
    uint8_t  access;       /* access byte (type, DPL, present flag) */
    uint8_t  flags_limit;  /* upper 4 bits: flags; lower 4: limit   */
    uint8_t  base_high;    /* upper 8 bits of base address          */
} gdt_entry_t;

/* The GDTR register value — points to the table and gives its size */
typedef struct __attribute__((packed)) {
    uint16_t limit;        /* table size in bytes minus 1           */
    uint32_t base;         /* linear address of first GDT entry     */
} gdt_descriptor_t;

/* Access byte flags */
#define GDT_PRESENT    0x80   /* segment is present in memory       */
#define GDT_RING0      0x00   /* kernel privilege level             */
#define GDT_RING3      0x60   /* user privilege level               */
#define GDT_DESCRIPTOR 0x10   /* not a system segment               */
#define GDT_EXECUTABLE 0x08   /* code (executable) segment          */
#define GDT_READABLE   0x02   /* code readable / data writable      */

/* Flags nibble (high 4 bits of byte 6) */
#define GDT_GRANULARITY 0x80  /* limit in 4 KiB pages              */
#define GDT_32BIT       0x40  /* 32-bit protected mode segment      */

#define GDT_ENTRIES 5

static gdt_entry_t  gdt[GDT_ENTRIES];
static gdt_descriptor_t gdtr;

/* Build one 8-byte GDT descriptor */
static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags) {
    gdt[idx].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[idx].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    gdt[idx].base_high   = (uint8_t)((base >> 24) & 0xFF);
    gdt[idx].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[idx].flags_limit = (uint8_t)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    gdt[idx].access      = access;
                          }

                          /* Defined in gdt_flush.asm — loads GDTR and far-jumps to reload CS */
                          extern void gdt_flush(uint32_t gdtr_ptr);

                          void gdt_init(void) {
                              gdtr.limit = (uint16_t)(sizeof(gdt_entry_t) * GDT_ENTRIES - 1);
                              gdtr.base  = (uint32_t)(uintptr_t)&gdt;

                              /* 0: Null descriptor — always required */
                              gdt_set_entry(0, 0, 0, 0, 0);

                              /* 1: Kernel code — ring 0, full 4 GiB, 32-bit */
                              gdt_set_entry(1, 0, 0xFFFFF,
                                            GDT_PRESENT | GDT_RING0 | GDT_DESCRIPTOR | GDT_EXECUTABLE | GDT_READABLE,
                                            GDT_GRANULARITY | GDT_32BIT);

                              /* 2: Kernel data — ring 0, full 4 GiB */
                              gdt_set_entry(2, 0, 0xFFFFF,
                                            GDT_PRESENT | GDT_RING0 | GDT_DESCRIPTOR | GDT_READABLE,
                                            GDT_GRANULARITY | GDT_32BIT);

                              /* 3: User code — ring 3, full 4 GiB, 32-bit */
                              gdt_set_entry(3, 0, 0xFFFFF,
                                            GDT_PRESENT | GDT_RING3 | GDT_DESCRIPTOR | GDT_EXECUTABLE | GDT_READABLE,
                                            GDT_GRANULARITY | GDT_32BIT);

                              /* 4: User data — ring 3, full 4 GiB */
                              gdt_set_entry(4, 0, 0xFFFFF,
                                            GDT_PRESENT | GDT_RING3 | GDT_DESCRIPTOR | GDT_READABLE,
                                            GDT_GRANULARITY | GDT_32BIT);

                              gdt_flush((uint32_t)(uintptr_t)&gdtr);
                          }
