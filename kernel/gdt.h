// gdt.h
#ifndef GDT_H
#define GDT_H
#include <stdint.h>

void gdt_init(void);
void gdt_set_kernel_stack(uint32_t esp0);

/* S1 新增：I/O 位图开关 */
void tss_allow_io_port(uint16_t port);
void tss_deny_io_port(uint16_t port);

#endif