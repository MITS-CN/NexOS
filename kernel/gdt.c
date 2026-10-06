// gdt.c
#include "gdt.h"
#include <stdint.h>

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

/* I/O 位图：8192 字节 = 65536 位，覆盖全部 16 位端口
   位 = 1 表示禁止用户态访问，位 = 0 表示允许 */
#define IOMAP_SIZE 8192

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0, ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
    uint8_t  iomap[IOMAP_SIZE];   /* 新增 */
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct gdt_entry gdt[6];
static struct gdt_ptr   gp;
static struct tss_entry tss;

extern void gdt_flush(uint32_t);
extern void tss_flush(void);

static void gdt_set(int i, uint32_t base, uint32_t limit,
                    uint8_t access, uint8_t gran) {
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].base_high   = (base >> 24) & 0xFF;
    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access      = access;
}

void gdt_init(void) {
    volatile uint16_t *probe = (volatile uint16_t *)0xB8000;

    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint32_t)&gdt;

    gdt_set(0, 0, 0,          0x00, 0x00);
    gdt_set(1, 0, 0xFFFFF,    0x9A, 0xCF);
    gdt_set(2, 0, 0xFFFFF,    0x92, 0xCF);
    gdt_set(3, 0, 0xFFFFF,    0xFA, 0xCF);
    gdt_set(4, 0, 0xFFFFF,    0xF2, 0xCF);
    /* limit 改为含 iomap 的完整 TSS 大小 - 1 */
    gdt_set(5, (uint32_t)&tss, sizeof(tss) - 1, 0x89, 0x00);

    uint8_t *p = (uint8_t *)&tss;
    for (uint32_t i = 0; i < sizeof(tss); i++) p[i] = 0;
    tss.ss0 = 0x10;

    /* iomap 默认全 1：所有端口对用户态禁止 */
    for (uint32_t i = 0; i < IOMAP_SIZE; i++) tss.iomap[i] = 0xFF;

    /* iomap_base 指向 iomap 数组在 TSS 内的偏移 */
    tss.iomap_base = (uint16_t)((uint32_t)&tss.iomap - (uint32_t)&tss);

    probe[0] = 0x0F41;        /* 'A' — 到达 gdt_flush 前 */

    gdt_flush((uint32_t)&gp);

    probe[0] = 0x0F42;        /* 'B' — gdt_flush 返回了 */

    tss_flush();

    probe[0] = 0x0F43;        /* 'C' — tss_flush 返回了 */
}

void gdt_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}

/* 允许用户态访问某个端口（位图对应位清 0） */
void tss_allow_io_port(uint16_t port) {
    uint32_t byte_idx = port >> 3;
    uint32_t bit_idx  = port & 7;
    if (byte_idx >= IOMAP_SIZE) return;
    tss.iomap[byte_idx] &= (uint8_t)~(1u << bit_idx);
}

/* 禁止用户态访问某个端口（位图对应位置 1） */
void tss_deny_io_port(uint16_t port) {
    uint32_t byte_idx = port >> 3;
    uint32_t bit_idx  = port & 7;
    if (byte_idx >= IOMAP_SIZE) return;
    tss.iomap[byte_idx] |= (uint8_t)(1u << bit_idx);
}