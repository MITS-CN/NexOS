#ifndef SHELL_H
#define SHELL_H

#include "syscall.h"
#include <stdint.h>

#define MAX_PATH 256
#define MAX_NAME 64
#define USER_VGA_BASE 0x10000000u

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define HIST_MAX  32
#define HIST_PATH "/user/ROOT/commands.history"

extern char cwd[MAX_PATH];
extern int  g_kbd_tid;
extern int  g_vga_tid;
extern int  g_mouse_tid;
extern int  g_atad_tid;

void putc_(char c);
void puts_(const char *s);
int  str_len(const char *s);
int  str_eq(const char *a, const char *b);
int  str_prefix(const char *s, const char *pre);
void skip_spaces(const char **p);
int  take_word(const char *src, char *out, int out_size);
void print_hex16(uint16_t v);
void print_dec(int v);
void print_dec_pad(uint32_t v, int width);
void perm_str(uint16_t mode, char *out);
uint32_t parse_oct(const char *s);
uint32_t parse_u32(const char *s);
void resolve_path(const char *in, char *out, int out_size);

void cmd_help(void);
void cmd_pwd(void);
void cmd_cd(const char *args);
void cmd_mem(void);

void cmd_ls(const char *args);
void cmd_cat(const char *args);
void cmd_mkdir(const char *args);
void cmd_touch(const char *args);
void cmd_write(const char *args);
void cmd_rm(const char *args);
void cmd_rmdir(const char *args);
void cmd_cp(const char *args);
void cmd_chmod(const char *args);
void cmd_chown(const char *args);

void cmd_part(const char *args);
void cmd_mkpart(const char *args);
void cmd_install(const char *args);
void cmd_exec(const char *args);
void cmd_ioperm(void);
void cmd_vgatest(void);
void cmd_irqtest(void);
void cmd_killkbd(void);
void cmd_kbdtid(void);
void cmd_killvga(void);
void cmd_vgatid(void);
void cmd_killmouse(void);
void cmd_mousetid(void);
void cmd_killatad(void);
void cmd_atadtid(void);

/* ★ S7c-3 */
void cmd_setuid(const char *args);
void cmd_id(void);

#endif