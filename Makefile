# StackOS Makefile
# just run 'make' to build, 'make run' to test in qemu

CC     = gcc
LD     = ld
NASM   = nasm
MKISO  = grub-mkrescue

# -m32 because we're targeting x86 32bit
# no stdlib, no SSE, bare metal stuff only
CFLAGS = -m32 -std=c11 -O2 -Wall -Wextra \
         -ffreestanding -fno-builtin -nostdlib \
         -fno-stack-protector -fno-omit-frame-pointer \
         -fno-pic -fno-pie \
         -mno-sse -mno-sse2 -mno-mmx \
         -Ikernel

ASFLAGS = -f elf32

# linker script handles memory layout
LDFLAGS = -m elf_i386 \
          -T kernel/arch/x86/linker.ld \
          --oformat elf32-i386 \
          -nostdlib

BUILD = build

# asm sources
ASM_SOURCES  = boot/boot.asm
ASM_SOURCES += kernel/arch/x86/gdt_flush.asm
ASM_SOURCES += kernel/arch/x86/isr_stubs.asm
ASM_SOURCES += kernel/proc/switch.asm

# kernel C sources
C_SOURCES  = kernel/kernel.c
C_SOURCES += kernel/kprintf.c
C_SOURCES += kernel/shell.c

# arch
C_SOURCES += kernel/arch/x86/gdt.c
C_SOURCES += kernel/arch/x86/idt.c

# memory management
C_SOURCES += kernel/mm/pmm.c
C_SOURCES += kernel/mm/heap.c
C_SOURCES += kernel/mm/paging.c

# drivers
C_SOURCES += kernel/drivers/vga.c
C_SOURCES += kernel/drivers/keyboard.c
C_SOURCES += kernel/drivers/timer.c
C_SOURCES += kernel/drivers/serial.c
C_SOURCES += kernel/drivers/disk/ata.c
C_SOURCES += kernel/drivers/pci/pci.c
C_SOURCES += kernel/drivers/net/e1000.c

# processes + syscalls
C_SOURCES += kernel/proc/process.c
C_SOURCES += kernel/syscall/syscall.c

# filesystem
C_SOURCES += kernel/fs/vfs.c
C_SOURCES += kernel/fs/initrd.c
C_SOURCES += kernel/fs/stackfs.c

# gui / framebuffer
C_SOURCES += kernel/gui/fb.c
C_SOURCES += kernel/gui/font.c
C_SOURCES += kernel/gui/fbterm.c

# networking
C_SOURCES += kernel/net/net.c

C_SOURCES += kernel/user.c

ASM_OBJECTS = $(patsubst %.asm, $(BUILD)/%.o, $(ASM_SOURCES))
C_OBJECTS   = $(patsubst %.c,   $(BUILD)/%.o, $(C_SOURCES))
ALL_OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

KERNEL_ELF = $(BUILD)/stack.elf
ISO_IMAGE  = BlizzardOS.iso

.PHONY: all clean run run-gui run-net debug

all: $(ISO_IMAGE)

# assemble .asm files
$(BUILD)/%.o: %.asm
	mkdir -p $(dir $@)
	$(NASM) $(ASFLAGS) $< -o $@

# compile .c files
$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# link everything into the kernel elf
$(KERNEL_ELF): $(ALL_OBJECTS)
	mkdir -p $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "kernel built -> $@"
	@size $@

# pack into bootable ISO using grub
$(ISO_IMAGE): $(KERNEL_ELF)
	mkdir -p iso/boot
	cp $(KERNEL_ELF) iso/boot/stack.elf
	$(MKISO) -o $@ iso/
	@echo "ISO ready -> $(ISO_IMAGE)"

# headless run, serial output to terminal
run: $(ISO_IMAGE)
	qemu-system-i386 -cdrom $(ISO_IMAGE) -m 128M \
	  -display none -serial stdio -no-reboot

# run with GUI (needs stack.img disk image)
run-gui: $(ISO_IMAGE)
	qemu-system-i386 \
	  -cdrom $(ISO_IMAGE) \
	  -drive file=stack.img,format=raw,if=ide,index=1,media=disk \
	  -m 128M -no-reboot \
	  -display gtk \
	  -serial stdio



# start qemu paused, attach gdb and break at kernel_main
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
	@echo "cleaned up"
