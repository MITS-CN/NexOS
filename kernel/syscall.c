#include "syscall.h"
#include "idt.h"
#include "thread.h"
#include "sched.h"
#include "ipc.h"
#include "kbd.h"
#include "elf.h"
#include "vfs.h"
#include "part.h"
#include "elf_loader.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "install.h"
#include "gdt.h"          /* S2: tss_allow_io_port */
#include <stdint.h>

#define SYS_PRINT    1
#define SYS_EXIT     2
#define SYS_SEND     3
#define SYS_RECV     4
#define SYS_GETID    5
#define SYS_GETCHAR  6
#define SYS_PUTCHAR  7
#define SYS_OPEN    8
#define SYS_CLOSE   9
#define SYS_READ    10
#define SYS_WRITE   11
#define SYS_READDIR 12
#define SYS_MKDIR   13
#define SYS_UNLINK  14
#define SYS_PART_LIST  15
#define SYS_PART_MKP   16
#define SYS_EXEC    17
#define SYS_FG      18
#define SYS_YIELD   19
#define SYS_MEMINFO 20
#define SYS_INSTALL 21
#define SYS_IO_PERM 22       /* S1 新增 */

extern void isr128(void);
extern void vga_putc(char c);

int syscall_handler(uint32_t num, uint32_t a, uint32_t b,
                    uint32_t c, uint32_t d, uint32_t e);

void syscall_init(void) {
    idt_set(0x80, (uint32_t)isr128, 0x08, 0xEE);
}

static int user_range_ok(uint32_t p, uint32_t len) {
    if (len == 0) return 1;
    if (p < USER_BASE) return 0;
    if (p + len < p) return 0;
    if (p + len > USER_LIMIT) return 0;
    return 1;
}

static int user_str_ok(const char *s, uint32_t maxlen) {
    if ((uint32_t)s < USER_BASE) return 0;
    for (uint32_t i = 0; i < maxlen; i++) {
        uint32_t addr = (uint32_t)s + i;
        if (addr < USER_BASE || addr >= USER_LIMIT) return 0;
        if (s[i] == 0) return 1;
    }
    return 0;
}

int syscall_handler(uint32_t num, uint32_t a, uint32_t b,
                    uint32_t c, uint32_t d, uint32_t e) {
    (void)c; (void)d; (void)e;

    switch (num) {
        case SYS_PRINT: {
            extern void vga_puts(const char *);
            if (!user_str_ok((const char *)a, 4096)) return -1;
            vga_puts((const char *)a);
            return 0;
        }
                case SYS_OPEN: {
            if (!user_str_ok((const char *)a, MAX_PATH)) return -1;
            return vfs_open((const char *)a, (int)b);
        }

        case SYS_CLOSE:
            return vfs_close((int)a);

        case SYS_READ: {
            if (!user_range_ok(b, c)) return -1;
            return vfs_fd_read((int)a, (uint8_t *)b, c);
        }

        case SYS_WRITE: {
            if (!user_range_ok(b, c)) return -1;
            return vfs_fd_write((int)a, (const uint8_t *)b, c);
        }

        case SYS_READDIR: {
            int fd  = (int)a;
            int idx = (int)b;
            if (!user_range_ok(c, MAX_NAME)) return -1;
            if (!user_range_ok(d, 4)) return -1;
            return vfs_fd_readdir(fd, idx, (char *)c, (int *)d);
        }

        case SYS_MKDIR: {
            if (!user_str_ok((const char *)a, MAX_PATH)) return -1;
            return vfs_create((const char *)a, VFS_DIR) ? 0 : -1;
        }

        case SYS_UNLINK: {
            if (!user_str_ok((const char *)a, MAX_PATH)) return -1;
            return vfs_unlink((const char *)a);
        }

        case SYS_EXIT: {
            /* 1. 切回内核页目录（当前还在用户页目录上） */
            uint32_t *kdir = paging_kernel_dir();
            if (current_thread->page_dir &&
                current_thread->page_dir != kdir) {
                paging_switch_dir(kdir);

                /* 2. 释放用户空间 */
                paging_free_dir(current_thread->page_dir);
                current_thread->page_dir = 0;
            }

            /* 3. 把键盘还给 prev_owner */
            thread_t *prev = current_thread->prev_owner;
            if (prev && prev->state == THREAD_BLOCKED) {
                prev->state = THREAD_READY;
                kbd_set_owner(prev);
            } else {
                kbd_set_owner(0);
            }

            /* 4. 标记自己 DEAD */
            current_thread->state = THREAD_DEAD;
            sched_yield();
            return 0;
        }

        case SYS_GETID:
            return current_thread->id;
        
        case SYS_GETCHAR: {
            for (;;) {
                thread_t *owner = kbd_get_owner();
                if (owner && owner != current_thread) {
                    /* 不是前台线程：阻塞 */
                    current_thread->state = THREAD_BLOCKED;
                    sched_yield();
                    continue;
                }
                if (!owner) kbd_set_owner(current_thread);   /* 这行 */

                int ch = kbd_getchar();
                if (ch >= 0) return ch;
                __asm__ volatile("sti; hlt");
            }
        }

        case SYS_YIELD:
            sched_yield();
            return 0;

        
        case SYS_PUTCHAR:
            vga_putc((char)a);
            return 0;

        case SYS_MEMINFO:
            return (int)(pmm_total_pages() - pmm_used_pages());

        case SYS_INSTALL:
            return install_to_drive((int)a);

        /* S1 新增：请求开放用户态 I/O 端口 */
        case SYS_IO_PERM: {
            /* 只允许用户线程调用 */
            if (!current_thread || !current_thread->is_user) return -1;

            uint16_t port = (uint16_t)(a & 0xFFFF);

            /* 白名单：目前只放行 VGA CRTC 的索引/数据端口 */
            if (port != 0x3D4 && port != 0x3D5) return -2;

            tss_allow_io_port(port);
            return 0;
        }

        case SYS_SEND: {
            int tid = (int)a;
            if (!user_range_ok(b, sizeof(user_msg_t))) return -1;
            user_msg_t *um = (user_msg_t *)b;
            message_t m = {0};
            m.sender = current_thread->id;
            m.type   = um->type;
            for (int i = 0; i < 8; i++) m.data[i] = um->data[i];
            return ipc_send(tid, &m);
        }

        case SYS_RECV: {
            if (!user_range_ok(a, sizeof(user_msg_t))) return -1;
            user_msg_t *um = (user_msg_t *)a;
            message_t m;
            ipc_recv(&m);
            um->sender = m.sender;
            um->type   = m.type;
            for (int i = 0; i < 8; i++) um->data[i] = m.data[i];
            return 0;
        }

        case SYS_FG: {
            thread_t *t = 0;

            /* 通过 id 找 thread */

            extern thread_t *sched_find(int tid);
            t = sched_find((int)a);
            if (!t) return -1;
            kbd_set_owner(t);

            /* 唤醒新前台（如果它阻塞） */

            if (t->state == THREAD_BLOCKED) t->state = THREAD_READY;
            return 0;
        }

        case SYS_PART_LIST: {
            /* a = drive, b = 用户缓冲区（4 * 16 字节） */
            if (!user_range_ok(b, 4 * 16)) return -1;
            partition_t table[4];
            if (part_read_table((int)a, table) < 0) return -1;

            /* 打包成 4*16 字节发给用户 */
            uint8_t *out = (uint8_t *)b;
            for (int i = 0; i < 4; i++) {
                uint8_t *p = out + i * 16;
                p[0] = table[i].bootable ? 1 : 0;
                p[1] = (uint8_t)table[i].type;
                p[2] = 0; p[3] = 0;
                p[4]  = table[i].start_lba & 0xFF;
                p[5]  = (table[i].start_lba >> 8) & 0xFF;
                p[6]  = (table[i].start_lba >> 16) & 0xFF;
                p[7]  = (table[i].start_lba >> 24) & 0xFF;
                p[8]  = table[i].sectors & 0xFF;
                p[9]  = (table[i].sectors >> 8) & 0xFF;
                p[10] = (table[i].sectors >> 16) & 0xFF;
                p[11] = (table[i].sectors >> 24) & 0xFF;
                p[12] = 0; p[13] = 0; p[14] = 0; p[15] = 0;
            }
            return 0;
        }

        case SYS_PART_MKP: {
            int drive = (int)a;
            int index = (int)b;
            int type  = (int)c;
            uint32_t start_lba = d;
            uint32_t sectors   = e;
            partition_t ent;
            ent.bootable  = 0;
            ent.type      = type;
            ent.start_lba = start_lba;
            ent.sectors   = sectors;
            return part_set_entry(drive, index, &ent);
        }

        case SYS_EXEC: {
            if (!user_str_ok((const char *)a, MAX_PATH)) return -1;

            char kpath[MAX_PATH];
            const char *upath = (const char *)a;
            int i = 0;
            while (upath[i] && i < MAX_PATH - 1) { kpath[i] = upath[i]; i++; }
            kpath[i] = 0;

            int fd = vfs_open(kpath, 0);
            if (fd < 0) return -2;

            uint32_t sz = vfs_fd_size(fd);
            if (sz == 0 || sz > 512 * 1024) { vfs_close(fd); return -3; }

            uint8_t *buf = (uint8_t *)kmalloc(sz);
            if (!buf) { vfs_close(fd); return -4; }

            int rd = vfs_fd_read(fd, buf, sz);
            vfs_close(fd);
            if (rd != (int)sz) { kfree(buf); return -5; }

            uint32_t *dir = paging_create_dir();
            if (!dir) { kfree(buf); return -6; }

            elf_load_result_t elf;
            int r = elf_load_to_dir(dir, buf, sz, &elf);
            kfree(buf);
            if (r != 0) return -7;

            thread_t *t = thread_create_elf(elf.entry, elf.stack_top, dir);
            if (!t) return -8;

            /* 记录链：新线程的 prev_owner 是当前 owner */
            t->prev_owner = kbd_get_owner();
            if (!t->prev_owner) t->prev_owner = current_thread;

            /* 键盘交给新线程 */
            kbd_set_owner(t);

            /* 当前线程阻塞，等新线程退出 */
            current_thread->state = THREAD_BLOCKED;
            sched_yield();

            /* 被唤醒后（新线程 exit）继续执行，返回 tid */
            return t->id;
        }

        default:
            return -1;
    }
}