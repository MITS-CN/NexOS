/* Multiboot 头 —— 给 GRUB 用 */
.section .multiboot, "a"
.align 4
.long 0x1BADB002
.long 0x0
.long -(0x1BADB002)

/* 内核入口 —— 两种启动方式都从这里开始 */
.section .text.entry
.global _start
.type _start, @function

_start:
    cli
    mov $stack_top, %esp
    mov %esp, %ebp

    /* 保存 GRUB 传的 magic 和 mbi，清 BSS 前必须存好 */
    push %eax
    push %ebx

    /* 清 BSS */
    mov $_bss_start, %edi
    mov $_bss_end, %ecx
    sub %edi, %ecx
    xor %eax, %eax
    rep stosb

    /* 恢复 magic / mbi */
    pop %ebx
    pop %eax

    push %ebx                /* 第二个参数 mbi */
    push %eax                /* 第一个参数 magic */
    call kmain

1:  cli
    hlt
    jmp 1b