/* =============================================================================
 *  StackOS — kernel/kernel.c
 *  PCI + E1000 initialised BEFORE sti().
 *  ============================================================================= */
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/serial.h"
#include "arch/x86/gdt.h"
#include "arch/x86/idt.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "mm/paging.h"
#include "proc/process.h"
#include "syscall/syscall.h"
#include "fs/vfs.h"
#include "fs/stackfs.h"
#include "fs/initrd.h"
#include "user.h"
#include "drivers/disk/ata.h"
#include "drivers/pci/pci.h"
#include "drivers/net/e1000.h"
#include "net/net.h"
#include "gui/fb.h"
#include "gui/font.h"
#include "gui/fbterm.h"
#include "kprintf.h"
#include "shell.h"
#include <stdint.h>

static void slog(const char *s){serial_write(s);serial_write("\n");}
static void serial_hex(uint32_t v){
    serial_write("0x");
    for(int i=28;i>=0;i-=4){uint8_t n=(v>>i)&0xF;serial_putchar(n<10?'0'+n:'A'+n-10);}
}

typedef struct __attribute__((packed)){
    uint32_t flags,mem_lower,mem_upper,boot_device,cmdline;
    uint32_t mods_count,mods_addr,syms[4];
    uint32_t mmap_length,mmap_addr,drives_length,drives_addr;
    uint32_t config_table,boot_loader_name,apm_table;
    uint32_t vbe_control_info,vbe_mode_info;
    uint16_t vbe_mode,vbe_interface_seg,vbe_interface_off,vbe_interface_len;
    uint64_t fb_addr;
    uint32_t fb_pitch,fb_width,fb_height;
    uint8_t  fb_bpp,fb_type;
} multiboot_info_t;

#define MULTIBOOT_MAGIC_EXPECTED 0x2BADB002
#define MB_FLAG_FB (1<<12)
#define KERNEL_HEAP_SIZE (4*1024*1024)

static uint8_t kernel_heap_region[KERNEL_HEAP_SIZE] __attribute__((aligned(16)));
extern uint32_t kernel_end;

static void map_framebuffer(uint32_t fb_phys,uint32_t size){
    uint32_t pages=(size+4095)/4096;
    page_directory_t *dir=paging_kernel_directory();
    for(uint32_t i=0;i<pages;i++){
        uint32_t addr=fb_phys+i*4096;
        paging_map_page(dir,addr,addr,PAGE_PRESENT|PAGE_WRITE);
    }
    slog("[fb] pages mapped");
}

static void draw_splash(void){
    fb_fill(COL_DESKTOP);
    fb_rect(0,0,(int)fb.width,32,COL_PANEL);
    fb_hline(0,32,(int)fb.width,COL_BORDER);
    font_drawstr(12,10,"StackOS  v1.0.0  |  100% from scratch",COL_STACK,0,1);
    int tby=(int)fb.height-36;
    fb_rect(0,tby,(int)fb.width,36,COL_PANEL);
    fb_hline(0,tby,(int)fb.width,COL_BORDER);
    font_drawstr(12,tby+11,"StackOS",COL_STACK,0,1);
    int cw=500,ch=300,cx=((int)fb.width-cw)/2,cy=((int)fb.height-ch)/2;
    fb_rect_round(cx,cy,cw,ch,10,COL_SURFACE);
    fb_rect_round_outline(cx,cy,cw,ch,10,COL_BORDER,1);
    fb_rect(cx,cy,cw,34,COL_TITLEBAR_A);
    font_drawstr(cx+14,cy+10,"Welcome to StackOS",COL_WHITE,0,1);
    int tx=cx+24,ty=cy+56;
    font_drawstr(tx,ty,    "Kernel:   x86-32  monolithic  protected mode", COL_TEXT,0,1);
    font_drawstr(tx,ty+22, "Heap:     4 MiB  kmalloc / kfree",             COL_TEXT,0,1);
    font_drawstr(tx,ty+44, "Video:    VESA linear framebuffer 1024x768x32",COL_TEXT,0,1);
    font_drawstr(tx,ty+66, "Storage:  StackFS  persistent ATA disk",       COL_TEXT,0,1);
    font_drawstr(tx,ty+88, "Network:  E1000  ARP  IPv4  ICMP  UDP  DHCP",  COL_TEXT,0,1);
    font_drawstr(tx,ty+110,"Users:    root (admin)  /  stack (normal)",    COL_TEXT,0,1);
    font_drawstr(cx+14,cy+ch-24,"Press any key to continue...",COL_TEXT_DIM,0,1);
    fb_flush();
}

static void heartbeat_task(void){while(1){proc_sleep(10000);proc_yield();}}

static void disk_first_boot(void){
    slog("[disk] first boot — formatting StackFS");
    if(stackfs_format("StackOS")!=0){slog("[disk] format FAILED");return;}
    vfs_node_t *root=stackfs_mount();
    if(!root){slog("[disk] mount FAILED");return;}
    vfs_mount("/disk",root);
    slog("[disk] mounted at /disk");
    vfs_node_t *disk=vfs_open("/disk");
    if(disk&&disk->ops&&disk->ops->mkdir){
        disk->ops->mkdir(disk,"etc");
        disk->ops->mkdir(disk,"home");
        disk->ops->mkdir(disk,"tmp");
        disk->ops->mkdir(disk,"var");
        slog("[disk] created /disk/{etc,home,tmp,var}");
    }
    vfs_node_t *etc=vfs_open("/disk/etc");
    if(etc&&etc->ops&&etc->ops->create){
        etc->ops->create(etc,"passwd",0);
        etc=vfs_open("/disk/etc");
        if(etc){
            vfs_node_t *pw=vfs_finddir(etc,"passwd");
            if(pw){const char *s="root:admin:2\nstack:stack:1\n";uint32_t n=0;while(s[n])n++;vfs_write(pw,0,n,(const uint8_t*)s);kfree(pw);}
        }
    }
    vfs_node_t *etc2=vfs_open("/disk/etc");
    if(etc2&&etc2->ops&&etc2->ops->create){
        etc2->ops->create(etc2,"motd",0);
        etc2=vfs_open("/disk/etc");
        if(etc2){
            vfs_node_t *motd=vfs_finddir(etc2,"motd");
            if(motd){const char *s="Welcome to StackOS.\nType 'help' for commands.\n";uint32_t n=0;while(s[n])n++;vfs_write(motd,0,n,(const uint8_t*)s);kfree(motd);}
        }
    }
    slog("[disk] first boot complete");
}

void kernel_main(uint32_t mb_magic,multiboot_info_t *mb_info){
    serial_init();
    slog("=== StackOS booting ===");
    terminal_init();
    slog("STEP1  terminals");

    if(mb_magic!=MULTIBOOT_MAGIC_EXPECTED){
        slog("PANIC bad magic");for(;;)__asm__ volatile("cli;hlt");
    }
    slog("STEP2  multiboot OK");

    gdt_init();      slog("STEP3  GDT");
    idt_init();      slog("STEP4  IDT");

    uint32_t total_kb=mb_info->mem_lower+mb_info->mem_upper;
    pmm_init(total_kb,(uint32_t)(uintptr_t)&kernel_end);
    slog("STEP5  PMM");

    heap_init(kernel_heap_region,KERNEL_HEAP_SIZE);
    slog("STEP6  heap");

    paging_init();   slog("STEP7  paging");
    timer_init(100); slog("STEP8  timer");
    keyboard_init(); slog("STEP9  keyboard");
    proc_init();     slog("STEP10 proc");
    syscall_init();  slog("STEP11 syscall");
    vfs_init();      slog("STEP12 vfs");
    initrd_init();   slog("STEP13 initrd");

    if(ata_init()==0){
        slog("STEP14a ATA disk found");
        if(stackfs_detect()){
            slog("STEP14b StackFS found — mounting");
            vfs_node_t *r=stackfs_mount();
            if(r){vfs_mount("/disk",r);slog("STEP14c /disk mounted");}
            else   slog("STEP14c mount FAILED");
        }else{
            slog("STEP14b first boot — auto-format");
            disk_first_boot();
        }
    }else{
        slog("STEP14 no ATA disk");
    }

    user_init();
    slog("STEP15 users");
    users_save();
    slog("STEP15a users saved");

    slog("STEP16 PCI");
    pci_init();
    slog("STEP16a PCI done");

    slog("STEP17 E1000");
    if(e1000_init()==0){
        slog("STEP17a E1000 found");
        net_init();
        slog("STEP17b network stack ready");
        slog("STEP17c DHCP request...");
        if(dhcp_request()){
            slog("STEP17d DHCP success");
        }else{
            slog("STEP17d DHCP failed — fallback to 10.0.2.15");
            net_ip   = IP4(10,0,2,15);
            net_gw   = IP4(10,0,2,2);
            net_mask = IP4(255,255,255,0);
            net_dns  = IP4(10,0,2,3);
        }
    }else{
        slog("STEP17 no E1000");
    }

    __asm__ volatile("sti");
    slog("STEP18 interrupts ON");

    serial_write("[mb] flags=");serial_hex(mb_info->flags);serial_write("\n");

    int gui_ok=0;
    if(mb_info->flags&MB_FLAG_FB){
        slog("STEP19 fb present");
        uint32_t fba=(uint32_t)(mb_info->fb_addr&0xFFFFFFFF);
        uint32_t fbw=mb_info->fb_width;
        uint32_t fbh=mb_info->fb_height;
        uint32_t fbp=mb_info->fb_pitch;
        uint8_t  fbb=mb_info->fb_bpp;
        serial_write("[fb] addr=");serial_hex(fba);
        serial_write(" w=");serial_hex(fbw);
        serial_write(" h=");serial_hex(fbh);
        serial_write(" bpp=");serial_hex(fbb);serial_write("\n");
        if(fba&&fbw&&fbh){
            map_framebuffer(fba,fbp*fbh+fbp);
            if(fb_init(fba,fbw,fbh,fbp,fbb)==0){
                slog("STEP19a fb_init OK");
                gui_ok=1;
                draw_splash();
                slog("STEP19b waiting for key");
                keyboard_getchar();
                fbterm_init();
                slog("STEP19c fbterm ready");
            }
        }else{
            slog("STEP19 fb dims zero");
        }
    }else{
        slog("STEP19 no fb — text mode");
    }

    if(!gui_ok){
        terminal_setcolor(VGA_COLOR_LIGHT_BROWN,VGA_COLOR_BLACK);
        terminal_writeline("StackOS — text mode");
        terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    }

    slog("STEP20 entering shell");
    proc_create_kernel("heartbeat",heartbeat_task);
    shell_run();
    for(;;)__asm__ volatile("cli;hlt");
}
