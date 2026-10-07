#include "syscall.h"
#include <stdint.h>   /* ★ 加这行 */

#define MAX_PATH 256
#define MAX_NAME 64

/* ★ S2: 用户态可见的 VGA 文本显存虚拟地址（和 kernel/paging.h 一致） */
#define USER_VGA_BASE 0x10000000u

static char cmd[128];
static int  cmd_len = 0;
static char cat_buf[256];
static char cwd[MAX_PATH] = "/";

static void putc_(char c) { sys_putchar(c); }
static void puts_(const char *s) { while (*s) putc_(*s++); }

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static int str_prefix(const char *s, const char *pre) {
    while (*pre) { if (*s++ != *pre++) return 0; }
    return 1;
}

static void skip_spaces(const char **p) {
    while (**p == ' ') (*p)++;
}

static int take_word(const char *src, char *out, int out_size) {
    int i = 0;
    while (src[i] && src[i] != ' ' && i < out_size - 1) {
        out[i] = src[i];
        i++;
    }
    out[i] = 0;
    return i;
}

static void print_hex16(uint16_t v) {
    const char *h = "0123456789ABCDEF";
    putc_(h[(v >> 12) & 0xF]);
    putc_(h[(v >>  8) & 0xF]);
    putc_(h[(v >>  4) & 0xF]);
    putc_(h[ v        & 0xF]);
}

static void resolve_path(const char *in, char *out, int out_size) {
    char tmp[MAX_PATH];

    if (!in || !*in) in = ".";

    if (in[0] == '/') {
        tmp[0] = 0;
    } else {
        int i = 0;
        while (cwd[i] && i < MAX_PATH - 1) { tmp[i] = cwd[i]; i++; }
        tmp[i] = 0;
    }

    const char *p = in;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;

        char seg[64];
        int len = 0;
        while (*p && *p != '/' && len < 63) seg[len++] = *p++;
        seg[len] = 0;

        if (str_eq(seg, ".")) continue;

        if (str_eq(seg, "..")) {
            int i = 0;
            while (tmp[i]) i++;
            while (i > 0 && tmp[i - 1] != '/') i--;
            if (i > 0) i--;
            if (i == 0) { tmp[0] = '/'; tmp[1] = 0; }
            else tmp[i] = 0;
            continue;
        }

        int i = 0;
        while (tmp[i]) i++;
        if (i == 0 || tmp[i - 1] != '/') tmp[i++] = '/';
        int j = 0;
        while (seg[j] && i < MAX_PATH - 1) tmp[i++] = seg[j++];
        tmp[i] = 0;
    }

    if (tmp[0] == 0) { tmp[0] = '/'; tmp[1] = 0; }

    int i = 0;
    while (tmp[i] && i < out_size - 1) { out[i] = tmp[i]; i++; }
    out[i] = 0;
}

static void cmd_help(void) {
    puts_("Commands:\n");
    puts_("  help            - show this\n");
    puts_("  clear           - clear screen\n");
    puts_("  pwd             - show current directory\n");
    puts_("  cd <dir>        - change directory\n");
    puts_("  tid             - show thread id\n");
    puts_("  echo XXX        - print XXX\n");
    puts_("  ls [path]       - list directory\n");
    puts_("  cat <file>      - print file\n");
    puts_("  mkdir <dir>     - create directory\n");
    puts_("  touch <file>    - create empty file\n");
    puts_("  write <f> <txt> - write text to file\n");
    puts_("  rm <file>       - delete file\n");
    puts_("  rm -r <dir>     - recursively delete directory\n");
    puts_("  rmdir <dir>     - delete empty directory\n");
    puts_("  exit            - exit shell\n");
    puts_("  part            - list partition table\n");
    puts_("  cp <src> <dst>  - copy file\n");
    puts_("  install [0|1]   - install system to disk (0=master, 1=slave)\n");
    puts_("  exec <path>     - load and run ELF\n");
    puts_("  mkpart N T S C  - create partition N: type T, start LBA S, sectors C\n");
    puts_("  ioperm          - S1 test: request VGA I/O port 0x3D4/0x3D5\n");
    puts_("  vgatest         - S2 test: read/write VGA MMIO from user mode\n");
    puts_("  irqtest         - S3 test: claim IRQ1 (will fail if kbd.elf holds it)\n");
}

static void cmd_pwd(void) {
    puts_(cwd);
    putc_('\n');
}

static void cmd_cd(const char *args) {
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

static void cmd_ls(const char *args) {
    char path[MAX_PATH];
    if (!args || !*args) {
        resolve_path("", path, MAX_PATH);
    } else {
        char word[MAX_PATH];
        if (take_word(args, word, MAX_PATH) == 0) resolve_path("", path, MAX_PATH);
        else resolve_path(word, path, MAX_PATH);
    }

    int fd = sys_open(path, 0);
    if (fd < 0) { puts_("ls: cannot open\n"); return; }

    for (int i = 0; ; i++) {
        char name[MAX_NAME];
        int  type = 0;
        if (sys_readdir(fd, i, name, &type) < 0) break;
        puts_(type == 2 ? "[dir]  " : "[file] ");
        puts_(name);
        putc_('\n');
    }
    sys_close(fd);
}

static void cmd_cat(const char *args) {
    if (!args || !*args) { puts_("cat: missing arg\n"); return; }
    char word[MAX_PATH];
    if (take_word(args, word, MAX_PATH) == 0) {
        puts_("cat: missing arg\n");
        return;
    }
    char path[MAX_PATH];
    resolve_path(word, path, MAX_PATH);

    int fd = sys_open(path, 0);
    if (fd < 0) { puts_("cat: cannot open\n"); return; }

    for (;;) {
        int n = sys_read(fd, cat_buf, 255);
        if (n <= 0) break;
        for (int i = 0; i < n; i++) putc_(cat_buf[i]);
    }
    sys_close(fd);
}

static void cmd_mkdir(const char *args) {
    if (!args || !*args) { puts_("mkdir: missing arg\n"); return; }
    char word[MAX_PATH];
    if (take_word(args, word, MAX_PATH) == 0) {
        puts_("mkdir: missing arg\n");
        return;
    }
    char path[MAX_PATH];
    resolve_path(word, path, MAX_PATH);
    if (sys_mkdir(path) < 0) puts_("mkdir: failed\n");
}

static void cmd_touch(const char *args) {
    if (!args || !*args) { puts_("touch: missing arg\n"); return; }
    char word[MAX_PATH];
    if (take_word(args, word, MAX_PATH) == 0) {
        puts_("touch: missing arg\n");
        return;
    }
    char path[MAX_PATH];
    resolve_path(word, path, MAX_PATH);
    int fd = sys_open(path, 0x0100);
    if (fd < 0) puts_("touch: failed\n");
    else sys_close(fd);
}

static void cmd_write(const char *args) {
    char word[MAX_PATH];
    int used = take_word(args, word, MAX_PATH);
    if (used == 0) { puts_("write: missing file\n"); return; }

    while (*args && *args != ' ') args++;
    skip_spaces(&args);

    char path[MAX_PATH];
    resolve_path(word, path, MAX_PATH);

    int fd = sys_open(path, 0x0100 | 0x0200);
    if (fd < 0) { puts_("write: cannot open\n"); return; }

    int len = 0;
    while (args[len]) len++;
    if (len > 0) {
        sys_write(fd, args, len);
        sys_write(fd, "\n", 1);
    }
    sys_close(fd);
}

static int do_rm_recursive(const char *path) {
    int fd = sys_open(path, 0);
    if (fd < 0) {
        return sys_unlink(path);
    }
    sys_close(fd);

    for (int i = 0; ; ) {
        fd = sys_open(path, 0);
        if (fd < 0) break;

        char name[MAX_NAME];
        int  type = 0;
        if (sys_readdir(fd, i, name, &type) < 0) {
            sys_close(fd);
            break;
        }
        sys_close(fd);

        char child[MAX_PATH];
        int j = 0;
        while (path[j] && j < MAX_PATH - 1) { child[j] = path[j]; j++; }
        if (j > 0 && child[j - 1] != '/') child[j++] = '/';
        int k = 0;
        while (name[k] && j < MAX_PATH - 1) child[j++] = name[k++];
        child[j] = 0;

        if (do_rm_recursive(child) < 0) {
            i++;
        }
    }

    return sys_unlink(path);
}

static void cmd_rmdir(const char *args) {
    if (!args || !*args) { puts_("rmdir: missing arg\n"); return; }
    char word[MAX_PATH];
    if (take_word(args, word, MAX_PATH) == 0) {
        puts_("rmdir: missing arg\n");
        return;
    }
    char path[MAX_PATH];
    resolve_path(word, path, MAX_PATH);
    if (sys_unlink(path) < 0) puts_("rmdir: failed (not empty?)\n");
}

static void cmd_rm(const char *args) {
    if (!args || !*args) { puts_("rm: missing arg\n"); return; }

    const char *p = args;
    int recursive = 0;

    if (p[0] == '-' && p[1] == 'r') {
        recursive = 1;
        p += 2;
        if (*p == 'f') p++;
        while (*p == ' ') p++;
    }

    char word[MAX_PATH];
    if (take_word(p, word, MAX_PATH) == 0) {
        puts_("rm: missing arg\n");
        return;
    }

    char path[MAX_PATH];
    resolve_path(word, path, MAX_PATH);

    if (recursive) {
        if (do_rm_recursive(path) < 0) puts_("rm: failed\n");
    } else {
        if (sys_unlink(path) < 0) puts_("rm: failed (dir not empty?)\n");
    }
}
static void cmd_cp(const char *args) {
    if (!args || !*args) { puts_("cp: missing args\n"); return; }

    char src_word[MAX_PATH];
    const char *p = args;
    if (take_word(p, src_word, MAX_PATH) == 0) {
        puts_("cp: missing src\n");
        return;
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    char dst_word[MAX_PATH];
    if (take_word(p, dst_word, MAX_PATH) == 0) {
        puts_("cp: missing dst\n");
        return;
    }

    char src[MAX_PATH], dst[MAX_PATH];
    resolve_path(src_word, src, MAX_PATH);
    resolve_path(dst_word, dst, MAX_PATH);

    puts_("cp: src="); puts_(src);
    puts_(" dst="); puts_(dst); putc_('\n');

    int sfd = sys_open(src, 0);
    if (sfd < 0) { puts_("cp: cannot open source\n"); return; }

    int dfd = sys_open(dst, 0x0100 | 0x0200);
    if (dfd < 0) {
        puts_("cp: cannot create dest\n");
        sys_close(sfd);
        return;
    }

    static char cp_buf[512];
    for (;;) {
        int n = sys_read(sfd, cp_buf, sizeof(cp_buf));
        if (n <= 0) break;
        int w = sys_write(dfd, cp_buf, n);
        if (w != n) {
            puts_("cp: write failed\n");
            break;
        }
    }

    sys_close(sfd);
    sys_close(dfd);
}

static void cmd_part(const char *args) {
    int drive = 0;
    if (args && *args) {
        char w[8];
        if (take_word(args, w, 8) > 0) drive = w[0] - '0';
    }

    unsigned char buf[64];
    if (sys_part_list(drive, buf) < 0) {
        puts_("part: read failed\n");
        return;
    }

    puts_("Drive "); putc_('0' + drive); puts_(":\n");
    puts_("  #  boot  type  start_lba    sectors\n");

    int any = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char *p = buf + i * 16;
        int boot = p[0];
        int type = p[1];
        uint32_t start = (uint32_t)p[4] | ((uint32_t)p[5] << 8)
                       | ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
        uint32_t sectors = (uint32_t)p[8] | ((uint32_t)p[9] << 8)
                         | ((uint32_t)p[10] << 16) | ((uint32_t)p[11] << 24);

        if (type == 0 && sectors == 0) continue;
        any = 1;

        putc_(' '); putc_('0' + i); putc_(' ');
        puts_(boot ? "yes " : "no  ");
        putc_('0'); putc_('x');
        const char *h = "0123456789ABCDEF";
        putc_(h[(type >> 4) & 0xF]); putc_(h[type & 0xF]);
        puts_("    ");

        char tb[16]; int ti = 0;
        uint32_t v = start;
        if (v == 0) tb[ti++] = '0';
        while (v) { tb[ti++] = '0' + (v % 10); v /= 10; }
        while (ti--) putc_(tb[ti]);

        puts_("   ");
        ti = 0;
        v = sectors;
        if (v == 0) tb[ti++] = '0';
        while (v) { tb[ti++] = '0' + (v % 10); v /= 10; }
        while (ti--) putc_(tb[ti]);
        putc_('\n');
    }
    if (!any) puts_("  (empty)\n");
}

static uint32_t parse_u32(const char *s) {
    uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

static void cmd_mkpart(const char *args) {
    if (!args || !*args) { puts_("mkpart: missing args\n"); return; }

    char w[32];
    int drive, index, type;
    uint32_t start, sectors;
    const char *p = args;

    if (take_word(p, w, 32) == 0) { puts_("mkpart: drive?\n"); return; }
    drive = w[0] - '0';
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    if (take_word(p, w, 32) == 0) { puts_("mkpart: index?\n"); return; }
    index = w[0] - '0';
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    if (take_word(p, w, 32) == 0) { puts_("mkpart: type?\n"); return; }
    if (w[0] == '0' && (w[1] == 'x' || w[1] == 'X')) {
        type = 0;
        const char *h = w + 2;
        while (*h) {
            type <<= 4;
            if (*h >= '0' && *h <= '9') type |= (*h - '0');
            else if (*h >= 'a' && *h <= 'f') type |= (*h - 'a' + 10);
            else if (*h >= 'A' && *h <= 'F') type |= (*h - 'A' + 10);
            h++;
        }
    } else {
        type = (int)parse_u32(w);
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    if (take_word(p, w, 32) == 0) { puts_("mkpart: start?\n"); return; }
    start = parse_u32(w);
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    if (take_word(p, w, 32) == 0) { puts_("mkpart: sectors?\n"); return; }
    sectors = parse_u32(w);

    puts_("mkpart: drive="); putc_('0' + drive);
    puts_(" index="); putc_('0' + index);
    puts_(" type=0x");
    const char *h = "0123456789ABCDEF";
    putc_(h[(type >> 4) & 0xF]);
    putc_(h[type & 0xF]);
    puts_("\n");

    if (sys_part_mkp(drive, index, type, start, sectors) < 0)
        puts_("mkpart: failed\n");
    else
        puts_("mkpart: ok\n");
}

static void cmd_install(const char *args) {
    int drive = 1;
    if (args && *args) {
        while (*args == ' ') args++;
        if (*args == '0') drive = 0;
        else if (*args == '1') drive = 1;
    }

    puts_("install: target drive ");
    putc_('0' + drive);
    puts_("\n");
    puts_("install: writing boot + kernel + NXFS + init.elf + kbd.elf...\n");

    int r = sys_install(drive);
    if (r < 0) {
        if (r == -100) {
            puts_("install: refused. This is an installed system.\n");
            puts_("install: Boot from ISO to use the installer.\n");
        } else {
            puts_("install: FAILED, code=");
            char b[4] = { '0' + ((-r) % 10), '\n', 0, 0 };
            puts_(b);
        }
        return;
    }
    puts_("install: done.\n");
}

static void cmd_exec(const char *args) {
    if (!args || !*args) { puts_("exec: missing arg\n"); return; }

    char word[MAX_PATH];
    if (take_word(args, word, MAX_PATH) == 0) {
        puts_("exec: missing arg\n");
        return;
    }

    char path[MAX_PATH];
    resolve_path(word, path, MAX_PATH);

    puts_("exec: loading "); puts_(path); puts_("\n");

    int tid = sys_exec(path);

    if (tid < 0) {
        puts_("exec: failed, code=");
        char b[4] = { '0' + ((-tid) % 10), '\n', 0, 0 };
        puts_(b);
        return;
    }

    puts_("exec: child exited, tid=");
    char b[4] = { '0' + (tid % 10), '\n', 0, 0 };
    puts_(b);
}

static void cmd_mem(void) {
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

static void cmd_ioperm(void) {
    puts_("ioperm: request 0x3D4 ... ");
    int r1 = sys_io_perm(0x3D4);
    if (r1 == 0) puts_("OK\n");
    else {
        puts_("FAIL r=");
        putc_('0' + ((-r1) % 10));
        putc_('\n');
        return;
    }

    puts_("ioperm: request 0x3D5 ... ");
    int r2 = sys_io_perm(0x3D5);
    if (r2 == 0) puts_("OK\n");
    else {
        puts_("FAIL r=");
        putc_('0' + ((-r2) % 10));
        putc_('\n');
        return;
    }

    puts_("ioperm: request 0x60  ... ");
    int r3 = sys_io_perm(0x60);
    if (r3 == 0) puts_("OK (unexpected!)\n");
    else {
        puts_("rejected r=");
        putc_('0' + ((-r3) % 10));
        putc_('\n');
    }

    outb(0x3D4, 0x0F);
    uint8_t hi = inb(0x3D5);
    outb(0x3D4, 0x0E);
    uint8_t lo = inb(0x3D5);
    uint16_t orig = ((uint16_t)hi << 8) | lo;

    puts_("ioperm: CRTC cursor read  = 0x");
    print_hex16(orig);
    putc_('\n');

    outb(0x3D4, 0x0F); outb(0x3D5, 0x00);
    outb(0x3D4, 0x0E); outb(0x3D5, 0x00);

    outb(0x3D4, 0x0F);
    uint8_t vhi = inb(0x3D5);
    outb(0x3D4, 0x0E);
    uint8_t vlo = inb(0x3D5);
    uint16_t verify = ((uint16_t)vhi << 8) | vlo;

    outb(0x3D4, 0x0F); outb(0x3D5, hi);
    outb(0x3D4, 0x0E); outb(0x3D5, lo);

    puts_("ioperm: write 0x0000, read back = 0x");
    print_hex16(verify);
    puts_(verify == 0 ? "  OK\n" : "  FAIL\n");
}

static void cmd_vgatest(void) {
    volatile uint16_t *vga = (volatile uint16_t *)USER_VGA_BASE;

    uint16_t orig = vga[0];
    puts_("vgatest: [0xB8000] orig       = 0x");
    print_hex16(orig);
    putc_('\n');

    uint16_t mark = (uint16_t)((0x07 << 8) | 'X');
    vga[0] = mark;

    uint16_t after = vga[0];
    puts_("vgatest: write 'X', read back = 0x");
    print_hex16(after);
    putc_('\n');

    vga[0] = orig;

    if (after == mark) {
        puts_("vgatest: OK - user mode can R/W VGA MMIO\n");
    } else {
        puts_("vgatest: FAIL - read back mismatch\n");
    }
}

static void cmd_irqtest(void) {
    puts_("irqtest: trying to register IRQ1...\n");
    int r = sys_irq_register(1);
    if (r < 0) {
        if (r == -3) {
            puts_("irqtest: FAIL - IRQ1 already owned by kbd.elf (expected in S4)\n");
        } else {
            puts_("irqtest: FAIL r=");
            putc_('0' + ((-r) % 10));
            putc_('\n');
        }
        return;
    }

    puts_("irqtest: OK (kbd.elf NOT running?). Now press keys. ESC to quit.\n");

    user_msg_t m;
    for (;;) {
        int rr = sys_recv(&m);
        if (rr < 0) break;
        if (m.type != MSG_IRQ) continue;
        if ((int)m.data[0] != 1) continue;

        uint32_t sc = m.data[1];
        puts_("IRQ1: scancode=0x");
        const char *h = "0123456789ABCDEF";
        putc_(h[(sc >> 4) & 0xF]);
        putc_(h[ sc       & 0xF]);
        putc_('\n');

        if (sc == 0x01) break;
    }

    sys_irq_unregister(1);
    puts_("irqtest: unregistered.\n");
}

static int read_char(void) {
    user_msg_t m;
    for (;;) {
        if (sys_recv(&m) < 0) continue;
        if (m.type == MSG_CHAR) return (int)m.data[0];
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
    else { puts_("unknown: "); puts_(p); putc_('\n'); }

    cmd_len = 0;
}

int main(void) {
    puts_("NexOS-NEXT Shell v0.5\n");

    /* ★ S4.5: 键盘驱动现在位于 /system/drive/kbd.elf */
    puts_("Starting keyboard driver (/system/drive/kbd.elf)...\n");
    int kbd_tid = sys_exec_bg("/system/drive/kbd.elf");
    if (kbd_tid < 0) {
        puts_("shell: FATAL - cannot start kbd.elf, code=");
        char b[4] = { '0' + ((-kbd_tid) % 10), '\n', 0, 0 };
        puts_(b);
        for (;;) sys_yield();
    }

    /* 告诉 kbd.elf：我是你的输出目标 */
    user_msg_t hello;
    hello.sender  = 0;
    hello.type    = MSG_HELLO;
    for (int i = 0; i < 8; i++) hello.data[i] = 0;
    sys_send(kbd_tid, &hello);

    puts_("Keyboard driver running (tid=");
    char b[4] = { '0' + (kbd_tid % 10), ')', '\n', '\0' };
    puts_(b);

    puts_("Type 'help' for commands.\n\n");

    for (;;) {
        puts_(cwd);
        puts_("> ");
        cmd_len = 0;

        for (;;) {
            int c = read_char();

            if (c == '\n') {
                run_cmd();
                break;
            } else if (c == '\b') {
                if (cmd_len > 0) {
                    cmd_len--;
                    putc_('\b');
                }
            } else if (c >= 32 && c < 127) {
                if (cmd_len < 127) {
                    cmd[cmd_len++] = (char)c;
                    putc_((char)c);
                }
            }
        }
    }
    return 0;
}