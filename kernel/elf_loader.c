#include "elf_loader.h"
#include "elf.h"
#include "paging.h"
#include "pmm.h"

static int validate_header(const Elf32_Ehdr *eh) {
    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3)
        return -1;
    if (eh->e_ident[4] != ELFCLASS32) return -2;
    if (eh->e_ident[5] != ELFDATA2LSB) return -3;
    if (eh->e_type != ET_EXEC) return -4;
    if (eh->e_machine != EM_386) return -5;
    if (eh->e_phentsize != sizeof(Elf32_Phdr)) return -6;
    if (eh->e_phnum == 0 || eh->e_phnum > 32) return -7;
    if (eh->e_entry < USER_BASE || eh->e_entry >= USER_CODE_END) return -8;
    return 0;
}

static int validate_phdr(const Elf32_Phdr *ph, uint32_t elf_size) {
    if (ph->p_type == PT_NULL) return 0;
    if (ph->p_type != PT_LOAD) return 0;

    if (ph->p_filesz > ph->p_memsz) return -10;
    if (ph->p_vaddr < USER_BASE) return -11;
    if (ph->p_vaddr + ph->p_memsz > USER_CODE_END) return -12;
    if (ph->p_vaddr + ph->p_memsz < ph->p_vaddr) return -13;
    if (ph->p_offset + ph->p_filesz > elf_size) return -14;
    if (ph->p_offset + ph->p_filesz < ph->p_offset) return -15;
    if (ph->p_align > 1 &&
        (ph->p_vaddr % ph->p_align) != (ph->p_offset % ph->p_align))
        return -16;
    return 0;
}

int elf_load(const void *elf_data, uint32_t elf_size, elf_load_result_t *out) {
    if (!elf_data || !out) return -1;
    if (elf_size < sizeof(Elf32_Ehdr)) return -2;

    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)elf_data;

    int r = validate_header(eh);
    if (r) return r;

    uint32_t ph_end = eh->e_phoff + (uint32_t)eh->e_phnum * sizeof(Elf32_Phdr);
    if (ph_end > elf_size || ph_end < eh->e_phoff) return -9;

    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr *ph = (const Elf32_Phdr *)((uint32_t)elf_data
                              + eh->e_phoff + i * sizeof(Elf32_Phdr));
        r = validate_phdr(ph, elf_size);
        if (r) return r;
    }

    for (int i = 0; i < eh->e_phnum; i++) {
        const Elf32_Phdr *ph = (const Elf32_Phdr *)((uint32_t)elf_data
                              + eh->e_phoff + i * sizeof(Elf32_Phdr));

        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;

        uint32_t vstart = ph->p_vaddr & ~(PAGE_SIZE - 1);
        uint32_t vend   = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1)
                        & ~(PAGE_SIZE - 1);

        for (uint32_t va = vstart; va < vend; va += PAGE_SIZE) {
            void *phys = pmm_alloc_page();
            if (!phys) return -20;

            uint8_t *p = (uint8_t *)phys;
            for (int j = 0; j < PAGE_SIZE; j++) p[j] = 0;

            uint32_t data_start = ph->p_vaddr;
            uint32_t data_end   = ph->p_vaddr + ph->p_filesz;
            uint32_t page_end   = va + PAGE_SIZE;

            if (page_end > data_start && va < data_end) {
                uint32_t cs = va > data_start ? va : data_start;
                uint32_t ce = page_end < data_end ? page_end : data_end;
                uint32_t cl = ce - cs;
                if (cl > 0) {
                    const uint8_t *src = (const uint8_t *)elf_data
                                       + ph->p_offset + (cs - ph->p_vaddr);
                    uint8_t *dst = p + (cs - va);
                    for (uint32_t k = 0; k < cl; k++) dst[k] = src[k];
                }
            }

            uint32_t flags = PAGE_USER;
            if (ph->p_flags & PF_W) flags |= PAGE_RW;

            paging_map(va, (uint32_t)phys, flags);
        }
    }

    uint32_t stack_bottom = USER_STACK_TOP - USER_STACK_SIZE;
    for (uint32_t va = stack_bottom; va < USER_STACK_TOP; va += PAGE_SIZE) {
        void *phys = pmm_alloc_page();
        if (!phys) return -21;
        uint8_t *p = (uint8_t *)phys;
        for (int j = 0; j < PAGE_SIZE; j++) p[j] = 0;
        paging_map(va, (uint32_t)phys, PAGE_RW | PAGE_USER);
    }

    out->entry     = eh->e_entry;
    out->stack_top = USER_STACK_TOP;
    return 0;
}

int elf_load_to_dir(uint32_t *dir, const void *elf_data, uint32_t elf_size,
                    elf_load_result_t *out) {
    if (!dir) return -1;

    uint32_t *old = paging_get_dir();
    paging_switch_dir(dir);
    int r = elf_load(elf_data, elf_size, out);
    paging_switch_dir(old);
    return r;
}

/* ★ S7a: 把 argv 字符串 + 指针数组写到新进程用户栈
   布局（高地址 → 低地址）：
     [argv[n-1] 字符串]
     ...
     [argv[0] 字符串]
     [argv[n-1] 指针]
     ...
     [argv[0] 指针]
     [NULL]
     [argc]            ← esp 指向这里
*/
int elf_setup_argv(uint32_t *dir, uint32_t stack_top, int argc,
                   const char *const *argv, uint32_t *out_esp) {
    if (!dir || argc < 0 || argc > 16) return -1;

    uint32_t str_size = 0;
    for (int i = 0; i < argc; i++) {
        const char *s = argv[i];
        if (!s) return -2;
        uint32_t l = 0;
        while (s[l]) l++;
        str_size += l + 1;
    }
    str_size = (str_size + 3) & ~3u;

    uint32_t arr_size = (uint32_t)(argc + 1) * 4;
    uint32_t total    = str_size + arr_size + 4;

    if (total > 32 * 1024) return -3;

    uint32_t esp = stack_top - total;
    esp &= ~15u;

    uint32_t argc_va = esp;
    uint32_t arr_va  = esp + 4;
    uint32_t str_va  = arr_va + arr_size;

    uint32_t *old = paging_get_dir();
    paging_switch_dir(dir);

    *(volatile uint32_t *)argc_va = (uint32_t)argc;

    uint32_t str_off = 0;
    for (int i = 0; i < argc; i++) {
        const char *s = argv[i];
        uint32_t l = 0;
        while (s[l]) l++;

        uint32_t sv = str_va + str_off;
        for (uint32_t j = 0; j < l; j++)
            *(volatile char *)(sv + j) = s[j];
        *(volatile char *)(sv + l) = 0;

        *(volatile uint32_t *)(arr_va + (uint32_t)i * 4) = sv;

        str_off += l + 1;
    }
    *(volatile uint32_t *)(arr_va + (uint32_t)argc * 4) = 0;

    paging_switch_dir(old);

    *out_esp = esp;
    return 0;
}