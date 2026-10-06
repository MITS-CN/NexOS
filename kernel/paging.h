#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_PRESENT  0x01
#define PAGE_RW       0x02
#define PAGE_USER     0x04

/* S3: 用户态可见的 VGA 文本显存虚拟地址
   物理 0xB8000 重映射到这里，用户驱动进程直接写 80x25 字符格 */
#define USER_VGA_BASE 0x10000000u

void      paging_init(void);
void      paging_map(uint32_t virt, uint32_t phys, uint32_t flags);
void      paging_map_in(uint32_t *dir, uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t *paging_get_dir(void);
uint32_t *paging_kernel_dir(void);
uint32_t *paging_create_dir(void);
void      paging_switch_dir(uint32_t *dir);
void      paging_free_dir(uint32_t *dir);

#endif