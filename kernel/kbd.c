#include "kbd.h"
#include "io.h"
#include "thread.h"
#include "sched.h"

#define KBD_BUF_SIZE 256

static volatile uint8_t  kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0;
static volatile uint32_t kbd_tail = 0;

static int shift_pressed = 0;
static int ctrl_pressed  = 0;
static thread_t *kbd_owner = 0; 

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

void kbd_init(void) {
    kbd_head      = 0;
    kbd_tail      = 0;
    shift_pressed = 0;
    ctrl_pressed  = 0;
}

void kbd_irq(void) {
    uint8_t sc = inb(0x60);

    if (sc == 0x2A || sc == 0x36) { shift_pressed = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift_pressed = 0; return; }

    if (sc == 0x1D) { ctrl_pressed = 1; return; }
    if (sc == 0x9D) { ctrl_pressed = 0; return; }

    if (sc & 0x80) return;
    if (sc >= 128) return;

    char c = shift_pressed ? shift_map[sc] : scancode_map[sc];
    if (c == 0) return;

    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail) return;
    kbd_buf[kbd_head] = (uint8_t)c;
    kbd_head = next;
}

int kbd_getchar(void) {
    if (kbd_head == kbd_tail) return -1;
    char c = (char)kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return (int)c;
}

void kbd_wait(void) {
    /* 用 hlt 循环代替阻塞，见 syscall.c 的 SYS_GETCHAR */
}

/* 非阻塞读键：从缓冲区取，没数据返回 0 */
int kbd_poll(void) {
    if (kbd_head != kbd_tail) {
        char c = (char)kbd_buf[kbd_tail];
        kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
        return (int)(unsigned char)c;
    }
    return 0;
}

/* 阻塞读一个可见字符 */
static int kbd_wait_char(void) {
    for (;;) {
        int c = kbd_poll();
        if (c > 0 && c < 128) return c;
        __asm__ volatile("hlt");
    }
}

/* Y/N 确认，返回 1 = yes，0 = no */
int kbd_confirm(const char *prompt) {
    extern void vga_puts(const char *);
    extern void vga_putc(char);

    vga_puts(prompt);
    vga_puts(" [y/N] ");

    for (;;) {
        int c = kbd_wait_char();
        if (c == 'y' || c == 'Y') {
            vga_puts("yes\n");
            return 1;
        }
        if (c == 'n' || c == 'N' || c == '\n' || c == '\r') {
            vga_puts("no\n");
            return 0;
        }
    }
}

thread_t *kbd_get_owner(void) { return kbd_owner; }
void      kbd_set_owner(thread_t *t) { kbd_owner = t; }