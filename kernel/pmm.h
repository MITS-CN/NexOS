#ifndef PMM_H
#define PMM_H

#include <stdint.h>

#define PAGE_SIZE  4096

void pmm_init(void);
void     *pmm_alloc_page(void);
void      pmm_free_page(void *addr);
uint32_t  pmm_total_pages(void);
uint32_t  pmm_used_pages(void);

/* ★ S6c: 连续多页分配 */
void     *pmm_alloc_pages(uint32_t n);
void      pmm_free_pages(void *addr, uint32_t n);

#endif