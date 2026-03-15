/* =============================================================================
 StackOS — kernel/drivers/keyboard.c*
 PS/2 keyboard — scancode set 1, capslock LED, extended keys (arrows etc)
 ============================================================================= */
#include "keyboard.h"
#include "vga.h"
#include "../arch/x86/idt.h"
#include "../arch/x86/io.h"

#define KB_DATA 0x60
#define KB_CMD  0x64
#define KB_BUF  512

static const char normal[128] = {
    0,    0,   '1', '2', '3', '4', '5', '6',
    '7',  '8', '9', '0', '-', '=', '\b','\t',
    'q',  'w', 'e', 'r', 't', 'y', 'u', 'i',
    'o',  'p', '[', ']', '\n', 0,  'a', 's',
    'd',  'f', 'g', 'h', 'j', 'k', 'l', ';',
    '\'', '`',  0,  '\\','z', 'x', 'c', 'v',
    'b',  'n', 'm', ',', '.', '/',  0,  '*',
    0,   ' ',  0,   0,   0,   0,   0,   0,
    0,    0,   0,   0,   0,   0,   0,  '7',
    '8',  '9', '-', '4', '5', '6', '+', '1',
    '2',  '3', '0', '.',  0,   0,   0,   0,
};

static const char shifted[128] = {
    0,    0,   '!', '@', '#', '$', '%', '^',
    '&',  '*', '(', ')', '_', '+', '\b','\t',
    'Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I',
    'O',  'P', '{', '}', '\n', 0,  'A', 'S',
    'D',  'F', 'G', 'H', 'J', 'K', 'L', ':',
    '"',  '~',  0,  '|', 'Z', 'X', 'C', 'V',
    'B',  'N', 'M', '<', '>', '?',  0,  '*',
    0,   ' ',  0,   0,   0,   0,   0,   0,
};

static volatile char     buf[KB_BUF];
static volatile uint32_t rp = 0, wp = 0;
static volatile int shift = 0, ctrl = 0, alt = 0;
static volatile int caps = 0, num = 0, scroll = 0;
static volatile int extended = 0;   /* 0xE0 prefix seen */

static void push(char c) {
    uint32_t next = (wp + 1) % KB_BUF;
    if (next != rp) { buf[wp] = c; wp = next; }
}

/*
 * update_leds — sends the LED state to the PS/2 controller.
 *
 * Uses a timeout on every busy-wait so we can never deadlock.
 * Safe to call from the IRQ handler (interrupts are off during IRQ dispatch
 * anyway) but NOT safe to call before sti() — which is why keyboard_init()
 * no longer calls it.  LEDs sync up automatically on the first toggle keypress.
 */
static void update_leds(void) {
    uint32_t timeout;

    /* Wait for PS/2 input buffer to be empty (bit 1 of status = IBF) */
    timeout = 100000;
    while ((inb(KB_CMD) & 0x02) && --timeout);
    if (!timeout) return;   /* controller not responding — bail out safely */

        outb(KB_DATA, 0xED);    /* "set LEDs" command */

        /* Wait again for controller to process the command byte */
        timeout = 100000;
    while ((inb(KB_CMD) & 0x02) && --timeout);
    if (!timeout) return;

    /* LED byte: bit0 = scroll, bit1 = num, bit2 = caps */
    outb(KB_DATA, (uint8_t)((scroll & 1) | ((num & 1) << 1) | ((caps & 1) << 2)));
}

static int is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static void kb_irq(registers_t *regs) {
    (void)regs;
    uint8_t sc = inb(KB_DATA);

    /* Extended key prefix */
    if (sc == 0xE0) { extended = 1; return; }

    if (extended) {
        extended = 0;
        /* Key release of extended key — ignore */
        if (sc & 0x80) return;
        /* Push 0xE0 marker then the scan code so shell can parse arrows etc */
        push((char)0xE0);
        push((char)sc);
        return;
    }

    /* Key release — clear modifier flags */
    if (sc & 0x80) {
        uint8_t r = sc & 0x7F;
        if (r == 0x2A || r == 0x36) shift = 0;
        if (r == 0x1D)              ctrl  = 0;
        if (r == 0x38)              alt   = 0;
        return;
    }

    /* Key press — modifiers */
    switch (sc) {
        case 0x2A: case 0x36: shift  = 1; return;
        case 0x1D:             ctrl   = 1; return;
        case 0x38:             alt    = 1; return;
        case 0x3A: caps   ^= 1; update_leds(); return;
        case 0x45: num    ^= 1; update_leds(); return;
        case 0x46: scroll ^= 1; update_leds(); return;
    }

    /* Ctrl combos */
    if (ctrl) {
        if (sc == 0x2E) { push(0x03); return; }  /* Ctrl+C */
            if (sc == 0x26) { push(0x0C); return; }  /* Ctrl+L */
    }

    if (sc >= 128) return;
    char c = shift ? shifted[sc] : normal[sc];
    if (!c) return;

    /* Apply capslock to letters */
    if (caps && is_letter(c)) {
        if (shift) { if (c >= 'A' && c <= 'Z') c += 32; }
        else       { if (c >= 'a' && c <= 'z') c -= 32; }
    }

    push(c);
}

void keyboard_init(void) {
    extern void irq_register_handler(int irq, void (*h)(registers_t *));
    irq_register_handler(1, kb_irq);
    caps = num = scroll = 0;
    /*
     * Do NOT call update_leds() here.
     *
     * keyboard_init() is called before sti() in kernel_main.  The PS/2
     * controller sends an ACK byte after the 0xED command, but with
     * interrupts disabled we never process it — the input-buffer-full bit
     * never clears and the second busy-wait spins forever, hanging the boot.
     *
     * The LEDs will reflect the correct state automatically the first time
     * the user presses CapsLock / NumLock / ScrollLock.
     */
}

char keyboard_poll(void) {
    if (rp == wp) return 0;
    char c = buf[rp];
    rp = (rp + 1) % KB_BUF;
    return c;
}

char keyboard_getchar(void) {
    char c;
    while (!(c = keyboard_poll()))
        __asm__ volatile ("hlt");
    return c;
}

int keyboard_ctrl(void)  { return ctrl; }
int keyboard_shift(void) { return shift; }
int keyboard_alt(void)   { return alt; }
int keyboard_caps(void)  { return caps; }
