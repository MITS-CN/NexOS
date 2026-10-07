/* user/vga.c —— 用户态 VGA 文本驱动进程 */

#include "syscall.h"
#include <stdint.h>

#define VGA_BASE    0x10000000u
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_ATTR    0x07

static volatile uint16_t *vga = (volatile uint16_t *)VGA_BASE;
static int cursor = 0;

/* ★ S5-fix: 0x0F ← 低字节，0x0E ← 高字节（和内核 vga_move_cursor 一致） */
static void set_hw_cursor(int pos) {
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
        vga[i] = vga[i + VGA_WIDTH];
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH;
         i < VGA_HEIGHT * VGA_WIDTH; i++)
        vga[i] = (uint16_t)((VGA_ATTR << 8) | ' ');
    cursor = (VGA_HEIGHT - 1) * VGA_WIDTH;
}

static void vga_putc(char c) {
    if (c == '\r') {
        cursor = (cursor / VGA_WIDTH) * VGA_WIDTH;
        set_hw_cursor(cursor);
        return;
    }
    if (c == '\n') {
        cursor = (cursor / VGA_WIDTH + 1) * VGA_WIDTH;
    } else if (c == '\b') {
        if (cursor > 0) {
            cursor--;
            vga[cursor] = (uint16_t)((VGA_ATTR << 8) | ' ');
        }
        set_hw_cursor(cursor);
        return;
    } else {
        vga[cursor++] = (uint16_t)((VGA_ATTR << 8) | (uint8_t)c);
    }
    if (cursor >= VGA_WIDTH * VGA_HEIGHT) vga_scroll();
    set_hw_cursor(cursor);
}

static void vga_loop(void) {
    user_msg_t m;

    for (;;) {
        if (sys_recv(&m) < 0) continue;

        if (m.type == MSG_EXIT) {
            sys_exit();
        }

        if (m.type != MSG_VGA_CHAR) continue;

        vga_putc((char)(m.data[0] & 0xFF));
    }
}

int main(void) {
    sys_io_perm(0x3D4);
    sys_io_perm(0x3D5);

    int r = sys_vga_claim();
    if (r < 0) {
        sys_exit();
    }

    int c = sys_vga_get_cursor();
    if (c >= 0 && c < VGA_WIDTH * VGA_HEIGHT) {
        cursor = c;
    }
    set_hw_cursor(cursor);

    vga_loop();

    sys_exit();
    return 0;
}