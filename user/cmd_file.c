#include "shell.h"

/* ls [-l] [path] — 支持文件和目录 */
void cmd_ls(const char *args) {
    int long_fmt = 0;
    char path[MAX_PATH];

    const char *p = args;
    while (p && *p == ' ') p++;

    if (p && p[0] == '-' && p[1] == 'l') {
        long_fmt = 1;
        p += 2;
        while (*p == ' ') p++;
    }

    if (!p || !*p) {
        resolve_path("", path, MAX_PATH);
    } else {
        char word[MAX_PATH];
        if (take_word(p, word, MAX_PATH) == 0) resolve_path("", path, MAX_PATH);
        else resolve_path(word, path, MAX_PATH);
    }

    stat_t st;
    if (sys_stat(path, &st) < 0) {
        puts_("ls: cannot access\n");
        return;
    }

    /* 文件：直接输出一个 */
    if (st.type == 1) {
        const char *base = path;
        for (const char *q = path; *q; q++)
            if (*q == '/') base = q + 1;

        if (long_fmt) {
            char perms[10];
            perm_str(st.mode, perms);
            putc_('-');
            puts_(perms);
            putc_(' ');
            puts_("uid=");
            print_dec_pad(st.uid, 2);
            puts_(" size=");
            print_dec_pad(st.size, 5);
            puts_("  ");
        }
        puts_(base);
        putc_('\n');
        return;
    }

    int fd = sys_open(path, 0);
    if (fd < 0) { puts_("ls: cannot open\n"); return; }

    for (int i = 0; ; i++) {
        char name[MAX_NAME];
        int  type = 0;
        if (sys_readdir(fd, i, name, &type) < 0) break;

        if (long_fmt) {
            char child[MAX_PATH];
            int j = 0;
            while (path[j] && j < MAX_PATH - 1) { child[j] = path[j]; j++; }
            if (j > 0 && child[j - 1] != '/') child[j++] = '/';
            int k = 0;
            while (name[k] && j < MAX_PATH - 1) child[j++] = name[k++];
            child[j] = 0;

            stat_t cs;
            if (sys_stat(child, &cs) < 0) {
                puts_("?????????? ");
            } else {
                char perms[10];
                perm_str(cs.mode, perms);
                putc_(cs.type == 2 ? 'd' : '-');
                puts_(perms);
                putc_(' ');
                puts_("uid=");
                print_dec_pad(cs.uid, 2);
                puts_(" size=");
                print_dec_pad(cs.size, 5);
                puts_("  ");
            }
        } else {
            puts_(type == 2 ? "[dir]  " : "[file] ");
        }

        puts_(name);
        putc_('\n');
    }
    sys_close(fd);
}

void cmd_cat(const char *args) {
    static char cat_buf[256];

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

void cmd_mkdir(const char *args) {
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

void cmd_touch(const char *args) {
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

void cmd_write(const char *args) {
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

void cmd_rmdir(const char *args) {
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

void cmd_rm(const char *args) {
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

void cmd_cp(const char *args) {
    static char cp_buf[512];

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

void cmd_chmod(const char *args) {
    if (!args || !*args) { puts_("chmod: missing args\n"); return; }

    char word[MAX_PATH];
    const char *p = args;
    if (take_word(p, word, MAX_PATH) == 0) {
        puts_("chmod: missing mode\n"); return;
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    char fname[MAX_PATH];
    if (take_word(p, fname, MAX_PATH) == 0) {
        puts_("chmod: missing path\n"); return;
    }

    char path[MAX_PATH];
    resolve_path(fname, path, MAX_PATH);

    uint32_t mode = parse_oct(word);
    int r = sys_chmod(path, (uint16_t)(mode & 0777));
    if (r < 0) puts_("chmod: failed\n");
}

void cmd_chown(const char *args) {
    if (!args || !*args) { puts_("chown: missing args\n"); return; }

    char word[MAX_PATH];
    const char *p = args;
    if (take_word(p, word, MAX_PATH) == 0) {
        puts_("chown: missing uid\n"); return;
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    char fname[MAX_PATH];
    if (take_word(p, fname, MAX_PATH) == 0) {
        puts_("chown: missing path\n"); return;
    }

    char path[MAX_PATH];
    resolve_path(fname, path, MAX_PATH);

    uint32_t uid = parse_u32(word);

    int r = sys_chown(path, (uint16_t)uid);
    if (r < 0) puts_("chown: failed (need root?)\n");
}