#include "paging.h"
#include "pmm.h"

#define PD_INDEX(v)  ((v) >> 22)
#define PT_INDEX(v)  (((v) >> 12) & 0x3FF)

/* ★ S7c-3 修：恒等映射从 16MB 扩到 128MB
   原因：PMM 会分配 0-128MB 的物理页。如果页目录 / PT 落在 >16MB，
   内核切 CR3 后访问不到它 → Page fault。
   代价：32 个 PDE × 4KB PT = 128KB 额外内存。 */
#define IDENTITY_SIZE  (128 * 1024 * 1024)

static uint32_t *kernel_dir  = 0;
static uint32_t *current_dir = 0;

extern void paging_load_dir(uint32_t);

void paging_init(void) {
    kernel_dir = (uint32_t *)pmm_alloc_page();
    for (int i = 0; i < 1024; i++) kernel_dir[i] = 0;

    /* 恒等映射 0-128MB（内核 + VGA + PMM bitmap + 所有 PMM 可分配页） */
    for (uint32_t addr = 0; addr < IDENTITY_SIZE; addr += 0x400000) {
        uint32_t *pt = (uint32_t *)pmm_alloc_page();
        for (int i = 0; i < 1024; i++)
            pt[i] = (addr + i * 0x1000) | PAGE_PRESENT | PAGE_RW;
        kernel_dir[PD_INDEX(addr)] = ((uint32_t)pt) | PAGE_PRESENT | PAGE_RW;
    }

    current_dir = kernel_dir;
    paging_load_dir((uint32_t)kernel_dir);

    /* S3: 物理 0xB8000 重映射到 USER_VGA_BASE */
    paging_map_in(kernel_dir, USER_VGA_BASE, 0xB8000, PAGE_RW | PAGE_USER);
}

uint32_t *paging_get_dir(void)    { return current_dir; }
uint32_t *paging_kernel_dir(void) { return kernel_dir;  }

/* 创建新页目录：复制内核部分 PDE（现在 0-128MB 共 32 个），用户空间 PDE 全零 */
uint32_t *paging_create_dir(void) {
    uint32_t *new_dir = (uint32_t *)pmm_alloc_page();
    if (!new_dir) return 0;

    for (int i = 0; i < 1024; i++) {
        uint32_t pde = kernel_dir[i];

        if (!(pde & PAGE_PRESENT)) {
            new_dir[i] = 0;
        } else if (pde & PAGE_USER) {
            new_dir[i] = 0;                /* 用户空间：每进程独立 */
        } else {
            new_dir[i] = pde;              /* 内核空间：共享 */
        }
    }

    paging_map_in(new_dir, USER_VGA_BASE, 0xB8000, PAGE_RW | PAGE_USER);

    return new_dir;
}

void paging_switch_dir(uint32_t *dir) {
    current_dir = dir;
    __asm__ volatile("mov %0, %%cr3" :: "r"(dir) : "memory");
}

void paging_map_in(uint32_t *dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd = PD_INDEX(virt);
    uint32_t pt = PT_INDEX(virt);

    if (!(dir[pd] & PAGE_PRESENT)) {
        uint32_t *new_pt = (uint32_t *)pmm_alloc_page();
        for (int i = 0; i < 1024; i++) new_pt[i] = 0;

        uint32_t pd_flags = PAGE_PRESENT | PAGE_RW;
        if (flags & PAGE_USER) pd_flags |= PAGE_USER;

        dir[pd] = ((uint32_t)new_pt) | pd_flags;
    }

    uint32_t *table = (uint32_t *)(dir[pd] & ~0xFFF);
    table[pt] = (phys & ~0xFFF) | (flags & 0xFFF) | PAGE_PRESENT;
}

void paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    paging_map_in(current_dir, virt, phys, flags);
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

void paging_free_dir(uint32_t *dir) {
    if (!dir) return;
    if (dir == kernel_dir) return;

    for (int i = 0; i < 1024; i++) {
        uint32_t pde = dir[i];
        if (!(pde & PAGE_PRESENT)) continue;
        if (!(pde & PAGE_USER)) continue;

        uint32_t *pt = (uint32_t *)(pde & ~0xFFF);
        for (int j = 0; j < 1024; j++) {
            uint32_t pte = pt[j];
            if (pte & PAGE_PRESENT) {
                uint32_t pa = pte & ~0xFFF;
                if (pa == 0xB8000) continue;
                pmm_free_page((void *)pa);
            }
        }
        pmm_free_page(pt);
    }
    pmm_free_page(dir);
}