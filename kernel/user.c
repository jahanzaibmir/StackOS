/* =============================================================================
   StackOS — kernel/user.c
   User management — persistent via /disk/etc/passwd.
   Default users: root/admin (superuser), stack/stack (normal user)
   ============================================================================= */
#include "user.h"
#include "kprintf.h"
#include "drivers/vga.h"
#include "fs/vfs.h"
#include "mm/heap.h"
#include <stddef.h>

static stack_user_t users[USER_MAX];
static int          user_count   = 0;
static int          current_user = -1;

static int ustreq(const char *a,const char *b){
    while(*a&&*a==*b){a++;b++;}return *a=='\0'&&*b=='\0';
}
static void ustrcpy(char *d,const char *s,int max){
    int i=0;while(i<max-1&&s[i]){d[i]=s[i];i++;}d[i]='\0';
}

void users_save(void){
    char buf[USER_MAX*(USER_NAME_MAX+USER_PASS_MAX+8)];
    int pos=0;
    for(int i=0;i<user_count;i++){
        if(!users[i].active)continue;
        const char *n=users[i].name;while(*n)buf[pos++]=*n++;
        buf[pos++]=':';
        const char *p=users[i].pass;while(*p)buf[pos++]=*p++;
        buf[pos++]=':';
        buf[pos++]=(char)('0'+users[i].priv);
        buf[pos++]='\n';
    }
    if(pos==0)return;
    vfs_node_t *etc=vfs_open("/disk/etc");
    if(!etc){
        vfs_node_t *disk=vfs_open("/disk");
        if(!disk||!disk->ops||!disk->ops->mkdir)return;
        disk->ops->mkdir(disk,"etc");
        etc=vfs_open("/disk/etc");
        if(!etc)return;
    }
    vfs_node_t *f=vfs_finddir(etc,"passwd");
    if(!f){
        if(!etc->ops||!etc->ops->create)return;
        etc->ops->create(etc,"passwd",0);
        etc=vfs_open("/disk/etc");
        if(!etc)return;
        f=vfs_finddir(etc,"passwd");
    }
    if(!f){kprintf("[user] ERROR: cannot create passwd\n");return;}
    int32_t wr=vfs_write(f,0,(uint32_t)pos,(const uint8_t*)buf);
    kfree(f);
    if(wr>0)kprintf("[user] saved %d user(s) to disk\n",user_count);
    else    kprintf("[user] ERROR: passwd write failed\n");
}

static int users_load(void){
    vfs_node_t *f=vfs_open("/disk/etc/passwd");
    if(!f){kprintf("[user] no passwd file\n");return 0;}
    if(f->size==0){kfree(f);return 0;}
    uint8_t *buf=(uint8_t*)kmalloc(f->size+1);
    if(!buf){kfree(f);return 0;}
    int32_t nr=vfs_read(f,0,f->size,buf);
    kfree(f);
    if(nr<=0){kfree(buf);return 0;}
    buf[nr]='\0';
    user_count=0;
    char *p=(char*)buf;
    while(*p&&user_count<USER_MAX){
        char name[USER_NAME_MAX]={0};int ni=0;
        while(*p&&*p!=':'&&*p!='\n'&&ni<USER_NAME_MAX-1)name[ni++]=*p++;
        if(*p!=':'){while(*p&&*p!='\n')p++;if(*p)p++;continue;}p++;
        char pass[USER_PASS_MAX]={0};int pi=0;
        while(*p&&*p!=':'&&*p!='\n'&&pi<USER_PASS_MAX-1)pass[pi++]=*p++;
        if(*p!=':'){while(*p&&*p!='\n')p++;if(*p)p++;continue;}p++;
        uint8_t priv=0;
        if(*p>='0'&&*p<='9')priv=(uint8_t)(*p-'0');
        while(*p&&*p!='\n')p++;if(*p=='\n')p++;
        if(!name[0])continue;
        ustrcpy(users[user_count].name,name,USER_NAME_MAX);
        ustrcpy(users[user_count].pass,pass,USER_PASS_MAX);
        users[user_count].priv=priv;users[user_count].active=1;
        user_count++;
    }
    kfree(buf);
    kprintf("[user] loaded %d user(s) from disk\n",user_count);
    return user_count;
}

void user_init(void){
    user_count=0;current_user=-1;
    if(users_load()>0)return;
    /* defaults — root/admin and stack/stack */
    ustrcpy(users[0].name,"root",USER_NAME_MAX);
    ustrcpy(users[0].pass,"admin",USER_PASS_MAX);
    users[0].priv=PRIV_ROOT;users[0].active=1;
    ustrcpy(users[1].name,"stack",USER_NAME_MAX);
    ustrcpy(users[1].pass,"stack",USER_PASS_MAX);
    users[1].priv=PRIV_USER;users[1].active=1;
    user_count=2;
    kprintf("[user] default accounts created\n");
}

int user_add(const char *name,const char *pass,uint8_t priv){
    if(user_count>=USER_MAX)return -1;
    ustrcpy(users[user_count].name,name,USER_NAME_MAX);
    ustrcpy(users[user_count].pass,pass,USER_PASS_MAX);
    users[user_count].priv=priv;users[user_count].active=1;
    user_count++;users_save();return 0;
}
int user_del(const char *name){
    for(int i=0;i<user_count;i++){
        if(users[i].active&&ustreq(users[i].name,name)){
            users[i].active=0;users_save();return 0;}
    }
    return -1;
}
int user_chpass(const char *name,const char *newpass){
    for(int i=0;i<user_count;i++){
        if(users[i].active&&ustreq(users[i].name,name)){
            ustrcpy(users[i].pass,newpass,USER_PASS_MAX);
            users_save();return 0;}
    }
    return -1;
}
int user_login(const char *name,const char *pass){
    for(int i=0;i<user_count;i++){
        if(!users[i].active)continue;
        if(ustreq(users[i].name,name)&&ustreq(users[i].pass,pass)){
            current_user=i;return 0;}
    }
    return -1;
}
void user_logout(void){current_user=-1;}
stack_user_t *user_current(void){
    if(current_user<0)return NULL;return &users[current_user];
}
int user_is_root(void){
    if(current_user<0)return 0;
    return users[current_user].priv>=PRIV_ROOT;
}
void user_list(void){
    terminal_setcolor(VGA_COLOR_LIGHT_CYAN,VGA_COLOR_BLACK);
    terminal_writeline("NAME       LEVEL");
    terminal_setcolor(VGA_COLOR_WHITE,VGA_COLOR_BLACK);
    for(int i=0;i<user_count;i++){
        if(!users[i].active)continue;
        const char *lvl=users[i].priv==PRIV_ROOT?"root (admin)":
                        users[i].priv==PRIV_USER?"user":"guest";
        kprintf("%-10s %s\n",users[i].name,lvl);
    }
}
