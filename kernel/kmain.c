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

void kmain(void) {
    volatile uint16_t *probe = (volatile uint16_t *)0xB8000;
    probe[0] = 0x0F10;

    serial_init();
    probe[0] = 0x0F11;

    vga_clear();
    probe[0] = 0x0F12;

    vga_puts("### PROBE BUILD ###\n");
    probe[0] = 0x0F13;

    gdt_init();
    probe[0] = 0x0F14;   /* 若能看到 '4'，gdt_init 返回了 */

    idt_init();
    probe[0] = 0x0F15;

    syscall_init();
    probe[0] = 0x0F16;

    vga_puts("NexOS-NEXT 32-bit microkernel\n");
    probe[0] = 0x0F17;

    pmm_init();      vga_puts("[OK] PMM\n");
    paging_init();      vga_puts("[OK] Paging\n");
    heap_init();        vga_puts("[OK] Heap\n");
    vfs_init();         vga_puts("[OK] VFS (ramfs)\n");
    thread_init();
    timer_init();       vga_puts("[OK] Timer @100Hz\n");
    vga_puts("BEFORE kbd_init\n");                          
    kbd_init();
    vga_puts("AFTER kbd_init\n");
    //kbd_init();         vga_puts("[OK] Keyboard\n");

    vga_puts("[OK] Syscalls (int 0x80)\n");
    vga_puts("[OK] User mode (ring 3)\n");
    vga_puts("[OK] IPC\n\n");

    __asm__ volatile("sti");

    /* ATA 初始化 */
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

        /* 从 NXFS 读 /init.elf */
    vga_puts("Loading /init.elf from NXFS...\n");

    int fd = vfs_open("/init.elf", 0);
    if (fd < 0) {
        vga_puts("[FAIL] /init.elf not found in NXFS\n");
        vga_puts("[FAIL] disk may need to be reformatted or\n");
        vga_puts("[FAIL] init.elf must be installed first.\n");
        for (;;) __asm__ volatile("hlt");
    }

    uint32_t elf_size = vfs_fd_size(fd);
    if (elf_size == 0 || elf_size > 512 * 1024) {
        vga_puts("[FAIL] /init.elf size invalid: ");
        vga_hex(elf_size);
        vga_puts("\n");
        for (;;) __asm__ volatile("hlt");
    }

    uint8_t *elf_buf = (uint8_t *)kmalloc(elf_size);
    if (!elf_buf) {
        vga_puts("[FAIL] kmalloc for ELF failed\n");
        for (;;) __asm__ volatile("hlt");
    }

    int rd = vfs_fd_read(fd, elf_buf, elf_size);
    vfs_close(fd);
    if (rd != (int)elf_size) {
        vga_puts("[FAIL] read /init.elf failed\n");
        for (;;) __asm__ volatile("hlt");
    }

    vga_puts("[OK] /init.elf loaded, size=");
    vga_hex(elf_size);
    vga_puts("\n");

    elf_load_result_t elf;
    int r = elf_load(elf_buf, elf_size, &elf);
    if (r) {
        vga_puts("[FAIL] elf_load = ");
        vga_hex((uint32_t)r);
        vga_puts("\n");
        for (;;) __asm__ volatile("hlt");
    }

    kfree(elf_buf);

    vga_puts("[OK] ELF loaded, entry=");
    vga_hex(elf.entry);
    vga_puts(" stack=");
    vga_hex(elf.stack_top);
    vga_puts("\n\n");

    thread_create_elf(elf.entry, elf.stack_top);

    for (volatile int i = 0; i < 30000000; i++);
    sched_start();

    for (volatile int i = 0; i < 30000000; i++);
    sched_start();
}