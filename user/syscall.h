#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>

/* ★ ABI 约定：和 kernel/thread.h 一致 */
#define MSG_IRQ            0x100
#define MSG_IRQ_OWNER_DIED 0x101
#define MSG_HELLO          0x200
#define MSG_CHAR           0x201
#define MSG_EXIT           0x202
#define MSG_VGA_CHAR       0x300
#define MSG_VGA_OWNER_DIED 0x301
/* ★ S5.5: 鼠标查询/应答 */
#define MSG_MOUSE_QUERY    0x302
#define MSG_MOUSE_REPORT   0x303

typedef struct {
    int      sender;
    int      type;
    uint32_t data[8];
} user_msg_t;

#define IRQ_NO_READ  0xFFFFu

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline int sys_print(const char *s) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(1), "b"(s)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline void sys_exit(void) {
    int dummy;
    __asm__ volatile("int $0x80"
        : "=a"(dummy)
        : "0"(2)
        : "ebx", "ecx", "edx", "esi", "edi", "memory");
    for (;;) __asm__ volatile("pause");
}

static inline int sys_getid(void) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(5)
        : "ebx", "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_getchar(void) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(6)
        : "ebx", "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline void sys_putchar(char c) {
    int dummy;
    __asm__ volatile("int $0x80"
        : "=a"(dummy)
        : "0"(7), "b"((int)c)
        : "ecx", "edx", "esi", "edi", "memory");
}

static inline int sys_open(const char *path, int flags) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(8), "b"(path), "c"(flags)
        : "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_close(int fd) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(9), "b"(fd)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_read(int fd, void *buf, int len) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(10), "b"(fd), "c"(buf), "d"(len)
        : "esi", "edi", "memory");
    return r;
}

static inline int sys_write(int fd, const void *buf, int len) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(11), "b"(fd), "c"(buf), "d"(len)
        : "esi", "edi", "memory");
    return r;
}

static inline int sys_readdir(int fd, int idx, char *name, int *type) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(12), "b"(fd), "c"(idx), "d"(name), "S"(type)
        : "edi", "memory");
    return r;
}

static inline int sys_mkdir(const char *path) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(13), "b"(path)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_unlink(const char *path) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(14), "b"(path)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_part_list(int drive, void *buf) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(15), "b"(drive), "c"(buf)
        : "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_part_mkp(int drive, int index, int type,
                               uint32_t start_lba, uint32_t sectors) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(16), "b"(drive), "c"(index),
          "d"(type), "S"(start_lba), "D"(sectors)
        : "memory");
    return r;
}

static inline int sys_meminfo(void) {
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "0"(20) : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline void sys_yield(void) {
    int dummy;
    __asm__ volatile("int $0x80"
        : "=a"(dummy)
        : "0"(19)
        : "ebx", "ecx", "edx", "esi", "edi", "memory");
}

static inline int sys_exec(const char *path) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(17), "b"(path)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_install(int drive) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(21), "b"(drive)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_io_perm(int port) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(22), "b"(port)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_irq_register(int irq, uint16_t read_port) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(23), "b"(irq), "c"((int)read_port)
        : "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_irq_unregister(int irq) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(24), "b"(irq)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_recv(user_msg_t *m) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(4), "b"(m)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_send(int tid, const user_msg_t *m) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(3), "b"(tid), "c"(m)
        : "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_exec_bg(const char *path) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(25), "b"(path)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_vga_claim(void) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(26)
        : "ebx", "ecx", "edx", "esi", "edi", "memory");
    return r;
}

static inline int sys_vga_get_cursor(void) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(27)
        : "ebx", "ecx", "edx", "esi", "edi", "memory");
    return r;
}

#endif