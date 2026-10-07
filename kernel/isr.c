#include "isr.h"
#include "io.h"
#include "timer.h"
#include "irq.h"

static const char *exception_names[] = {
    "Divide by zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "BOUND range", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor overrun", "Invalid TSS", "Segment not present",
    "Stack fault", "General protection", "Page fault", "Reserved",
    "x87 FP error", "Alignment check", "Machine check", "SIMD FP error",
    "Virtualization", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved"
};

extern void vga_puts(const char *s);
extern void vga_hex(uint32_t v);
extern void vga_putc(char c);

static void dump_regs(struct regs *r) {
    vga_puts("  EAX="); vga_hex(r->eax);
    vga_puts(" EBX="); vga_hex(r->ebx);
    vga_puts(" ECX="); vga_hex(r->ecx);
    vga_puts(" EDX="); vga_hex(r->edx);
    vga_putc('\n');
    vga_puts("  ESI="); vga_hex(r->esi);
    vga_puts(" EDI="); vga_hex(r->edi);
    vga_puts(" EBP="); vga_hex(r->ebp);
    vga_puts(" ESP="); vga_hex(r->esp);
    vga_putc('\n');
    vga_puts("  EIP="); vga_hex(r->eip);
    vga_puts(" CS="); vga_hex(r->cs);
    vga_puts(" EFLAGS="); vga_hex(r->eflags);
    vga_putc('\n');
}

void isr_handler(struct regs *r) {
    if (r->int_no == 128) {
        extern int syscall_handler(uint32_t, uint32_t, uint32_t,
                                   uint32_t, uint32_t, uint32_t);
        r->eax = syscall_handler(r->eax, r->ebx, r->ecx,
                                 r->edx, r->esi, r->edi);
        return;
    }
    if (r->int_no < 32) {
        vga_puts("\n[EXCEPTION] ");
        vga_puts(exception_names[r->int_no]);
        vga_puts("\n");
        if (r->err_code) {
            vga_puts("  err_code="); vga_hex(r->err_code); vga_putc('\n');
        }
        dump_regs(r);
        vga_puts("\nSystem halted.\n");
        for (;;) __asm__ volatile("cli; hlt");
    }
}

void irq_handler(struct regs *r) {
    if (r->int_no >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);

    int irq_no = (int)(r->int_no - 32);
    if (irq_no < 0 || irq_no >= IRQ_MAX) return;

    /* IRQ0 时钟永远内核处理，不允许用户独占 */
    if (irq_no == 0) {
        timer_tick();
        return;
    }

    /* 如果被用户线程独占，转发 IPC，不执行内核默认处理
       ★ S4.6: 端口读取由 irq_dispatch 内部按注册配置完成 */
    int owner = irq_owner(irq_no);
    if (owner >= 0) {
        irq_dispatch(irq_no);
        return;
    }

    /* 其他 IRQ 目前不处理 */
}