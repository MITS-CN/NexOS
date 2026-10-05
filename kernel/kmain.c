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
#include "kbd.h"
#include "io.h"
#include "vfs.h"
#include "ata.h"
#include "nxfs.h"
#include "install.h"

#define VGA_MEMORY ((volatile uint16_t *)0xB8000)
#define VGA_WIDTH  80
#define VGA_HEIGHT 25

static int cursor = 0;

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

void vga_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_MEMORY[i] = (uint16_t)((0x07 << 8) | ' ');
    cursor = 0;
    outb(0x3D4, 0x0A); outb(0x3D5, 0x0E);
    outb(0x3D4, 0x0B); outb(0x3D5, 0x0F);
    vga_move_cursor();
}

void vga_putc(char c) {
    serial_putc(c);
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

const uint8_t *g_init_elf_data = 0;
uint32_t       g_init_elf_size = 0;

/* ---- Multiboot module 查找 ---- */

#define MULTIBOOT_BOOTLOADER_MAGIC  0x2BADB002

typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} mb_module_t;

/* 在 multiboot info 里按名字找 module。
   成功返回 0，失败返回 -1。 */
static int mb_module_find(uint32_t mbi, const char *name,
                          uint32_t *out_start, uint32_t *out_end) {
    if (!mbi) return -1;

    uint32_t flags = *(uint32_t *)mbi;
    if (!(flags & (1u << 3))) return -1;   /* 无 modules */

    uint32_t mods_count = *(uint32_t *)(mbi + 20);
    uint32_t mods_addr  = *(uint32_t *)(mbi + 24);
    if (mods_count == 0 || mods_addr == 0) return -1;

    mb_module_t *mods = (mb_module_t *)mods_addr;

    for (uint32_t i = 0; i < mods_count; i++) {
        const char *cmdline = (const char *)mods[i].cmdline;
        if (!cmdline) continue;

        /* 比较名字（精确匹配，忽略前导空格） */
        while (*cmdline == ' ') cmdline++;
        const char *a = name;
        const char *b = cmdline;
        while (*a && *a == *b) { a++; b++; }
        if (*a != 0) continue;
        /* 名字后必须是 \0 或空格 */
        if (*b != 0 && *b != ' ') continue;

        *out_start = mods[i].mod_start;
        *out_end   = mods[i].mod_end;
        return 0;
    }
    return -1;
}

/* ---- 内核主函数 ---- */

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
    } else {
        vga_puts("[INFO] booted via stage2 (disk)\n");
    }

    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        vga_puts("[INFO] booted via Multiboot (GRUB/ISO)\n");
        install_set_installer_mode(1);    /* ★ 安装器模式 */
    } else {
        vga_puts("[INFO] booted via stage2 (disk)\n");
        install_set_installer_mode(0);    /* ★ 已安装系统 */
    }

    pmm_init();         vga_puts("[OK] PMM\n");
    paging_init();      vga_puts("[OK] Paging\n");
    heap_init();        vga_puts("[OK] Heap\n");
    vfs_init();         vga_puts("[OK] VFS (ramfs)\n");
    thread_init();
    timer_init();       vga_puts("[OK] Timer @100Hz\n");
    kbd_init();         vga_puts("[OK] Keyboard\n");
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

    /* ============ 获取 init.elf ============ */
    uint8_t *elf_buf  = 0;
    uint32_t elf_size = 0;

    /* 路径 1：Multiboot module（ISO/GRUB 场景） */
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        uint32_t ms = 0, me = 0;

        if (mb_module_find(mbi, "init.elf", &ms, &me) == 0) {
            vga_puts("[DBG] ms="); vga_hex(ms);
            vga_puts(" me=");     vga_hex(me);
            vga_putc('\n');

            if (ms == 0 || me <= ms) {
                vga_puts("[WARN] module address invalid\n");
            } else {
                elf_buf  = (uint8_t *)ms;
                elf_size = me - ms;
                vga_puts("[OK] init.elf from GRUB module, size=");
                vga_hex(elf_size);
                vga_puts("\n");
                vga_puts("[DBG] bytes: ");
                vga_hex(elf_buf[0]); vga_putc(' ');
                vga_hex(elf_buf[1]); vga_putc(' ');
                vga_hex(elf_buf[2]); vga_putc(' ');
                vga_hex(elf_buf[3]); vga_putc('\n');

                g_init_elf_data = elf_buf;
                g_init_elf_size = elf_size;
            }
        }
    }

    /* 路径 2：从 NXFS 读（磁盘场景 或 无 module） */
    if (!elf_buf) {
        vga_puts("Loading /init.elf from NXFS...\n");
        int fd = vfs_open("/init.elf", 0);
        if (fd < 0) {
            vga_puts("[FAIL] /init.elf not found\n");
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
        vga_puts("[OK] /init.elf loaded, size=");
        vga_hex(elf_size);
        vga_puts("\n");

        g_init_elf_data = elf_buf;
        g_init_elf_size = elf_size;

    }

    /* ============ 加载并执行 ============ */
    elf_load_result_t elf;
    int r = elf_load(elf_buf, elf_size, &elf);
    if (r) {
        vga_puts("[FAIL] elf_load = ");
        vga_hex((uint32_t)r);
        vga_puts("\n");
        for (;;) __asm__ volatile("hlt");
    }

    /* 只有从 NXFS kmalloc 出来的才 free；module 的是 GRUB 的内存，不动 */
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