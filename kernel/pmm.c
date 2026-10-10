#include "pmm.h"

#define MAX_PAGES  (1024 * 1024)
static uint8_t bitmap[MAX_PAGES / 8];

static uint32_t total_pages = 0;
static uint32_t used_pages  = 0;

static inline void bm_set(uint32_t b)  { bitmap[b >> 3] |=  (1u << (b & 7)); }
static inline void bm_clr(uint32_t b)  { bitmap[b >> 3] &= ~(1u << (b & 7)); }
static inline int  bm_test(uint32_t b) { return (bitmap[b >> 3] >> (b & 7)) & 1u; }

extern char _kernel_start[], _kernel_end[];

#define PHYS_TOP  (128 * 1024 * 1024)

void pmm_init(void) {
    for (uint32_t i = 0; i < sizeof(bitmap); i++) bitmap[i] = 0xFF;

    uint32_t first = 0x100000 / PAGE_SIZE;
    uint32_t last  = PHYS_TOP / PAGE_SIZE;
    for (uint32_t i = first; i < last; i++) bm_clr(i);

    total_pages = last;

    uint32_t ks = (uint32_t)_kernel_start;
    uint32_t ke = (uint32_t)_kernel_end;
    for (uint32_t a = ks & ~(PAGE_SIZE - 1); a < ke; a += PAGE_SIZE)
        bm_set(a / PAGE_SIZE);

    used_pages = 0;
    for (uint32_t i = 0; i < total_pages; i++)
        if (bm_test(i)) used_pages++;
}

void *pmm_alloc_page(void) {
    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bm_test(i)) {
            bm_set(i);
            used_pages++;
            return (void *)(i * PAGE_SIZE);
        }
    }
    return 0;
}

void pmm_free_page(void *addr) {
    uint32_t page = (uint32_t)addr / PAGE_SIZE;
    if (page < total_pages && bm_test(page)) {
        bm_clr(page);
        used_pages--;
    }
}

/* ★ S6c */
void *pmm_alloc_pages(uint32_t n) {
    if (n == 0) return 0;
    if (n == 1) return pmm_alloc_page();

    uint32_t run_start = 0;
    uint32_t run_len   = 0;

    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bm_test(i)) {
            if (run_len == 0) run_start = i;
            run_len++;
            if (run_len == n) {
                for (uint32_t j = 0; j < n; j++) {
                    bm_set(run_start + j);
                    used_pages++;
                }
                return (void *)(run_start * PAGE_SIZE);
            }
        } else {
            run_len = 0;
        }
    }
    return 0;
}

void pmm_free_pages(void *addr, uint32_t n) {
    uint32_t start = (uint32_t)addr / PAGE_SIZE;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t p = start + i;
        if (p < total_pages && bm_test(p)) {
            bm_clr(p);
            used_pages--;
        }
    }
}

uint32_t pmm_total_pages(void) { return total_pages; }
uint32_t pmm_used_pages(void)  { return used_pages; }