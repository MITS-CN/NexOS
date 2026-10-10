#include "shm.h"
#include "pmm.h"

typedef struct {
    uint32_t phys;
    int      owner;
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
            void *p = pmm_alloc_page();
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
            pmm_free_page((void *)slots[i].phys);
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