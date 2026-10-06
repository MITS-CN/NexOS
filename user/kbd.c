/* user/kbd.c —— 用户态键盘驱动进程
 *
 * 职责：
 *   1. 独占 IRQ1
 *   2. 收到 scancode，维护 Shift/Ctrl 状态，翻译成字符
 *   3. 把字符通过 IPC 发给"输出目标"（由 shell 通过 MSG_HELLO 告知）
 */

#include "syscall.h"
#include <stdint.h>

#define IRQ_KBD  1

/* 普通字符映射（小写） */
static const char scancode_map[128] = {
    0,    27,   '1',  '2',  '3',  '4',  '5',  '6',
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0
};

/* Shift 状态下的字符映射 */
static const char shift_map[128] = {
    0,    27,   '!',  '@',  '#',  '$',  '%',  '^',
    '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
    'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
    0,    ' ',  0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0
};

static int shift_pressed = 0;
static int ctrl_pressed  = 0;
static int out_tid       = -1;   /* shell 的 tid，由 MSG_HELLO 告知 */

static void kbd_loop(void) {
    user_msg_t m;

    for (;;) {
        if (sys_recv(&m) < 0) continue;

        /* shell 告诉我它的 tid */
        if (m.type == MSG_HELLO) {
            out_tid = m.sender;
            continue;
        }

        /* 只处理 IRQ1 消息 */
        if (m.type != MSG_IRQ) continue;
        if ((int)m.data[0] != IRQ_KBD) continue;

        uint8_t sc = (uint8_t)m.data[1];

        /* 修饰键状态 */
        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
        if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; continue; }
        if (sc == 0x1D) { ctrl_pressed = 1; continue; }
        if (sc == 0x9D) { ctrl_pressed = 0; continue; }

        /* 释放事件（最高位为 1）忽略 */
        if (sc & 0x80) continue;
        if (sc >= 128) continue;

        char c = shift_pressed ? shift_map[sc] : scancode_map[sc];
        if (c == 0) continue;

        /* 发送字符给 shell */
        if (out_tid >= 0) {
            user_msg_t out;
            out.sender  = 0;
            out.type    = MSG_CHAR;
            out.data[0] = (uint32_t)(uint8_t)c;
            for (int i = 1; i < 8; i++) out.data[i] = 0;
            sys_send(out_tid, &out);
        }
    }
}

int main(void) {
    /* 独占 IRQ1 */
    int r = sys_irq_register(IRQ_KBD);
    if (r < 0) {
        /* 注册失败，退出（内核会在线程死时释放所有 IRQ） */
        sys_exit();
    }

    kbd_loop();

    sys_exit();
    return 0;
}