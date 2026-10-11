#include "shell.h"

void cmd_part(const char *args) {
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

void cmd_mkpart(const char *args) {
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

void cmd_install(const char *args) {
    int drive = 1;
    if (args && *args) {
        while (*args == ' ') args++;
        if (*args == '0') drive = 0;
        else if (*args == '1') drive = 1;
    }

    puts_("install: target drive ");
    putc_('0' + drive);
    puts_("\n");
    puts_("install: writing boot + kernel + NXFS + 5 ELFs...\n");

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

void cmd_exec(const char *args) {
    static char argv_buf[16][MAX_PATH];
    static const char *argv[17];

    if (!args || !*args) { puts_("exec: missing arg\n"); return; }

    char word[MAX_PATH];
    int used = take_word(args, word, MAX_PATH);
    if (used == 0) {
        puts_("exec: missing arg\n");
        return;
    }

    char path[MAX_PATH];
    resolve_path(word, path, MAX_PATH);

    int argc = 0;

    int i = 0;
    while (path[i] && i < MAX_PATH - 1) {
        argv_buf[0][i] = path[i];
        i++;
    }
    argv_buf[0][i] = 0;
    argv[argc++] = argv_buf[0];

    const char *p = args;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    while (*p && argc < 16) {
        int n = 0;
        while (*p && *p != ' ' && n < MAX_PATH - 1) {
            argv_buf[argc][n++] = *p++;
        }
        argv_buf[argc][n] = 0;
        argv[argc] = argv_buf[argc];
        argc++;
        while (*p == ' ') p++;
    }
    argv[argc] = 0;

    int background = 0;
    if (argc >= 2) {
        const char *last = argv[argc - 1];
        if (last[0] == '&' && last[1] == 0) {
            background = 1;
            argc--;
            argv[argc] = 0;
        }
    }

    puts_("exec: loading "); puts_(path);
    if (background) puts_(" (background)");
    puts_("\n");

    if (background) {
        int tid = sys_exec_bg(path, argc, argv);
        if (tid < 0) {
            puts_("exec: bg failed, code=");
            char b[4] = { '0' + ((-tid) % 10), '\n', 0, 0 };
            puts_(b);
            return;
        }
        puts_("exec: background started, tid=");
        print_dec(tid);
        putc_('\n');
    } else {
        int tid = sys_exec(path, argc, argv);
        if (tid < 0) {
            puts_("exec: failed, code=");
            char b[4] = { '0' + ((-tid) % 10), '\n', 0, 0 };
            puts_(b);
            return;
        }
        puts_("exec: child exited, tid=");
        print_dec(tid);
        putc_('\n');
    }
}

void cmd_ioperm(void) {
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

void cmd_vgatest(void) {
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

void cmd_irqtest(void) {
    puts_("irqtest: trying to register IRQ1...\n");
    int r = sys_irq_register(1, 0x60);
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

    puts_("irqtest: OK. Now press keys. ESC to quit.\n");

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

void cmd_killkbd(void) {
    if (g_kbd_tid < 0) { puts_("killkbd: no kbd.elf running\n"); return; }
    puts_("killkbd: sending MSG_EXIT to tid=");
    print_dec(g_kbd_tid);
    putc_('\n');
    user_msg_t m; m.sender = 0; m.type = MSG_EXIT;
    for (int i = 0; i < 8; i++) m.data[i] = 0;
    sys_send(g_kbd_tid, &m);
}

void cmd_kbdtid(void) {
    puts_("kbd.elf tid = "); print_dec(g_kbd_tid); putc_('\n');
}

void cmd_killvga(void) {
    if (g_vga_tid < 0) { puts_("killvga: no vga.elf running\n"); return; }
    puts_("killvga: sending MSG_EXIT to tid=");
    print_dec(g_vga_tid);
    putc_('\n');
    user_msg_t m; m.sender = 0; m.type = MSG_EXIT;
    for (int i = 0; i < 8; i++) m.data[i] = 0;
    sys_send(g_vga_tid, &m);
}

void cmd_vgatid(void) {
    puts_("vga.elf tid = "); print_dec(g_vga_tid); putc_('\n');
}

void cmd_killmouse(void) {
    if (g_mouse_tid < 0) { puts_("killmouse: no mouse.elf running\n"); return; }
    puts_("killmouse: sending MSG_EXIT to tid=");
    print_dec(g_mouse_tid);
    putc_('\n');
    user_msg_t m; m.sender = 0; m.type = MSG_EXIT;
    for (int i = 0; i < 8; i++) m.data[i] = 0;
    sys_send(g_mouse_tid, &m);
}

void cmd_mousetid(void) {
    puts_("mouse.elf tid = "); print_dec(g_mouse_tid); putc_('\n');
}

void cmd_killatad(void) {
    if (g_atad_tid < 0) { puts_("killatad: no atad.elf running\n"); return; }
    puts_("killatad: sending MSG_EXIT to tid=");
    print_dec(g_atad_tid);
    putc_('\n');
    user_msg_t m; m.sender = 0; m.type = MSG_EXIT;
    for (int i = 0; i < 8; i++) m.data[i] = 0;
    sys_send(g_atad_tid, &m);
}

void cmd_atadtid(void) {
    puts_("atad.elf tid = "); print_dec(g_atad_tid); putc_('\n');
}