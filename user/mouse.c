/* user/mouse.c —— 用户态 PS/2 鼠标驱动进程 */

#include "syscall.h"
#include <stdint.h>

#define IRQ_MOUSE   12
#define MOUSE_PORT  0x60

static uint8_t packet[4];
static int     pkt_idx = 0;

static void emit_scroll(int z) {
    int vga_tid = sys_vga_get_owner();
    if (vga_tid < 0) {
        sys_print(z > 0 ? "[mouse] SCROLL UP (no vga)\n"
                        : "[mouse] SCROLL DOWN (no vga)\n");
        return;
    }

    user_msg_t m;
    m.sender  = 0;
    m.type    = MSG_SCROLL;
    m.data[0] = (uint32_t)(int32_t)z;
    for (int i = 1; i < 8; i++) m.data[i] = 0;
    sys_send(vga_tid, &m);
}

static void handle_byte(uint8_t b) {
    if (pkt_idx == 0 && !(b & 0x08)) return;

    packet[pkt_idx++] = b;

    if (pkt_idx == 4) {
        pkt_idx = 0;

        int z = (int8_t)(packet[3] << 4) >> 4;
        /* ★ 翻转：约定 +z = 上滚（看更早） */
        if (z != 0) emit_scroll(-z);
    }
}

int main(void) {
    int r = sys_irq_register(IRQ_MOUSE, MOUSE_PORT);
    if (r < 0) sys_exit();

    sys_mouse_enable();

    user_msg_t m;
    for (;;) {
        if (sys_recv(&m) < 0) continue;

        if (m.type == MSG_EXIT) sys_exit();

        if (m.type != MSG_IRQ) continue;
        if ((int)m.data[0] != IRQ_MOUSE) continue;

        handle_byte((uint8_t)(m.data[1] & 0xFF));
    }

    sys_exit();
    return 0;
}