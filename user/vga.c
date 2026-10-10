/* user/vga.c —— 用户态 VGA 文本驱动进程（带 scrollback） */

#include "syscall.h"
#include <stdint.h>

#define VGA_BASE      0x10000000u
#define SCREEN_COLS   80
#define SCREEN_LINES  25
#define SB_LINES      128          /* 环形缓冲总行数 */
#define VGA_ATTR      0x07
#define BLANK_CELL    ((uint16_t)((VGA_ATTR << 8) | ' '))

static volatile uint16_t *vga = (volatile uint16_t *)VGA_BASE;

/* scrollback 环形缓冲：SB_LINES × 80 格 */
static uint16_t sb[SB_LINES][SCREEN_COLS];

static int write_line = 0;    /* 当前写入行（环形索引） */
static int cur_col    = 0;    /* 当前列 */
static int view_offset = 0;   /* 0 = 显示最新；>0 = 往上翻 */

/* ★ 把 0x3D4/0x3D5 的 cursor 设到指定位置；pos 越界时会让光标消失 */
static void set_hw_cursor(int pos) {
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* 环形索引：write_line 偏移 off 行 */
static int line_idx(int off) {
    int idx = write_line + off;
    while (idx < 0) idx += SB_LINES;
    return idx % SB_LINES;
}

static void clear_line(int idx) {
    for (int i = 0; i < SCREEN_COLS; i++) sb[idx][i] = BLANK_CELL;
}

static void redraw(void) {
    for (int i = 0; i < SCREEN_LINES; i++) {
        int src = line_idx(i - SCREEN_LINES + 1 - view_offset);
        for (int j = 0; j < SCREEN_COLS; j++) {
            vga[i * SCREEN_COLS + j] = sb[src][j];
        }
    }

    /* 光标只在 view_offset == 0 时显示；浏览模式下把光标推出屏幕 */
    if (view_offset == 0) {
        set_hw_cursor((SCREEN_LINES - 1) * SCREEN_COLS + cur_col);
    } else {
        set_hw_cursor(SCREEN_LINES * SCREEN_COLS + 1);   /* 越界，硬件不显示 */
    }
}

static void sb_newline(void) {
    cur_col = 0;
    write_line = (write_line + 1) % SB_LINES;
    clear_line(write_line);
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

    /* 任何新字符写入 → 回到底部 */
    view_offset = 0;
    redraw();
}

static void sb_scroll(int z) {
    view_offset += z;
    if (view_offset < 0) view_offset = 0;
    if (view_offset > SB_LINES - SCREEN_LINES)
        view_offset = SB_LINES - SCREEN_LINES;
    redraw();
}

static void vga_loop(void) {
    user_msg_t m;

    for (;;) {
        if (sys_recv(&m) < 0) continue;

        if (m.type == MSG_EXIT) {
            /* 退出前把视图拉回底部，让硬件 CRTC 位置有效，
               内核 fallback 从这里接管光标不会错乱 */
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

    int r = sys_vga_claim();
    if (r < 0) sys_exit();

    /* 初始化 scrollback：全填空格 */
    for (int i = 0; i < SB_LINES; i++) clear_line(i);

    /* 从硬件拿当前 cursor（第一次启动时是内核留下的光标位置）。
       把它换算成 (行, 列) 并对齐 write_line 和 cur_col。 */
    int c = sys_vga_get_cursor();
    if (c >= 0 && c < SCREEN_LINES * SCREEN_COLS) {
        int line = c / SCREEN_COLS;
        int col  = c % SCREEN_COLS;

        /* 让当前屏幕内容落在 sb 的底部：write_line = SCREEN_LINES-1 附近 */
        for (int i = 0; i < SB_LINES; i++) {
            int src = (i < SCREEN_LINES) ? i : 0;
            (void)src;
        }

        /* write_line 停在屏幕第 SCREEN_LINES-1 行位置，也就是 sb 索引 SCREEN_LINES-1 */
        write_line = SCREEN_LINES - 1;
        cur_col    = col;

        /* 但这样新字符会覆盖屏幕最后一行；内核其实已经把内容写进显存了，
           我们搬到 sb 的 [0, SCREEN_LINES-1] 区间 */
        for (int i = 0; i < SCREEN_LINES; i++) {
            for (int j = 0; j < SCREEN_COLS; j++) {
                sb[i][j] = vga[i * SCREEN_COLS + j];
            }
        }
        (void)line;
    } else {
        write_line = 0;
        cur_col    = 0;
    }

    redraw();

    vga_loop();

    sys_exit();
    return 0;
}