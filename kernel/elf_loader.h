#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>

typedef struct {
    uint32_t entry;
    uint32_t stack_top;
} elf_load_result_t;

int elf_load(const void *elf_data, uint32_t elf_size, elf_load_result_t *out);

int elf_load_to_dir(uint32_t *dir, const void *elf_data, uint32_t elf_size,
                    elf_load_result_t *out);

/* ★ S7a: 把 argv 写进新进程用户栈 */
int elf_setup_argv(uint32_t *dir, uint32_t stack_top, int argc,
                   const char *const *argv, uint32_t *out_esp);

#endif