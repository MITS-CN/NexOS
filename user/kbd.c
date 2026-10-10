/* user/kbd.c —— 用户态键盘驱动进程 */

#include "syscall.h"
#include <stdint.h>

#define IRQ_KBD       1
#define KBD_DATA_PORT 0x60

/* 特殊键值（和 shell 约定） */
#define KEY_UP    0x80
#define KEY_DOWN  0x81

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
static int out_tid       = -1;

static void kbd_loop(void) {
    user_msg_t m;

    for (;;) {
        if (sys_recv(&m) < 0) continue;

        if (m.type == MSG_HELLO) {
            out_tid = m.sender;
            continue;
        }

        if (m.type == MSG_EXIT) {
            sys_exit();
        }

        if (m.type != MSG_IRQ) continue;
        if ((int)m.data[0] != IRQ_KBD) continue;

        uint8_t sc = (uint8_t)m.data[1];

        if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; continue; }
        if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; continue; }
        if (sc == 0x1D) { ctrl_pressed = 1; continue; }
        if (sc == 0x9D) { ctrl_pressed = 0; continue; }

        if (sc & 0x80) continue;
        if (sc >= 128) continue;

        /* ★ C3: 方向键 —— 0x48 上 / 0x50 下 */
        if (sc == 0x48 || sc == 0x50) {
            if (out_tid >= 0) {
                user_msg_t out;
                out.sender  = 0;
                out.type    = MSG_CHAR;
                out.data[0] = (sc == 0x48) ? KEY_UP : KEY_DOWN;
                for (int i = 1; i < 8; i++) out.data[i] = 0;
                sys_send(out_tid, &out);
            }
            continue;
        }

        /* S5.6 C1: Ctrl+C → MSG_SIGINT（sc 0x2E = 'c'） */
        if (ctrl_pressed && sc == 0x2E) {
            if (out_tid >= 0) {
                user_msg_t sig;
                sig.sender = 0;
                sig.type   = MSG_SIGINT;
                for (int i = 0; i < 8; i++) sig.data[i] = 0;
                sys_send(out_tid, &sig);
            }
            continue;
        }

        char c = shift_pressed ? shift_map[sc] : scancode_map[sc];
        if (c == 0) continue;

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
    int r = sys_irq_register(IRQ_KBD, KBD_DATA_PORT);
    if (r < 0) {
        sys_exit();
    }

    kbd_loop();
    sys_exit();
    return 0;
}