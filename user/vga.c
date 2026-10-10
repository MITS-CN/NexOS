/* user/vga.c —— 用户态 VGA 文本驱动进程（带 scrollback） */

#include "syscall.h"
#include <stdint.h>

#define VGA_BASE      0x10000000u
#define SCREEN_COLS   80
#define SCREEN_LINES  25
#define SB_LINES      128
#define VGA_ATTR      0x07
#define BLANK_CELL    ((uint16_t)((VGA_ATTR << 8) | ' '))

static volatile uint16_t *vga = (volatile uint16_t *)VGA_BASE;

static uint16_t sb[SB_LINES][SCREEN_COLS];

static int write_line = 0;
static int cur_col    = 0;
static int view_offset = 0;
static int total_lines_written = 0;

static void set_hw_cursor(int pos) {
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static int line_idx(int off) {
    int idx = write_line + off;
    while (idx < 0) idx += SB_LINES;
    return idx % SB_LINES;
}

static void clear_line(int idx) {
    for (int i = 0; i < SCREEN_COLS; i++) sb[idx][i] = BLANK_CELL;
}

static int max_view_offset(void) {
    int m = total_lines_written - SCREEN_LINES;
    if (m < 0) m = 0;
    if (m > SB_LINES - SCREEN_LINES) m = SB_LINES - SCREEN_LINES;
    return m;
}

/* ★ 改：banner 移到屏幕最后一行右下角，不再遮顶行 */
static void show_scrollback_banner(void) {
    const uint16_t attr = (uint16_t)((0x0F << 8));   /* 亮白 */

    char buf[24];
    int n = 0;
    const char *pfx = "[SB +";
    while (pfx[n]) { buf[n] = pfx[n]; n++; }

    int v = view_offset;
    char digits[8];
    int d = 0;
    if (v == 0) digits[d++] = '0';
    while (v > 0) { digits[d++] = '0' + (v % 10); v /= 10; }
    while (d > 0) buf[n++] = digits[--d];

    buf[n++] = ']';
    buf[n] = 0;

    int row = SCREEN_LINES - 1;
    int start = SCREEN_COLS - n;
    if (start < 0) start = 0;
    for (int i = start; i < SCREEN_COLS; i++) {
        char c = buf[i - start];
        vga[row * SCREEN_COLS + i] = (uint16_t)(attr | (uint8_t)c);
    }
}

static void redraw(void) {
    for (int i = 0; i < SCREEN_LINES; i++) {
        int src = line_idx(i - SCREEN_LINES + 1 - view_offset);
        for (int j = 0; j < SCREEN_COLS; j++) {
            vga[i * SCREEN_COLS + j] = sb[src][j];
        }
    }

    if (view_offset > 0) {
        show_scrollback_banner();
    }

    if (view_offset == 0) {
        set_hw_cursor((SCREEN_LINES - 1) * SCREEN_COLS + cur_col);
    } else {
        set_hw_cursor(SCREEN_LINES * SCREEN_COLS + 1);
    }
}

static void sb_newline(void) {
    cur_col = 0;
    write_line = (write_line + 1) % SB_LINES;
    clear_line(write_line);
    total_lines_written++;
}

static void sb_putc(char c) {
    if (c == '\r') {
        cur_col = 0;
    } else if (c == '\n') {
        sb_newline();
    } else if (c == '\b') {
        if (cur_col > 0) {
            cur_col--;
            sb[write_line][cur_col] = BLANK_CELL;
        }
    } else {
        sb[write_line][cur_col] = (uint16_t)((VGA_ATTR << 8) | (uint8_t)c);
        cur_col++;
        if (cur_col >= SCREEN_COLS) sb_newline();
    }

    view_offset = 0;
    redraw();
}

static void sb_scroll(int z) {
    view_offset += z;

    int maxv = max_view_offset();
    if (view_offset < 0)    view_offset = 0;
    if (view_offset > maxv) view_offset = maxv;

    redraw();
}

static void vga_loop(void) {
    user_msg_t m;

    for (;;) {
        if (sys_recv(&m) < 0) continue;

        if (m.type == MSG_EXIT) {
            view_offset = 0;
            redraw();
            sys_exit();
        }

        if (m.type == MSG_VGA_CHAR) {
            sb_putc((char)(m.data[0] & 0xFF));
            continue;
        }

        if (m.type == MSG_SCROLL) {
            int z = (int)(int32_t)m.data[0];
            sb_scroll(z);
            continue;
        }
    }
}

int main(void) {
    sys_io_perm(0x3D4);
    sys_io_perm(0x3D5);

    klog_info_t info;
    int r = sys_vga_fetch_log((void *)sb, &info);

    if (r == 0) {
        write_line = (int)(info.write_line % SB_LINES);
        cur_col    = (int)(info.cur_col % SCREEN_COLS);
        total_lines_written = (int)info.total_written;

        if (write_line < 0) write_line = 0;
        if (cur_col < 0)    cur_col = 0;
        if (total_lines_written < 0) total_lines_written = 0;
    } else {
        for (int i = 0; i < SB_LINES; i++) clear_line(i);
        write_line = 0;
        cur_col = 0;
        total_lines_written = 0;
    }

    int r2 = sys_vga_claim();
    if (r2 < 0) sys_exit();

    redraw();

    vga_loop();

    sys_exit();
    return 0;
}