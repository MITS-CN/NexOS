#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>   /* ★ 加这行 */

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

#endif