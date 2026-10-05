#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>

typedef struct {
    uint32_t entry;
    uint32_t stack_top;
} elf_load_result_t;

/* 加载到当前页目录 */
int elf_load(const void *elf_data, uint32_t elf_size, elf_load_result_t *out);

/* 加载到指定页目录（B2 新增） */
int elf_load_to_dir(uint32_t *dir, const void *elf_data, uint32_t elf_size,
                    elf_load_result_t *out);

#endif