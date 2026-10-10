#include <stdint.h>
#include "gdt.h"
#include "idt.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "thread.h"
#include "timer.h"
#include "sched.h"
#include "serial.h"
#include "syscall.h"
#include "elf_loader.h"
#include "elf.h"
#include "io.h"
#include "vfs.h"
#include "ata.h"
#include "nxfs.h"
#include "install.h"
#include "ipc.h"

#define VGA_MEMORY ((volatile uint16_t *)0xB8000)
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

/* ★ S5.6: 内核启动日志环形缓冲（和 vga.elf 的 sb 结构一致） */
#define KLOG_LINES 128
#define KLOG_BYTES (KLOG_LINES * VGA_WIDTH * 2)

static uint16_t klog[KLOG_LINES][VGA_WIDTH];
static int      klog_write_line = 0;
static int      klog_col        = 0;
static uint32_t klog_total      = 1;   /* 已分配行数（含正在写的那行） */

static int cursor = 0;

static int vga_owner_tid = -1;
static int panic_mode = 0;

void vga_set_panic(int on) { panic_mode = on ? 1 : 0; }

static void vga_move_cursor(void) {
    uint16_t pos = cursor;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void vga_scroll(void) {
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
        VGA_MEMORY[i] = VGA_MEMORY[i + VGA_WIDTH];
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH;
         i < VGA_HEIGHT * VGA_WIDTH; i++)
        VGA_MEMORY[i] = (uint16_t)((0x07 << 8) | ' ');
    cursor = (VGA_HEIGHT - 1) * VGA_WIDTH;
}

/* ---- klog ---- */

static void klog_blank_line(int idx) {
    for (int i = 0; i < VGA_WIDTH; i++)
        klog[idx][i] = (uint16_t)((0x07 << 8) | ' ');
}

static void klog_newline(void) {
    klog_col = 0;
    klog_write_line = (klog_write_line + 1) % KLOG_LINES;
    klog_blank_line(klog_write_line);
    klog_total++;
}

static void klog_putc(char c) {
    if (c == '\r') {
        klog_col = 0;
    } else if (c == '\n') {
        klog_newline();
    } else if (c == '\b') {
        if (klog_col > 0) {
            klog_col--;
            klog[klog_write_line][klog_col] = (uint16_t)((0x07 << 8) | ' ');
        }
    } else {
        klog[klog_write_line][klog_col] = (uint16_t)((0x07 << 8) | (uint8_t)c);
        klog_col++;
        if (klog_col >= VGA_WIDTH) klog_newline();
    }
}

static void klog_reset(void) {
    for (int i = 0; i < KLOG_LINES; i++) klog_blank_line(i);
    klog_write_line = 0;
    klog_col        = 0;
    klog_total      = 1;
}

/* ★ S5.6: 供 syscall.c 调用 —— 把 klog 拷到用户空间 + 填 info */
int vga_fetch_log(uint8_t *dst, uint32_t dst_size, void *info_ptr) {
    if (dst_size < KLOG_BYTES) return -1;

    const uint8_t *k = (const uint8_t *)klog;
    for (uint32_t i = 0; i < KLOG_BYTES; i++) dst[i] = k[i];

    uint32_t *info = (uint32_t *)info_ptr;
    info[0] = klog_total;
    info[1] = (uint32_t)klog_write_line;
    info[2] = (uint32_t)klog_col;
    info[3] = 0;
    return 0;
}

void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = (uint16_t)((0x07 << 8) | ' ');
    cursor = 0;

    klog_reset();

    outb(0x3D4, 0x0A); outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);
    vga_move_cursor();
}

static uint16_t vga_read_hw_cursor(void) {
    outb(0x3D4, 0x0F);
    uint8_t lo = inb(0x3D5);
    outb(0x3D4, 0x0E);
    uint8_t hi = inb(0x3D5);
    return ((uint16_t)hi << 8) | lo;
}

int vga_get_cursor(void) {
    return cursor;
}

void vga_set_owner(int tid) {
    vga_owner_tid = tid;
}

void vga_clear_owner(void) {
    if (vga_owner_tid < 0) return;
    uint16_t hw = vga_read_hw_cursor();
    if (hw < VGA_WIDTH * VGA_HEIGHT) {
        cursor = (int)hw;
    }
    vga_owner_tid = -1;
}

int vga_get_owner(void) {
    return vga_owner_tid;
}

void vga_putc(char c) {
    serial_putc(c);

    /* ★ S5.6: 无条件写 klog（无论走 IPC 还是 fallback，klog 都是权威） */
    klog_putc(c);

    if (!panic_mode && vga_owner_tid >= 0) {
        message_t m;
        m.sender  = -1;
        m.type    = MSG_VGA_CHAR;
        m.data[0] = (uint32_t)(uint8_t)c;
        for (int i = 1; i < 8; i++) m.data[i] = 0;
        ipc_send(vga_owner_tid, &m);
        return;
    }

    if (c == '\r') {
        cursor = (cursor / VGA_WIDTH) * VGA_WIDTH;
        vga_move_cursor();
        return;
    }
    if (c == '\n') {
        cursor = (cursor / VGA_WIDTH + 1) * VGA_WIDTH;
    } else if (c == '\b') {
        if (cursor > 0) {
            cursor--;
            VGA_MEMORY[cursor] = (uint16_t)((0x07 << 8) | ' ');
        }
        vga_move_cursor();
        return;
    } else {
        VGA_MEMORY[cursor++] = (uint16_t)((0x07 << 8) | (uint8_t)c);
    }
    if (cursor >= VGA_WIDTH * VGA_HEIGHT) vga_scroll();
    vga_move_cursor();
}

void vga_puts(const char *s) { while (*s) vga_putc(*s++); }

void vga_hex(uint32_t v) {
    const char *h = "0123456789ABCDEF";
    vga_puts("0x");
    for (int i = 28; i >= 0; i -= 4) vga_putc(h[(v >> i) & 0xF]);
}

/* ---- 8042 helpers ---- */

static void kbd_wait_input_clear(void) {
    for (int i = 0; i < 100000; i++) {
        if (!(inb(0x64) & 0x02)) return;
    }
}

static void kbd_wait_output_full(void) {
    for (int i = 0; i < 100000; i++) {
        if (inb(0x64) & 0x01) return;
    }
}

static void kbd_drain_output(void) {
    for (int i = 0; i < 128; i++) {
        if (!(inb(0x64) & 0x01)) break;
        inb(0x60);
    }
}

static void mouse_write(uint8_t data) {
    kbd_wait_input_clear();
    outb(0x64, 0xD4);
    kbd_wait_input_clear();
    outb(0x60, data);
    kbd_wait_output_full();
    inb(0x60);
}

static uint8_t mouse_read_id(void) {
    kbd_wait_input_clear();
    outb(0x64, 0xD4);
    kbd_wait_input_clear();
    outb(0x60, 0xF2);
    kbd_wait_output_full();
    inb(0x60);
    kbd_wait_output_full();
    return inb(0x60);
}

static void kbd_8042_init(void) {
    kbd_drain_output();

    kbd_wait_input_clear();
    outb(0x64, 0xAE);
    for (volatile int i = 0; i < 1000; i++);

    kbd_wait_input_clear();
    outb(0x64, 0xA8);
    for (volatile int i = 0; i < 1000; i++);

    kbd_drain_output();

    kbd_wait_input_clear();
    outb(0x64, 0x20);
    kbd_wait_output_full();
    uint8_t cfg = inb(0x60);

    uint8_t new_cfg = cfg;
    int need_write = 0;
    if (!(cfg & 0x01)) { new_cfg |= 0x01; need_write = 1; }
    if (!(cfg & 0x02)) { new_cfg |= 0x02; need_write = 1; }
    if (cfg & 0x20)    { new_cfg &= ~0x20; need_write = 1; }

    if (need_write) {
        kbd_wait_input_clear();
        outb(0x64, 0x60);
        kbd_wait_input_clear();
        outb(0x60, new_cfg);
    }

    kbd_drain_output();

    kbd_wait_input_clear();
    outb(0x60, 0xF4);
    kbd_wait_output_full();
    inb(0x60);
    kbd_drain_output();

    mouse_write(0xF6);
    kbd_drain_output();

    mouse_write(0xF3); mouse_write(200);
    mouse_write(0xF3); mouse_write(100);
    mouse_write(0xF3); mouse_write(80);
    uint8_t id = mouse_read_id();
    (void)id;

    kbd_drain_output();
}

const uint8_t *g_init_elf_data  = 0;
uint32_t       g_init_elf_size  = 0;
const uint8_t *g_kbd_elf_data   = 0;
uint32_t       g_kbd_elf_size   = 0;
const uint8_t *g_vga_elf_data   = 0;
uint32_t       g_vga_elf_size   = 0;
const uint8_t *g_mouse_elf_data = 0;
uint32_t       g_mouse_elf_size = 0;

#define MULTIBOOT_BOOTLOADER_MAGIC  0x2BADB002

typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} mb_module_t;

static int mb_module_find(uint32_t mbi, const char *name,
                          uint32_t *out_start, uint32_t *out_end) {
    if (!mbi) return -1;

    uint32_t flags = *(uint32_t *)mbi;
    if (!(flags & (1u << 3))) return -1;

    uint32_t mods_count = *(uint32_t *)(mbi + 20);
    uint32_t mods_addr  = *(uint32_t *)(mbi + 24);
    if (mods_count == 0 || mods_addr == 0) return -1;

    mb_module_t *mods = (mb_module_t *)mods_addr;

    for (uint32_t i = 0; i < mods_count; i++) {
        const char *cmdline = (const char *)mods[i].cmdline;
        if (!cmdline) continue;

        while (*cmdline == ' ') cmdline++;
        const char *a = name;
        const char *b = cmdline;
        while (*a && *a == *b) { a++; b++; }
        if (*a != 0) continue;
        if (*b != 0 && *b != ' ') continue;

        *out_start = mods[i].mod_start;
        *out_end   = mods[i].mod_end;
        return 0;
    }
    return -1;
}

static void materialize_modules_into_nxfs(void) {
    if (!vfs_lookup("/system"))       vfs_create("/system",       VFS_DIR);
    if (!vfs_lookup("/system/init"))  vfs_create("/system/init",  VFS_DIR);
    if (!vfs_lookup("/system/drive")) vfs_create("/system/drive", VFS_DIR);

    if (g_init_elf_data && g_init_elf_size > 0) {
        if (!vfs_lookup("/system/init/init.elf")) {
            int fd = vfs_open("/system/init/init.elf", O_CREAT | O_TRUNC);
            if (fd >= 0) {
                vfs_fd_write(fd, g_init_elf_data, g_init_elf_size);
                vfs_close(fd);
                vga_puts("[OK] init.elf written into NXFS\n");
            }
        }
    }

    if (g_kbd_elf_data && g_kbd_elf_size > 0) {
        if (!vfs_lookup("/system/drive/kbd.elf")) {
            int fd = vfs_open("/system/drive/kbd.elf", O_CREAT | O_TRUNC);
            if (fd >= 0) {
                vfs_fd_write(fd, g_kbd_elf_data, g_kbd_elf_size);
                vfs_close(fd);
                vga_puts("[OK] kbd.elf written into NXFS\n");
            }
        }
    }

    if (g_vga_elf_data && g_vga_elf_size > 0) {
        if (!vfs_lookup("/system/drive/vga.elf")) {
            int fd = vfs_open("/system/drive/vga.elf", O_CREAT | O_TRUNC);
            if (fd >= 0) {
                vfs_fd_write(fd, g_vga_elf_data, g_vga_elf_size);
                vfs_close(fd);
                vga_puts("[OK] vga.elf written into NXFS\n");
            }
        }
    }

    if (g_mouse_elf_data && g_mouse_elf_size > 0) {
        if (!vfs_lookup("/system/drive/mouse.elf")) {
            int fd = vfs_open("/system/drive/mouse.elf", O_CREAT | O_TRUNC);
            if (fd >= 0) {
                vfs_fd_write(fd, g_mouse_elf_data, g_mouse_elf_size);
                vfs_close(fd);
                vga_puts("[OK] mouse.elf written into NXFS\n");
            }
        }
    }
}

void kmain(uint32_t magic, uint32_t mbi) {
    serial_init();
    vga_clear();

    gdt_init();
    idt_init();
    syscall_init();

    vga_puts("NexOS-NEXT 32-bit microkernel\n");
    vga_puts("=============================\n");

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_puts("[INFO] booted via Multiboot (GRUB/ISO)\n");
        install_set_installer_mode(1);
    } else {
        vga_puts("[INFO] booted via stage2 (disk)\n");
        install_set_installer_mode(0);
    }

    pmm_init();         vga_puts("[OK] PMM\n");
    paging_init();      vga_puts("[OK] Paging\n");
    heap_init();        vga_puts("[OK] Heap\n");
    vfs_init();         vga_puts("[OK] VFS (ramfs)\n");
    thread_init();
    timer_init();       vga_puts("[OK] Timer @100Hz\n");

    kbd_8042_init();
    vga_puts("[OK] PS/2 8042 init (kbd + mouse)\n");

    vga_puts("[OK] Keyboard driver moved to user space (S4.5)\n");
    vga_puts("[OK] Syscalls (int 0x80)\n");
    vga_puts("[OK] User mode (ring 3)\n");
    vga_puts("[OK] IPC\n\n");

    __asm__ volatile("sti");

    if (ata_init() < 0) {
        vga_puts("[FAIL] ATA init\n");
        for (;;) __asm__ volatile("hlt");
    }
    vga_puts("[OK] ATA (primary master)\n");

    int nx = nxfs_init();
    if (nx < 0) {
        vga_puts("[FAIL] NXFS mount, code=");
        vga_hex((uint32_t)nx);
        vga_puts("\n");
        for (;;) __asm__ volatile("hlt");
    }
    vga_puts("[OK] NXFS mounted\n");
    vfs_use_nxfs();
    vga_puts("[OK] VFS now on NXFS\n\n");

    uint8_t *elf_buf  = 0;
    uint32_t elf_size = 0;

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        uint32_t ms = 0, me = 0;

        if (mb_module_find(mbi, "init.elf", &ms, &me) == 0) {
            if (ms == 0 || me <= ms) {
                vga_puts("[WARN] init.elf module address invalid\n");
            } else {
                elf_buf  = (uint8_t *)ms;
                elf_size = me - ms;
                vga_puts("[OK] init.elf from GRUB module, size=");
                vga_hex(elf_size);
                vga_puts("\n");

                g_init_elf_data = elf_buf;
                g_init_elf_size = elf_size;
            }
        }
    }

    if (!elf_buf) {
        vga_puts("Loading /system/init/init.elf from NXFS...\n");
        int fd = vfs_open("/system/init/init.elf", 0);
        if (fd < 0) {
            vga_puts("[FAIL] /system/init/init.elf not found\n");
            for (;;) __asm__ volatile("hlt");
        }
        elf_size = vfs_fd_size(fd);
        if (elf_size == 0 || elf_size > 512 * 1024) {
            vga_puts("[FAIL] size invalid\n");
            for (;;) __asm__ volatile("hlt");
        }
        elf_buf = (uint8_t *)kmalloc(elf_size);
        if (!elf_buf) {
            vga_puts("[FAIL] kmalloc failed\n");
            for (;;) __asm__ volatile("hlt");
        }
        int rd = vfs_fd_read(fd, elf_buf, elf_size);
        vfs_close(fd);
        if (rd != (int)elf_size) {
            vga_puts("[FAIL] read failed\n");
            for (;;) __asm__ volatile("hlt");
        }
        vga_puts("[OK] /system/init/init.elf loaded, size=");
        vga_hex(elf_size);
        vga_puts("\n");

        g_init_elf_data = elf_buf;
        g_init_elf_size = elf_size;
    }

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        uint32_t ks = 0, ke = 0;
        if (mb_module_find(mbi, "kbd.elf", &ks, &ke) == 0) {
            if (ks != 0 && ke > ks) {
                g_kbd_elf_data = (const uint8_t *)ks;
                g_kbd_elf_size = ke - ks;
                vga_puts("[OK] kbd.elf from GRUB module, size=");
                vga_hex(g_kbd_elf_size);
                vga_puts("\n");
            }
        }

        uint32_t vs = 0, ve = 0;
        if (mb_module_find(mbi, "vga.elf", &vs, &ve) == 0) {
            if (vs != 0 && ve > vs) {
                g_vga_elf_data = (const uint8_t *)vs;
                g_vga_elf_size = ve - vs;
                vga_puts("[OK] vga.elf from GRUB module, size=");
                vga_hex(g_vga_elf_size);
                vga_puts("\n");
            }
        }

        uint32_t ms2 = 0, me2 = 0;
        if (mb_module_find(mbi, "mouse.elf", &ms2, &me2) == 0) {
            if (ms2 != 0 && me2 > ms2) {
                g_mouse_elf_data = (const uint8_t *)ms2;
                g_mouse_elf_size = me2 - ms2;
                vga_puts("[OK] mouse.elf from GRUB module, size=");
                vga_hex(g_mouse_elf_size);
                vga_puts("\n");
            }
        }
    }

    materialize_modules_into_nxfs();

    elf_load_result_t elf;
    int r = elf_load(elf_buf, elf_size, &elf);
    if (r) {
        vga_puts("[FAIL] elf_load = ");
        vga_hex((uint32_t)r);
        vga_puts("\n");
        for (;;) __asm__ volatile("hlt");
    }

    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
        kfree(elf_buf);

    vga_puts("[OK] ELF loaded, entry=");
    vga_hex(elf.entry);
    vga_puts(" stack=");
    vga_hex(elf.stack_top);
    vga_puts("\n\n");

    thread_create_elf(elf.entry, elf.stack_top, paging_get_dir());

    for (volatile int i = 0; i < 30000000; i++);
    sched_start();
}