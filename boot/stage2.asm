org 0x7E00
bits 16

KERNEL_HDR_LBA   equ 17
KERNEL_DATA_LBA  equ 18
KERNEL_HDR_ADDR  equ 0x0500       ; ★ 从 0x7A00 改到 0x0500，远离栈
CHUNK_SECTORS    equ 64           ; ★ 从 120 改到 64，32KB/段，所有 BIOS 都支持
CHUNK_STRIDE     equ 0x800        ; ★ (64*512)/16 = 0x800
MAX_CHUNKS       equ 64           ; 2MB 上限

db 'SXN2'

start:
    cli
    mov ax, cs
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drv], dl

    mov al, 'S'
    call putc

    ; ---- 读元数据头到 0x0000:0x0500 ----
    mov word [dap_count], 1
    mov word [dap_offset], KERNEL_HDR_ADDR
    mov word [dap_segment], 0x0000
    mov dword [dap_lba], KERNEL_HDR_LBA
    mov dword [dap_lba+4], 0

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drv]
    int 0x13
    jc error

    ; ---- 检查 'NEXK' ----
    cmp dword [KERNEL_HDR_ADDR], 0x4B58454E
    jne hdr_error

    ; ---- 读 size ----
    mov eax, [KERNEL_HDR_ADDR + 4]
    test eax, eax
    jz hdr_error

    ; chunks = (size + 32767) >> 15
    add eax, 32767
    shr eax, 15

    cmp eax, MAX_CHUNKS
    ja hdr_error

    ; ---- 循环读 chunks 段 ----
    mov cx, ax
    mov bx, 0x1000                        ; 首段物理 0x10000
    mov dword [dap_lba], KERNEL_DATA_LBA

.read_loop:
    push cx

    mov word [dap_count], CHUNK_SECTORS
    mov word [dap_offset], 0
    mov word [dap_segment], bx

    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drv]
    int 0x13
    jc error

    add bx, CHUNK_STRIDE                  ; ★ 用宏，不是硬编码
    add dword [dap_lba], CHUNK_SECTORS

    mov al, '.'
    call putc

    pop cx
    loop .read_loop

    ; ---- 切保护模式 ----
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

putc:
    push ax
    push bx
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    pop bx
    pop ax
    ret

hdr_error:
    mov si, msg_hdr
    jmp print_err

error:
    mov si, msg_err
print_err:
    mov ah, 0x0E
.loop:
    lodsb
    or al, al
    jz .halt
    int 0x10
    jmp .loop
.halt:
    hlt
    jmp .halt

msg_err db 'stage2: disk read error', 13, 10, 0
msg_hdr db 'stage2: bad kernel header', 13, 10, 0
boot_drv db 0

dap:
    db 0x10, 0
dap_count:
    dw 0
dap_offset:
    dw 0
dap_segment:
    dw 0
dap_lba:
    dd 0
    dd 0

align 8
gdt:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF

gdt_desc:
    dw gdt_desc - gdt - 1
    dd gdt

bits 32

pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7C00

    mov byte [0xB8000], 'P'
    mov byte [0xB8001], 0x0F

    ;告诉内核：无 multiboot，magic=0, mbi=0
    xor eax, eax
    xor ebx, ebx

    jmp 0x10000