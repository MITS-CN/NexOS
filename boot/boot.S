.set MAGIC,    0x1BADB002
.set FLAGS,    0x0
.set CHECKSUM, -(MAGIC + FLAGS)

.section .multiboot, "a"
.align 4
.long MAGIC
.long FLAGS
.long CHECKSUM

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

.section .text
.global _start
.type _start, @function

_start:
    mov $stack_top, %esp

    /* 用寄存器保存参数（rep stosb 不影响 ESI/EBP） */
    mov %eax, %esi        /* ESI = magic */
    mov %ebx, %ebp        /* EBP = mbi   */

    /* 清零 BSS */
    mov $_bss_start, %edi
    mov $_bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    rep stosb

    /* 按 C 调用约定压栈（最后一个参数先压） */
    push %ebp             /* mbi   */
    push %esi             /* magic */
    call kmain

1:  cli
    hlt
    jmp 1b