#include "shell.h"

char cwd[MAX_PATH] = "/";
int  g_kbd_tid   = -1;
int  g_vga_tid   = -1;
int  g_mouse_tid = -1;
int  g_atad_tid  = -1;

job_t jobs[MAX_JOBS] = {0};

static char cmd[512];
static int  cmd_len = 0;

static char hist[HIST_MAX][512];
static int  hist_count  = 0;
static int  hist_pos    = -1;
static char cur_save[512];
static int  cur_save_len = 0;

static int  g_hist_fd = -1;

static void ensure_hist_dirs(void) {
    int fd;

    fd = sys_open("/user", 0);
    if (fd < 0) sys_mkdir("/user");
    else        sys_close(fd);

    fd = sys_open("/user/ROOT", 0);
    if (fd < 0) sys_mkdir("/user/ROOT");
    else        sys_close(fd);
}

static int hist_add(const char *s) {
    int len = str_len(s);
    if (len == 0) return 0;

    if (hist_count > 0) {
        const char *last = hist[hist_count - 1];
        int same = 1;
        for (int i = 0; i <= len; i++) {
            if (last[i] != s[i]) { same = 0; break; }
        }
        if (same) return 0;
    }

    if (hist_count >= HIST_MAX) {
        for (int i = 0; i < HIST_MAX - 1; i++)
            for (int j = 0; j < 512; j++)
                hist[i][j] = hist[i + 1][j];
        hist_count--;
    }

    for (int i = 0; i <= len && i < 512; i++)
        hist[hist_count][i] = s[i];
    hist_count++;
    return 1;
}

static void hist_append(const char *s) {
    if (g_hist_fd < 0) return;
    int len = str_len(s);
    if (len == 0 || len > 510) return;

    char buf[512];
    for (int i = 0; i < len; i++) buf[i] = s[i];
    buf[len]     = '\n';
    buf[len + 1] = 0;

    sys_write(g_hist_fd, buf, len + 1);
}

static void hist_load(void) {
    int fd = sys_open(HIST_PATH, 0);
    if (fd < 0) {
        fd = sys_open(HIST_PATH, 0x0100);
    }
    if (fd < 0) return;

    char buf[128];
    char line[512];
    int  line_len = 0;
    int  n;

    for (int i = 0; i < 512; i++) line[i] = 0;

    for (;;) {
        n = sys_read(fd, buf, sizeof(buf));
        if (n <= 0) break;

        for (int i = 0; i < n; i++) {
            char ch = buf[i];
            if (ch == '\n') {
                if (line_len > 0 && hist_count < HIST_MAX) {
                    for (int j = 0; j <= line_len && j < 512; j++)
                        hist[hist_count][j] = line[j];
                    hist_count++;
                }
                line_len = 0;
                for (int j = 0; j < 512; j++) line[j] = 0;
            } else if (line_len < 511) {
                line[line_len++] = ch;
            }
        }
    }

    g_hist_fd = fd;
}

static void redraw_input(const char *s) {
    while (cmd_len > 0) {
        putc_('\b');
        cmd_len--;
    }

    for (int i = 0; i < 512; i++) cmd[i] = 0;

    int n = 0;
    while (s[n] && n < 511) {
        cmd[n] = s[n];
        putc_(s[n]);
        n++;
    }
    cmd[n]   = 0;
    cmd_len  = n;
}

static void restart_kbd(void) {
    g_kbd_tid = sys_exec_bg("/system/drive/kbd.elf", 0, 0);
    if (g_kbd_tid < 0) {
        puts_("[shell] restart kbd.elf FAILED, code=");
        print_dec(-g_kbd_tid);
        putc_('\n');
        return;
    }

    user_msg_t hello;
    hello.sender  = 0;
    hello.type    = MSG_HELLO;
    for (int i = 0; i < 8; i++) hello.data[i] = 0;
    sys_send(g_kbd_tid, &hello);

    puts_("[shell] kbd.elf started, tid=");
    print_dec(g_kbd_tid);
    putc_('\n');
}

static void restart_vga(void) {
    g_vga_tid = sys_exec_bg("/system/drive/vga.elf", 0, 0);
    if (g_vga_tid < 0) {
        puts_("[shell] restart vga.elf FAILED, code=");
        print_dec(-g_vga_tid);
        putc_('\n');
        return;
    }

    puts_("[shell] vga.elf started, tid=");
    print_dec(g_vga_tid);
    putc_('\n');
}

static void restart_mouse(void) {
    g_mouse_tid = sys_exec_bg("/system/drive/mouse.elf", 0, 0);
    if (g_mouse_tid < 0) {
        puts_("[shell] restart mouse.elf FAILED, code=");
        print_dec(-g_mouse_tid);
        putc_('\n');
        return;
    }

    puts_("[shell] mouse.elf started, tid=");
    print_dec(g_mouse_tid);
    putc_('\n');
}

static void restart_atad(void) {
    g_atad_tid = sys_exec_bg("/system/drive/atad.elf", 0, 0);
    if (g_atad_tid < 0) {
        puts_("[shell] restart atad.elf FAILED, code=");
        print_dec(-g_atad_tid);
        putc_('\n');
        return;
    }

    user_msg_t hello;
    hello.sender  = 0;
    hello.type    = MSG_HELLO;
    for (int i = 0; i < 8; i++) hello.data[i] = 0;
    sys_send(g_atad_tid, &hello);

    puts_("[shell] atad.elf started, tid=");
    print_dec(g_atad_tid);
    putc_('\n');
}

void cmd_help(void) {
    puts_("Commands:\n");
    puts_("  help            - show this\n");
    puts_("  clear           - clear screen\n");
    puts_("  pwd             - show current directory\n");
    puts_("  cd <dir>        - change directory\n");
    puts_("  tid             - show thread id\n");
    puts_("  echo XXX        - print XXX\n");
    puts_("  ls [-l] [path]  - list directory (-l: long)\n");
    puts_("  cat <file>      - print file\n");
    puts_("  mkdir <dir>     - create directory\n");
    puts_("  touch <file>    - create empty file\n");
    puts_("  write <f> <txt> - write text to file\n");
    puts_("  rm <file>       - delete file\n");
    puts_("  rm -r <dir>     - recursively delete directory\n");
    puts_("  rmdir <dir>     - delete empty directory\n");
    puts_("  chmod MODE PATH - change permission (octal)\n");
    puts_("  chown UID PATH  - change owner (root only)\n");
    puts_("  id              - show current uid\n");
    puts_("  setuid N        - drop to uid N (irreversible)\n");
    puts_("  jobs            - list background jobs\n");
    puts_("  fg %N           - bring job N to foreground\n");
    puts_("  bg %N           - resume job N in background\n");
    puts_("  exit            - exit shell\n");
    puts_("  part            - list partition table\n");
    puts_("  cp <src> <dst>  - copy file\n");
    puts_("  install [0|1]   - install system to disk\n");
    puts_("  exec P [A1..] [&] - run ELF; & = background\n");
    puts_("  mkpart N T S C  - create partition\n");
    puts_("  ioperm          - S1 test: VGA I/O\n");
    puts_("  vgatest         - S2 test: VGA MMIO\n");
    puts_("  irqtest         - S3 test: claim IRQ1\n");
    puts_("  killkbd/kbdtid  - kbd.elf control\n");
    puts_("  killvga/vgatid  - vga.elf control\n");
    puts_("  killmouse/mousetid - mouse.elf control\n");
    puts_("  killatad/atadtid   - atad.elf control\n");
    puts_("  keys: Up/Down = history, Ctrl+C = cancel input\n");
}

void cmd_pwd(void) {
    puts_(cwd);
    putc_('\n');
}

void cmd_cd(const char *args) {
    char word[MAX_PATH];
    if (!args || !*args) {
        cwd[0] = '/'; cwd[1] = 0;
        return;
    }
    if (take_word(args, word, MAX_PATH) == 0) return;

    char path[MAX_PATH];
    resolve_path(word, path, MAX_PATH);

    int fd = sys_open(path, 0);
    if (fd < 0) { puts_("cd: no such directory\n"); return; }
    sys_close(fd);

    int i = 0;
    while (path[i] && i < MAX_PATH - 1) { cwd[i] = path[i]; i++; }
    cwd[i] = 0;
}

void cmd_mem(void) {
    int free = sys_meminfo();
    puts_("free pages: ");
    char b[12]; int i = 0;
    if (free == 0) b[i++] = '0';
    while (free) { b[i++] = '0' + (free % 10); free /= 10; }
    while (i--) putc_(b[i]);
    puts_(" (");
    int kb = sys_meminfo() * 4;
    i = 0;
    if (kb == 0) b[i++] = '0';
    while (kb) { b[i++] = '0' + (kb % 10); kb /= 10; }
    while (i--) putc_(b[i]);
    puts_(" KB)\n");
}

static int read_char(void) {
    user_msg_t m;
    for (;;) {
        if (sys_recv(&m) < 0) continue;

        if (m.type == MSG_CHAR) return (int)m.data[0];
        if (m.type == MSG_SIGINT) return -2;

        if (m.type == MSG_IRQ_OWNER_DIED) {
            int irq = (int)m.data[0];
            if (irq == 1) {
                putc_('\n');
                puts_("[shell] kbd.elf died (IRQ1 released), restarting...\n");
                restart_kbd();
            } else if (irq == 12) {
                putc_('\n');
                puts_("[shell] mouse.elf died (IRQ12 released), restarting...\n");
                restart_mouse();
            }
            continue;
        }

        if (m.type == MSG_VGA_OWNER_DIED) {
            putc_('\n');
            puts_("[shell] vga.elf died, restarting...\n");
            restart_vga();
            continue;
        }

        if (m.type == MSG_ATA_OWNER_DIED) {
            putc_('\n');
            puts_("[shell] atad.elf died, restarting...\n");
            restart_atad();
            continue;
        }

        /* ★ S7d: 处理后台任务死亡通知 */
        if (m.type == MSG_THREAD_DIED) {
            int dead_tid = m.sender;
            for (int i = 0; i < MAX_JOBS; i++) {
                if (jobs[i].used && jobs[i].tid == dead_tid) {
                    jobs[i].used = 0;
                    putc_('\n');
                    puts_("[shell] job ");
                    putc_('0' + (i + 1));
                    puts_(" exited\n");

                    /* ★ 关键：把键盘目标抢回给当前主 shell */
                    if (g_kbd_tid >= 0) {
                        user_msg_t msg;
                        msg.sender  = 0;
                        msg.type    = MSG_KBD_TARGET;
                        msg.data[0] = (uint32_t)sys_getid(); // 自己
                        for (int k = 1; k < 8; k++) msg.data[k] = 0;
                        sys_send(g_kbd_tid, &msg);
                    }

                    return -3;   /* ★ 让主循环刷新提示符 */
                }
            }
            continue;
        }
    }
}

static void run_cmd(void) {
    cmd[cmd_len] = 0;
    putc_('\n');

    const char *p = cmd;
    skip_spaces(&p);

    if (cmd_len == 0) { }
    else if (str_eq(p, "help"))  cmd_help();
    else if (str_eq(p, "clear")) { for (int i = 0; i < 25; i++) putc_('\n'); }
    else if (str_eq(p, "pwd"))   cmd_pwd();
    else if (str_eq(p, "cd"))    cmd_cd(0);
    else if (str_prefix(p, "cd "))    cmd_cd(p + 3);
    else if (str_eq(p, "tid")) {
        char b[4] = { '0' + (sys_getid() % 10), '\n', 0, 0 };
        puts_("tid = "); puts_(b);
    }
    else if (str_eq(p, "exit")) { puts_("Bye.\n"); sys_exit(); }
    else if (str_prefix(p, "echo ")) { puts_(p + 5); putc_('\n'); }
    else if (str_eq(p, "ls"))         cmd_ls(0);
    else if (str_prefix(p, "ls "))    cmd_ls(p + 3);
    else if (str_prefix(p, "cat "))   cmd_cat(p + 4);
    else if (str_prefix(p, "cp "))    cmd_cp(p + 3);
    else if (str_prefix(p, "mkdir ")) cmd_mkdir(p + 6);
    else if (str_prefix(p, "touch ")) cmd_touch(p + 6);
    else if (str_prefix(p, "write ")) cmd_write(p + 6);
    else if (str_prefix(p, "rmdir ")) cmd_rmdir(p + 6);
    else if (str_prefix(p, "chmod ")) cmd_chmod(p + 6);
    else if (str_prefix(p, "chown ")) cmd_chown(p + 6);
    else if (str_prefix(p, "install"))  cmd_install(p + 7);
    else if (str_prefix(p, "rm "))    cmd_rm(p + 3);
    else if (str_prefix(p, "exec "))  cmd_exec(p + 5);
    else if (str_eq(p, "part"))      cmd_part(0);
    else if (str_prefix(p, "part ")) cmd_part(p + 5);
    else if (str_prefix(p, "mkpart ")) cmd_mkpart(p + 7);
    else if (str_eq(p, "mem")) cmd_mem();
    else if (str_eq(p, "ioperm")) cmd_ioperm();
    else if (str_eq(p, "vgatest")) cmd_vgatest();
    else if (str_eq(p, "irqtest")) cmd_irqtest();
    else if (str_eq(p, "killkbd")) cmd_killkbd();
    else if (str_eq(p, "kbdtid")) cmd_kbdtid();
    else if (str_eq(p, "killvga")) cmd_killvga();
    else if (str_eq(p, "vgatid")) cmd_vgatid();
    else if (str_eq(p, "killmouse")) cmd_killmouse();
    else if (str_eq(p, "mousetid")) cmd_mousetid();
    else if (str_eq(p, "killatad")) cmd_killatad();
    else if (str_eq(p, "atadtid")) cmd_atadtid();
    else if (str_eq(p, "id"))     cmd_id();
    else if (str_prefix(p, "setuid ")) cmd_setuid(p + 7);
    /* ★ S7d */
    else if (str_eq(p, "jobs")) cmd_jobs();
    else if (str_prefix(p, "fg ")) cmd_fg(p + 3);
    else if (str_prefix(p, "bg ")) cmd_bg(p + 3);
    else { puts_("unknown: "); puts_(p); putc_('\n'); }

    cmd_len = 0;
}

int main(int argc, char **argv) {
    /* argc > 1: argv 测试模式 */
    if (argc > 1) {
        puts_("=== argv test ===\n");
        puts_("argc = ");
        print_dec(argc);
        putc_('\n');
        for (int i = 0; i < argc; i++) {
            puts_("argv[");
            putc_('0' + (i % 10));
            puts_("] = ");
            if (argv[i]) puts_(argv[i]);
            else         puts_("(null)");
            putc_('\n');
        }
        puts_("=================\n");
        sys_exit();
    }

    /* ★ 只有初始 shell (argc == 0) 才启动驱动 */
    int is_primary_shell = (argc == 0);

    if (is_primary_shell) {
        puts_("NexOS-NEXT Shell v0.5\n");
        puts_("Starting VGA driver (/system/drive/vga.elf)...\n");
        restart_vga();
        puts_("Starting keyboard driver (/system/drive/kbd.elf)...\n");
        restart_kbd();
        puts_("Starting mouse driver (/system/drive/mouse.elf)...\n");
        restart_mouse();
        puts_("Starting ATA driver (/system/drive/atad.elf)...\n");
        restart_atad();
        puts_("Type 'help' for commands.\n\n");
    } else {
        puts_("NexOS-NEXT Subshell\n");
        puts_("Type 'help' for commands.\n\n");
    }

    ensure_hist_dirs();
    hist_load();

    for (;;) {
        puts_(cwd);
        puts_("> ");

        for (int i = 0; i < 512; i++) cmd[i] = 0;
        cmd_len = 0;
        hist_pos = -1;

        for (;;) {
            int c = read_char();

            if (c == KEY_UP) {
                if (hist_count == 0) continue;
                if (hist_pos == -1) {
                    cur_save_len = cmd_len;
                    for (int i = 0; i < cmd_len && i < 512; i++)
                        cur_save[i] = cmd[i];
                    cur_save[cmd_len] = 0;
                    hist_pos = hist_count - 1;
                } else if (hist_pos > 0) {
                    hist_pos--;
                } else {
                    continue;
                }
                redraw_input(hist[hist_pos]);
                continue;
            }

            if (c == KEY_DOWN) {
                if (hist_pos == -1) continue;
                if (hist_pos < hist_count - 1) {
                    hist_pos++;
                    redraw_input(hist[hist_pos]);
                } else {
                    hist_pos = -1;
                    redraw_input(cur_save);
                }
                continue;
            }

            if (c == -3) {
                cmd_len = 0;
                hist_pos = -1;
                break;    /* 打断内层输入循环，回到外层重新打印提示符 */
            }

            if (c == -2) {
                putc_('\n');
                puts_("^C\n");
                for (int i = 0; i < 512; i++) cmd[i] = 0;
                cmd_len = 0;
                hist_pos = -1;
                break;
            }

            if (c == '\n') {
                if (hist_add(cmd)) {
                    hist_append(cmd);
                }
                hist_pos = -1;
                run_cmd();
                break;
            } else if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    putc_('\b');
                }
            } else if (c >= 32 && c < 127) {
                if (cmd_len < 511) {
                    cmd[cmd_len++] = (char)c;
                    cmd[cmd_len]   = 0;
                    putc_((char)c);
                }
            }
        }
    }
    return 0;
}

