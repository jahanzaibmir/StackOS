/* =============================================================================
   StackOS — kernel/syscall/syscall.c
   int 0x80 syscall dispatcher
   ============================================================================= */
#include "syscall.h"
#include "../proc/process.h"
#include "../drivers/vga.h"
#include "../drivers/keyboard.h"
#include "../kprintf.h"
#include <stdint.h>
#include <stddef.h>

#define FD_STDIN  0
#define FD_STDOUT 1
#define FD_STDERR 2

static uint8_t  sbrk_pool[512*1024];
static uint32_t sbrk_ptr=0;

static int32_t sys_exit(int code){proc_exit(code);return 0;}

static int32_t sys_write(int fd,const char *buf,size_t len){
    if(!buf)return -1;
    if(fd==FD_STDOUT||fd==FD_STDERR){
        for(size_t i=0;i<len;i++)terminal_putchar(buf[i]);
        return(int32_t)len;}
    return -1;
}
static int32_t sys_read(int fd,char *buf,size_t len){
    if(!buf||len==0)return -1;
    if(fd==FD_STDIN){
        for(size_t i=0;i<len;i++){
            buf[i]=keyboard_getchar();
            if(buf[i]=='\n'){i++;return(int32_t)i;}}
        return(int32_t)len;}
    return -1;
}
static int32_t sys_getpid(void){return(int32_t)proc_current()->pid;}
static int32_t sys_sleep(uint32_t ms){proc_sleep(ms);return 0;}
static int32_t sys_yield(void){proc_yield();return 0;}
static int32_t sys_sbrk(int32_t inc){
    uint32_t old=sbrk_ptr;
    if(inc<0){if((uint32_t)(-inc)>sbrk_ptr)return -1;sbrk_ptr-=(uint32_t)(-inc);}
    else{if(sbrk_ptr+(uint32_t)inc>sizeof(sbrk_pool))return -1;sbrk_ptr+=(uint32_t)inc;}
    return(int32_t)(uintptr_t)(sbrk_pool+old);
}

void syscall_dispatch(registers_t *regs){
    int32_t ret=-1;
    switch(regs->eax){
        case SYS_EXIT:   ret=sys_exit((int)regs->ebx);break;
        case SYS_WRITE:  ret=sys_write((int)regs->ebx,(const char*)(uintptr_t)regs->ecx,(size_t)regs->edx);break;
        case SYS_READ:   ret=sys_read((int)regs->ebx,(char*)(uintptr_t)regs->ecx,(size_t)regs->edx);break;
        case SYS_GETPID: ret=sys_getpid();break;
        case SYS_SLEEP:  ret=sys_sleep(regs->ebx);break;
        case SYS_YIELD:  ret=sys_yield();break;
        case SYS_SBRK:   ret=sys_sbrk((int32_t)regs->ebx);break;
        default:kprintf("[syscall] unknown %u from PID %u\n",regs->eax,proc_current()->pid);
    }
    regs->eax=(uint32_t)ret;
}

void syscall_init(void){
    kprintf("[syscall] StackOS syscall interface ready (int 0x%x)\n",SYSCALL_VECTOR);
}
