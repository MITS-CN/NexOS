#include "syscall.h"
#include "idt.h"
#include "thread.h"
#include "sched.h"
#include "ipc.h"
#include "elf.h"
#include "vfs.h"
#include "part.h"
#include "elf_loader.h"
#include "heap.h"
#include "paging.h"
#include "pmm.h"
#include "install.h"
#include "gdt.h"
#include "irq.h"
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
#define SYS_IO_PERM 22
#define SYS_IRQ_REGISTER   23
#define SYS_IRQ_UNREGISTER 24
#define SYS_EXEC_BG        25

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

        /* ★ S4.6.2: 退出时通知父进程（如果自己是 IRQ owner） */
        case SYS_EXIT: {
            uint32_t *kdir = paging_kernel_dir();
            if (current_thread->page_dir &&
                current_thread->page_dir != kdir) {
                paging_switch_dir(kdir);
                paging_free_dir(current_thread->page_dir);
                current_thread->page_dir = 0;
            }

            thread_t *prev = current_thread->prev_owner;

            int dying_irq = irq_find_by_owner(current_thread->id);
            if (dying_irq >= 0) {
                /* 先释放，让新进程能立即注册 */
                irq_release_all(current_thread->id);

                if (prev) {
                    message_t m;
                    m.sender  = -1;
                    m.type    = MSG_IRQ_OWNER_DIED;
                    m.data[0] = (uint32_t)dying_irq;
                    for (int i = 1; i < 8; i++) m.data[i] = 0;
                    ipc_send(prev->id, &m);
                }
            }

            if (prev && prev->state == THREAD_BLOCKED) {
                prev->state = THREAD_READY;
            }

            current_thread->state = THREAD_DEAD;
            sched_yield();
            return 0;
        }

        case SYS_GETID:
            return current_thread->id;

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

        case SYS_IO_PERM: {
            if (!current_thread || !current_thread->is_user) return -1;
            uint16_t port = (uint16_t)(a & 0xFFFF);
            if (port != 0x3D4 && port != 0x3D5) return -2;
            tss_allow_io_port(port);
            return 0;
        }

        case SYS_IRQ_REGISTER: {
            if (!current_thread || !current_thread->is_user) return -1;
            int irq = (int)a;
            uint16_t read_port = (uint16_t)(b & 0xFFFF);
            if (irq == 0) return -2;
            return irq_register(irq, current_thread->id, read_port);
        }

        case SYS_IRQ_UNREGISTER: {
            if (!current_thread || !current_thread->is_user) return -1;
            int irq = (int)a;
            return irq_unregister(irq, current_thread->id);
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
            extern thread_t *sched_find(int tid);
            thread_t *t = sched_find((int)a);
            if (!t) return -1;
            if (t->state == THREAD_BLOCKED) t->state = THREAD_READY;
            return 0;
        }

        case SYS_PART_LIST: {
            if (!user_range_ok(b, 4 * 16)) return -1;
            partition_t table[4];
            if (part_read_table((int)a, table) < 0) return -1;

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

            t->prev_owner = current_thread;

            current_thread->state = THREAD_BLOCKED;
            sched_yield();

            return t->id;
        }

        /* ★ S4.6.2: 设置 prev_owner，让子进程死时能通知父进程 */
        case SYS_EXEC_BG: {
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

            /* ★ S4.6.2: 记录父进程（用于 IRQ owner 死亡通知） */
            t->prev_owner = current_thread;

            return t->id;
        }

        default:
            return -1;
    }
}