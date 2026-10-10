#include "shm.h"
#include "pmm.h"

typedef struct {
    uint32_t phys;       /* 头页物理地址；0 = 空闲 */
    int      owner;      /* 拥有者 tid */
} shm_slot_t;

static shm_slot_t slots[SHM_SLOTS];
static int inited = 0;

void shm_init(void) {
    for (int i = 0; i < SHM_SLOTS; i++) {
        slots[i].phys  = 0;
        slots[i].owner = -1;
    }
    inited = 1;
}

int shm_alloc_for(int owner_tid) {
    if (!inited) shm_init();
    for (int i = 0; i < SHM_SLOTS; i++) {
        if (slots[i].phys == 0) {
            /* ★ S6c: 分配 2 个连续物理页（8KB） */
            void *p = pmm_alloc_pages(2);
            if (!p) return -1;
            slots[i].phys  = (uint32_t)p;
            slots[i].owner = owner_tid;
            return i;
        }
    }
    return -1;
}

void shm_free_owner(int owner_tid) {
    if (!inited) return;
    for (int i = 0; i < SHM_SLOTS; i++) {
        if (slots[i].owner == owner_tid && slots[i].phys != 0) {
            pmm_free_pages((void *)slots[i].phys, 2);
            slots[i].phys  = 0;
            slots[i].owner = -1;
        }
    }
}

uint32_t shm_get_phys(int slot) {
    if (slot < 0 || slot >= SHM_SLOTS) return 0;
    return slots[slot].phys;
}

int shm_get_owner(int slot) {
    if (slot < 0 || slot >= SHM_SLOTS) return -1;
    return slots[slot].owner;
}

/* ★ S6c: 反查 */
int shm_find_by_owner(int owner_tid) {
    if (!inited) shm_init();
    for (int i = 0; i < SHM_SLOTS; i++) {
        if (slots[i].owner == owner_tid && slots[i].phys != 0)
            return i;
    }
    return -1;
}