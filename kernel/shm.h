#ifndef SHM_H
#define SHM_H

#include <stdint.h>

#define SHM_SLOTS       32
#define SHM_SIZE        8192            /* 每 slot = 2 页 = 8KB */
#define SHM_USER_BASE   0x20000000u
#define SHM_HEAD_SIZE   4096
#define SHM_DATA_SIZE   4096

void      shm_init(void);

int       shm_alloc_for(int owner_tid);
void      shm_free_owner(int owner_tid);

uint32_t  shm_get_phys(int slot);        /* 返回头页物理地址 */
int       shm_get_owner(int slot);
int       shm_find_by_owner(int owner_tid);   /* ★ S6c */

#endif