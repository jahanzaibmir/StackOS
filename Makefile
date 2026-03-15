# BlizzardOS Makefile
CC      = gcc
LD      = ld
NASM    = nasm
MKISO   = grub-mkrescue
CFLAGS  = -m32 -std=c11 -O2 -Wall -Wextra -ffreestanding -fno-builtin \
          -nostdlib -fno-stack-protector -fno-omit-frame-pointer \
          -fno-pic -fno-pie -mno-sse -mno-sse2 -mno-mmx -Ikernel
ASFLAGS = -f elf32
LDFLAGS = -m elf_i386 -T kernel/arch/x86/linker.ld \
          --oformat elf32-i386 -nostdlib
BUILD   = build

ASM_SOURCES  = boot/boot.asm
ASM_SOURCES += kernel/arch/x86/gdt_flush.asm
ASM_SOURCES += kernel/arch/x86/isr_stubs.asm
ASM_SOURCES += kernel/proc/switch.asm

C_SOURCES  = kernel/kernel.c
C_SOURCES += kernel/kprintf.c
C_SOURCES += kernel/shell.c
C_SOURCES += kernel/arch/x86/gdt.c
C_SOURCES += kernel/arch/x86/idt.c
C_SOURCES += kernel/mm/pmm.c
C_SOURCES += kernel/mm/heap.c
C_SOURCES += kernel/mm/paging.c
C_SOURCES += kernel/drivers/vga.c
C_SOURCES += kernel/drivers/keyboard.c
C_SOURCES += kernel/drivers/timer.c
C_SOURCES += kernel/proc/process.c
C_SOURCES += kernel/syscall/syscall.c
C_SOURCES += kernel/fs/vfs.c
C_SOURCES += kernel/fs/initrd.c
C_SOURCES += kernel/user.c
C_SOURCES += kernel/gui/fb.c
C_SOURCES += kernel/gui/font.c
C_SOURCES += kernel/drivers/serial.c
C_SOURCES += kernel/gui/fbterm.c
C_SOURCES += kernel/drivers/disk/ata.c
C_SOURCES += kernel/fs/stackfs.c
C_SOURCES += kernel/drivers/pci/pci.c
C_SOURCES += kernel/drivers/net/e1000.c
C_SOURCES += kernel/net/net.c

ASM_OBJECTS = $(patsubst %.asm,$(BUILD)/%.o,$(ASM_SOURCES))
C_OBJECTS   = $(patsubst %.c,$(BUILD)/%.o,$(C_SOURCES))
ALL_OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

KERNEL_ELF = $(BUILD)/stack.elf
ISO_IMAGE  = BlizzardOS.iso

.PHONY: all clean run run-gui run-net debug

all: $(ISO_IMAGE)

$(BUILD)/%.o: %.asm
	mkdir -p $(dir $@)
	$(NASM) $(ASFLAGS) $< -o $@

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(ALL_OBJECTS)
	mkdir -p $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo '  OK Kernel: $@'
	@size $@

$(ISO_IMAGE): $(KERNEL_ELF)
	mkdir -p iso/boot
	cp $(KERNEL_ELF) iso/boot/stack.elf
	$(MKISO) -o $@ iso/
	@echo '  OK ISO: $(ISO_IMAGE)'

run: $(ISO_IMAGE)
	qemu-system-i386 -cdrom $(ISO_IMAGE) -m 128M -display none -serial stdio -no-reboot

run-gui: $(ISO_IMAGE)
	qemu-system-i386 \
	  -cdrom $(ISO_IMAGE) \
	  -drive file=stack.img,format=raw,if=ide,index=1,media=disk \
	  -m 128M -no-reboot -display gtk -serial stdio

run-net: $(ISO_IMAGE)
	qemu-system-i386 \
	  -cdrom $(ISO_IMAGE) \
	  -drive file=stack.img,format=raw,if=ide,index=1,media=disk \
	  -m 128M -no-reboot -display gtk -serial stdio \
	  -netdev user,id=net0 \
	  -device e1000,netdev=net0

debug: $(ISO_IMAGE)
	qemu-system-i386 \
	  -cdrom $(ISO_IMAGE) \
	  -drive file=stack.img,format=raw,if=ide,index=1,media=disk \
	  -m 128M -s -S \
	  -netdev user,id=net0 \
	  -device e1000,netdev=net0 &
	gdb $(KERNEL_ELF) \
	  -ex 'target remote :1234' \
	  -ex 'break kernel_main' \
	  -ex 'continue'

clean:
	rm -rf $(BUILD) $(ISO_IMAGE) iso/boot/stack.elf
	@echo '  OK Cleaned'
