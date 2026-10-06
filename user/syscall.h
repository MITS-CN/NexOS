#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>   /* ★ 加这行 */

/* ★ S3: 和内核 thread.h 里的 user_msg_t 二进制兼容 */
typedef struct {
    int      sender;
    int      type;
    uint32_t data[8];
} user_msg_t;

/* ★ S4: 键盘驱动 IPC 协议（和 kernel/irq.h 的 MSG_IRQ 对应） */
#define MSG_IRQ   0x100   /* 内核 → kbd：scancode（data[0]=irq, data[1]=sc） */
#define MSG_HELLO 0x200   /* shell → kbd：我是你的输出目标 */
#define MSG_CHAR  0x201   /* kbd → shell：一个字符（data[0]=char） */

/* ★ S1：用户态端口 I/O 内联（需要先 sys_io_perm 开放） */
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

/* ★ S1: 请求开放用户态 I/O 端口 */
static inline int sys_io_perm(int port) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(22), "b"(port)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

/* ★ S3: 注册 IRQ 独占 */
static inline int sys_irq_register(int irq) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(23), "b"(irq)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

/* ★ S3: 解绑 IRQ */
static inline int sys_irq_unregister(int irq) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(24), "b"(irq)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

/* ★ S3: 阻塞接收 IPC 消息 */
static inline int sys_recv(user_msg_t *m) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(4), "b"(m)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

/* ★ S4: 发送 IPC 消息 */
static inline int sys_send(int tid, const user_msg_t *m) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(3), "b"(tid), "c"(m)
        : "edx", "esi", "edi", "memory");
    return r;
}

/* ★ S4: 后台 exec —— 加载并运行 ELF，不阻塞当前进程，返回新进程 tid */
static inline int sys_exec_bg(const char *path) {
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "0"(25), "b"(path)
        : "ecx", "edx", "esi", "edi", "memory");
    return r;
}

#endif