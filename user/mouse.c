#include "syscall.h"
#include <stdint.h>

#define IRQ_MOUSE   12
#define MOUSE_PORT  0x60

static uint8_t packet[4];
static int     pkt_idx = 0;

static void emit_scroll(int z) {
    if (z > 0) {
        sys_print("[mouse] SCROLL UP\n");
    } else {
        sys_print("[mouse] SCROLL DOWN\n");
    }
}

static void handle_byte(uint8_t b) {
    /* 同步：包首字节 bit3 必须为 1 */
    if (pkt_idx == 0 && !(b & 0x08)) return;

    packet[pkt_idx++] = b;

    if (pkt_idx == 4) {
        pkt_idx = 0;

        /* IntelliMouse：byte[3] 低 4 位 = Z 轴，有符号 */
        int z = (int8_t)(packet[3] << 4) >> 4;

        if (z != 0) emit_scroll(z);
    }
}

int main(void) {
    /* 1. 注册 IRQ12，内核中断里读端口 0x60 */
    int r = sys_irq_register(IRQ_MOUSE, MOUSE_PORT);
    if (r < 0) sys_exit();

    /* 2. 让鼠标开始上报（0xF4 to aux port） */
    sys_mouse_enable();

    /* 3. 收 IRQ12 消息 → 组包 */
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