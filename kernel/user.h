/* =============================================================================
   StackOS — kernel/user.h
   ============================================================================= */
#pragma once
#include <stdint.h>

#define USER_MAX       16
#define USER_NAME_MAX  16
#define USER_PASS_MAX  32

#define PRIV_GUEST  0
#define PRIV_USER   1
#define PRIV_ROOT   2   /* was PRIV_FROST */

typedef struct {
    char    name[USER_NAME_MAX];
    char    pass[USER_PASS_MAX];
    uint8_t priv;
    int     active;
} stack_user_t;   /* was blizz_user_t */

void          user_init(void);
int           user_login(const char *name, const char *pass);
void          user_logout(void);
stack_user_t *user_current(void);
int           user_is_root(void);   /* was user_is_frost */
int           user_add(const char *name, const char *pass, uint8_t priv);
int           user_del(const char *name);
int           user_chpass(const char *name, const char *newpass);
void          user_list(void);
void          users_save(void);
