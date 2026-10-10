.section .text
.global _start
.type _start, @function

_start:
    xor %ebp, %ebp

    /* 从栈顶读 argc（0 = 无 argv） */
    mov (%esp), %eax
    test %eax, %eax
    jz 1f
    lea 4(%esp), %ecx        /* argv 指针 */
    jmp 2f
1:
    xor %ecx, %ecx           /* argv = NULL */
2:
    push %ecx
    push %eax
    call main

    mov $2, %eax
    int $0x80
1:  hlt
    jmp 1b