/* =============================================================================
 *  StackOS — kernel/shell.c   v1.0.0
 *  StackShell — command interpreter
 *
 *  Command renames from StackOS:
 *    blizzinfo   → sysinfo
 *    frostup     → elevate   (password: admin)
 *    frostdown   → lower
 *    blizzquote  → stackquote
 *    diskformat  → format
 *    diskmount   → mount
 *    diskinfo    → drives
 *    adduser     → useradd
 *    deluser     → userdel
 *    chpass      → passwd
 *  Default users: root/admin  stack/stack
 *  Prompt: stack@stackos:/$ or root@stackos:/#
 *  ============================================================================= */
#include "shell.h"
#include "kprintf.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "mm/pmm.h"
#include "mm/heap.h"
#include "proc/process.h"
#include "fs/vfs.h"
#include "fs/stackfs.h"
#include "drivers/disk/ata.h"
#include "drivers/net/e1000.h"
#include "net/net.h"
#include "user.h"
#include <stdint.h>
#include <stddef.h>

#define CMD_BUF     512
#define MAX_ARGS    32
#define HISTORY_MAX 50
#define VERSION     "1.0.0"
#define OS_NAME     "StackOS"

/* ── String helpers ──────────────────────────────────────────────────────── */
static int kstrcmp(const char *a,const char *b){
    while(*a&&*a==*b){a++;b++;}return(unsigned char)*a-(unsigned char)*b;
}
static size_t kstrlen(const char *s){size_t n=0;while(s[n])n++;return n;}
static void kstrcpy(char *d,const char *s,size_t max){
    size_t i=0;while(i<max-1&&s[i]){d[i]=s[i];i++;}d[i]='\0';
}
static uint32_t katoi(const char *s){
    uint32_t n=0;while(*s>='0'&&*s<='9')n=n*10+(uint32_t)(*s++-'0');return n;
}

/* ── CWD ─────────────────────────────────────────────────────────────────── */
static char cwd[VFS_PATH_MAX]="/";

static vfs_node_t *resolve(const char *path){
    if(!path||!path[0])return vfs_open(cwd);
    if(path[0]=='/')return vfs_open(path);
    char full[VFS_PATH_MAX];
    kstrcpy(full,cwd,VFS_PATH_MAX);
    size_t cl=kstrlen(full);
    if(full[cl-1]!='/'){full[cl]='/';full[cl+1]='\0';cl++;}
    kstrcpy(full+cl,path,VFS_PATH_MAX-cl);
    return vfs_open(full);
}

static vfs_node_t *resolve_dir_and_name(const char *path,const char **name_out){
    if(!path||!path[0])return NULL;
    const char *slash=NULL;
    for(const char *p=path;*p;p++)if(*p=='/')slash=p;
    if(!slash){*name_out=path;return vfs_open(cwd);}
    if(slash==path){*name_out=slash+1;return vfs_open("/");}
    char dp[VFS_PATH_MAX];
    size_t dl=(size_t)(slash-path);
    if(dl>=VFS_PATH_MAX)dl=VFS_PATH_MAX-1;
    for(size_t i=0;i<dl;i++)dp[i]=path[i];dp[dl]='\0';
    *name_out=slash+1;
    return vfs_open(dp);
}

static void vnode_free(vfs_node_t *n){
    if(!n)return;
    if(vfs_node_is_mount_root(n))return;
    kfree(n);
}

#define NEED_ROOT() do{ \
if(!user_is_root()){ \
    terminal_setcolor(VGA_COLOR_LIGHT_RED,VGA_COLOR_BLACK); \
    terminal_writeline("Permission denied. Run 'elevate' first."); \
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);return;} \
}while(0)

/* ── History ─────────────────────────────────────────────────────────────── */
static char history[HISTORY_MAX][CMD_BUF];
static int  hist_count=0,hist_pos=0;

static void hist_push(const char *line){
    if(!line||!line[0])return;
    if(hist_count>0&&kstrcmp(history[(hist_count-1)%HISTORY_MAX],line)==0)return;
    kstrcpy(history[hist_count%HISTORY_MAX],line,CMD_BUF);
    hist_count++;hist_pos=hist_count;
}

/* ── Keys ────────────────────────────────────────────────────────────────── */
#define KEY_UP   0x100
#define KEY_DOWN 0x101

static int read_key(void){
    char c=keyboard_getchar();
    if((uint8_t)c==0xE0){
        char c2=keyboard_getchar();
        switch((uint8_t)c2){
            case 0x48:return KEY_UP;  case 0x50:return KEY_DOWN;
            case 0x4B:return 0x102;   case 0x4D:return 0x103;
            case 0x49:return 0x104;   case 0x51:return 0x105;
            case 0x47:return 0x106;   case 0x4F:return 0x107;
            case 0x53:return 0x108;
        }
        return 0;
    }
    return(int)(uint8_t)c;
}

static void clear_input_line(size_t pos){
    for(size_t i=0;i<pos;i++)terminal_putchar('\b');
    for(size_t i=0;i<pos;i++)terminal_putchar(' ');
    for(size_t i=0;i<pos;i++)terminal_putchar('\b');
}

static size_t read_line(char *buf,size_t max){
    size_t pos=0;buf[0]='\0';hist_pos=hist_count;
    while(1){
        int k=read_key();
        if(k=='\n'||k=='\r'){terminal_putchar('\n');buf[pos]='\0';return pos;}
        else if(k==0x0C){terminal_clear();buf[0]='\0';return 0;}
        else if(k==0x03){terminal_putchar('^');terminal_putchar('C');terminal_putchar('\n');buf[0]='\0';return 0;}
        else if(k=='\b'){if(pos>0){pos--;buf[pos]='\0';terminal_putchar('\b');}}
        else if(k==KEY_UP){
            if(hist_pos>0&&hist_count>0){
                hist_pos--;clear_input_line(pos);
                kstrcpy(buf,history[hist_pos%HISTORY_MAX],max);
                pos=kstrlen(buf);terminal_write(buf);
            }
        }
        else if(k==KEY_DOWN){
            if(hist_pos<hist_count){
                hist_pos++;clear_input_line(pos);
                if(hist_pos==hist_count){buf[0]='\0';pos=0;}
                else{kstrcpy(buf,history[hist_pos%HISTORY_MAX],max);pos=kstrlen(buf);terminal_write(buf);}
            }
        }
        else if(k>0&&k<256&&pos<max-1){buf[pos++]=(char)k;buf[pos]='\0';terminal_putchar((char)k);}
    }
}

static void read_pass(char *buf,size_t max){
    size_t pos=0;buf[0]='\0';
    while(1){
        char c=keyboard_getchar();
        if(c=='\n'||c=='\r'){terminal_putchar('\n');buf[pos]='\0';return;}
        else if(c=='\b'){if(pos>0)pos--;}
        else if(pos<max-1)buf[pos++]=c;
    }
}

static int tokenise(char *line,char *argv[],int max){
    int argc=0;char *p=line;
    while(*p){
        while(*p==' '||*p=='\t')p++;if(!*p)break;if(argc>=max)break;
        argv[argc++]=p;while(*p&&*p!=' '&&*p!='\t')p++;if(*p)*p++='\0';
    }
    return argc;
}

static void print_prompt(void){
    stack_user_t *u=user_current();
    if(user_is_root())terminal_setcolor(VGA_COLOR_LIGHT_RED,VGA_COLOR_BLACK);
    else               terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    terminal_write(u?u->name:"guest");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY,VGA_COLOR_BLACK);terminal_write("@stackos:");
    terminal_setcolor(VGA_COLOR_LIGHT_BLUE,VGA_COLOR_BLACK);terminal_write(cwd);
    terminal_setcolor(user_is_root()?VGA_COLOR_LIGHT_RED:VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    terminal_write(user_is_root()?"# ":"$ ");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  COMMANDS
 *  ═══════════════════════════════════════════════════════════════════════════ */

/* sysinfo — was blizzinfo */
static void cmd_sysinfo(void){
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    kprintf("%s %s  arch=x86-32  kernel=monolithic\n",OS_NAME,VERSION);
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}
static void cmd_ver(void){kprintf("StackOS %s\n",VERSION);}
static void cmd_runtime(void){
    uint64_t t=timer_ticks();uint32_t s=(uint32_t)(t/100),m=s/60;s%=60;uint32_t h=m/60;m%=60;
    kprintf("Runtime: %02u:%02u:%02u\n",h,m,s);
}
static void cmd_ram(void){
    size_t ff=pmm_free_frames(),fk=ff*4;
    terminal_setcolor(VGA_COLOR_LIGHT_GREEN,VGA_COLOR_BLACK);
    kprintf("Free: %u KiB  %u MiB\n",(uint32_t)fk,(uint32_t)(fk/1024));
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}
static void cmd_cpu(void){terminal_writeline("CPU: x86 32-bit protected mode  Timer: PIT @ 100Hz");}
static void cmd_arch(void){terminal_writeline("x86  32-bit  little-endian");}
static void cmd_ticks(void){kprintf("%u ticks\n",(uint32_t)timer_ticks());}
static void cmd_cls(void){terminal_clear();}

static void cmd_banner(void){
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    terminal_writeline("  _____ _             _     ___  ____");
    terminal_writeline(" / ____| |           | |   / _ \\/ ___|");
    terminal_writeline("| (___ | |_ __ _  ___| | _| | | \\___ \\");
    terminal_writeline(" \\___ \\| __/ _` |/ __| |/ / | | |___) |");
    terminal_writeline(" ____) | || (_| | (__|   <| |_| |___/ /");
    terminal_writeline("|_____/ \\__\\__,_|\\___|_|\\_\\\\___/|____/");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}

static void cmd_color(int argc,char *argv[]){
    if(argc<3)return;
    terminal_setcolor((vga_color_t)katoi(argv[1]),(vga_color_t)katoi(argv[2]));
}
static void cmd_rainbow(void){
    const char *msg="  S T A C K O S  ";
    vga_color_t c[]={VGA_COLOR_LIGHT_RED,VGA_COLOR_LIGHT_BROWN,VGA_COLOR_LIGHT_GREEN,
        VGA_COLOR_LIGHT_CYAN,VGA_COLOR_LIGHT_BLUE,VGA_COLOR_LIGHT_MAGENTA};
        for(size_t i=0;msg[i];i++){terminal_setcolor(c[i%6],VGA_COLOR_BLACK);terminal_putchar(msg[i]);}
        terminal_putchar('\n');terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}
static void cmd_print(int argc,char *argv[]){
    for(int i=1;i<argc;i++){terminal_write(argv[i]);if(i<argc-1)terminal_putchar(' ');}
    terminal_putchar('\n');
}
static void cmd_say(int argc,char *argv[]){
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);cmd_print(argc,argv);
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}
static void cmd_shout(int argc,char *argv[]){
    terminal_setcolor(VGA_COLOR_LIGHT_RED,VGA_COLOR_BLACK);
    for(int i=1;i<argc;i++){
        for(const char *p=argv[i];*p;p++){char c=*p;if(c>='a'&&c<='z')c-=32;terminal_putchar(c);}
        if(i<argc-1)terminal_putchar(' ');
    }
    terminal_putchar('\n');terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}
static void cmd_repeat(int argc,char *argv[]){
    if(argc<3)return;
    uint32_t n=katoi(argv[1]);if(n>100)n=100;
    for(uint32_t i=0;i<n;i++){
        for(int j=2;j<argc;j++){terminal_write(argv[j]);if(j<argc-1)terminal_putchar(' ');}
        terminal_putchar('\n');
    }
}
static void cmd_count(int argc,char *argv[]){
    if(argc<3)return;
    int32_t a=(int32_t)katoi(argv[1]),b=(int32_t)katoi(argv[2]);
    for(int32_t i=a;i<=b;i++)kprintf("%d ",i);terminal_putchar('\n');
}
static void cmd_calc(int argc,char *argv[]){
    if(argc<4)return;
    int32_t a=(int32_t)katoi(argv[1]),b=(int32_t)katoi(argv[3]);char op=argv[2][0];int32_t r=0;
    if(op=='+')r=a+b;else if(op=='-')r=a-b;else if(op=='*')r=a*b;
    else if(op=='/'){if(!b){terminal_writeline("div by zero");return;}r=a/b;}
    else{terminal_writeline("ops: + - * /");return;}
    kprintf("%d %c %d = %d\n",a,op,b,r);
}
static void cmd_hex(int argc,char *argv[]){if(argc<2)return;kprintf("0x%x\n",katoi(argv[1]));}
static void cmd_dec(int argc,char *argv[]){
    if(argc<2)return;uint32_t v=0;const char *s=argv[1];
    if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;
    while(*s){v<<=4;
        if(*s>='0'&&*s<='9')v|=(uint32_t)(*s-'0');
        else if(*s>='a'&&*s<='f')v|=(uint32_t)(*s-'a'+10);
        else if(*s>='A'&&*s<='F')v|=(uint32_t)(*s-'A'+10);
        s++;}kprintf("%u\n",v);
}
static void cmd_flip(int argc,char *argv[]){
    for(int i=1;i<argc;i++){size_t l=kstrlen(argv[i]);for(size_t j=l;j>0;j--)terminal_putchar(argv[i][j-1]);terminal_putchar(' ');}
    terminal_putchar('\n');
}
static void cmd_upper(int argc,char *argv[]){
    for(int i=1;i<argc;i++){for(const char *p=argv[i];*p;p++){char c=*p;if(c>='a'&&c<='z')c-=32;terminal_putchar(c);}if(i<argc-1)terminal_putchar(' ');}
    terminal_putchar('\n');
}
static void cmd_lower(int argc,char *argv[]){
    for(int i=1;i<argc;i++){for(const char *p=argv[i];*p;p++){char c=*p;if(c>='A'&&c<='Z')c+=32;terminal_putchar(c);}if(i<argc-1)terminal_putchar(' ');}
    terminal_putchar('\n');
}

/* ── Filesystem ──────────────────────────────────────────────────────────── */
static void cmd_ls(int argc,char *argv[]){
    const char *path=(argc>=2)?argv[1]:cwd;
    vfs_node_t *d=resolve(path);
    if(!d||!(d->flags&VFS_DIR)){kprintf("ls: '%s': not a directory\n",path);vnode_free(d);return;}
    uint32_t idx=0,files=0,dirs=0;dirent_t *de;
    while((de=vfs_readdir(d,idx++))){
        char nc[VFS_NAME_MAX];kstrcpy(nc,de->name,VFS_NAME_MAX);
        vfs_node_t *ch=vfs_finddir(d,nc);
        if(ch&&(ch->flags&VFS_DIR)){
            terminal_setcolor(VGA_COLOR_LIGHT_BLUE,VGA_COLOR_BLACK);
            kprintf("  [DIR]  %s/\n",nc);dirs++;
        }else{
            terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
            kprintf("  [FILE] %s",nc);if(ch)kprintf("  (%u B)",ch->size);
            terminal_putchar('\n');files++;
        }
        vnode_free(ch);
    }
    terminal_setcolor(VGA_COLOR_LIGHT_GREY,VGA_COLOR_BLACK);
    kprintf("  %u file(s)  %u dir(s)\n",files,dirs);
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    vnode_free(d);
}
static void cmd_cat(int argc,char *argv[]){
    if(argc<2)return;
    vfs_node_t *f=resolve(argv[1]);
    if(!f||(f->flags&VFS_DIR)){kprintf("cat: '%s': not a file\n",argv[1]);vnode_free(f);return;}
    uint8_t buf[128];uint32_t off=0;int32_t nr;
    while((nr=vfs_read(f,off,sizeof(buf),buf))>0){
        for(int32_t i=0;i<nr;i++)terminal_putchar((char)buf[i]);off+=(uint32_t)nr;}
        terminal_putchar('\n');vnode_free(f);
}
static void cmd_cd(int argc,char *argv[]){
    const char *path=(argc>=2)?argv[1]:"/";
    vfs_node_t *d=resolve(path);
    if(!d||!(d->flags&VFS_DIR)){kprintf("cd: '%s': not found\n",path);vnode_free(d);return;}
    vnode_free(d);
    if(path[0]=='/')kstrcpy(cwd,path,VFS_PATH_MAX);
    else{size_t cl=kstrlen(cwd);if(cwd[cl-1]!='/'){cwd[cl]='/';cwd[cl+1]='\0';cl++;}kstrcpy(cwd+cl,path,VFS_PATH_MAX-cl);}
    size_t len=kstrlen(cwd);if(len>1&&cwd[len-1]=='/')cwd[len-1]='\0';
}
static void cmd_pwd(void){kprintf("%s\n",cwd);}
static void cmd_back(void){
    size_t l=kstrlen(cwd);if(l<=1)return;
    for(size_t i=l-1;i>0;i--){if(cwd[i]=='/'){cwd[i]='\0';break;}}
    if(!kstrlen(cwd)){cwd[0]='/';cwd[1]='\0';}
}
static void cmd_stat(int argc,char *argv[]){
    if(argc<2)return;vfs_node_t *n=resolve(argv[1]);
    if(!n){kprintf("stat: '%s': not found\n",argv[1]);return;}
    kprintf("Name: %s\nType: %s\nSize: %u bytes\nInode: %u\n",
            n->name,(n->flags&VFS_DIR)?"directory":"file",n->size,n->inode);
    vnode_free(n);
}
static void cmd_tree(int argc,char *argv[]){
    const char *path=(argc>=2)?argv[1]:cwd;
    vfs_node_t *d=resolve(path);
    if(!d||!(d->flags&VFS_DIR)){kprintf("tree: not a dir\n");vnode_free(d);return;}
    kprintf("%s\n",path);uint32_t idx=0;dirent_t *de;
    while((de=vfs_readdir(d,idx++))){
        char nc[VFS_NAME_MAX];kstrcpy(nc,de->name,VFS_NAME_MAX);
        vfs_node_t *ch=vfs_finddir(d,nc);
        if(ch&&(ch->flags&VFS_DIR)){terminal_setcolor(VGA_COLOR_LIGHT_BLUE,VGA_COLOR_BLACK);kprintf("  +-- %s/\n",nc);}
        else{terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);kprintf("  |-- %s\n",nc);}
        vnode_free(ch);
    }
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);vnode_free(d);
}
static void cmd_touch(int argc,char *argv[]){
    if(argc<2)return;const char *name=NULL;
    vfs_node_t *d=resolve_dir_and_name(argv[1],&name);
    if(!d||!(d->flags&VFS_DIR)){kprintf("touch: dir not found\n");vnode_free(d);return;}
    if(!d->ops||!d->ops->create){terminal_writeline("read-only");vnode_free(d);return;}
    if(d->ops->create(d,name,0)==0)kprintf("created: %s\n",argv[1]);
    vnode_free(d);
}
static void cmd_mkdir(int argc,char *argv[]){
    if(argc<2)return;const char *name=NULL;
    vfs_node_t *d=resolve_dir_and_name(argv[1],&name);
    if(!d||!(d->flags&VFS_DIR)){kprintf("mkdir: dir not found\n");vnode_free(d);return;}
    if(!d->ops||!d->ops->mkdir){terminal_writeline("read-only");vnode_free(d);return;}
    if(d->ops->mkdir(d,name)==0)kprintf("created: %s/\n",argv[1]);
    vnode_free(d);
}
static void cmd_rm(int argc,char *argv[]){
    if(argc<2)return;const char *name=NULL;
    vfs_node_t *d=resolve_dir_and_name(argv[1],&name);
    if(!d||!d->ops||!d->ops->unlink){terminal_writeline("rm: not found");vnode_free(d);return;}
    if(d->ops->unlink(d,name)==0)kprintf("deleted: %s\n",argv[1]);
    else kprintf("rm: '%s' not found\n",argv[1]);
    vnode_free(d);
}
static void cmd_write(int argc,char *argv[]){
    if(argc<3)return;const char *name=NULL;
    {vfs_node_t *d=resolve_dir_and_name(argv[1],&name);
        if(!d||!(d->flags&VFS_DIR)){kprintf("write: dir not found\n");vnode_free(d);return;}
        vfs_node_t *ex=vfs_finddir(d,name);
        if(!ex){if(!d->ops||!d->ops->create){terminal_writeline("read-only");vnode_free(d);return;}
        d->ops->create(d,name,0);}
        else vnode_free(ex);vnode_free(d);}
        const char *name2=NULL;
    vfs_node_t *d2=resolve_dir_and_name(argv[1],&name2);if(!d2)return;
    vfs_node_t *f=vfs_finddir(d2,name2);vnode_free(d2);if(!f)return;
    char buf[512];size_t pos=0;
    for(int i=2;i<argc&&pos<510;i++){const char *p=argv[i];while(*p&&pos<510)buf[pos++]=*p++;if(i<argc-1&&pos<510)buf[pos++]=' ';}
    buf[pos++]='\n';
    int32_t wr=vfs_write(f,0,(uint32_t)pos,(const uint8_t*)buf);
    if(wr>0)kprintf("written %u bytes to %s\n",(uint32_t)wr,argv[1]);
    vnode_free(f);
}
static void cmd_append(int argc,char *argv[]){
    if(argc<3)return;const char *name=NULL;
    {vfs_node_t *d=resolve_dir_and_name(argv[1],&name);
        if(!d||!(d->flags&VFS_DIR)){vnode_free(d);return;}
        vfs_node_t *ex=vfs_finddir(d,name);
        if(!ex){if(!d->ops||!d->ops->create){vnode_free(d);return;}d->ops->create(d,name,0);}
        else vnode_free(ex);vnode_free(d);}
        const char *name2=NULL;
    vfs_node_t *d2=resolve_dir_and_name(argv[1],&name2);if(!d2)return;
    vfs_node_t *f=vfs_finddir(d2,name2);vnode_free(d2);if(!f)return;
    char buf[512];size_t pos=0;
    for(int i=2;i<argc&&pos<510;i++){const char *p=argv[i];while(*p&&pos<510)buf[pos++]=*p++;if(i<argc-1&&pos<510)buf[pos++]=' ';}
    buf[pos++]='\n';
    vfs_write(f,f->size,(uint32_t)pos,(const uint8_t*)buf);
    kprintf("appended %u bytes\n",(uint32_t)pos);vnode_free(f);
}
static void cmd_cp(int argc,char *argv[]){
    if(argc<3)return;vfs_node_t *src=resolve(argv[1]);
    if(!src||(src->flags&VFS_DIR)){kprintf("cp: '%s' not found\n",argv[1]);vnode_free(src);return;}
    uint8_t cb[4096];uint32_t total=0;
    {uint8_t tmp[512];int32_t nr;
        while((nr=vfs_read(src,total,sizeof(tmp),tmp))>0){
            if(total+(uint32_t)nr>sizeof(cb))nr=(int32_t)(sizeof(cb)-total);
            for(int32_t i=0;i<nr;i++)cb[total+(uint32_t)i]=tmp[i];
            total+=(uint32_t)nr;if(total>=sizeof(cb))break;}}
            vnode_free(src);
            const char *dn=NULL;
            {vfs_node_t *d=resolve_dir_and_name(argv[2],&dn);
                if(!d||!d->ops||!d->ops->create){vnode_free(d);return;}
                d->ops->create(d,dn,0);vnode_free(d);}
                const char *dn2=NULL;vfs_node_t *d2=resolve_dir_and_name(argv[2],&dn2);if(!d2)return;
                vfs_node_t *dst=vfs_finddir(d2,dn2);vnode_free(d2);if(!dst)return;
                vfs_write(dst,0,total,cb);kprintf("copied %u bytes\n",total);vnode_free(dst);
}
static void cmd_wc(int argc,char *argv[]){
    if(argc<2)return;vfs_node_t *f=resolve(argv[1]);
    if(!f||(f->flags&VFS_DIR)){vnode_free(f);return;}
    uint8_t buf[128];uint32_t off=0,ch=0,words=0,lines=0;int32_t nr;int iw=0;
    while((nr=vfs_read(f,off,sizeof(buf),buf))>0){
        for(int32_t i=0;i<nr;i++){ch++;if(buf[i]=='\n')lines++;if(buf[i]==' '||buf[i]=='\n'||buf[i]=='\t')iw=0;else if(!iw){words++;iw=1;}}
        off+=(uint32_t)nr;}
        kprintf("lines=%u words=%u chars=%u\n",lines,words,ch);vnode_free(f);
}
static void cmd_head(int argc,char *argv[]){
    if(argc<2)return;vfs_node_t *f=resolve(argv[1]);
    if(!f||(f->flags&VFS_DIR)){vnode_free(f);return;}
    uint32_t max=(argc>=3)?katoi(argv[2]):5;
    uint8_t buf[128];uint32_t off=0,ln=0;int32_t nr;
    while((nr=vfs_read(f,off,sizeof(buf),buf))>0&&ln<max){
        for(int32_t i=0;i<nr&&ln<max;i++){terminal_putchar((char)buf[i]);if(buf[i]=='\n')ln++;}
        off+=(uint32_t)nr;}
        vnode_free(f);
}

/* ── Processes ───────────────────────────────────────────────────────────── */
static void cmd_ps(void){proc_list();}
static void cmd_sleep(int argc,char *argv[]){if(argc<2)return;proc_sleep(katoi(argv[1]));terminal_writeline("done.");}
static void cmd_yield(void){proc_yield();}

/* ── Users ───────────────────────────────────────────────────────────────── */
static void cmd_whoami(void){
    stack_user_t *u=user_current();
    if(!u){terminal_writeline("not logged in");return;}
    kprintf("%s  [%s]\n",u->name,u->priv==PRIV_ROOT?"root/admin":"user");
}

/* elevate — was frostup. Password: admin */
static void cmd_elevate(void){
    terminal_write("root password: ");char pass[USER_PASS_MAX];read_pass(pass,USER_PASS_MAX);
    if(user_login("root",pass)==0){
        terminal_setcolor(VGA_COLOR_LIGHT_RED,VGA_COLOR_BLACK);
        terminal_writeline("Elevated to root mode.");
        terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    }else{
        terminal_setcolor(VGA_COLOR_LIGHT_RED,VGA_COLOR_BLACK);
        terminal_writeline("Wrong password.");
        terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    }
}

/* frostdown — was lower */
static void cmd_frostdown(void){user_login("stack","stack");terminal_writeline("Back to normal user.");}

static void cmd_login(void){
    char name[USER_NAME_MAX],pass[USER_PASS_MAX];
    terminal_write("username: ");read_line(name,USER_NAME_MAX);
    terminal_write("password: ");read_pass(pass,USER_PASS_MAX);
    if(user_login(name,pass)==0){
        terminal_setcolor(VGA_COLOR_LIGHT_GREEN,VGA_COLOR_BLACK);
        kprintf("Welcome, %s!\n",name);
        terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    }else{
        terminal_setcolor(VGA_COLOR_LIGHT_RED,VGA_COLOR_BLACK);
        terminal_writeline("Login failed.");
        terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    }
}
static void cmd_logout(void){user_logout();terminal_writeline("Logged out.");}
static void cmd_users(void){user_list();}

/* useradd — was adduser */
static void cmd_useradd(int argc,char *argv[]){
    NEED_ROOT();if(argc<3){terminal_writeline("usage: useradd <name> <pass>");return;}
    if(user_add(argv[1],argv[2],PRIV_USER)==0)kprintf("user '%s' created.\n",argv[1]);
    else terminal_writeline("failed.");
}

/* userdel — was deluser */
static void cmd_userdel(int argc,char *argv[]){
    NEED_ROOT();if(argc<2)return;
    if(kstrcmp(argv[1],"root")==0||kstrcmp(argv[1],"stack")==0){
        terminal_writeline("Cannot delete built-in accounts.");return;}
        if(user_del(argv[1])==0)kprintf("user '%s' deleted.\n",argv[1]);
        else kprintf("userdel: '%s' not found\n",argv[1]);
}

/* passwd — was chpass */
static void cmd_passwd(int argc,char *argv[]){
    NEED_ROOT();if(argc<3)return;
    if(user_chpass(argv[1],argv[2])==0)kprintf("password changed for '%s'.\n",argv[1]);
    else kprintf("passwd: '%s' not found\n",argv[1]);
}

/* ── Power ───────────────────────────────────────────────────────────────── */
static void cmd_shutdown(void){
    NEED_ROOT();terminal_writeline("System halted.");
    __asm__ volatile("cli");for(;;)__asm__ volatile("hlt");
}
static void cmd_reboot(void){
    NEED_ROOT();terminal_writeline("Rebooting...");
    __asm__ volatile("ljmp $0xFFFF, $0x0000");
}

/* ── Diagnostics ─────────────────────────────────────────────────────────── */
static void cmd_stack_reg(void){uint32_t esp;__asm__ volatile("mov %%esp,%0":"=r"(esp));kprintf("ESP: 0x%x\n",esp);}
static void cmd_eflags(void){uint32_t fl;__asm__ volatile("pushf;pop %0":"=r"(fl));kprintf("EFLAGS: 0x%x\n",fl);}
static void cmd_cr0(void){NEED_ROOT();uint32_t v;__asm__ volatile("mov %%cr0,%0":"=r"(v));kprintf("CR0=0x%x\n",v);}
static void cmd_cr3(void){NEED_ROOT();uint32_t v;__asm__ volatile("mov %%cr3,%0":"=r"(v));kprintf("CR3=0x%x\n",v);}
static void cmd_history(void){
    if(!hist_count){terminal_writeline("No history.");return;}
    int s=(hist_count>HISTORY_MAX)?hist_count-HISTORY_MAX:0;
    for(int i=s;i<hist_count;i++)kprintf("  %3d  %s\n",i+1,history[i%HISTORY_MAX]);
}

/* ── Fun ─────────────────────────────────────────────────────────────────── */
static void cmd_hi(void){stack_user_t *u=user_current();kprintf("Hello, %s!\n",u?u->name:"stranger");}
static void cmd_os(void){
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    terminal_writeline("StackOS - built 100% from scratch.");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}
static void cmd_credits(void){terminal_writeline("  StackOS — built by Jahanzaib");}

/* stackquote — was blizzquote */
static void cmd_stackquote(void){
    static uint32_t i=0;
    const char *q[]={"In ring 0, no one can hear you segfault.",
        "No glibc. No mercy.",
        "Real OSes write their own kmalloc.",
        "A triple fault is just the CPU saying hello.",
        "0xB8000: where your text lives.",
        "StackOS: because someone had to build it."};
        kprintf("\"%s\"\n",q[(i++)%6]);
}
static void cmd_panic_test(void){
    NEED_ROOT();
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLUE);terminal_clear();
    terminal_writeline("  *** StackOS KERNEL PANIC (user-triggered) ***");
    for(;;)__asm__ volatile("cli;hlt");
}

/* ── Disk — drives/format/mount (was diskinfo/diskformat/diskmount) ───────── */
static void cmd_drives(void){
    if(!ata_drive.present){terminal_writeline("No disk.");return;}
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    kprintf("Model  : %s\nPos    : %s\nSectors: %u (%u MiB)\nStackFS: %s\n",
            ata_drive.model,ata_drive.position,ata_drive.sectors,
            (ata_drive.sectors/2)/1024,stackfs_detect()?"detected":"not formatted");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}
static void cmd_format(void){
    NEED_ROOT();if(!ata_drive.present){terminal_writeline("No disk.");return;}
    terminal_write("Format disk? All data lost. (yes/no): ");
    char line[16];read_line(line,16);
    if(kstrcmp(line,"yes")!=0){terminal_writeline("Cancelled.");return;}
    if(stackfs_format("StackOS")==0){
        vfs_node_t *r=stackfs_mount();
        if(r){vfs_mount("/disk",r);terminal_writeline("Formatted and mounted.");}
    }else terminal_writeline("Format failed.");
}
static void cmd_mount(void){
    if(!ata_drive.present){terminal_writeline("No disk.");return;}
    if(!stackfs_detect()){terminal_writeline("No StackFS. Run 'format' first.");return;}
    vfs_node_t *r=stackfs_mount();
    if(r){vfs_mount("/disk",r);terminal_writeline("Mounted at /disk.");}
    else terminal_writeline("Mount failed.");
}

/* ── Network ─────────────────────────────────────────────────────────────── */
static void cmd_netinfo(void){
    mac_addr_t mac;e1000_get_mac(&mac);
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    kprintf("IP  : %u.%u.%u.%u\n",(net_ip>>24)&0xFF,(net_ip>>16)&0xFF,(net_ip>>8)&0xFF,net_ip&0xFF);
    kprintf("GW  : %u.%u.%u.%u\n",(net_gw>>24)&0xFF,(net_gw>>16)&0xFF,(net_gw>>8)&0xFF,net_gw&0xFF);
    kprintf("Mask: %u.%u.%u.%u\n",(net_mask>>24)&0xFF,(net_mask>>16)&0xFF,(net_mask>>8)&0xFF,net_mask&0xFF);
    kprintf("DNS : %u.%u.%u.%u\n",(net_dns>>24)&0xFF,(net_dns>>16)&0xFF,(net_dns>>8)&0xFF,net_dns&0xFF);
    kprintf("MAC : %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac.addr[0],mac.addr[1],mac.addr[2],mac.addr[3],mac.addr[4],mac.addr[5]);
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}
static void cmd_ping(int argc,char *argv[]){
    if(argc<2){terminal_writeline("usage: ping <ip>");return;}
    const char *s=argv[1];uint32_t p[4]={0,0,0,0};int part=0;
    while(*s&&part<4){if(*s=='.'){part++;s++;continue;}if(*s>='0'&&*s<='9')p[part]=p[part]*10+(uint32_t)(*s-'0');s++;}
    ip4_addr_t dst=IP4(p[0],p[1],p[2],p[3]);
    kprintf("PING %u.%u.%u.%u\n",p[0],p[1],p[2],p[3]);
    for(uint16_t i=1;i<=4;i++){icmp_send_ping(dst,0x5374,i);uint64_t st=timer_ticks();while(timer_ticks()-st<20);}
    terminal_writeline("Done. Check serial for pong replies.");
}
static void cmd_dhcp(void){
    kprintf("Requesting IP via DHCP...\n");
    if(dhcp_request())kprintf("Done. IP: %u.%u.%u.%u\n",(net_ip>>24)&0xFF,(net_ip>>16)&0xFF,(net_ip>>8)&0xFF,net_ip&0xFF);
    else kprintf("DHCP failed.\n");
}

/* ── Help ────────────────────────────────────────────────────────────────── */
static void cmd_help(void){
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    terminal_writeline("=== StackOS Commands ===");
    terminal_setcolor(VGA_COLOR_LIGHT_BROWN,VGA_COLOR_BLACK);terminal_writeline("-- SYSTEM --");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    terminal_writeline("  sysinfo  ver  runtime  ram  cpu  arch  ticks  cls  banner  color");
    terminal_setcolor(VGA_COLOR_LIGHT_BROWN,VGA_COLOR_BLACK);terminal_writeline("-- FILES --");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    terminal_writeline("  ls  cat  write  append  touch  mkdir  rm  cp  wc  head");
    terminal_writeline("  cd  pwd  back  stat  tree");
    terminal_setcolor(VGA_COLOR_LIGHT_BROWN,VGA_COLOR_BLACK);terminal_writeline("-- NETWORK --");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    terminal_writeline("  netinfo  ping <ip>  dhcp");
    terminal_setcolor(VGA_COLOR_LIGHT_BROWN,VGA_COLOR_BLACK);terminal_writeline("-- DISK --");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    terminal_writeline("  drives  format  mount");
    terminal_setcolor(VGA_COLOR_LIGHT_BROWN,VGA_COLOR_BLACK);terminal_writeline("-- USERS --");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    terminal_writeline("  whoami  login  logout  users  useradd  userdel  passwd");
    terminal_writeline("  elevate  lower");
    terminal_setcolor(VGA_COLOR_LIGHT_BROWN,VGA_COLOR_BLACK);terminal_writeline("-- SHELL --");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    terminal_writeline("  history  echo  say  shout  repeat  count  calc  hex  dec");
    terminal_writeline("  flip  upper  lower  rainbow  hi  os  credits  stackquote");
    terminal_setcolor(VGA_COLOR_LIGHT_BROWN,VGA_COLOR_BLACK);terminal_writeline("-- POWER/DIAG [root] --");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    terminal_writeline("  shutdown  reboot  panic  stack  eflags  cr0  cr3  ps  sleep");
    terminal_setcolor(VGA_COLOR_LIGHT_GREY,VGA_COLOR_BLACK);
    terminal_writeline("\nUp/Down=history  Ctrl+L=clear  Ctrl+C=cancel");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  DISPATCH
 *  ═══════════════════════════════════════════════════════════════════════════ */
static void dispatch(int argc,char *argv[]){
    const char *c=argv[0];
    if(!kstrcmp(c,"sysinfo")||!kstrcmp(c,"uname"))  cmd_sysinfo();
    else if(!kstrcmp(c,"ver"))        cmd_ver();
    else if(!kstrcmp(c,"runtime")||!kstrcmp(c,"uptime"))cmd_runtime();
    else if(!kstrcmp(c,"ram")||!kstrcmp(c,"mem"))   cmd_ram();
    else if(!kstrcmp(c,"cpu"))        cmd_cpu();
    else if(!kstrcmp(c,"arch"))       cmd_arch();
    else if(!kstrcmp(c,"ticks"))      cmd_ticks();
    else if(!kstrcmp(c,"cls")||!kstrcmp(c,"clear")) cmd_cls();
    else if(!kstrcmp(c,"banner"))     cmd_banner();
    else if(!kstrcmp(c,"color"))      cmd_color(argc,argv);
    else if(!kstrcmp(c,"rainbow"))    cmd_rainbow();
    else if(!kstrcmp(c,"echo")||!kstrcmp(c,"print"))cmd_print(argc,argv);
    else if(!kstrcmp(c,"say"))        cmd_say(argc,argv);
    else if(!kstrcmp(c,"shout"))      cmd_shout(argc,argv);
    else if(!kstrcmp(c,"repeat"))     cmd_repeat(argc,argv);
    else if(!kstrcmp(c,"count"))      cmd_count(argc,argv);
    else if(!kstrcmp(c,"calc"))       cmd_calc(argc,argv);
    else if(!kstrcmp(c,"hex"))        cmd_hex(argc,argv);
    else if(!kstrcmp(c,"dec"))        cmd_dec(argc,argv);
    else if(!kstrcmp(c,"flip"))       cmd_flip(argc,argv);
    else if(!kstrcmp(c,"upper"))      cmd_upper(argc,argv);
    else if(!kstrcmp(c,"lower2")||!kstrcmp(c,"tolower"))cmd_lower(argc,argv);
    else if(!kstrcmp(c,"ls")||!kstrcmp(c,"dir"))    cmd_ls(argc,argv);
    else if(!kstrcmp(c,"cat")||!kstrcmp(c,"read"))  cmd_cat(argc,argv);
    else if(!kstrcmp(c,"cd")||!kstrcmp(c,"goto"))   cmd_cd(argc,argv);
    else if(!kstrcmp(c,"pwd")||!kstrcmp(c,"where")) cmd_pwd();
    else if(!kstrcmp(c,"back"))       cmd_back();
    else if(!kstrcmp(c,"stat")||!kstrcmp(c,"info")) cmd_stat(argc,argv);
    else if(!kstrcmp(c,"tree"))       cmd_tree(argc,argv);
    else if(!kstrcmp(c,"touch"))      cmd_touch(argc,argv);
    else if(!kstrcmp(c,"mkdir"))      cmd_mkdir(argc,argv);
    else if(!kstrcmp(c,"rm")||!kstrcmp(c,"del"))    cmd_rm(argc,argv);
    else if(!kstrcmp(c,"write"))      cmd_write(argc,argv);
    else if(!kstrcmp(c,"append"))     cmd_append(argc,argv);
    else if(!kstrcmp(c,"cp"))         cmd_cp(argc,argv);
    else if(!kstrcmp(c,"wc"))         cmd_wc(argc,argv);
    else if(!kstrcmp(c,"head"))       cmd_head(argc,argv);
    else if(!kstrcmp(c,"ps")||!kstrcmp(c,"procs"))  cmd_ps();
    else if(!kstrcmp(c,"sleep")||!kstrcmp(c,"wait"))cmd_sleep(argc,argv);
    else if(!kstrcmp(c,"yield")||!kstrcmp(c,"give"))cmd_yield();
    else if(!kstrcmp(c,"whoami"))     cmd_whoami();
    else if(!kstrcmp(c,"login"))      cmd_login();
    else if(!kstrcmp(c,"logout"))     cmd_logout();
    else if(!kstrcmp(c,"users"))      cmd_users();
    else if(!kstrcmp(c,"useradd"))    cmd_useradd(argc,argv);
    else if(!kstrcmp(c,"userdel"))    cmd_userdel(argc,argv);
    else if(!kstrcmp(c,"passwd"))     cmd_passwd(argc,argv);
    else if(!kstrcmp(c,"elevate"))    cmd_elevate();
    else if(!kstrcmp(c,"lower"))      cmd_frostdown();
    else if(!kstrcmp(c,"shutdown")||!kstrcmp(c,"halt"))cmd_shutdown();
    else if(!kstrcmp(c,"reboot")||!kstrcmp(c,"restart"))cmd_reboot();
    else if(!kstrcmp(c,"panic"))      cmd_panic_test();
    else if(!kstrcmp(c,"stack"))      cmd_stack_reg();
    else if(!kstrcmp(c,"eflags"))     cmd_eflags();
    else if(!kstrcmp(c,"cr0"))        cmd_cr0();
    else if(!kstrcmp(c,"cr3"))        cmd_cr3();
    else if(!kstrcmp(c,"history"))    cmd_history();
    else if(!kstrcmp(c,"hi"))         cmd_hi();
    else if(!kstrcmp(c,"os"))         cmd_os();
    else if(!kstrcmp(c,"credits"))    cmd_credits();
    else if(!kstrcmp(c,"stackquote")) cmd_stackquote();
    else if(!kstrcmp(c,"drives"))     cmd_drives();
    else if(!kstrcmp(c,"format"))     cmd_format();
    else if(!kstrcmp(c,"mount"))      cmd_mount();
    else if(!kstrcmp(c,"netinfo"))    cmd_netinfo();
    else if(!kstrcmp(c,"ping"))       cmd_ping(argc,argv);
    else if(!kstrcmp(c,"dhcp"))       cmd_dhcp();
    else if(!kstrcmp(c,"help"))       cmd_help();
    else{
        terminal_setcolor(VGA_COLOR_LIGHT_RED,VGA_COLOR_BLACK);
        kprintf("unknown command: %s  (type 'help')\n",c);
        terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    }
}

static void show_banner(void){
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    terminal_writeline("  _____ _             _     ___  ____");
    terminal_writeline(" / ____| |           | |   / _ \\/ ___|");
    terminal_writeline("| (___ | |_ __ _  ___| | _| | | \\___ \\");
    terminal_writeline(" \\___ \\| __/ _` |/ __| |/ / | | |___) |");
    terminal_writeline(" ____) | || (_| | (__|   <| |_| |___/ /");
    terminal_writeline("|_____/ \\__\\__,_|\\___|_|\\_\\\\___/|____/");
    terminal_putchar('\n');
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    kprintf("  StackOS %s  |  type 'help'\n",VERSION);
    terminal_setcolor(VGA_COLOR_LIGHT_GREY,VGA_COLOR_BLACK);
    if(ata_drive.present)kprintf("  Disk: /disk (%s)\n",ata_drive.position);
    terminal_putchar('\n');
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
}

void shell_run(void){
    show_banner();
    user_login("stack","stack");  /* default login as stack user */
    char line[CMD_BUF];char *argv[MAX_ARGS];
    while(1){
        print_prompt();
        if(!read_line(line,CMD_BUF))continue;
        if(!line[0])continue;
        hist_push(line);
        int argc=tokenise(line,argv,MAX_ARGS);
        if(argc)dispatch(argc,argv);
    }
}
